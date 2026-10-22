// CTest native_mtvu_switches: host-only controls for TASK-092, the developer switches of the MTVU VBlank journal.
// The GS client VSYNC deferral (mtvu_bridge.cpp) and vblank_sync() (mtvu_runtime.cpp) both use
// vblank_journal_active(); this fixture checks that decision over every combination of the three switches.
#include "mtvu_runtime.hpp"

#include <cstdio>
#include <stdexcept>

namespace {

using namespace gt4::mtvu;

void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

struct Row {
  bool journal;
  bool enabled;
  bool verify;
  bool expected;  // VBlank journal active
};

// Written out by hand (not derived from the function) so a change to the rule shows up here.
constexpr Row kTruthTable[] = {
    {true, true, false, true},    // default path: MTVU on, verify off
    {true, true, true, false},    // GT4_MTVU_VERIFY=1: blocking drain, no journal
    {true, false, false, false},  // GT4_MTVU=0: no worker, no journal (was a crash)
    {true, false, true, false},
    {false, true, false, false},  // GT4_MTVU_VBLANK_JOURNAL=0: blocking drain
    {false, true, true, false},
    {false, false, false, false},
    {false, false, true, false},
};

void check_truth_table() {
  for (const Row &row : kTruthTable) {
    Settings settings;
    settings.vblank_journal = row.journal;
    settings.enabled = row.enabled;
    settings.verify = row.verify;
    const bool active = vblank_journal_active(settings);
    if (active != row.expected) {
      std::fprintf(stderr, "[mtvu-switches] journal %d enabled %d verify %d: active %d, expected %d\n", row.journal,
                   row.enabled, row.verify, active, row.expected);
      throw std::runtime_error("vblank_journal_active disagrees with the TASK-092 table");
    }
  }
}

void check_defaults_and_switch_off() {
  // The struct defaults are the player path (MTVU on, verify off, journal on).
  const Settings defaults;
  require(defaults.enabled && !defaults.verify && defaults.vblank_journal, "default switches are not the player path");
  require(vblank_journal_active(defaults), "default settings must activate the VBlank journal");
  // GT4_MTVU=0 and GT4_MTVU_VERIFY=1 as the parsed switches set them (the env parser lives in mtvu_runtime.cpp,
  // which this host-only fixture does not link).
  Settings mtvu_off;
  mtvu_off.enabled = false;
  require(!vblank_journal_active(mtvu_off), "GT4_MTVU=0 must not activate the VBlank journal");
  Settings verify_on;
  verify_on.verify = true;
  require(!vblank_journal_active(verify_on), "GT4_MTVU_VERIFY=1 must not activate the VBlank journal");
}

}  // namespace

int main() {
  try {
    check_truth_table();
    check_defaults_and_switch_off();
    std::puts("native_mtvu_switches: ok (8 combinations, environment defaults)");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "native_mtvu_switches: %s\n", error.what());
    return 1;
  }
}
