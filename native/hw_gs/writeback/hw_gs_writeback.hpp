// MIT License. Copyright (c) 2026 João Alano and GT4Recomp contributors.
// Written from docs/hw-renderer-spec (TASK-027 M4, chapters 00 reference mode, 03, 12.3, 12.9, 15),
// clean room (decision 0069).
//
// HWR reference renderer, write-back mode (M4). The GS front end (M3) parses the GIF stream into
// batches; each batch is translated to GPU work (vertices, pixel state, decoded texture) and drawn
// on the GPU, then the touched frame and depth words are written back to the shadow local memory
// (SLM) and the target is dropped. Display output is composed from the SLM (13.1 to 13.3).
// Blending is not implemented in M4 (ABE is treated as opaque and counted, IN-500).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "hw_gs_writeback_gpu.hpp"

namespace hwgs::writeback {

// Counters for the replay and the F8 report of the M4 milestone.
struct Counters {
    std::uint64_t batches = 0;
    std::uint64_t primitives = 0;
    std::uint64_t primitivesSkippedTexture = 0;  // invalid texture format or size (01.5, IN-307)
    std::uint64_t batchesSkippedTexture = 0;
    std::uint64_t batchesEmpty = 0;             // no pixels after scissor and coverage
    std::uint64_t batchesAbeIgnored = 0;        // ABE = 1 in M4: drawn as opaque (IN-500)
    std::uint64_t texturesDecoded = 0;
    std::uint64_t texelsDecoded = 0;
    std::uint64_t gpuChunks = 0;
    std::uint64_t gpuDraws = 0;
    std::uint64_t writebackPixels = 0;          // frame pixels written back to the SLM
    std::uint64_t flatDeindexedPrimitives = 0;  // IIP = 0 primitives (de-indexed, 03.2)
    std::uint64_t spritesExpanded = 0;
};

// Output of the display path for one VBlank (13.1 to 13.3, no deinterlace, no post-fx yet).
struct FrameOutput {
    bool valid = false;            // false when SMODE1 describes no known mode
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint32_t> pixels;  // RGBA8 packed with red in the low byte
};

// Everything the GPU needs for one batch, plus the skip decisions (CPU translation, no GPU calls).
struct BatchTranslation {
    bool skippedTexture = false;
    bool abeIgnored = false;
    memory::BufferView frameView{0, 0, memory::Psm::Ct32};
    memory::BufferView zView{0, 0, memory::Psm::Z32};
    PixelRect rect;  // union of the draw scissors: the rectangle read and written back
    gpu::GpuPixelState state;
    bool hasTexture = false;
    uint32_t texWidth = 0;
    uint32_t texHeight = 0;
    std::vector<uint32_t> texels;
    std::vector<gpu::GpuVertex> vertices;
    std::vector<gpu::GpuDraw> draws;
};

// Translates one batch against the SLM and the CLUT. Counts into  as the draw path does.
void TranslateBatch(const Batch &batch, const memory::LocalMemory &local, const clut::ClutUnit &clut,
                    BatchTranslation &out, Counters &counters);

// Draws a translated batch in chunks (one Execute per chunk), reading the frame and depth words of
// the rectangle from the SLM first and writing them back afterwards (chapter 00 write-back mode).
bool ExecuteBatch(gpu::GpuDrawer &gpu, const BatchTranslation &translation, memory::LocalMemory &local,
                  Counters &counters, std::string &error);

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    // Creates the GPU drawer on an existing device. False with `error` when the GPU path is unusable.
    bool Init(const gpu::VulkanHandles &handles, std::string &error);

    // GIF packets for path 1..3 (16-byte qwords). Path 0 and bad sizes are rejected.
    bool GifTransfer(std::uint32_t path, const std::uint8_t *bytes, std::size_t byteCount);
    bool GifBusy(std::uint32_t path) const;
    void GifReset(std::uint32_t pathMask);

    // Privileged writes (0x12000000..0x12001FFF). Returns false outside the display set.
    bool WritePrivileged(std::uint32_t address, std::uint64_t value);

    void ResetGs();                // GS reset (front end), registers and CLUT
    void ClearVram();              // zeroes the SLM (drops nothing: write-back mode has no caches)
    // LOCAL->HOST data of the pending read, at most byteCount bytes (10.2); returns the bytes copied.
    std::size_t ReadFifo(void *destination, std::size_t byteCount);

    // VBlank: commits pending transfers and flushes the batch (chapter 02.8).
    void Flush();
    // Composes the display image from the SLM and the display registers.
    FrameOutput Compose();

    // CPU view of the SLM (4 MiB, GS layout).
    const std::uint8_t *Slm() const;

    const Counters &GetCounters() const;
    const gpu::GpuStats &GpuCounters() const;
    const std::string &LastError() const;
    bool Failed() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hwgs::writeback
