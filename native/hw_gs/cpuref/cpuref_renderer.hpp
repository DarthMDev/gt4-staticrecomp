// SPDX-License-Identifier: MIT
//
// HWR CPU reference renderer: the front end feeds batches to the rasterizer, transfers land
// in the shadow local memory, and the display path composes the frame image from the
// circuits (M4 to M6 of the plan, all on the CPU).
// Copyright 2026 GT4Recomp contributors.
// Written from the TASK-026 specification (docs/hw-renderer-spec): chapters 02, 01.7, 13.1 to 13.3.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cpuref_draw.hpp"
#include "cpuref_memory.hpp"
#include "hw_front_end.hpp"
#include "hw_gs_display.hpp"

namespace hwgs::cpuref {

// Privileged register addresses (01.7); the page starts at 0x12000000.
namespace priv {
constexpr uint32_t kPage = 0x12000000u;
constexpr uint32_t kPmode = kPage + 0x00;
constexpr uint32_t kSmode1 = kPage + 0x10;
constexpr uint32_t kSmode2 = kPage + 0x20;
constexpr uint32_t kSyncv = kPage + 0x60;
constexpr uint32_t kDispfb1 = kPage + 0x70;
constexpr uint32_t kDisplay1 = kPage + 0x80;
constexpr uint32_t kDispfb2 = kPage + 0x90;
constexpr uint32_t kDisplay2 = kPage + 0xA0;
constexpr uint32_t kBgcolor = kPage + 0xE0;
constexpr uint32_t kCsr = kPage + 0x1000;
}  // namespace priv

// Output of the display path for one VBlank.
struct FrameImage {
    bool valid = false;           // false when SMODE1 describes no known mode
    display::Image image;
};

class CpuRenderer final : public BatchSink, public SyncSink {
public:
    CpuRenderer();

    // GIF packet data for path 1..3 (16-byte qwords). Path 0 is rejected (IN-312).
    bool GifTransfer(uint32_t path, const std::vector<uint8_t>& bytes);

    // Privileged write (guest address, 64-bit value). Returns false for unknown addresses.
    bool WritePrivileged(uint32_t address, uint64_t value);

    // LOCAL->HOST FIFO read of qwords 128-bit words (10.2); zeros and a fault on overrun.
    // Replay resource limit: our producer splits requests at its 4 MiB + 64 KiB response area
    // (GS-STREAM-PROTOCOL 6.4). Oversized direct calls return empty and count a fault.
    static constexpr uint32_t kMaxFifoQwords = ((4u << 20) + 65536u) / 16u;
    std::vector<uint8_t> ReadFifo(uint32_t qwords);

    // CSR reset keeps local memory; protocol hard reset additionally clears it (02.11).
    void ResetGs(bool clearVram);
    // CPU parser mask: bit 0 = PATH1, bit 1 = PATH2, bit 2 = PATH3 (02.1).
    void ResetGif(uint32_t pathMask);
    void SetDisplayField(bool odd) { csrField_ = odd; }
    bool DisplayField() const { return csrField_; }
    uint64_t fifoFaults = 0;

    // Ends a frame: flushes the batch, then composes the display image from the registers.
    FrameImage EndFrame();

    // BatchSink
    void OnBatch(Batch&& batch) override;
    // SyncSink: no flush is requested by SIGNAL, FINISH or LABEL in the reference.
    bool OnSync(SyncKind kind, uint32_t id, uint32_t idMask) override;

    const DrawStats& Stats() const { return stats_; }
    const CounterSet& Counters() const { return frontEnd_.Counters(); }
    cpuref::LocalMemorySink& Memory() { return memory_; }
    const cpuref::LocalMemorySink& Memory() const { return memory_; }

    // Display output inputs (13.1). Public so the replay and tests can inspect them.
    display::DisplayRegisters& DisplayRegs() { return displayRegs_; }

    uint64_t unhandledPrivileged = 0;
    uint64_t rejectedGifPaths = 0;

private:
    LocalMemorySink memory_;
    FrontEnd frontEnd_;
    DrawStats stats_;
    display::DisplayRegisters displayRegs_;
    std::array<uint32_t, 2> previousDby_{};
    std::array<uint8_t, 3> background_{};  // BGCOLOR R, G, B (01.7)
    bool csrField_ = false;
    std::optional<display::Image> lastImage_;
};

// Encodes an image as a binary P6 PPM (RGB, alpha dropped, maxval 255).
std::string EncodePpm(const display::Image& image);

}  // namespace hwgs::cpuref
