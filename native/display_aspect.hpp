#pragma once

// TASK-080 P1: native display aspect ratios (decision 0071, docs/ULTRAWIDE-NATIVE-STUDY.md).
//
// GT4_DISPLAY_ASPECT picks the ratio R of the picture on screen:
//   unset, "auto"   R = the game window's client area (fullscreen: the monitor), followed on resize and Alt+Enter;
//                   headless runs (no window) use 16:9
//   "16:9", "4:3"   fixed ratios
//   "W:H"           a custom ratio, W and H positive decimals (for example "21:9", "32:9", "3440:1440", "21.5:9")
//   "X.YY"          a custom ratio as one decimal number (for example "2.39")
// Any other value (empty, zero, negative, a ratio outside 1:2 .. 8:1) refuses the start, like GT4_FULLSCREEN.
//
// GT4_ASPECT_FOV_EXTRA (hidden comparison switch for owner test T1): a decimal factor f, default 1.0, range
// 0.5 .. 3.0. It widens the horizontal field of view by f on top of the geometry-correct value. The owner's tuned
// in-game values (Width 515 / Aspect 1.20, Width 573 / Aspect 1.34) correspond to f = s = R / (16/9).
//
// 3D (Confirmed, study sections 2-3): RaceBase::initMonitor (0x00388A80) builds the race camera's 4x4 monitor matrix
// on its own stack and hands it to the camera through a virtual call (jalr at 0x00388B30, return address 0x00388B38,
// a0 = camera, a1 = sp = the matrix, f12 = fovy). That call goes through ps2::dispatch, where the TASK-080 capsule
// overlay (scripts/display_aspect_overlay.inc) scales the stack copy before the camera reads it:
//   Hor+ (R >= 16:9): the x output column (m[0], m[4], m[8], m[12]) times (16/9) / (R * f)
//   Vert+ (R < 16:9): the y output column (m[1], m[5], m[9], m[13]) times R / (16/9); x column times 1 / f
// The matrix uses row vectors (clip = v * M; m[8], m[9] are the LAN monitor off-axis terms), so the x output is the
// column 0, 4, 8, 12. The presented frame is always the game's 16:9 frame stretched to R, so 16/9 is the reference
// whatever the game's own Wide setting is. At R = 16:9 and f = 1 nothing is written: frames stay byte-identical.
// The hook never writes the option object or the monitor record (save data); it writes only the 64-byte stack copy.
// Photo pause (func_003C49C8) calls initMonitor too, so leaving photo mode re-applies the same scale.
//
// Scene policy (presentation): the picture fills the screen (stretched to R) only while the race view is shown; menus,
// FMVs and photo mode stay pillarboxed 16:9. The race view is "RaceDisplay::render (0x0039F498, the race HUD, reached
// only through vtables, so through dispatch) ran in the last few VBlanks", minus photo mode: a display request for the
// photo/movie CRT row 2 (func_001074D0, struct at 0x006184D0) until the next camera hook or another request. The
// headless race trace (slice212b) showed that 480i races and menus make identical requests (func_001074D0 records only
// changes), so requests alone cannot mark the race; see ScenePolicy.

#include <cstdint>
#include <string>

namespace gt4::display_aspect {

inline constexpr double kGameRatio = 16.0 / 9.0;
// A ratio within this relative distance of 16:9 is treated as exactly 16:9 (1366x768 = 1.7786 is 16:9).
inline constexpr double kSnapTolerance = 0.005;
inline constexpr double kMinRatio = 0.5;
inline constexpr double kMaxRatio = 8.0;
inline constexpr double kMinFovExtra = 0.5;
inline constexpr double kMaxFovExtra = 3.0;

// Guest addresses (Confirmed, docs/ULTRAWIDE-NATIVE-STUDY.md section 3.1 and the slice208 translation unit_00A2.cpp).
inline constexpr std::uint32_t kInitMonitor = 0x00388A80u;       // RaceBase::initMonitor
inline constexpr std::uint32_t kCameraCallReturn = 0x00388B38u;  // return label of the camera virtual call (jalr 0x388B30)
inline constexpr std::uint32_t kMatrixBytes = 64u;               // 16 x f32 on the stack at a1 = sp
// RaceDisplay::render (the race HUD, every race frame), reached only through vtables 0x67E294 / 0x680D1C (study
// section 2.2; unit_00A7 entry 413 in the slice208 translation, no direct call site).
inline constexpr std::uint32_t kRaceDisplayRender = 0x0039F498u;
// Display request state written by func_001074D0 (docs/INGAME-SETTINGS-STUDY.md section 1.4).
inline constexpr std::uint32_t kDisplayRequestState = 0x006184D0u;
inline constexpr std::uint32_t kRequestPendingOffset = 0x24u;
inline constexpr std::uint32_t kRequestModeOffset = 0x2Cu;
inline constexpr std::uint32_t kRequestRowOffset = 0x30u;

enum class Source { automatic, fixed };

struct Setting {
  Source source = Source::automatic;
  double ratio = kGameRatio;  // fixed ratio (snapped); for automatic, the headless default
};

// GT4_DISPLAY_ASPECT. nullptr = automatic. Throws std::invalid_argument with a message for any invalid value.
Setting parse_display_aspect(const char *value);

// GT4_ASPECT_FOV_EXTRA. nullptr = 1.0. Throws std::invalid_argument for an invalid value.
double parse_fov_extra(const char *value);

// R snapped: exactly kGameRatio when within kSnapTolerance of 16:9, otherwise unchanged.
double snap_ratio(double ratio);

// Ratio of a client area; 16:9 when either side is zero (minimized or unknown). Snapped.
double client_ratio(std::uint32_t width, std::uint32_t height);

bool is_game_ratio(double ratio);

struct ProjectionScale {
  float x = 1.0f;  // factor on the x output column (0, 4, 8, 12)
  float y = 1.0f;  // factor on the y output column (1, 5, 9, 13)
  bool identity() const { return x == 1.0f && y == 1.0f; }
};

// Hor+ / Vert+ factors for a snapped ratio R and the extra FOV factor f (see the header comment). Pure.
ProjectionScale projection_scale(double ratio, double fov_extra);

// Scales a host-order 4x4 matrix in place. Identity scales leave it untouched. Pure.
void scale_matrix(float matrix[16], const ProjectionScale &scale);

// Scales the 16 little-endian f32 words at guest `address`. Returns false (no write) when the 64 bytes are not inside
// guest RAM of `ram_size` bytes or the scale is the identity.
bool scale_guest_matrix(std::uint8_t *ram, std::uint32_t ram_size, std::uint32_t address, const ProjectionScale &scale);

// --- Scene policy -----------------------------------------------------------------------------------------------

// One sample of the display request state (func_001074D0) read at guest VBlank.
struct DisplayRequest {
  std::uint32_t pending = 0;
  std::uint32_t mode = 0;  // a1 of func_001074D0 (1 interlaced, 2 progressive / 1080i)
  std::uint32_t row = 0;   // CRT table row (default row for menus and 480i races, 2 photo and movies, 6 480p, 7 1080i)
  bool operator==(const DisplayRequest &other) const {
    return pending == other.pending && mode == other.mode && row == other.row;
  }
};

// Pure state machine. Race view wanted = RaceDisplay::render ran within the last kRenderWindowVblanks VBlanks and
// photo mode is not on. Photo mode = the last new display request asked for kPhotoMovieRow; it ends at the next race
// camera hook (leaving photo pause re-runs initMonitor) or at another request. The presented state changes only after
// the wanted state has been the same for kSettleVblanks VBlanks (hysteresis against single missed HUD frames).
class ScenePolicy {
 public:
  static constexpr std::uint32_t kRenderWindowVblanks = 30;  // the HUD paused ~23 VBlanks at a race start (slice212b trace)
  static constexpr std::uint32_t kSettleVblanks = 3;
  static constexpr std::uint32_t kPhotoMovieRow = 2;

  void on_race_camera(std::uint64_t vblank);
  void on_race_display_render(std::uint64_t vblank);
  // Called at every guest VBlank with the current request sample. Returns the presented "race view" state.
  bool at_vblank(std::uint64_t vblank, const DisplayRequest &request);

  bool race_view_wanted() const { return wanted_; }
  bool race_view_presented() const { return presented_; }
  bool photo_mode() const { return photo_; }
  std::uint64_t request_events() const { return request_events_; }

 private:
  bool have_request_ = false;
  DisplayRequest last_request_{};
  bool render_seen_ = false;
  std::uint64_t render_vblank_ = 0;
  bool photo_ = false;
  bool wanted_ = false;
  bool presented_ = false;
  std::uint32_t stable_ = 0;
  std::uint64_t request_events_ = 0;
};

// Whether a new display request event happened between two samples: pending rises, or mode/row change.
bool is_new_request(const DisplayRequest &previous, const DisplayRequest &current);

// --- Runtime glue (one process-wide state; EE thread unless noted) ------------------------------------------------

// Reads GT4_DISPLAY_ASPECT and GT4_ASPECT_FOV_EXTRA once. An invalid value prints the refusal and exits with status 1
// (called at window start, and again at the first hook or VBlank so headless runs refuse too).
const Setting &configured_setting();
double configured_fov_extra();

// Window thread: the game window's client size (WM_SIZE, fullscreen). 0 x 0 is ignored (minimized).
void set_window_client_size(std::uint32_t width, std::uint32_t height);

// R now: the fixed ratio, or the client area ratio (16:9 before any window size is known). Snapped.
double current_ratio();

// The ratio the last race camera hook used (16:9 before the first hook).
double race_projection_ratio();

// Called by the dispatch overlay at the camera virtual call (return 0x00388B38). `matrix_address` = a1. Returns true
// when the matrix was scaled.
bool on_race_camera(std::uint8_t *ram, std::uint32_t ram_size, std::uint32_t matrix_address);

// Called by the dispatch overlay at every dispatch of RaceDisplay::render (0x0039F498): marks the race view.
void on_race_display_render();

// Presentation sink: race_view = fill the screen at `ratio`; otherwise pillarbox 16:9. Called on the EE thread only
// when the presented state or ratio changes (never while R stays 16:9).
using PresentationSink = void (*)(bool race_view, double ratio);
void set_presentation_sink(PresentationSink sink);

// Guest VBlank entry (mtvu_runtime.cpp vblank_sync): samples the display request and drives the scene policy.
void at_guest_vblank(const std::uint8_t *ram, std::uint32_t ram_size);

// "21:9 (2.333)" style text for logs.
std::string describe_ratio(double ratio);

}  // namespace gt4::display_aspect
