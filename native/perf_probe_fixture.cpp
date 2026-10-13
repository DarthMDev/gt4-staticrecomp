// Automated control for the host performance probe.
//
// The probe attributes host wall time per guest VBlank cycle. This fixture
// feeds deterministic stage durations and checks the accounting invariants,
// that a disabled probe accumulates nothing, and that the report prints every
// attribution line the owner needs.

#include "perf_probe.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *description) {
  if (!condition)
    throw std::runtime_error(std::string("probe fixture: ") + description);
}

void check_disabled_probe_ignores_measurements() {
  auto &probe = gt4::perf::Probe::shared();
  probe.set_enabled(false);
  probe.close_stage(gt4::perf::Stage::gs_execute, 5000);
  probe.note_wait(gt4::perf::Stage::gs_submit_wait, 5000);
  probe.add_dispatch(1024);
  probe.frame_boundary(gt4::perf::GuestSample{});
  require(probe.stage(gt4::perf::Stage::gs_execute).calls == 0,
          "disabled probe recorded a stage");
  require(probe.blocked_us() == 0, "disabled probe recorded a wait");
  probe.set_enabled(true);
}

void check_accounting() {
  using gt4::perf::Probe;
  using gt4::perf::Stage;
  Probe &probe = Probe::shared();
  probe.set_enabled(true);
  probe.set_report_period_seconds(3600);
  probe.restart();  // One call arms the interval clock; three cycles are measured.
  probe.frame_boundary(gt4::perf::GuestSample{});
  const std::uint64_t vblank_us = 500, submit_wait_us = 2500,
                      present_wait_us = 400, workgroups = 64ull * 8ull;
  for (int cycle = 0; cycle < 3; ++cycle) {
    probe.close_stage(Stage::host_vblank, vblank_us);
    probe.close_stage(Stage::gs_upload, 400);
    probe.close_stage(Stage::gs_execute, 3000);
    probe.note_wait(Stage::gs_submit_wait, submit_wait_us);
    probe.note_wait(Stage::present_wait, present_wait_us);
    probe.close_stage(Stage::present, 1500);
    probe.add_dispatch(workgroups);
    probe.add_presentation(static_cast<std::uint64_t>(640) * 448ull * 4ull);
    probe.add_audio(800, 800);
    // Real sleep longer than the host callback so the attribution below is an
    // exact identity instead of a clamped estimate.
    Sleep(2000);
    probe.frame_boundary(gt4::perf::GuestSample{4915200ull * (cycle + 1),
                                               static_cast<std::uint64_t>(cycle + 1),
                                               10, 20});
  }
  require(probe.frames() == 3, "three completed guest cycles expected");
  const auto &submits = probe.stage(Stage::gs_submit_wait);
  require(submits.calls == 3 && submits.total_us == 3 * submit_wait_us,
          "blocked submission waits were not accumulated");
  const auto &present_waits = probe.stage(Stage::present_wait);
  require(present_waits.calls == 3 && present_waits.total_us == 3 * present_wait_us,
          "blocked presentation waits were not accumulated");
  require(probe.blocked_us() == 3 * (submit_wait_us + present_wait_us),
          "frame blocked total differs from the sum of waits");
  require(probe.interval_us() ==
              probe.guest_between_us() +
                  probe.stage(Stage::host_vblank).total_us,
          "guest execution attribution does not close the cycle budget");
  require(probe.interval_us() >= probe.blocked_us(),
          "blocked time exceeds the measured interval");
  std::fprintf(stderr,
               "[fixture] probe accounting: %llu frames, blocked %.2f ms of "
               "%.2f ms, guest between %.2f ms\n",
               static_cast<unsigned long long>(probe.frames()),
               probe.blocked_us() / 1000.0, probe.interval_us() / 1000.0,
               probe.guest_between_us() / 1000.0);
}

void check_report_prints_every_attribution() {
  auto &probe = gt4::perf::Probe::shared();
  // Anonymous temporary file: the fixture must never write into the
  // repository or into a pinned evidence directory.
  std::FILE *stream = nullptr;
  if (tmpfile_s(&stream) != 0 || !stream)
    throw std::runtime_error("cannot open a temporary probe report output");
  probe.print_interval(stream, "fixture interval");
  std::rewind(stream);
  std::string text;
  char line[512];
  while (std::fgets(line, sizeof line, stream))
    text += line;
  std::fclose(stream);
  for (const char *needle : {"frames/s", "frame period ms", "median",
                             "guest_between", "host_vblank", "gs_submit",
                             "acquire", "present_wait", "audio", "gs_execute",
                             "dispatches", "primitives", "1146880"})
    require(text.find(needle) != std::string::npos,
            (std::string("probe report lacks '") + needle + "'").c_str());
  std::fprintf(stderr, "[fixture] probe report: attribution lines complete\n");
  // Reporting restarts the interval, so the fixture leaves no stale totals.
  probe.restart();
  require(probe.frames() == 0, "restart must clear the measured interval");
}

} // namespace

int main() {
  try {
    check_disabled_probe_ignores_measurements();
    check_accounting();
    check_report_prints_every_attribution();
    std::fprintf(stderr, "[fixture] PASS: host performance probe accounting\n");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "[fixture] FAIL: %s\n", error.what());
    return 1;
  }
}
