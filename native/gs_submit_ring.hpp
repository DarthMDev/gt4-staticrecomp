#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <utility>

namespace gt4::vulkan {

// Bookkeeping for asynchronous GS submissions, with no Vulkan state.
//
// Submissions receive consecutive timeline ids starting at 1 and occupy slots
// round-robin. The backend supplies the current completed timeline value and a
// blocking wait for a timeline value. A slot is reused only after the submission
// that last used it has completed, so the CPU blocks only when the ring is full.
class SubmitRing {
public:
  using CompletedQuery = std::function<std::uint64_t()>;
  using Wait = std::function<void(std::uint64_t value)>;

  SubmitRing(std::uint32_t slots, CompletedQuery completed, Wait wait)
      : slots_(slots), completed_(std::move(completed)), wait_(std::move(wait)) {
    if (!slots_ || !completed_ || !wait_)
      throw std::invalid_argument("SubmitRing needs slots and callbacks");
  }

  std::uint32_t slot_count() const { return slots_; }

  // Id that the submission now being recorded will receive.
  std::uint64_t next_id() const { return next_; }

  // Highest id that has been committed; zero before the first commit.
  std::uint64_t last_committed() const { return next_ - 1; }

  std::uint32_t slot_for(std::uint64_t id) const {
    if (!id)
      throw std::invalid_argument("Submission ids start at 1");
    return static_cast<std::uint32_t>((id - 1) % slots_);
  }

  // Chooses the slot for next_id(). Blocks only while the submission that
  // used the same slot, slots ids earlier, has not completed.
  std::uint32_t reserve() {
    const std::uint64_t previous = next_ > slots_ ? next_ - slots_ : 0;
    if (previous && completed_() < previous)
      wait_(previous);
    return slot_for(next_);
  }

  // Consumes next_id() after the backend has submitted it.
  std::uint64_t commit() { return next_++; }

  // Blocks until every committed submission has completed (shutdown, readback).
  void drain() {
    const std::uint64_t last = last_committed();
    if (last && completed_() < last)
      wait_(last);
  }

  // A cache entry last used by submission `used` may be overwritten by the
  // host only after that submission has completed on the GPU.
  bool may_overwrite(std::uint64_t used) const { return used <= completed_(); }

private:
  std::uint32_t slots_;
  std::uint64_t next_ = 1;
  CompletedQuery completed_;
  Wait wait_;
};

} // namespace gt4::vulkan
