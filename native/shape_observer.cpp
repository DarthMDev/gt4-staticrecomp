#include "shape_observer.hpp"

#include <bit>
#include <cstring>
#include <limits>

namespace gt4::observation {
namespace {
static_assert(std::endian::native == std::endian::little);

void increment(std::uint64_t &value, std::uint64_t amount = 1) noexcept {
  const auto remaining = std::numeric_limits<std::uint64_t>::max() - value;
  value += amount < remaining ? amount : remaining;
}

template <typename T>
bool read_field(MemoryView memory, std::uint32_t base, std::uint32_t offset,
                T &value) noexcept {
  if (offset > std::numeric_limits<std::uint32_t>::max() - base)
    return false;
  return copy_guest(memory, base + offset, std::as_writable_bytes(std::span(&value, 1)));
}
} // namespace

bool copy_guest(MemoryView memory, std::uint32_t address,
                std::span<std::byte> destination) noexcept {
  if (destination.empty())
    return false;
  std::span<const std::byte> storage;
  std::uint32_t offset = 0, limit = 0;
  if (address < 0x02000000) {
    storage = memory.ram;
    offset = address;
    limit = 0x02000000;
  } else if (address >= 0x70000000 && address < 0x70004000) {
    storage = memory.scratchpad;
    offset = address - 0x70000000;
    limit = 0x4000;
  } else {
    return false;
  }
  if (destination.size() > limit - offset || offset > storage.size() ||
      destination.size() > storage.size() - offset)
    return false;
  std::memcpy(destination.data(), storage.data() + offset, destination.size());
  return true;
}

Metadata sample_metadata(MemoryView memory, std::uint32_t shape_address) noexcept {
  Metadata sample;
  if (read_field(memory, shape_state_address, 3, sample.flag))
    sample.valid_fields |= flag_valid;
  if (read_field(memory, shape_state_address, 0x14, sample.enabled))
    sample.valid_fields |= enabled_valid;
  if (read_field(memory, shape_state_address, 0x20, sample.cached_bits))
    sample.valid_fields |= cached_bits_valid;
  if (read_field(memory, shape_state_address, 0x1F8, sample.cursor))
    sample.valid_fields |= cursor_valid;
  if (shape_address != 0) {
    if (read_field(memory, shape_address, 4, sample.shape_bits))
      sample.valid_fields |= shape_bits_valid;
    if (read_field(memory, shape_address, 0xA, sample.descriptor_count))
      sample.valid_fields |= count_valid;
  }
  return sample;
}

ShapeObserver::Owner *ShapeObserver::find_owner(std::uint32_t owner_id) noexcept {
  for (auto &owner : owners_)
    if (owner.occupied && owner.id == owner_id)
      return &owner;
  return nullptr;
}

bool ShapeObserver::advance(std::uint64_t &identifier) noexcept {
  if (identifier == std::numeric_limits<std::uint64_t>::max()) {
    stop();
    return false;
  }
  ++identifier;
  return true;
}

void ShapeObserver::start() noexcept {
  stop();
  counters_ = {};
  record_count_ = 0;
  enabled_ = advance(epoch_);
}

void ShapeObserver::stop() noexcept {
  enabled_ = false;
  for (auto &owner : owners_) {
    increment(counters_.aborted, owner.count);
    owner.occupied = false;
    owner.count = 0;
  }
}

void ShapeObserver::reset_epoch() noexcept {
  const bool was_enabled = enabled_;
  stop();
  enabled_ = advance(epoch_) && was_enabled;
}

void ShapeObserver::invalidate_owner(std::uint32_t owner_id) noexcept {
  if (auto *owner = find_owner(owner_id)) {
    increment(counters_.aborted, owner->count);
    owner->occupied = false;
    owner->count = 0;
  }
}

void ShapeObserver::begin(std::uint32_t owner_id, bool in_handler,
                          const Inputs &inputs) noexcept {
  if (!enabled_)
    return;
  if (in_handler) {
    increment(counters_.handler_skipped);
    return;
  }
  auto *owner = find_owner(owner_id);
  if (!owner) {
    for (auto &candidate : owners_)
      if (!candidate.occupied) {
        owner = &candidate;
        break;
      }
    if (!owner) {
      increment(counters_.capacity_rejected);
      stop();
      return;
    }
    if (!advance(next_incarnation_))
      return;
    owner->occupied = true;
    owner->id = owner_id;
    owner->incarnation = next_incarnation_;
  }
  for (std::size_t index = 0; index < owner->count; ++index)
    if (owner->pending[index].before.guest_sp == inputs.guest_sp) {
      increment(counters_.ambiguous_stack);
      stop();
      return;
    }
  if (owner->count == pending_per_owner) {
    increment(counters_.capacity_rejected);
    stop();
    return;
  }
  if (!advance(next_sequence_))
    return;
  owner->pending[owner->count++] = {epoch_, owner->incarnation, next_sequence_,
                                   owner_id, inputs, {}};
  increment(counters_.started);
  if (inputs.metadata.valid_fields != all_fields_valid)
    increment(counters_.incomplete_samples);
}

void ShapeObserver::complete(std::uint32_t owner_id, bool in_handler,
                             std::uint32_t return_pc, std::uint32_t guest_sp,
                             const Metadata &after) noexcept {
  if (!enabled_)
    return;
  if (in_handler) {
    increment(counters_.handler_skipped);
    return;
  }
  if (return_pc != shape_return_pc) {
    increment(counters_.rejected_return);
    return;
  }
  auto *owner = find_owner(owner_id);
  if (!owner || owner->count == 0) {
    increment(counters_.orphan);
    return;
  }
  auto &pending = owner->pending[owner->count - 1];
  if (pending.before.guest_sp != guest_sp) {
    increment(counters_.ambiguous_stack);
    stop();
    return;
  }
  --owner->count;
  increment(counters_.completed);
  if (after.valid_fields != all_fields_valid)
    increment(counters_.incomplete_samples);
  if (record_count_ == record_limit) {
    increment(counters_.record_dropped);
    stop();
    return;
  }
  pending.after = after;
  records_[record_count_++] = pending;
}

} // namespace gt4::observation
