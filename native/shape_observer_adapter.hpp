#pragma once

#include "shape_observer.hpp"

namespace ps2 { struct Cpu; }

namespace gt4::observation {

// EE-owner operations only; no observer state is serialized into the guest.
ShapeObserver &runtime_observer() noexcept;
bool observe_probe(const ps2::Cpu &cpu, std::uint32_t probe_pc) noexcept;
void invalidate_runtime_owner(int owner_id) noexcept;
void reset_runtime_epoch() noexcept;
std::uint64_t invalid_owner_count() noexcept;

} // namespace gt4::observation
