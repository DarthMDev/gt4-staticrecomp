// TASK-084 step 1 (slice261): the payload of one VIF1 chain batch, the unit of work posted to the VU1 worker.
//
// Before TASK-084 the EE posted one job per VIF1 packet (about 5,200 per frame in a race). One DMA chain is now one
// job: the payload is `u32 packet count`, then per packet a `u32 qwc` followed by qwc * 16 bytes. The worker replays
// each packet through the same entry point it used for a single-packet job, so the VIF1 command sequence, the VU1
// programs and the emitted GIF bytes are unchanged; only the job boundary and the wake-ups change.
//
// Pure host code: no donor headers, no guest state. The fixture (native_mtvu_vif_batch) round-trips random batches and
// rejects malformed payloads.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace gt4::mtvu {

inline constexpr std::size_t kVifBatchHeaderBytes = 4;

class VifBatch {
 public:
  VifBatch() { bytes_.resize(kVifBatchHeaderBytes); }

  void append(const std::uint8_t *data, std::uint32_t qwc) {
    const std::size_t at = bytes_.size();
    const std::size_t packet_bytes = static_cast<std::size_t>(qwc) * 16;
    bytes_.resize(at + 4 + packet_bytes);
    std::memcpy(bytes_.data() + at, &qwc, 4);
    if (packet_bytes != 0) std::memcpy(bytes_.data() + at + 4, data, packet_bytes);
    ++packets_;
  }

  bool empty() const { return packets_ == 0; }
  std::uint32_t packets() const { return packets_; }
  // Complete record payload, with the packet count written into its first four bytes.
  const std::uint8_t *payload() {
    std::memcpy(bytes_.data(), &packets_, 4);
    return bytes_.data();
  }
  std::size_t size() const { return bytes_.size(); }
  void reset() {
    bytes_.resize(kVifBatchHeaderBytes);
    packets_ = 0;
  }

 private:
  std::vector<std::uint8_t> bytes_;
  std::uint32_t packets_ = 0;
};

// Visits every packet of a batch payload in order. Throws std::logic_error on a truncated or over-long record; the
// worker captures it and rethrows it on the EE at the next drain.
template <typename Visit>
void walk_vif_batch(const std::uint8_t *payload, std::size_t bytes, Visit &&visit) {
  if (bytes < kVifBatchHeaderBytes) throw std::logic_error("VIF1 batch record is shorter than its header");
  std::uint32_t packets = 0;
  std::memcpy(&packets, payload, 4);
  std::size_t at = kVifBatchHeaderBytes;
  for (std::uint32_t index = 0; index < packets; ++index) {
    if (at + 4 > bytes) throw std::logic_error("VIF1 batch packet header is truncated");
    std::uint32_t qwc = 0;
    std::memcpy(&qwc, payload + at, 4);
    at += 4;
    const std::size_t packet_bytes = static_cast<std::size_t>(qwc) * 16;
    if (at + packet_bytes > bytes) throw std::logic_error("VIF1 batch packet is truncated");
    visit(payload + at, qwc);
    at += packet_bytes;
  }
  if (at != bytes) throw std::logic_error("VIF1 batch record has trailing bytes");
}

}  // namespace gt4::mtvu
