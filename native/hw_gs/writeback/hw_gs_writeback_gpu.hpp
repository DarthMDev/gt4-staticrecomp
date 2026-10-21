// MIT License. Copyright (c) 2026 João Alano and GT4Recomp contributors.
// Written from docs/hw-renderer-spec (TASK-027 M4, chapters 03 and 15), clean room (decision 0069).
//
// Vulkan-free interface of the HWR reference GPU drawer (write-back mode). One Execute() call draws
// one chunk of a guest batch: frame and depth words are uploaded from the shadow local memory, the
// primitives are drawn with the uber shader, and the frame and depth words are read back so the
// caller can write them to the SLM (chapter 00 reference mode). Only the implementation file
// includes Vulkan headers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hwgs::gpu {

// Vulkan handles owned by the caller (the HWR backend). Passed as void* to keep this header Vulkan-free.
struct VulkanHandles {
    void *instance = nullptr;
    void *physical = nullptr;
    void *device = nullptr;
    void *queue = nullptr;
    std::uint32_t queueFamily = 0;
};

// Vertex record of chapter 03.3 (32 bytes). X and Y are 12.4 two's complement values.
struct GpuVertex {
    float s;
    float t;
    std::uint8_t rgba[4];
    float q;
    std::uint16_t x;
    std::uint16_t y;
    std::uint32_t z;
    std::uint16_t u;
    std::uint16_t v;
    std::uint8_t fog;
    std::uint8_t pad[3];
};
static_assert(sizeof(GpuVertex) == 32, "GpuVertex must match the 32-byte record of chapter 03.3");

enum class GpuTopology : std::uint8_t { Points, Lines, Triangles };

// One primitive, drawn with its own scissor (its coverage rectangle clipped to the scissor).
struct GpuDraw {
    GpuTopology topology = GpuTopology::Triangles;
    std::uint32_t first = 0;     // first vertex in GpuChunk::vertices
    std::uint32_t count = 0;     // vertices of this primitive
    std::int32_t x0 = 0, y0 = 0; // inclusive scissor rectangle in GS pixels
    std::int32_t x1 = 0, y1 = 0;
};

// Pixel-stage state, the uniform block of hw_pixel_uber.frag (std140, all 4-byte fields).
// Field order must match PixelStateBlock in shaders/hw_pixel_uber.frag.
struct GpuPixelState {
    std::uint32_t fields[48] = {};
};

// Field indices of GpuPixelState (names follow the shader block).
namespace gpufield {
enum : std::uint32_t {
    Format, Fbmsk, Fba, Dither, DimxLo, DimxHi, Ate, Atst, Afail, Aref, DateOn, Datm, Ztest, Ztst, Zmsk,
    Zformat, BlendOn, Pabe, BlendA, BlendB, BlendC, BlendD, Fix, Colclamp, TexOn, Tfx, Tcc, TexelKind,
    Ta0, Ta1, Aem, FogOn, FogR, FogG, FogB, TexFst, TexLinear, WrapS, WrapT, WrapMinS, WrapMaxS,
    WrapMinT, WrapMaxT, TexLog2W, TexLog2H,
    Count
};
static_assert(Count <= 48, "GpuPixelState has 48 fields");
} // namespace gpufield

// One chunk of work. The frame and depth rectangles are inclusive and equal; words are the raw
// stored values of the 32-bit view (SLM readPixel), row-major with width = rect width.
// The four transfer flags select what Execute moves between the words and the frame/depth images over the
// rectangle: the write-back mode sets all four (upload, draw, read back); the resident-target cache (M7,
// IN-340) uploads only when a target is loaded, draws with no transfer, and reads back only when the shadow
// memory must become coherent (a chunk with no draws is a pure transfer).
struct GpuChunk {
    std::int32_t rectX0 = 0, rectY0 = 0, rectX1 = -1, rectY1 = -1;
    bool uploadFrame = true;
    bool uploadDepth = true;
    bool readbackFrame = true;
    bool readbackDepth = true;
    std::vector<std::uint32_t> frameWords;   // uploaded and/or read back per the flags
    std::vector<std::uint32_t> depthWords;
    bool hasTexture = false;
    std::uint32_t texWidth = 0;              // texels (power of two up to 1024 or region extent)
    std::uint32_t texHeight = 0;
    std::vector<std::uint32_t> texels;       // RGBA8 packed as R | G<<8 | B<<16 | A<<24
    GpuPixelState state;
    std::vector<GpuVertex> vertices;
    std::vector<GpuDraw> draws;
};

// Limits of one Execute() call (the caller splits larger batches).
constexpr std::uint32_t kGpuMaxVertices = 1u << 20;   // 32 MiB of vertex data
constexpr std::int32_t kGpuSurface = 2048;             // GS window size: frame and depth images
constexpr std::uint32_t kGpuMaxTextureSide = 1024;     // texel image side (region modes may use less)

struct GpuStats {
    std::uint64_t executions = 0;
    std::uint64_t draws = 0;
    std::uint64_t vertices = 0;
    std::uint64_t uploadBytes = 0;
    std::uint64_t readbackBytes = 0;
    std::uint64_t barriers = 0;
    std::uint64_t submissions = 0;  // queue submissions (IN-342: several chunks per submission)
};

class GpuDrawer {
public:
    GpuDrawer();
    ~GpuDrawer();
    GpuDrawer(const GpuDrawer &) = delete;
    GpuDrawer &operator=(const GpuDrawer &) = delete;

    // Creates the shader modules, pipelines, images and buffers. Fails with a readable message.
    bool Init(const VulkanHandles &handles, std::string &error);
    bool Ready() const;

    // Records one chunk (uploads, draws, readbacks as its flags say). A chunk with a readback submits and waits,
    // so its words are filled on return; other chunks are submitted later (IN-342), when a ring is full, a
    // readback needs the results, or Flush() is called.
    bool Execute(GpuChunk &chunk, std::string &error);

    // Submits the recorded chunks; with `wait`, also waits for them.
    bool Flush(bool wait, std::string &error);

    const GpuStats &Stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hwgs::gpu
