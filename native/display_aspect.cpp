#include "display_aspect.hpp"

#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace gt4::display_aspect {
namespace {

std::uint32_t read_le32(const std::uint8_t *bytes) {
  return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void write_le32(std::uint8_t *bytes, std::uint32_t value) {
  for (int i = 0; i < 4; ++i)
    bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
}

// A plain positive decimal: digits with at most one '.', no sign, no exponent, no spaces. Returns false otherwise.
bool parse_positive_decimal(const char *begin, const char *end, double &out) {
  if (begin == end || end - begin > 16)
    return false;
  bool dot = false, digit = false;
  for (const char *p = begin; p != end; ++p) {
    if (*p == '.') {
      if (dot)
        return false;
      dot = true;
    } else if (*p >= '0' && *p <= '9') {
      digit = true;
    } else {
      return false;
    }
  }
  if (!digit)
    return false;
  const std::string text(begin, end);
  char *stop = nullptr;
  errno = 0;
  const double value = std::strtod(text.c_str(), &stop);
  if (errno != 0 || stop != text.c_str() + text.size() || !std::isfinite(value) || value <= 0.0)
    return false;
  out = value;
  return true;
}

[[noreturn]] void refuse(const char *variable, const char *value, const char *rule) {
  throw std::invalid_argument(std::string(variable) + " '" + value + "' is not valid (" + rule + ")");
}

// Column factors: x output = elements 0, 4, 8, 12; y output = 1, 5, 9, 13.
constexpr int kXColumn[4] = {0, 4, 8, 12};
constexpr int kYColumn[4] = {1, 5, 9, 13};

}  // namespace

Setting parse_display_aspect(const char *value) {
  constexpr const char *kRule = "use auto, 16:9, 4:3, W:H or a decimal ratio between 0.5 and 8";
  if (value == nullptr || std::strcmp(value, "auto") == 0)
    return Setting{Source::automatic, kGameRatio};
  const char *end = value + std::strlen(value);
  double ratio = 0.0;
  if (const char *colon = std::strchr(value, ':')) {
    double width = 0.0, height = 0.0;
    if (!parse_positive_decimal(value, colon, width) || !parse_positive_decimal(colon + 1, end, height))
      refuse("GT4_DISPLAY_ASPECT", value, kRule);
    ratio = width / height;
  } else if (!parse_positive_decimal(value, end, ratio)) {
    refuse("GT4_DISPLAY_ASPECT", value, kRule);
  }
  if (!(ratio >= kMinRatio && ratio <= kMaxRatio))
    refuse("GT4_DISPLAY_ASPECT", value, kRule);
  return Setting{Source::fixed, snap_ratio(ratio)};
}

double parse_fov_extra(const char *value) {
  if (value == nullptr)
    return 1.0;
  double factor = 0.0;
  if (!parse_positive_decimal(value, value + std::strlen(value), factor) || factor < kMinFovExtra ||
      factor > kMaxFovExtra)
    refuse("GT4_ASPECT_FOV_EXTRA", value, "use a decimal factor between 0.5 and 3.0; 1.0 is geometry-correct");
  return factor;
}

double snap_ratio(double ratio) {
  if (std::fabs(ratio / kGameRatio - 1.0) <= kSnapTolerance)
    return kGameRatio;
  return ratio;
}

double client_ratio(std::uint32_t width, std::uint32_t height) {
  if (width == 0 || height == 0)
    return kGameRatio;
  const double ratio = static_cast<double>(width) / static_cast<double>(height);
  if (ratio < kMinRatio || ratio > kMaxRatio)
    return kGameRatio;  // a degenerate window (a few pixels high) is not a display ratio
  return snap_ratio(ratio);
}

bool is_game_ratio(double ratio) { return ratio == kGameRatio; }

ProjectionScale projection_scale(double ratio, double fov_extra) {
  ProjectionScale scale;
  const double r = snap_ratio(ratio);
  if (r >= kGameRatio) {
    scale.x = static_cast<float>(kGameRatio / (r * fov_extra));
  } else {
    scale.y = static_cast<float>(r / kGameRatio);
    scale.x = static_cast<float>(1.0 / fov_extra);
  }
  return scale;
}

void scale_matrix(float matrix[16], const ProjectionScale &scale) {
  if (scale.identity())
    return;
  if (scale.x != 1.0f)
    for (const int index : kXColumn)
      matrix[index] *= scale.x;
  if (scale.y != 1.0f)
    for (const int index : kYColumn)
      matrix[index] *= scale.y;
}

bool scale_guest_matrix(std::uint8_t *ram, std::uint32_t ram_size, std::uint32_t address, const ProjectionScale &scale) {
  if (scale.identity())
    return false;
  if (static_cast<std::uint64_t>(address) + kMatrixBytes > ram_size || (address & 3u) != 0)
    return false;
  float matrix[16];
  for (int i = 0; i < 16; ++i) {
    const std::uint32_t bits = read_le32(ram + address + 4u * static_cast<std::uint32_t>(i));
    std::memcpy(&matrix[i], &bits, sizeof bits);
  }
  scale_matrix(matrix, scale);
  for (int i = 0; i < 16; ++i) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &matrix[i], sizeof bits);
    write_le32(ram + address + 4u * static_cast<std::uint32_t>(i), bits);
  }
  return true;
}

bool is_new_request(const DisplayRequest &previous, const DisplayRequest &current) {
  if (current.pending != 0 && previous.pending == 0)
    return true;
  return current.mode != previous.mode || current.row != previous.row;
}

void ScenePolicy::on_race_camera(std::uint64_t) { photo_ = false; }

void ScenePolicy::on_race_display_render(std::uint64_t vblank) {
  render_seen_ = true;
  render_vblank_ = vblank;
}

bool ScenePolicy::at_vblank(std::uint64_t vblank, const DisplayRequest &request) {
  if (have_request_ && is_new_request(last_request_, request)) {
    ++request_events_;
    photo_ = request.row == kPhotoMovieRow;
  }
  have_request_ = true;
  last_request_ = request;
  wanted_ = render_seen_ && vblank <= render_vblank_ + kRenderWindowVblanks && !photo_;
  if (wanted_ == presented_) {
    stable_ = 0;
  } else if (++stable_ >= kSettleVblanks) {
    presented_ = wanted_;
    stable_ = 0;
  }
  return presented_;
}

// --- Runtime glue -------------------------------------------------------------------------------------------------

namespace {

struct Runtime {
  std::atomic<std::uint32_t> client_width{0}, client_height{0};
  double projection_ratio = kGameRatio;
  ScenePolicy policy;
  std::uint64_t vblank = 0;
  PresentationSink sink = nullptr;
  bool presented_fill = false;
  double presented_ratio = kGameRatio;
  std::uint64_t hooks = 0, scaled = 0, renders = 0;
  std::uint32_t request_lines = 0;
};

Runtime &runtime() {
  static Runtime state;
  return state;
}

struct Config {
  Setting setting;
  double fov_extra = 1.0;
};

const Config &config() {
  static const Config value = [] {
    Config c;
    try {
      c.setting = parse_display_aspect(std::getenv("GT4_DISPLAY_ASPECT"));
      c.fov_extra = parse_fov_extra(std::getenv("GT4_ASPECT_FOV_EXTRA"));
    } catch (const std::invalid_argument &error) {
      std::fprintf(stderr, "[aspect] %s, game not started\n", error.what());
      std::fflush(stderr);
      std::exit(1);
    }
    std::fprintf(stderr, "[aspect] GT4_DISPLAY_ASPECT %s, FOV extra %.3f\n",
                 c.setting.source == Source::automatic ? "auto (window client area)"
                                                       : describe_ratio(c.setting.ratio).c_str(),
                 c.fov_extra);
    return c;
  }();
  return value;
}

}  // namespace

const Setting &configured_setting() { return config().setting; }
double configured_fov_extra() { return config().fov_extra; }

void set_window_client_size(std::uint32_t width, std::uint32_t height) {
  if (width == 0 || height == 0)
    return;
  Runtime &r = runtime();
  r.client_width.store(width);
  r.client_height.store(height);
}

double current_ratio() {
  const Setting &setting = configured_setting();
  if (setting.source == Source::fixed)
    return setting.ratio;
  const Runtime &r = runtime();
  return client_ratio(r.client_width.load(), r.client_height.load());
}

double race_projection_ratio() { return runtime().projection_ratio; }

bool on_race_camera(std::uint8_t *ram, std::uint32_t ram_size, std::uint32_t matrix_address) {
  Runtime &r = runtime();
  const double ratio = current_ratio();
  const ProjectionScale scale = projection_scale(ratio, configured_fov_extra());
  r.projection_ratio = ratio;
  r.policy.on_race_camera(r.vblank);
  ++r.hooks;
  const bool scaled = scale_guest_matrix(ram, ram_size, matrix_address, scale);
  if (scaled)
    ++r.scaled;
  // One line per race camera init (a race start, a replay, leaving photo mode): rare.
  std::fprintf(stderr, "[aspect] race camera hook %llu at vblank %llu: R %s, matrix 0x%08X %s (x %.6f, y %.6f)\n",
               static_cast<unsigned long long>(r.hooks), static_cast<unsigned long long>(r.vblank),
               describe_ratio(ratio).c_str(), matrix_address, scaled ? "scaled" : "unchanged",
               static_cast<double>(scale.x), static_cast<double>(scale.y));
  return scaled;
}

void on_race_display_render() {
  Runtime &r = runtime();
  if (r.renders++ == 0)
    std::fprintf(stderr, "[aspect] first RaceDisplay::render at vblank %llu\n", static_cast<unsigned long long>(r.vblank));
  r.policy.on_race_display_render(r.vblank);
}

void set_presentation_sink(PresentationSink sink) { runtime().sink = sink; }

void at_guest_vblank(const std::uint8_t *ram, std::uint32_t ram_size) {
  Runtime &r = runtime();
  configured_setting();  // a headless run refuses an invalid value here
  ++r.vblank;
  DisplayRequest request;
  if (static_cast<std::uint64_t>(kDisplayRequestState) + kRequestRowOffset + 4u <= ram_size) {
    request.pending = read_le32(ram + kDisplayRequestState + kRequestPendingOffset);
    request.mode = read_le32(ram + kDisplayRequestState + kRequestModeOffset);
    request.row = read_le32(ram + kDisplayRequestState + kRequestRowOffset);
  }
  const std::uint64_t events_before = r.policy.request_events();
  const bool race_view = r.policy.at_vblank(r.vblank, request);
  if (r.policy.request_events() != events_before && r.request_lines < 256) {
    ++r.request_lines;
    std::fprintf(stderr, "[aspect] display request at vblank %llu: pending %u mode %u row %u; photo/movie %s\n",
                 static_cast<unsigned long long>(r.vblank), request.pending, request.mode, request.row,
                 r.policy.photo_mode() ? "yes" : "no");
  }
  // Fill only when the race projection was built for a ratio other than 16:9: never at 16:9, so a 16:9 player never
  // sees a presentation change and a 1.2 GS process is never sent anything.
  const double ratio = r.projection_ratio;
  const bool fill = race_view && !is_game_ratio(ratio);
  if (fill == r.presented_fill && (!fill || ratio == r.presented_ratio))
    return;
  r.presented_fill = fill;
  r.presented_ratio = ratio;
  std::fprintf(stderr, "[aspect] presentation at vblank %llu: %s\n", static_cast<unsigned long long>(r.vblank),
               fill ? ("fill " + describe_ratio(ratio)).c_str() : "16:9 pillarbox");
  if (r.sink)
    r.sink(fill, ratio);
}

std::string describe_ratio(double ratio) {
  char text[64];
  if (ratio == kGameRatio)
    std::snprintf(text, sizeof text, "16:9 (%.4f)", ratio);
  else if (ratio == 4.0 / 3.0)
    std::snprintf(text, sizeof text, "4:3 (%.4f)", ratio);
  else
    std::snprintf(text, sizeof text, "%.2f:9 (%.4f)", ratio * 9.0, ratio);
  return text;
}

}  // namespace gt4::display_aspect
