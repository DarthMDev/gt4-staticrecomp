// CTest native_mtvu_perf: TASK-011 step 3 per-thread accounting. Scopes on a thread bound to a worker accounting
// land there (not in the shared probe, which stays valid), nested worker categories keep exclusive attribution,
// and the legacy report categories end before the MTVU categories.
#include "perf_probe.hpp"

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace {

using gt4::perf::Accounting;
using gt4::perf::Category;
using gt4::perf::Probe;
using gt4::perf::Scope;

void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

std::uint64_t wall_us() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

void pause_ms(int milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }

}  // namespace

int main() {
  try {
    require(gt4::perf::kLegacyCategoryCount == static_cast<std::size_t>(Category::mtvu_wait_vblank),
            "legacy report categories end before the MTVU categories");
    require(static_cast<std::size_t>(Category::mtvu_output) + 1 == static_cast<std::size_t>(Category::count),
            "MTVU categories are the last ones");

    Probe &probe = Probe::shared();
    Accounting worker(true);
    std::thread thread([&worker] {
      gt4::perf::bind_worker(&worker, wall_us);
      {
        Scope program(Category::vu1_interpret);
        pause_ms(4);
        {
          Scope output(Category::mtvu_output);
          pause_ms(3);
        }
        pause_ms(1);
      }
    });
    thread.join();

    require(probe.valid(), "shared probe stays valid: worker scopes never reach it");
    const auto &categories = worker.interval().categories;
    const auto program = categories[static_cast<std::size_t>(Category::vu1_interpret)].exclusive_us;
    const auto output = categories[static_cast<std::size_t>(Category::mtvu_output)].exclusive_us;
    require(output >= 2500 && output < 60000, "output time recorded on the worker accounting");
    require(program >= 4500 && program < 60000, "program time excludes the nested output time");
    require(categories[static_cast<std::size_t>(Category::mtvu_wait_vblank)].exclusive_us == 0,
            "no EE wait is recorded by the worker");
    require(worker.fault() == gt4::perf::AccountingFault::none, "worker accounting has no fault");
  } catch (const std::exception &error) {
    std::fprintf(stderr, "native_mtvu_perf failed: %s\n", error.what());
    return 1;
  }
  std::printf("native_mtvu_perf ok: worker scopes on the worker accounting, exclusive attribution, shared probe valid\n");
  return 0;
}
