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
    // The device was created with VK_EXT_fragment_shader_interlock and fragmentShaderPixelInterlock (IN-345).
    bool pixelInterlock = false;
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
    WrapMinT, WrapMaxT, TexLog2W, TexLog2H, TexOriginX, TexOriginY,
    Count
};
static_assert(Count <= 48, "GpuPixelState has 48 fields");
} // namespace gpufield

// One chunk of work. The frame and depth rectangles are inclusive and equal; words are the raw
// stored values of the 32-bit view (SLM readPixel), row-major with width = rect width.
// Target slots (IN-344): GPU images that hold resident colour and depth targets. The last slot of each kind is
// the write-back mode's scratch pair, so a write-back batch never disturbs a resident target.
constexpr std::uint32_t kGpuColourSlots = 6;
constexpr std::uint32_t kGpuDepthSlots = 3;
constexpr std::uint32_t kGpuScratchColourSlot = kGpuColourSlots - 1;
constexpr std::uint32_t kGpuScratchDepthSlot = kGpuDepthSlots - 1;

// The four transfer flags select what Execute moves between the words and the frame/depth images over the
// rectangle: the write-back mode sets all four (upload, draw, read back); the resident-target cache (M7,
// IN-340) uploads only when a target is loaded, draws with no transfer, and reads back only when the shadow
// memory must become coherent (a chunk with no draws is a pure transfer).
struct GpuChunk {
    std::int32_t rectX0 = 0, rectY0 = 0, rectX1 = -1, rectY1 = -1;
    std::uint32_t colourSlot = 0;  // kGpuColourSlots images; the write-back mode uses the scratch slots
    std::uint32_t depthSlot = 0;
    bool uploadFrame = true;
    bool uploadDepth = true;
    bool readbackFrame = true;
    bool readbackDepth = true;
    std::vector<std::uint32_t> frameWords;   // uploaded and/or read back per the flags
    std::vector<std::uint32_t> depthWords;
    bool hasTexture = false;
    // IN-346: when >= 0, the texels are copied on the GPU from this colour slot's image (rectangle 0,0 to
    // texWidth x texHeight) instead of being uploaded from `texels`.
    std::int32_t textureSlot = -1;
    std::uint32_t texWidth = 0;              // texels (power of two up to 1024 or region extent)
    std::uint32_t texHeight = 0;
    std::vector<std::uint32_t> texels;       // RGBA8 packed as R | G<<8 | B<<16 | A<<24
    // IN-347: when set, the texels are decoded on the GPU from the GLM (texture view below; indexed formats
    // through `palette`) instead of being uploaded or copied. The caller made the GLM current first.
    bool glmTexture = false;
    std::uint32_t glmBp = 0, glmBw = 0, glmPsm = 0;
    bool glmIndexed = false;
    std::vector<std::uint32_t> palette;      // 256 RGBA8 entries when glmIndexed (CPU CLUT)
    // IN-349: indexed texels expanded from the GPU CLUT buffer instead of `palette` (Decode's CPSM, CSA, TEXA).
    bool glmGpuClut = false;
    std::uint32_t glmCpsm = 0, glmCsa = 0, glmTa0 = 0, glmTa1 = 0, glmAem = 0;
    GpuPixelState state;
    std::vector<GpuVertex> vertices;
    std::vector<GpuDraw> draws;
    // IN-365: when set, the chunk's vertices (and texels) are read from these borrowed arrays instead of the vectors
    // above (no per-chunk copy); they must stay valid until Execute returns.
    const GpuVertex *vertexSpan = nullptr;
    std::size_t vertexSpanCount = 0;
    const std::uint32_t *texelSpan = nullptr;
    std::size_t texelSpanCount = 0;
    const GpuVertex *VertexData() const { return vertexSpan ? vertexSpan : vertices.data(); }
    std::size_t VertexCount() const { return vertexSpan ? vertexSpanCount : vertices.size(); }
    const std::uint32_t *TexelData() const { return texelSpan ? texelSpan : texels.data(); }
    std::size_t TexelCount() const { return texelSpan ? texelSpanCount : texels.size(); }
};

// Limits of one Execute() call (the caller splits larger batches).
constexpr std::uint32_t kGpuMaxVertices = 1u << 20;   // 32 MiB of vertex data
constexpr std::int32_t kGpuSurface = 2048;             // GS window size: frame and depth images
constexpr std::uint32_t kGpuMaxTextureSide = 1024;     // largest texture side (region modes may use less)
constexpr std::uint32_t kGpuTexelAtlasSide = 4096;      // texel atlas: one region per textured chunk of a run (IN-352)

struct GpuStats {
    std::uint64_t executions = 0;
    std::uint64_t draws = 0;
    std::uint64_t vertices = 0;
    std::uint64_t uploadBytes = 0;
    std::uint64_t readbackBytes = 0;
    std::uint64_t barriers = 0;
    std::uint64_t submissions = 0;  // queue submissions (IN-342: several chunks per submission)
    std::uint64_t runs = 0;         // render passes holding batched chunks (IN-352)
    std::uint64_t runChunks = 0;    // chunks drawn inside runs
    std::uint64_t earlySubmissions = 0;  // submissions in mid-frame (IN-358)
    // GT4_HW_GPU_TIMING (12.8, roadmap requirement 4): GPU time in ns from timestamps written after every
    // recorded operation; the interval since the previous timestamp is charged to the operation's category
    // (so a barrier wait before an operation counts with it). busy = first to last timestamp per submission.
    std::uint64_t gpuNs[6] = {};    // pass, decode, clut, swizzle (both directions), upload, download
    std::uint64_t gpuBusyNs = 0;
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

    // ---- GLM (M10, chapter 04.9, IN-347): a 4 MiB GPU mirror of GS local memory ----
    // Records copies of the given byte ranges of `localMemory` (the 4 MiB shadow memory) into the GLM.
    bool UploadGlm(const std::uint8_t *localMemory, const std::vector<std::pair<std::uint32_t, std::uint32_t>> &ranges,
                   std::string &error);
    // Records the swizzle of a resident target's rectangle (inclusive) into the GLM at its own (BP, BW, PSM).
    bool SwizzleToGlm(bool depthSlot, std::uint32_t slot, std::uint32_t bp, std::uint32_t bw, std::uint32_t psm,
                      std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, std::string &error);
    // Records the inverse (IN-350): the slot's rectangle receives the stored words of (BP, BW, PSM) from the GLM.
    bool LoadSlotFromGlm(bool depthSlot, std::uint32_t slot, std::uint32_t bp, std::uint32_t bw, std::uint32_t psm,
                         std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, std::string &error);
    // Records a merge of words into the GLM (IN-350): `triples` holds (GLM word index, value, bit mask) per word;
    // the masked bits of each word take the value. Word indices must be unique.
    bool MergeIntoGlm(const std::vector<std::uint32_t> &triples, std::string &error);

    // Records an EE->GS upload into the GLM (IN-351): `pixels` texels of `bpp` bits from `data` (host order) are
    // written at (x0 + i % width, y0 + i / width) of (BP, BW, PSM), each with only its own bits. The rectangle
    // must not wrap onto itself inside the buffer (distinct addresses).
    bool UploadHostToGlm(std::uint32_t bp, std::uint32_t bw, std::uint32_t psm, std::uint32_t x0, std::uint32_t y0,
                         std::uint32_t width, std::uint32_t pixels, std::uint32_t bpp, const std::uint8_t *data,
                         std::size_t byteCount, std::string &error);
    // IN-355 asynchronous downloads: the copy of the given GLM byte ranges is recorded into the current
    // recording (it runs after everything recorded before it) into one of two host buffers; Complete waits only
    // for the submission that holds it (submitting it first if it is still being recorded) and copies the bytes
    // into `localMemory` at the same offsets. A ticket's buffer is reused two downloads later, so a ticket must
    // be completed (or dropped) before the second next one is recorded.
    struct DownloadTicket {
        bool valid = false;
        std::uint32_t buffer = 0;
        std::uint64_t serial = 0;  // the submission that holds the copy
    };
    DownloadTicket RecordGlmDownload(const std::vector<std::pair<std::uint32_t, std::uint32_t>> &ranges,
                                     std::string &error);
    bool CompleteGlmDownload(const DownloadTicket &ticket, std::uint8_t *localMemory, std::string &error);
    // IN-364: records the display compose kernel (13.3, progressive frames) into the display image: `parameters`
    // is the block hw_display_compose.comp reads (background, merge alpha, the two circuits' sources and
    // placements), out_width x out_height pixels. The caller made the GLM current for the circuits.
    bool ComposeDisplay(const std::vector<std::uint32_t> &parameters, std::uint32_t outWidth, std::uint32_t outHeight,
                        std::string &error);
    // The display image (R32_UINT, RGBA8 packed R | G << 8 | B << 16 | A << 24, kGpuSurface square, GENERAL layout).
    // As an opaque handle (the VkImage value): this header carries no Vulkan types.
    std::uint64_t DisplayImage() const;
    // Fixtures: the top-left width x height words of the display image (submits and waits).
    bool ReadDisplay(std::uint32_t width, std::uint32_t height, std::vector<std::uint32_t> &words, std::string &error);
    // Copies the given byte ranges of the GLM into `localMemory` at the same offsets (IN-350: pages whose newest
    // content lives only in the GLM, before the CPU reads or writes them). Submits and waits.
    bool DownloadGlm(const std::vector<std::pair<std::uint32_t, std::uint32_t>> &ranges, std::uint8_t *localMemory,
                     std::string &error);
    // Records a CLUT load from the GLM into the GPU CLUT buffer (chapter 07.7, IN-349): the same as
    // clut::ClutUnit::Copy with these TEX0/TEXCLUT fields (cbp in blocks; psm decides 16 or 256 entries).
    bool LoadClutFromGlm(std::uint32_t cbp, std::uint32_t cpsm, std::uint32_t csm, std::uint32_t csa, std::uint32_t psm,
                         std::uint32_t cbw, std::uint32_t cou, std::uint32_t cov, std::string &error);
    // Records an upload of the CPU CLUT buffer (512 halfwords) into the GPU CLUT buffer.
    bool UploadClut(const std::uint16_t *halfwords, std::string &error);
    // Fixtures: read the GPU CLUT buffer (512 halfwords; submits and waits).
    bool ReadClut(std::vector<std::uint16_t> &halfwords, std::string &error);
    // Fixtures: read the whole GLM, or the texel image's top-left width x height words (submits and waits).
    bool ReadGlm(std::vector<std::uint32_t> &words, std::string &error);
    bool ReadTexels(std::uint32_t width, std::uint32_t height, std::vector<std::uint32_t> &words, std::string &error);

    // IN-352 A/B switch: chunks of triangles drawn with the pixel interlock are batched into runs (one render
    // pass, texture preparation recorded before it, no barrier between the chunks). Off: one pass per chunk.
    void SetRuns(bool enabled);
    // IN-354 A/B switch: submissions alternate between two sets of rings and command buffers (CPU records the
    // next frame while the GPU executes the previous one). Off: every recording waits for the last submission.
    void SetDoubleBuffering(bool enabled);
    // GT4_HW_GPU_TIMING: per-category GPU timestamps (GpuStats::gpuNs). Off by default.
    void SetGpuTiming(bool enabled);
    // IN-358 A/B: submit the recording every `chunks` chunks (0: only at VBlank, a full ring or a readback).
    void SetChunksPerSubmission(std::uint32_t chunks);

    const GpuStats &Stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hwgs::gpu
