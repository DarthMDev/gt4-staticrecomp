// CTest native_mtvu_ring: host-only controls for the TASK-011 step 1 SPSC byte ring (mtvu_ring.hpp).
// Covers layout, FIFO, full and empty, wraparound against a model, threaded producer/consumer order with
// random record sizes and delays on tiny and small rings, and the oversize rejection.
#include "mtvu_ring.hpp"

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
        bell.wait_changed(seen);
      }
      if (rng() % 64 == 0) std::this_thread::yield();
    }
    RecordHeader stop;
    stop.kind = static_cast<std::uint16_t>(RecordKind::Stop);
    for (;;) {
      const std::uint32_t seen = bell.snapshot();
      if (ring.try_push(stop, nullptr)) break;
      bell.wait_changed(seen);
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
    bell.wait_changed(seen);
  }
  producer.join();
  require(expected_job == records, "every record delivered once");
  require(ring.empty(), "ring empty after stop");
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
  } catch (const std::exception &error) {
    std::fprintf(stderr, "native_mtvu_ring failed: %s\n", error.what());
    return 1;
  }
  std::printf("native_mtvu_ring ok: layout, FIFO, full/empty, oversize, wraparound model (3 rings x 200000 steps), threaded order (3 rings x 200000 records)\n");
  return 0;
}
