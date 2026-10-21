// HWR GS front end: GIF stream to register state, vertex kicks, batches with
// snapshots and the flush rules, auto-flush, transfers, VBlank and CLUT loads
// (chapter 02, with chapter 07.3 for loads).
// Copyright 2026 GT4Recomp contributors. Licensed under the MIT License (see LICENSE).
// Written from the TASK-026 specification (docs/hw-renderer-spec) (clean room).
#pragma once

#include <functional>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "hw_batch.hpp"
#include "hw_clut.hpp"
#include "hw_gif_parser.hpp"
#include "hw_gs_counters.hpp"
#include "hw_gs_registers.hpp"
#include "hw_gs_types.hpp"
#include "hw_transfers.hpp"

namespace hwgs {

// GT4_HW_AUTOFLUSH (chapter 02.6).
enum class AutoflushMode : uint8_t { Sprites, All, Off };

struct FrontEndOptions {
    AutoflushMode autoflush = AutoflushMode::Sprites;
    bool clutAlwaysLoad = false;  // GT4_HW_CLUT_ALWAYS_LOAD=1
};

enum class SyncKind : uint8_t { Signal, Finish, Label };

// Receives batches in stream order (chapter 12.3 gif_transfer contract).
class BatchSink {
public:
    virtual ~BatchSink() = default;
    virtual void OnBatch(Batch&& batch) = 0;
};

// Receives SIGNAL, FINISH and LABEL in stream order. Returning true asks for a flush.
class SyncSink {
public:
    virtual ~SyncSink() = default;
    virtual bool OnSync(SyncKind kind, uint32_t id, uint32_t idMask) = 0;
};

// Block range in GS local memory (wraps at 16384 blocks).
struct BlockRange {
    uint32_t begin = 0;
    uint32_t count = 0;
};

class FrontEnd final : public GifSink {
public:
    FrontEnd(FrontEndOptions options, BatchSink& batches, SyncSink& sync, TransferSink& memory);

    // Local memory image (4 MiB) used by CLUT loads. Defaults to zeros.
    void SetLocalMemory(const uint8_t* localMemory);

    // Called just before a CLUT load reads the local memory image (after the batch flush of 07.3), with a
    // conservative block range covering the palette. A renderer that keeps drawn pixels outside the image
    // (the M7 resident targets) writes them back here. Empty = no call.
    void SetBeforeLocalRead(std::function<void(const BlockRange&)> callback);

    // Feeds whole qwords to a GIF path (1..3).
    void GifTransfer(uint32_t path, const Qword* qwords, size_t count);
    bool GifBusy(uint32_t path) const;
    // XGKICK end of a path-1 packet without EOP.
    void GifPathOneEnd();
    // GIF reset: parser state for the selected paths, TRXDIR = 3, Q latch = 1.0.
    void GifReset(uint32_t pathMask);

    // VBlank (chapter 02.8): commits uploads, flushes when the batch's frame
    // block range overlaps one of the enabled display ranges.
    void VBlank(const BlockRange* displays, size_t count);

    // GS reset (chapter 02.11): drops the batch and queue, resets registers,
    // CLUT, transfers and the texture-flushed flag.
    void ResetGs();

    // Flushes the pending batch (end of frame or an explicit request).
    void FlushBatch(FlushReason reason);

    const RegisterFile& Registers() const { return registers_; }
    const clut::ClutUnit& Clut() const { return clut_; }
    const CounterSet& Counters() const { return counters_; }
    uint64_t FlushReasonCount(FlushReason reason) const {
        return flushCounts_[static_cast<size_t>(reason)];
    }
    const Batch& PendingBatch() const { return batch_; }
    bool TexflushPending() const { return texflushPending_; }

    // GifSink
    void WriteRegister(uint8_t address, uint64_t data) override;
    void Kick(const GifKick& kick) override;
    void ImageQword(uint32_t path, const Qword& qword) override;

private:
    struct Candidate {
        Vertex vertices[3];
        uint32_t count = 0;
    };

    struct TexelRect {
        PixelRect rect;
        bool valid = false;
    };

    void TakeSnapshot(uint32_t contextIndex);
    Vertex MakeVertex(const GifKick& kick, const ContextRegs& ctx) const;
    bool BuildCandidate(PrimType type, Candidate& candidate) const;
    bool ShouldDraw(const Candidate& candidate, PrimType type, uint32_t contextIndex,
                    PixelRect& rect) const;
    void AppendPrimitive(const Candidate& candidate, PrimType type, const PixelRect& rect);
    void AdvanceQueue(PrimType type);

    std::optional<FlushReason> DecideFlush() const;
    bool AutoflushWanted(const Candidate& candidate, PrimType type) const;
    TexelRect ComputeTexelRect(const Candidate& candidate) const;
    bool BatchTouches(uint32_t begin, uint32_t count, bool asDestination) const;
    bool FrameWritesColour(const ContextRegs& ctx) const;
    bool ZbufWritten(const ContextRegs& ctx) const;
    bool ZbufUsed(const ContextRegs& ctx) const;

    void OnTex0Write(uint32_t contextIndex);
    void NoteBatchWritesToClut();
    TransferHooks MakeTransferHooks();

    FrontEndOptions options_;
    BatchSink& batches_;
    SyncSink& sync_;
    TransferSink& memory_;

    CounterSet counters_;
    RegisterFile registers_;
    GifParser parser_;
    clut::ClutUnit clut_;
    TransferUnit transfers_;

    const uint8_t* localMemory_ = nullptr;
    std::function<void(const BlockRange&)> beforeLocalRead_;
    std::vector<uint8_t> zeroMemory_;

    Batch batch_;
    std::vector<Vertex> queue_;

    uint64_t rgbaq_ = 0;
    uint64_t st_ = 0;
    uint64_t uv_ = 0;
    uint8_t fog_ = 0;

    bool texflushPending_ = false;
    bool scissorWritten_ = false;
    bool flushing_ = false;
    bool inTransferHook_ = false;  // a transfer commit is running (no nested transfer commits)

    std::array<uint64_t, static_cast<size_t>(FlushReason::Count)> flushCounts_{};
};

}  // namespace hwgs
