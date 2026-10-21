//@section includes
#include "display_aspect.hpp"
//@section helpers
// TASK-080 P1 race camera aspect hook (capsule slice212c, scripts/prepare_license_guard.py --display-aspect). Runs only for the
// camera virtual call in RaceBase::initMonitor (jalr 0x00388B30, return label 0x00388B38), whatever the target: a0 is
// the camera, a1 = sp is the 4x4 monitor matrix the function just built on its own stack, f12 is fovy. The host scales
// that stack copy for the display ratio before the camera reads it (rule: native/display_aspect.hpp). It never writes
// the option object or the monitor record. At 16:9 with GT4_ASPECT_FOV_EXTRA 1.0 nothing is written.
// The second hook line marks every dispatch of RaceDisplay::render (0x0039F498, the race HUD; reached only through
// vtables) for the race-view presentation policy; it reads and writes no guest state.
namespace {

void race_camera_aspect_hook(Cpu &c) {
  // A dispatch of the return label itself (unwinding at kMaxDepth) is not the camera call.
  if (c.pc == c.r[31].w[0]) return;
  const std::uint32_t matrix = c.r[5].w[0];
  if (matrix != c.r[29].w[0]) {
    std::fprintf(stderr, "[aspect] camera call target 0x%08X: a1 0x%08X is not sp 0x%08X -> unchanged\n", c.pc, matrix,
                 c.r[29].w[0]);
    return;
  }
  gt4::display_aspect::on_race_camera(g_ram, kRamSize, matrix);
}

}  // namespace

//@section hook
    if (c.r[31].w[0] == gt4::display_aspect::kCameraCallReturn) [[unlikely]] race_camera_aspect_hook(c);
    if (pc == gt4::display_aspect::kRaceDisplayRender) [[unlikely]] gt4::display_aspect::on_race_display_render();
