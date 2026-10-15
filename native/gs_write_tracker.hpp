#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace gt4::vulkan {

// Which submission last wrote each 8 KiB page of GS VRAM, plus the writes that are
// recorded but not yet submitted. A CPU read of bytes [lo, hi) must wait only for
// latest_writer(lo, hi), and must submit pending work first only when a pending
// write overlaps the read. Every write marks all pages its byte range touches, so
// the wait is never shorter than the writers of the bytes read.
//
// Submissions share one queue and form a timeline chain, so completing id N also
// completes every id below N. Waiting for the latest writer is therefore enough.
class WriteTracker {
public:
  static constexpr std::uint64_t kPageBytes = 8192;
  static constexpr std::uint64_t kMemoryBytes = 4u << 20;  // 4 MiB of GS VRAM
  static constexpr std::uint64_t kPages = kMemoryBytes / kPageBytes;
  // Beyond this many pending spans, treat all of VRAM as pending (always safe).
  static constexpr std::size_t kPendingLimit = 4096;

  // Records a write to bytes [lo, hi) that is not yet part of any submission.
  void note_write(std::uint64_t lo, std::uint64_t hi) {
    if (hi <= lo)
      return;
    if (pending_.size() >= kPendingLimit) {
      pending_.assign(1, Span{0, kMemoryBytes});
      return;
    }
    pending_.push_back(Span{lo, hi});
  }

  bool has_pending() const { return !pending_.empty(); }

  // True when a pending write touches a byte of [lo, hi).
  bool pending_overlaps(std::uint64_t lo, std::uint64_t hi) const {
    for (const Span &span : pending_)
      if (span.lo < hi && lo < span.hi)
        return true;
    return false;
  }

  // Gives every pending write the id of the submission that now carries it.
  void submitted(std::uint64_t id) {
    if (pending_.empty())
      return;
    if (!id)
      throw std::logic_error("VRAM writes were recorded without a GPU submission");
    for (const Span &span : pending_)
      for_each_page(span.lo, span.hi, [&](std::uint64_t page) { writer_[page] = id; });
    pending_.clear();
  }

  // Largest submission id that wrote a page touched by [lo, hi); zero when none did.
  std::uint64_t latest_writer(std::uint64_t lo, std::uint64_t hi) const {
    std::uint64_t latest = 0;
    for_each_page(lo, hi, [&](std::uint64_t page) { latest = std::max(latest, writer_[page]); });
    return latest;
  }

  std::uint64_t writer_of_page(std::uint64_t page) const { return writer_[page % kPages]; }

private:
  struct Span {
    std::uint64_t lo, hi;
  };

  // Visits the page index (address / 8 KiB, modulo VRAM size, as the donor does) of every
  // page touched by bytes [lo, hi). Ranges of a full VRAM or more visit every page once.
  template <typename Visit> static void for_each_page(std::uint64_t lo, std::uint64_t hi, Visit visit) {
    if (hi <= lo)
      return;
    const std::uint64_t first = lo / kPageBytes, last = (hi - 1) / kPageBytes;
    const std::uint64_t count = std::min(last - first + 1, kPages);
    for (std::uint64_t index = 0; index < count; ++index)
      visit((first + index) % kPages);
  }

  std::vector<Span> pending_;
  std::uint64_t writer_[kPages] = {};
};

} // namespace gt4::vulkan
