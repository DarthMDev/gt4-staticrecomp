#include "shape_observer_adapter.hpp"

#include "kernel.hpp"
#include "ps2/memory.hpp"

#include <limits>

namespace gt4::observation {
namespace {
ShapeObserver observer;
std::uint64_t invalid_owners = 0;

MemoryView runtime_memory() noexcept {
  return {std::as_bytes(std::span(ps2::g_ram, ps2::kRamSize)),
          std::as_bytes(std::span(ps2::g_spr, ps2::kScratchpadSize))};
}
} // namespace

ShapeObserver &runtime_observer() noexcept { return observer; }
std::uint64_t invalid_owner_count() noexcept { return invalid_owners; }

void invalidate_runtime_owner(int owner_id) noexcept {
  if (owner_id > 0)
    observer.invalidate_owner(static_cast<std::uint32_t>(owner_id));
}

void reset_runtime_epoch() noexcept { observer.reset_epoch(); }

bool observe_probe(const ps2::Cpu &cpu, std::uint32_t probe_pc) noexcept {
  if (probe_pc != shape_call_pc && probe_pc != shape_return_pc)
    return false;
  if (!observer.enabled())
    return true;
  if (ps2::kernel::in_handler()) {
    if (probe_pc == shape_call_pc)
      observer.begin(0, true, {});
    else
      observer.complete(0, true, cpu.pc, 0, {});
    return true;
  }
  const int owner_id = ps2::kernel::current_thread();
  if (owner_id <= 0) {
    if (invalid_owners != std::numeric_limits<std::uint64_t>::max())
      ++invalid_owners;
    return true;
  }
  const auto owner = static_cast<std::uint32_t>(owner_id);
  if (probe_pc == shape_call_pc) {
    Inputs inputs;
    for (std::size_t argument = 0; argument < 4; ++argument)
      for (std::size_t lane = 0; lane < 4; ++lane)
        inputs.argument_bits[argument][lane] = cpu.r[argument + 4].w[lane];
    inputs.fpr12_bits = cpu.f[12];
    inputs.guest_sp = cpu.r[29].w[0];
    inputs.metadata = sample_metadata(runtime_memory(), inputs.argument_bits[0][0]);
    observer.begin(owner, false, inputs);
  } else {
    observer.complete_from_memory(owner, false, cpu.pc, cpu.r[29].w[0], runtime_memory());
  }
  return true;
}

} // namespace gt4::observation
