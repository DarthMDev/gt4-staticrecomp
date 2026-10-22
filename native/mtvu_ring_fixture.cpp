// CTest native_mtvu_ring: host-only controls for the TASK-011 step 1 SPSC byte ring (mtvu_ring.hpp).
// Covers layout, FIFO, full and empty, wraparound against a model, threaded producer/consumer order with
// random record sizes and delays on tiny and small rings, and the oversize rejection.
#include "mtvu_ring.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using namespace gt4::mtvu;

void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

// Pure function of the size and sequence number, so the consumer can recompute what the producer wrote.
std::vector<std::uint8_t> make_payload(std::size_t bytes, std::uint64_t sequence) {
  std::vector<std::uint8_t> payload(bytes);
  for (std::size_t i = 0; i < bytes; ++i) payload[i] = static_cast<std::uint8_t>(sequence * 131u + i * 7u + 3u);
  return payload;
}

void check_layout() {
  require(sizeof(RecordHeader) == 16, "header size");
  require(record_bytes(0) == 16 && record_bytes(1) == 32 && record_bytes(16) == 32 && record_bytes(17) == 48,
          "record padding");
  Doorbell bell;
  SpscByteRing ring(256, bell);
  require(ring.max_payload() == 112, "max payload of a 256-byte ring");
  require(ring.empty() && ring.used() == 0, "new ring is empty");
  bool rejected = false;
  try {
    SpscByteRing bad(100, bell);
    (void)bad;
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "capacity must be a multiple of 32");
}

void check_fifo_full_empty() {
  Doorbell bell;
  SpscByteRing ring(128, bell);
  std::vector<std::uint8_t> out;
  RecordHeader header;
  require(!ring.try_pop(header, out), "pop from empty ring fails");

  // Each 16-byte payload record occupies 32 bytes: four records fill the 128-byte ring exactly.
  std::mt19937_64 rng(1);
  for (std::uint64_t i = 0; i < 4; ++i) {
    RecordHeader h;
    h.kind = static_cast<std::uint16_t>(RecordKind::VifPacket);
    h.job = i;
    h.bytes = 16;
    const auto payload = make_payload(16, i);
    require(ring.try_push(h, payload.data()), "push into a non-full ring");
  }
  require(ring.used() == 128, "ring holds four records");
  RecordHeader full;
  full.kind = static_cast<std::uint16_t>(RecordKind::VifWrite);
  full.bytes = 0;
  require(!ring.try_push(full, nullptr), "push into a full ring fails");

  require(ring.try_pop(header, out) && header.job == 0 && out.size() == 16, "first record out");
  require(ring.try_push(full, nullptr), "space after one pop");
  for (std::uint64_t expect = 1; expect < 4; ++expect) {
    require(ring.try_pop(header, out) && header.job == expect, "FIFO order");
  }
  require(ring.try_pop(header, out) && header.kind == static_cast<std::uint16_t>(RecordKind::VifWrite),
          "last record out");
  require(!ring.try_pop(header, out) && ring.empty(), "drained ring is empty");
}

void check_oversize_rejected() {
  Doorbell bell;
  SpscByteRing ring(256, bell);
  RecordHeader h;
  h.bytes = 113;
  std::vector<std::uint8_t> payload(113, 0);
  bool thrown = false;
  try {
    ring.try_push(h, payload.data());
  } catch (const std::logic_error &) {
    thrown = true;
  }
  require(thrown, "payload above max_payload throws");
}

// Single thread, model-checked: random sizes, random interleaving, many wraps.
void check_wraparound_model(std::size_t capacity, std::uint64_t seed, int operations) {
  Doorbell bell;
  SpscByteRing ring(capacity, bell);
  std::mt19937_64 rng(seed);
  std::deque<std::pair<RecordHeader, std::vector<std::uint8_t>>> model;
  std::uint64_t next_sequence = 0;
  std::uint64_t record_bytes_pushed = 0;
  std::vector<std::uint8_t> out;
  RecordHeader h;
  for (int step = 0; step < operations; ++step) {
    const bool push = (rng() % 100) < 55;
    if (push) {
      const std::size_t bytes = rng() % (ring.max_payload() + 1);
      RecordHeader header;
      header.kind = static_cast<std::uint16_t>(RecordKind::Gif);
      header.path = static_cast<std::uint8_t>(1 + rng() % 2);
      header.job = next_sequence;
      header.bytes = static_cast<std::uint32_t>(bytes);
      const auto payload = make_payload(bytes, next_sequence);
      if (ring.try_push(header, payload.data())) {
        model.emplace_back(header, payload);
        record_bytes_pushed += record_bytes(bytes);
        ++next_sequence;
      } else {
        require(!model.empty(), "an empty ring must accept any legal record");
      }
    } else if (!model.empty()) {
      require(ring.try_pop(h, out), "model says a record is waiting");
      const auto &expected = model.front();
      require(h.job == expected.first.job && h.path == expected.first.path && h.bytes == expected.second.size(),
              "record header order");
      require(out == expected.second, "record payload order");
      model.pop_front();
    } else {
      require(!ring.try_pop(h, out) && ring.empty(), "empty model, ring must be empty");
    }
  }
  // Drain and verify the tail of the model.
  while (!model.empty()) {
    require(ring.try_pop(h, out) && h.job == model.front().first.job && out == model.front().second, "drain order");
    model.pop_front();
  }
  require(!ring.try_pop(h, out) && ring.empty(), "ring empty after model drain");
  // Positions are monotonic: the test must have wrapped the buffer many times.
  require(record_bytes_pushed > 20 * capacity, "model wrapped the ring");
}

// Threaded producer and consumer with blocking waits through the shared doorbell.
void check_threaded_order(std::size_t capacity, std::uint64_t seed, std::uint64_t records) {
  Doorbell bell;
  SpscByteRing ring(capacity, bell);
  std::thread producer([&] {
    std::mt19937_64 rng(seed);
    for (std::uint64_t i = 0; i < records; ++i) {
      const std::size_t bytes = rng() % (ring.max_payload() + 1);
      RecordHeader h;
      h.kind = static_cast<std::uint16_t>(RecordKind::VifPacket);
      h.job = i;
      h.bytes = static_cast<std::uint32_t>(bytes);
      const auto payload = make_payload(bytes, i);
      for (;;) {
        const std::uint32_t seen = bell.snapshot();
        if (ring.try_push(h, payload.data())) break;
        bool slept = false;
        bell.wait_changed(seen, slept);
      }
      if (rng() % 64 == 0) std::this_thread::yield();
    }
    RecordHeader stop;
    stop.kind = static_cast<std::uint16_t>(RecordKind::Stop);
    for (;;) {
      const std::uint32_t seen = bell.snapshot();
      if (ring.try_push(stop, nullptr)) break;
      bool slept = false;
      bell.wait_changed(seen, slept);
    }
  });

  std::mt19937_64 rng(seed ^ 0x9E3779B97F4A7C15ull);
  std::uint64_t expected_job = 0;
  std::vector<std::uint8_t> out;
  RecordHeader h;
  for (;;) {
    const std::uint32_t seen = bell.snapshot();
    if (ring.try_pop(h, out)) {
      if (h.kind == static_cast<std::uint16_t>(RecordKind::Stop)) break;
      require(h.job == expected_job, "threaded FIFO order");
      require(out.size() == h.bytes, "threaded payload size");
      const auto expected = make_payload(h.bytes, h.job);
      require(out == expected, "threaded payload content");
      ++expected_job;
      if (rng() % 32 == 0) std::this_thread::sleep_for(std::chrono::microseconds(rng() % 20));
      continue;
    }
    bool slept = false;
    bell.wait_changed(seen, slept);
  }
  producer.join();
  require(expected_job == records, "every record delivered once");
  require(ring.empty(), "ring empty after stop");
}

// TASK-084 step 1: the sleeping-flag wakeup policy and its counters.
void check_doorbell_policy() {
  Doorbell bell;
  Doorbell::Stats stats = bell.stats();
  require(stats.rings == 0 && stats.waits == 0 && stats.wakeups == 0, "a new doorbell has no traffic");

  // A ring while nobody sleeps must not count a wakeup, and it must move the value.
  const std::uint32_t before = bell.snapshot();
  bell.ring();
  require(bell.snapshot() != before, "a ring moves the doorbell value");
  stats = bell.stats();
  require(stats.rings == 1, "a ring is counted");
  require(stats.wakeups == 0, "a ring with nobody asleep waits for the next snapshot, no wakeup");

  // The re-check path: the value already moved, so the caller must not block.
  bool slept = true;
  bell.wait_changed(before, slept);
  require(!slept, "a wait whose value already moved returns without sleeping");
  stats = bell.stats();
  require(stats.waits == 1 && stats.wakeups == 0, "the re-check path counts a wait but no wakeup");
  require(bell.snapshot() != before, "the re-check path does not consume the value change");

  // A real wait: the peer rings while the waiter sleeps, and the wakeup is counted on both sides.
  const std::uint32_t seen = bell.snapshot();
  std::atomic<bool> slept_flag{false};
  std::thread waiter([&bell, seen, &slept_flag] {
    bool did_sleep = false;
    bell.wait_changed(seen, did_sleep);
    slept_flag.store(did_sleep);
  });
  for (int attempt = 0; attempt < 1000 && bell.stats().waits < 2; ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  bell.ring();
  waiter.join();
  require(slept_flag.load(), "a wait on an unchanged value really sleeps");
  stats = bell.stats();
  require(stats.waits == 2, "the sleeping wait is counted");
  require(stats.wakeups == 1, "the sleeping wait records one wakeup");
  require(stats.rings == 2, "both rings are counted");
}

// TASK-084 step 2: try_peek/release lend the record bytes without a copy; the tail only moves on release.
void check_peek_release() {
  Doorbell bell;
  SpscByteRing ring(256, bell);
  const auto first = make_payload(40, 1);
  const auto second = make_payload(64, 2);
  RecordHeader header;
  header.kind = static_cast<std::uint16_t>(RecordKind::Gif);
  header.job = 7;
  header.bytes = static_cast<std::uint32_t>(first.size());
  require(ring.try_push(header, first.data()), "peek: first push");
  header.bytes = static_cast<std::uint32_t>(second.size());
  header.job = 8;
  require(ring.try_push(header, second.data()), "peek: second push");

  RecordHeader seen;
  const std::uint8_t *payload = nullptr;
  require(ring.try_peek(seen, payload), "peek: first record visible");
  require(seen.job == 7 && seen.bytes == first.size(), "peek: first header");
  require(std::vector<std::uint8_t>(payload, payload + seen.bytes) == first, "peek: first payload");
  require(ring.used() == record_bytes(first.size()) + record_bytes(second.size()), "peek: the tail did not move");
  ring.release();
  require(ring.try_peek(seen, payload), "peek: second record visible");
  require(seen.job == 8 && seen.bytes == second.size(), "peek: second header");
  require(std::vector<std::uint8_t>(payload, payload + seen.bytes) == second, "peek: second payload");
  ring.release();
  require(!ring.try_peek(seen, payload), "peek: ring empty after both releases");
  require(ring.empty(), "peek: ring reports empty");
}

}  // namespace

int main() {
  try {
    check_layout();
    check_fifo_full_empty();
    check_oversize_rejected();
    check_wraparound_model(128, 11, 200000);
    check_wraparound_model(256, 12, 200000);
    check_wraparound_model(4096, 13, 200000);
    check_threaded_order(128, 21, 200000);
    check_threaded_order(512, 22, 200000);
    check_threaded_order(64 * 1024, 23, 200000);
    check_doorbell_policy();
    check_peek_release();
  } catch (const std::exception &error) {
    std::fprintf(stderr, "native_mtvu_ring failed: %s\n", error.what());
    return 1;
  }
  std::printf("native_mtvu_ring ok: layout, FIFO, full/empty, oversize, wraparound model (3 rings x 200000 steps), threaded order (3 rings x 200000 records), doorbell wakeup policy\n");
  return 0;
}
