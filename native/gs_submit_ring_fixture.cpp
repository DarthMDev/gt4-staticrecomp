// Pure ring fixture: slot order, ring-full blocking, cache eviction guard and
// shutdown drain, against a fake timeline. No Vulkan device is needed.
#include "gs_submit_ring.hpp"

#include <cstdint>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <vector>

namespace {

using gt4::vulkan::SubmitRing;

// The fake GPU completes values up to `completed`. A wait advances completion
// to the requested value and records the call, like a blocking timeline wait.
struct FakeTimeline {
  std::uint64_t completed = 0;
  std::vector<std::uint64_t> waits;

  SubmitRing make(std::uint32_t slots) {
    return SubmitRing(
        slots, [this] { return completed; },
        [this](std::uint64_t value) {
          waits.push_back(value);
          if (value > completed)
            completed = value;
        });
  }
};

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void check_slot_order() {
  FakeTimeline timeline;
  SubmitRing ring = timeline.make(3);
  const std::uint32_t expected[] = {0, 1, 2, 0, 1, 2, 0};
  for (std::uint64_t id = 1; id <= 7; ++id) {
    require(ring.next_id() == id, "ids are consecutive from 1");
    require(ring.reserve() == expected[id - 1], "slots are reused in order");
    require(ring.slot_for(id) == expected[id - 1], "slot_for matches reserve");
    ring.commit();
  }
  // Completion was never queried as needed: the first three submissions fill the
  // ring without waiting, and ids four and later wait only on their own predecessor.
  require(timeline.waits.size() == 4, "one blocking wait per reuse");
  require(timeline.waits.front() == 1 && timeline.waits.back() == 4,
          "reuse waits on the submission that last used the slot");
  std::fprintf(stderr, "[ring] slot order and reuse waits passed\n");
}

void check_ring_full_blocking() {
  FakeTimeline timeline;
  SubmitRing ring = timeline.make(3);
  for (int index = 0; index < 3; ++index) {
    ring.reserve();
    ring.commit();
  }
  require(timeline.waits.empty(), "a non-full ring never waits");
  // Submission four needs submission one complete: the ring is full.
  ring.reserve();
  require(timeline.waits.size() == 1 && timeline.waits.back() == 1,
          "a full ring waits for the oldest submission only");
  ring.commit();
  // With submission two already complete, submission five does not wait at all.
  timeline.completed = 2;
  timeline.waits.clear();
  ring.reserve();
  require(timeline.waits.empty(), "a completed predecessor does not block");
  ring.commit();
  std::fprintf(stderr, "[ring] ring-full blocking passed\n");
}

void check_eviction_guard() {
  FakeTimeline timeline;
  SubmitRing ring = timeline.make(3);
  ring.reserve();
  ring.commit();  // id 1 committed, not complete
  require(!ring.may_overwrite(1), "an incomplete entry is protected");
  require(!ring.may_overwrite(ring.next_id()),
          "the batch being recorded is protected");
  timeline.completed = 1;
  require(ring.may_overwrite(1), "a completed entry may be evicted");
  require(!ring.may_overwrite(2), "a later entry stays protected");
  std::fprintf(stderr, "[ring] cache eviction guard passed\n");
}

void check_shutdown_drain() {
  FakeTimeline timeline;
  SubmitRing ring = timeline.make(3);
  ring.drain();
  require(timeline.waits.empty(), "draining nothing does not wait");
  for (int index = 0; index < 2; ++index) {
    ring.reserve();
    ring.commit();
  }
  ring.drain();
  require(timeline.waits.size() == 1 && timeline.waits.back() == 2,
          "drain waits for the last committed submission");
  require(timeline.completed == 2, "drain leaves nothing pending");
  ring.drain();
  require(timeline.waits.size() == 1, "a drained ring does not wait again");
  std::fprintf(stderr, "[ring] shutdown drain passed\n");
}

void check_invalid_configuration() {
  bool refused = false;
  try {
    FakeTimeline timeline;
    timeline.make(0);
  } catch (const std::invalid_argument &) {
    refused = true;
  }
  require(refused, "zero slots are refused");
  refused = false;
  try {
    FakeTimeline timeline;
    SubmitRing ring = timeline.make(3);
    ring.slot_for(0);
  } catch (const std::invalid_argument &) {
    refused = true;
  }
  require(refused, "submission id zero is refused");
}

// Random completion lag: the fake GPU completes at most two submissions behind
// the CPU, sometimes not at all. The reuse invariant must hold throughout.
void check_random_lag() {
  std::mt19937 random(7);
  FakeTimeline timeline;
  SubmitRing ring = timeline.make(3);
  for (int step = 0; step < 4000; ++step) {
    const std::uint64_t id = ring.next_id();
    ring.reserve();
    require(timeline.completed + 3 >= id,
            "a reserved slot has completed its previous submission");
    ring.commit();
    if (random() % 3 == 0 && timeline.completed + 1 < ring.next_id())
      ++timeline.completed;
  }
  std::fprintf(stderr, "[ring] random completion lag passed (%zu waits)\n",
               timeline.waits.size());
}

} // namespace

int main() {
  try {
    check_slot_order();
    check_ring_full_blocking();
    check_eviction_guard();
    check_shutdown_drain();
    check_invalid_configuration();
    check_random_lag();
    std::fprintf(stderr, "[ring] PASS: slot order, ring-full blocking, eviction guard, drain\n");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "[ring] FAIL: %s\n", error.what());
    return 1;
  }
}
