// Fixture for TASK-080 P1 (native/display_aspect.hpp): GT4_DISPLAY_ASPECT and GT4_ASPECT_FOV_EXTRA parsing, the 16:9
// snap, the Hor+/Vert+ factors, the matrix scale on a synthetic matrix (host and guest little-endian words), and the
// scene policy that drives the presentation.

#include "display_aspect.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {

using namespace gt4::display_aspect;

void require(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

bool near(double a, double b, double tolerance = 1e-6) { return std::fabs(a - b) <= tolerance; }

bool refused(const char *value) {
  try {
    parse_display_aspect(value);
  } catch (const std::invalid_argument &) {
    return true;
  }
  return false;
}

bool fov_refused(const char *value) {
  try {
    parse_fov_extra(value);
  } catch (const std::invalid_argument &) {
    return true;
  }
  return false;
}

void test_parse() {
  require(parse_display_aspect(nullptr).source == Source::automatic, "unset is auto");
  require(parse_display_aspect("auto").source == Source::automatic, "auto is auto");
  const Setting wide = parse_display_aspect("16:9");
  require(wide.source == Source::fixed && wide.ratio == kGameRatio, "16:9 is exactly the game ratio");
  require(parse_display_aspect("4:3").ratio == 4.0 / 3.0, "4:3");
  require(near(parse_display_aspect("21:9").ratio, 21.0 / 9.0), "21:9");
  require(near(parse_display_aspect("32:9").ratio, 32.0 / 9.0), "32:9");
  require(near(parse_display_aspect("3440:1440").ratio, 3440.0 / 1440.0), "3440:1440");
  require(near(parse_display_aspect("21.5:9").ratio, 21.5 / 9.0), "21.5:9");
  require(near(parse_display_aspect("2.39").ratio, 2.39), "decimal 2.39");
  require(near(parse_display_aspect("16:10").ratio, 1.6), "16:10");
  require(parse_display_aspect("1.7778").ratio == kGameRatio, "a near-16:9 decimal snaps to 16:9");
  require(parse_display_aspect("1366:768").ratio == kGameRatio, "1366x768 snaps to 16:9");
  const char *bad[] = {"", "Auto", "AUTO", "0:9", "16:0", "abc", "-16:9", "16:9:1", " 16:9", "16:9 ", "100:1",
                       "1:100", "1e3", "16/9", ":9", "16:", ".", "1..7", "0", "nan", "inf", "0x10:9"};
  for (const char *value : bad) {
    if (!refused(value)) {
      std::fprintf(stderr, "value '%s' was accepted\n", value);
      require(false, "an invalid GT4_DISPLAY_ASPECT is refused");
    }
  }
}

void test_fov_extra() {
  require(parse_fov_extra(nullptr) == 1.0, "unset FOV extra is 1.0");
  require(parse_fov_extra("1.0") == 1.0, "1.0");
  require(near(parse_fov_extra("1.37"), 1.37), "1.37");
  require(parse_fov_extra("0.5") == 0.5 && parse_fov_extra("3") == 3.0, "range ends");
  const char *bad[] = {"", "0", "0.49", "3.01", "x", "-1", "1,2", "1.2.3"};
  for (const char *value : bad)
    require(fov_refused(value), "an invalid GT4_ASPECT_FOV_EXTRA is refused");
}

void test_ratios() {
  require(client_ratio(0, 0) == kGameRatio && client_ratio(1920, 0) == kGameRatio, "unknown client is 16:9");
  require(client_ratio(1920, 1080) == kGameRatio, "1920x1080 is exactly 16:9");
  require(client_ratio(1280, 721) == kGameRatio, "a window one row off is 16:9");
  require(near(client_ratio(3440, 1440), 3440.0 / 1440.0), "3440x1440");
  require(near(client_ratio(3088, 1440), 3088.0 / 1440.0), "S23 3088x1440");
  require(near(client_ratio(1440, 900), 1.6), "16:10 window");
  require(client_ratio(4000, 10) == kGameRatio, "a degenerate window is not a ratio");
  require(is_game_ratio(snap_ratio(16.0 / 9.0)) && !is_game_ratio(1.6), "is_game_ratio");
  require(describe_ratio(kGameRatio).rfind("16:9", 0) == 0, "describe 16:9");
}

void test_projection_scale() {
  require(projection_scale(kGameRatio, 1.0).identity(), "16:9 with factor 1 is the identity");
  const ProjectionScale ultra = projection_scale(21.0 / 9.0, 1.0);
  require(ultra.x == static_cast<float>(16.0 / 21.0) && ultra.y == 1.0f, "21:9 Hor+: x * 16/21");
  require(projection_scale(32.0 / 9.0, 1.0).x == 0.5f, "32:9 Hor+: x * 0.5");
  const ProjectionScale tall = projection_scale(4.0 / 3.0, 1.0);
  require(tall.x == 1.0f && tall.y == 0.75f, "4:3 Vert+: y * 0.75");
  const ProjectionScale ten = projection_scale(1.6, 1.0);
  require(ten.x == 1.0f && ten.y == 0.9f, "16:10 Vert+: y * 0.9");
  const ProjectionScale extra = projection_scale(kGameRatio, 1.25);
  require(extra.x == 0.8f && extra.y == 1.0f, "16:9 with factor 1.25: x * 0.8");
  // The owner's 3440x1440 values (Width 573, Aspect 1.34) equal f = s; geometry-correct is f = 1.
  const double s = (3440.0 / 1440.0) / kGameRatio;
  const ProjectionScale owner = projection_scale(3440.0 / 1440.0, s);
  require(near(owner.x, 1.0 / (s * s), 1e-6), "owner-style factor applies s twice");
  const ProjectionScale tall_extra = projection_scale(4.0 / 3.0, 2.0);
  require(tall_extra.x == 0.5f && tall_extra.y == 0.75f, "Vert+ with a factor widens x too");
}

void fill_matrix(float m[16]) {
  // A GT4-like monitor matrix: m[0] = 2*D/W, m[5] = 2*D/H, off-axis m[8], m[9], m[10] = m[15] = 1.
  const float values[16] = {1.299f, 0.0f, 0.0f, 0.0f, 0.0f, 2.300f, 0.0f, 0.0f,
                            0.125f, -0.25f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  std::memcpy(m, values, sizeof values);
}

void test_scale_matrix() {
  float m[16], original[16];
  fill_matrix(m);
  fill_matrix(original);
  scale_matrix(m, projection_scale(kGameRatio, 1.0));
  require(std::memcmp(m, original, sizeof m) == 0, "identity leaves the matrix bit-identical");
  scale_matrix(m, projection_scale(32.0 / 9.0, 1.0));
  require(m[0] == original[0] * 0.5f && m[8] == original[8] * 0.5f, "Hor+ scales the x column");
  require(m[5] == original[5] && m[9] == original[9] && m[10] == 1.0f && m[15] == 1.0f, "Hor+ leaves the rest");
  require(std::signbit(m[4]) == std::signbit(original[4]) && m[12] == 0.0f, "zero entries stay zero");
  fill_matrix(m);
  scale_matrix(m, projection_scale(4.0 / 3.0, 1.0));
  require(m[5] == original[5] * 0.75f && m[9] == original[9] * 0.75f && m[0] == original[0], "Vert+ scales y");
  // tan(half hFOV) / tan(half vFOV) = m[5] / m[0]: the picture ratio follows R.
  fill_matrix(m);
  const double before = static_cast<double>(m[5]) / m[0];
  scale_matrix(m, projection_scale(21.0 / 9.0, 1.0));
  require(near(static_cast<double>(m[5]) / m[0], before * (21.0 / 16.0), 1e-5), "frustum ratio grows by R/(16/9)");
}

void put_le32(std::vector<std::uint8_t> &ram, std::uint32_t address, std::uint32_t value) {
  for (int i = 0; i < 4; ++i)
    ram[address + static_cast<std::uint32_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::uint32_t get_le32(const std::vector<std::uint8_t> &ram, std::uint32_t address) {
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i)
    value |= static_cast<std::uint32_t>(ram[address + static_cast<std::uint32_t>(i)]) << (8 * i);
  return value;
}

void test_guest_matrix() {
  constexpr std::uint32_t ram_size = 1u << 20;
  constexpr std::uint32_t stack = 0x000FFF00u;
  std::vector<std::uint8_t> ram(ram_size, 0xCD);
  float m[16];
  fill_matrix(m);
  for (std::uint32_t i = 0; i < 16; ++i) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &m[i], 4);
    put_le32(ram, stack + 4 * i, bits);
  }
  const std::vector<std::uint8_t> before = ram;
  require(!scale_guest_matrix(ram.data(), ram_size, stack, projection_scale(kGameRatio, 1.0)), "16:9 writes nothing");
  require(ram == before, "16:9 leaves guest RAM byte-identical");
  require(scale_guest_matrix(ram.data(), ram_size, stack, projection_scale(21.0 / 9.0, 1.0)), "21:9 writes");
  float scaled0 = 0.0f;
  const std::uint32_t bits0 = get_le32(ram, stack);
  std::memcpy(&scaled0, &bits0, 4);
  require(scaled0 == m[0] * static_cast<float>(16.0 / 21.0), "guest m[0] scaled");
  require(get_le32(ram, stack + 20) == get_le32(before, stack + 20), "guest m[5] unchanged");
  for (std::uint32_t at = 0; at < ram_size; ++at)
    if (at < stack || at >= stack + 64)
      require(ram[at] == before[at], "nothing outside the 64 matrix bytes is written");
  require(!scale_guest_matrix(ram.data(), ram_size, ram_size - 32, projection_scale(2.4, 1.0)), "outside RAM refused");
  require(!scale_guest_matrix(ram.data(), ram_size, stack + 2, projection_scale(2.4, 1.0)), "unaligned refused");
}

void test_scene_policy() {
  const DisplayRequest menu{0, 1, 1}, menu_pending{1, 1, 1}, race480p{1, 2, 6}, photo{1, 1, 2};
  require(!is_new_request(menu, menu), "same sample is not a request");
  require(is_new_request(menu, menu_pending), "pending rising edge is a request");
  require(!is_new_request(menu_pending, menu), "pending falling edge is not a request");
  require(is_new_request(menu, race480p) && is_new_request(race480p, photo), "row or mode change is a request");

  ScenePolicy policy;
  std::uint64_t v = 0;
  for (; v < 100; ++v)
    require(!policy.at_vblank(v, menu), "menus (no RaceDisplay::render) are not the race view");
  policy.on_race_camera(v);  // the camera init alone (pre-race, loading) is not the race view
  for (int i = 0; i < 10; ++i)
    require(!policy.at_vblank(v++, menu), "no HUD yet: pillarbox");
  // The HUD renders every frame: race view after the settle VBlanks.
  policy.on_race_display_render(v);
  require(!policy.at_vblank(v++, menu), "hysteresis 1");
  policy.on_race_display_render(v);
  require(!policy.at_vblank(v++, menu), "hysteresis 2");
  policy.on_race_display_render(v);
  require(policy.at_vblank(v++, menu), "race view after kSettleVblanks");
  // A few frames without the HUD (a hitch) stay inside the render window.
  for (int i = 0; i < 5; ++i)
    require(policy.at_vblank(v++, menu), "short HUD gap keeps the race view");
  // Photo pause: a request for row 2 ends the race view even if render still runs.
  for (std::uint32_t i = 0; i < ScenePolicy::kSettleVblanks; ++i) {
    policy.on_race_display_render(v);
    policy.at_vblank(v++, photo);
  }
  require(policy.photo_mode() && !policy.race_view_presented(), "photo mode is pillarboxed");
  // Leaving photo mode re-runs initMonitor (camera hook): race view again while the HUD renders.
  policy.on_race_camera(v);
  for (std::uint32_t i = 0; i < ScenePolicy::kSettleVblanks; ++i) {
    policy.on_race_display_render(v);
    policy.at_vblank(v++, photo);
  }
  require(!policy.photo_mode() && policy.race_view_presented(), "after photo mode the race view returns");
  // Race over: the HUD stops; after the render window and the settle time the menus are pillarboxed.
  for (std::uint32_t i = 0; i < ScenePolicy::kRenderWindowVblanks + ScenePolicy::kSettleVblanks + 1; ++i)
    policy.at_vblank(v++, menu);
  require(!policy.race_view_presented(), "menus after the race are pillarboxed");
  // A movie request (row 2) with no HUD stays pillarboxed; a later 480p race request clears photo mode.
  policy.at_vblank(v++, photo);
  require(policy.photo_mode(), "movie row marks photo/movie");
  policy.at_vblank(v++, race480p);
  require(!policy.photo_mode(), "another request clears it");
  // A single HUD frame lasts at most the render window plus the settle time.
  ScenePolicy brief;
  brief.on_race_display_render(0);
  for (std::uint64_t t = 0; t < 40; ++t) {
    const bool shown = brief.at_vblank(t, menu);
    require(!shown || t < ScenePolicy::kRenderWindowVblanks + ScenePolicy::kSettleVblanks + 1, "one HUD frame");
  }
  require(!brief.race_view_presented(), "a single HUD frame is gone after the window");
}

}  // namespace

int main() {
  test_parse();
  test_fov_extra();
  test_ratios();
  test_projection_scale();
  test_scale_matrix();
  test_guest_matrix();
  test_scene_policy();
  std::puts("display_aspect fixture: all checks passed");
  return 0;
}
