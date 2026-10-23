// TASK-100 S3: compile native/game_target.hpp as the Spec II target and expose the result to game_target_fixture.
//
// GT4_TARGET=spec2 defines GT4_TARGET_SPEC2 for the whole build in CMake; here it is defined locally so one fixture
// binary can check both tables without a second build.

#define GT4_TARGET_SPEC2 1
#include "game_target.hpp"

bool spec2_target_has_no_hooks() {
  const gt4::target::Profile &profile = gt4::target::active();
  if (profile.id != gt4::target::Id::spec2)
    return false;
  if (profile.ram_size != 32u << 20)
    return false;
  const gt4::target::HookAddresses &h = profile.hooks;
  return h.widescreen_owner_pointer == 0 && h.widescreen_option_offset == 0 && h.widescreen_global == 0 &&
         h.race_display_index_offset == 0 && h.init_monitor == 0 && h.camera_call_return == 0 &&
         h.race_display_render == 0 && h.display_request_state == 0 && h.hud_parameter_block == 0 &&
         h.hud_matrix_offset == 0 && h.license_load_demo_inputs == 0 && h.license_loader_entry == 0 &&
         h.license_loader_return == 0 && h.license_replay_loader_return == 0 && h.license_crc_table == 0 &&
         h.shape_call_pc == 0 && h.shape_target_pc == 0 && h.shape_return_pc == 0;
}

bool spec2_match_addresses_never_match() {
  const gt4::target::HookAddresses &h = gt4::target::active().hooks;
  return gt4::target::match_or_never(h.camera_call_return) == gt4::target::kNeverMatch &&
         gt4::target::match_or_never(h.race_display_render) == gt4::target::kNeverMatch &&
         gt4::target::match_or_never(0x388B38u) == 0x388B38u;
}
