// SPDX-License-Identifier: MIT
//
// HWR CPU reference replay: parses GT4_PGS_CAPTURE streams (record layout in scripts/hw_capture.py)
// and feeds them to the CPU reference renderer, one display image per 'V' record.
// Copyright 2026 GT4Recomp contributors.
// Written from docs/hw-renderer-spec (clean room, decision 0069): chapter 14.3 (M1 replay).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "cpuref_renderer.hpp"

namespace hwgs::cpuref {

struct ReplaySummary {
    uint64_t records = 0;
    uint64_t gifPackets = 0;
    uint64_t gifBytes = 0;
    uint64_t privileged = 0;
    uint64_t fifoReads = 0;
    uint64_t frames = 0;
    uint64_t framesWithImage = 0;
    uint64_t rejectedGifPackets = 0;  // GIF path 0 or odd sizes (IN-312)
};

// Replays one capture stream. onFrame receives every frame end with the display result.
// Returns false and sets error when the stream is malformed (the summary holds the progress).
bool ReplayCapture(const std::vector<uint8_t>& stream, CpuRenderer& renderer, ReplaySummary& summary,
                   const std::function<void(uint64_t frame, const FrameImage& image)>& onFrame,
                   std::string& error);

// Writes capture streams in the same record layout (used by the tests and synthetic captures).
class CaptureWriter {
public:
    void Gif(uint8_t path, const std::vector<uint8_t>& bytes);
    void Privileged(uint32_t address, uint64_t value);
    void FifoRead(uint32_t qwords);
    void FrameEnd(uint64_t frame);

    // Builds a GIF packet with one tag and the given data (qword multiple of 16 bytes).
    static std::vector<uint8_t> Packet(uint32_t nloop, bool eop, bool pre, uint32_t prim, uint32_t format,
                                       const std::vector<uint8_t>& regs, const std::vector<uint64_t>& words);

    const std::vector<uint8_t>& Bytes() const { return bytes_; }

private:
    void AppendU32(uint32_t value);
    void AppendU64(uint64_t value);
    std::vector<uint8_t> bytes_;
};

}  // namespace hwgs::cpuref
