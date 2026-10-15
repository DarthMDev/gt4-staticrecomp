// Pure fixture for page-tracked GS readbacks (slice 183): which submission a read waits for,
// when pending work is submitted, and that the bytes read equal a full-sync reference.
// No Vulkan device is needed. The model applies writes in submission order; a read of
// [lo, hi) waits for the latest writer of its pages and must match the full-sync state.
#include "gs_write_tracker.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <vector>

namespace {

using gt4::vulkan::WriteTracker;

constexpr std::uint64_t kMemory = WriteTracker::kMemoryBytes;
constexpr std::uint64_t kPage = WriteTracker::kPageBytes;

struct Write {
  std::uint64_t id;  // submission that carries the write (0 while pending)
  std::uint64_t lo, hi;
  std::uint8_t tag;  // distinct per write; zero is never used
};

// Model of the GPU queue: submitted writes in order, plus writes recorded but not yet submitted.
struct Model {
  WriteTracker tracker;
  std::vector<Write> submitted;
  std::vector<Write> pending;
  std::uint64_t last_id = 0;
  std::uint8_t next_tag = 1;

  void write(std::uint64_t lo, std::uint64_t hi) {
    tracker.note_write(lo, hi);
    pending.push_back(Write{0, lo, hi, next_tag});
    next_tag = next_tag == 255 ? 1 : static_cast<std::uint8_t>(next_tag + 1);
  }

  // Submits every pending write as one submission, as the adapter does on flush.
  void submit() {
    if (pending.empty())
      return;
    const std::uint64_t id = ++last_id;
    tracker.submitted(id);
    for (Write w : pending) {
      w.id = id;
      submitted.push_back(w);
    }
    pending.clear();
  }

  // Bytes of [lo, hi) as the GPU has written them through submission `through`.
  std::vector<std::uint8_t> bytes_through(std::uint64_t lo, std::uint64_t hi, std::uint64_t through) const {
    std::vector<std::uint8_t> out(hi - lo, 0);
    for (const Write &w : submitted) {
      if (w.id > through)
        continue;
      for (std::uint64_t at = std::max(lo, w.lo); at < std::min(hi, w.hi); ++at)
        out[at - lo] = w.tag;
    }
    return out;
  }

  // The submission a correct readback of [lo, hi) must wait for: the latest one with a write
  // to a page the range touches. Pages are the tracker granularity (8 KiB, 512 pages).
  std::uint64_t expected_wait(std::uint64_t lo, std::uint64_t hi) const {
    std::uint64_t latest = 0;
    for (const Write &w : submitted)
      if (w.id > latest && pages_touch(w.lo, w.hi, lo, hi))
        latest = w.id;
    return latest;
  }

  static bool pages_touch(std::uint64_t a_lo, std::uint64_t a_hi, std::uint64_t b_lo, std::uint64_t b_hi) {
    for (std::uint64_t pa = a_lo / kPage; pa <= (a_hi - 1) / kPage; ++pa)
      for (std::uint64_t pb = b_lo / kPage; pb <= (b_hi - 1) / kPage; ++pb)
        if (pa % WriteTracker::kPages == pb % WriteTracker::kPages)
          return true;
    return false;
  }

  bool pending_touches(std::uint64_t lo, std::uint64_t hi) const {
    for (const Write &w : pending)
      if (w.lo < hi && lo < w.hi)
        return true;
    return false;
  }
};

void require(bool condition, const char *what) {
  if (!condition)
    throw std::runtime_error(what);
}

// One readback: submit only when a pending write overlaps the bytes, wait for the expected
// writer, and compare the bytes with the full-sync state.
void read(Model &model, std::uint64_t lo, std::uint64_t hi, std::uint64_t &forced, std::uint64_t &checks) {
  const bool overlaps = model.tracker.pending_overlaps(lo, hi);
  require(overlaps == model.pending_touches(lo, hi), "pending overlap query differs from the model");
  if (overlaps) {
    model.submit();
    ++forced;
  }
  const std::uint64_t target = model.tracker.latest_writer(lo, hi);
  require(target == model.expected_wait(lo, hi), "readback waits for a different submission");
  require(target <= model.last_id, "readback waits for a submission that does not exist");
  require(model.bytes_through(lo, hi, target) == model.bytes_through(lo, hi, model.last_id),
          "readback bytes differ from the full-sync reference");
  ++checks;
}

void deterministic_cases() {
  Model m;
  // Page 0 written by submission 1, page 5 by submission 2: a read of page 0 waits for 1 only.
  m.write(0, 100);
  m.submit();
  m.write(5 * kPage, 5 * kPage + 64);
  m.submit();
  require(m.tracker.latest_writer(0, 16) == 1, "page 0 writer must be submission 1");
  require(m.tracker.latest_writer(5 * kPage, 5 * kPage + 1) == 2, "page 5 writer must be submission 2");
  require(m.tracker.latest_writer(kPage, 2 * kPage) == 0, "untouched pages have no writer");
  // A pending write to page 9 does not force a read of page 0.
  m.write(9 * kPage, 9 * kPage + 8);
  require(!m.tracker.pending_overlaps(0, 16), "unrelated pending write must not force a submit");
  require(m.tracker.latest_writer(0, 16) == 1, "read of page 0 must wait for submission 1 only");
  // A pending write to the read bytes must be submitted first, then waited for.
  m.write(8, 24);
  require(m.tracker.pending_overlaps(0, 16), "overlapping pending write must force a submit");
  m.submit();
  require(m.tracker.latest_writer(0, 16) == 3, "forced read must wait for the submission that wrote it");
  // A write across a page edge marks both pages.
  Model b;
  b.write(kPage - 4, kPage + 4);
  b.submit();
  require(b.tracker.latest_writer(kPage + 100, kPage + 101) == 1, "a write across a page edge marks both pages");
  // A whole-memory readback waits for the latest writer of any page.
  require(m.tracker.latest_writer(0, kMemory) == 3, "full-memory readback waits for the latest writer");
  // Writes without a submission id are refused rather than lost.
  Model c;
  c.tracker.note_write(0, 4);
  bool threw = false;
  try {
    c.tracker.submitted(0);
  } catch (const std::logic_error &) {
    threw = true;
  }
  require(threw, "writes without a submission id must throw");
  // Pending overflow collapses to all of VRAM: every read is then conservatively forced.
  Model d;
  for (std::size_t i = 0; i <= WriteTracker::kPendingLimit; ++i)
    d.tracker.note_write(i * 16, i * 16 + 16);
  require(d.tracker.pending_overlaps(kMemory - 16, kMemory), "overflowed pending writes must cover all of VRAM");
}

} // namespace

int main() {
  try {
    deterministic_cases();
    std::uint64_t forced = 0, checks = 0, writes = 0;
    std::mt19937_64 random(0x5EED183u);
    Model model;
    for (int step = 0; step < 6000; ++step) {
      const std::uint32_t action = static_cast<std::uint32_t>(random() % 10);
      if (action < 4) {
        // Writes cluster in a few areas, sometimes spanning pages, sometimes tiny.
        const std::uint64_t base = (random() % 48) * kPage + (random() % 3) * 256;
        const std::uint64_t length = 1 + random() % (random() % 4 == 0 ? 3 * kPage : 256);
        model.write(base, base + length);
        ++writes;
      } else if (action < 6) {
        model.submit();
      } else {
        const std::uint64_t base = (random() % 48) * kPage + (random() % kPage);
        const std::uint64_t length = 1 + random() % (random() % 3 == 0 ? 2 * kPage : 512);
        read(model, base, base + length, forced, checks);
      }
    }
    // Finally a full-memory readback after everything is submitted.
    model.submit();
    read(model, 0, kMemory, forced, checks);
    std::fprintf(stderr, "[gs_write_tracker] PASS: %llu writes, %llu readbacks checked (%llu forced submits)\n",
                 static_cast<unsigned long long>(writes), static_cast<unsigned long long>(checks),
                 static_cast<unsigned long long>(forced));
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "[gs_write_tracker] FAIL: %s\n", error.what());
    return 1;
  }
}
