// TASK-084 step 1: the VIF1 chain batch payload (native/mtvu_vif_batch.hpp).
//
// Checks: an empty batch carries only its header; a batch round-trips every appended packet byte for byte, in order,
// including zero-qwc packets and large packets; a size-bounded batch never exceeds its bound; malformed payloads
// (short header, truncated packet header, truncated packet, trailing bytes) are rejected. The fixture is pure host
// code: it never runs a worker and never touches guest state.
#include "mtvu_vif_batch.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <stdexcept>
#include <vector>

namespace {

int failures = 0;

void require(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

struct Packet {
  std::vector<std::uint8_t> bytes;
  std::uint32_t qwc = 0;
};

void check_empty_batch() {
  gt4::mtvu::VifBatch batch;
  require(batch.empty(), "a new batch is empty");
  require(batch.packets() == 0, "a new batch has no packets");
  require(batch.size() == gt4::mtvu::kVifBatchHeaderBytes, "an empty batch holds only its header");
  const std::uint8_t *payload = batch.payload();
  std::uint32_t count = 0xFFFFFFFFu;
  std::memcpy(&count, payload, 4);
  require(count == 0, "an empty batch writes a zero packet count");
  int visited = 0;
  gt4::mtvu::walk_vif_batch(payload, batch.size(), [&visited](const std::uint8_t *, std::uint32_t) { ++visited; });
  require(visited == 0, "an empty batch visits no packet");
}

void check_round_trip(std::uint64_t seed) {
  std::mt19937_64 rng(seed);
  gt4::mtvu::VifBatch batch;
  std::vector<Packet> expected;
  const unsigned packets = static_cast<unsigned>(1 + rng() % 40);
  for (unsigned index = 0; index < packets; ++index) {
    const std::uint32_t qwc = (rng() % 10 == 0) ? 0u : static_cast<std::uint32_t>(rng() % 64);
    Packet packet;
    packet.qwc = qwc;
    packet.bytes.resize(static_cast<std::size_t>(qwc) * 16);
    for (std::uint8_t &byte : packet.bytes) byte = static_cast<std::uint8_t>(rng());
    batch.append(packet.bytes.data(), qwc);
    expected.push_back(std::move(packet));
  }
  require(!batch.empty(), "a filled batch is not empty");
  require(batch.packets() == packets, "packet count matches the appends");
  std::size_t expected_bytes = gt4::mtvu::kVifBatchHeaderBytes;
  for (const Packet &packet : expected) expected_bytes += 4 + packet.bytes.size();
  require(batch.size() == expected_bytes, "batch size counts every packet length and payload");

  const std::uint8_t *payload = batch.payload();
  std::size_t index = 0;
  gt4::mtvu::walk_vif_batch(payload, batch.size(), [&](const std::uint8_t *data, std::uint32_t qwc) {
    require(index < expected.size(), "the walk visits no extra packet");
    if (index >= expected.size()) return;
    require(qwc == expected[index].qwc, "packet qwc round-trips");
    require(std::vector<std::uint8_t>(data, data + expected[index].bytes.size()) == expected[index].bytes,
            "packet bytes round-trip");
    ++index;
  });
  require(index == expected.size(), "the walk visits every packet");

  batch.reset();
  require(batch.empty() && batch.packets() == 0, "reset empties the batch");
  require(batch.size() == gt4::mtvu::kVifBatchHeaderBytes, "reset restores the header-only size");
}

void check_size_bound() {
  // The runtime posts a batch before it would cross its bound, so a batch never exceeds the bound it was built with.
  constexpr std::size_t bound = 512;
  gt4::mtvu::VifBatch batch;
  std::vector<std::uint8_t> packet(16 * 8, 0xA5);
  int posts = 0;
  for (int index = 0; index < 100; ++index) {
    if (!batch.empty() && batch.size() + 4 + packet.size() > bound) {
      require(batch.size() <= bound, "a posted batch stays inside the bound");
      batch.reset();
      ++posts;
    }
    batch.append(packet.data(), 8);
  }
  require(posts > 0, "the bounded loop posted at least one full batch");
  require(batch.size() <= bound, "the last batch stays inside the bound");
}

void expect_rejected(const std::vector<std::uint8_t> &payload, const char *what) {
  bool rejected = false;
  try {
    gt4::mtvu::walk_vif_batch(payload.data(), payload.size(), [](const std::uint8_t *, std::uint32_t) {});
  } catch (const std::logic_error &) {
    rejected = true;
  }
  require(rejected, what);
}

void check_malformed() {
  expect_rejected({}, "a batch shorter than its header is rejected");
  expect_rejected({1, 0, 0}, "a three-byte batch is rejected");
  expect_rejected({1, 0, 0, 0}, "a missing packet header is rejected");
  expect_rejected({1, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0}, "a truncated packet is rejected");
  expect_rejected({0, 0, 0, 0, 0, 0, 0, 0}, "trailing bytes after the last packet are rejected");
}

}  // namespace

int main() {
  check_empty_batch();
  for (std::uint64_t seed = 1; seed <= 200; ++seed) check_round_trip(seed * 2654435761u + 17u);
  check_size_bound();
  check_malformed();
  if (failures != 0) {
    std::fprintf(stderr, "mtvu_vif_batch fixture: %d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  std::puts("mtvu_vif_batch fixture: ok");
  return EXIT_SUCCESS;
}
