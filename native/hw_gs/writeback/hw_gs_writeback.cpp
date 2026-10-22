// MIT License. Copyright (c) 2026 João Alano and GT4Recomp contributors.
// Written from docs/hw-renderer-spec (TASK-027 M4, chapters 00, 01.5, 02.8, 03.2 to 03.5, 03.7, 03.8, 10.4,
// 12.9, 13.1 to 13.3, 15), clean room (decision 0069).
//
// Translation of one batch into GPU work follows the CPU reference (cpuref/cpuref_draw.cpp) rule for
// rule where it decides WHAT is drawn: scissor and coverage rectangles, texture skip, flat colour
// (last vertex), sprite corners, point cell. Rasterisation itself is done by the GPU (chapter 03.3 vertex
// mapping), so coverage can differ at triangle edges; see IMPLEMENTATION-NOTES IN-500.

#include "hw_gs_writeback.hpp"

#include <algorithm>
#include <bit>
#include <array>
#include <chrono>
#include <cstring>
#include <optional>
#include <unordered_map>

#include "hw_batch.hpp"
#include "hw_clut.hpp"
#include "hw_front_end.hpp"
#include "hw_gs_display.hpp"
#include "hw_gs_pixel.hpp"
#include "hw_gs_registers.hpp"
#include "hw_gs_types.hpp"
#include "hw_memory.hpp"
#include "../cpuref/cpuref_draw.hpp"
#include "../cpuref/cpuref_memory.hpp"

namespace hwgs::writeback {
namespace {

using pixel::Colour;

constexpr BlockRange kEverything{0, kBlocksTotal};

// Privileged register addresses (chapter 01.7), as the CPU reference defines them.
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
} // namespace priv

struct Clip {
    int32_t x0 = 0, x1 = -1, y0 = 0, y1 = -1;
};

Clip Intersect(const Clip& a, const Clip& b) {
    return Clip{std::max(a.x0, b.x0), std::min(a.x1, b.x1), std::max(a.y0, b.y0), std::min(a.y1, b.y1)};
}

bool Empty(const Clip& c) { return c.x0 > c.x1 || c.y0 > c.y1; }

Clip ToClip(const PixelRect& r) { return Clip{r.x0, r.x1, r.y0, r.y1}; }

pixel::FrameFormat FrameFormatOf(uint32_t psmCode) {
    switch (psmCode) {
        case 0x01:
        case 0x31:
            return pixel::FrameFormat::Ct24;
        case 0x02:
        case 0x0A:
        case 0x32:
        case 0x3A:
            return pixel::FrameFormat::Ct16;
        default:
            return pixel::FrameFormat::Ct32;
    }
}

pixel::ZFormat ZFormatOf(uint32_t psmCode) {
    switch (psmCode) {
        case 0x31:
            return pixel::ZFormat::Z24;
        case 0x32:
        case 0x3A:
            return pixel::ZFormat::Z16;
        default:
            return pixel::ZFormat::Z32;
    }
}

enum class TexClass : uint8_t { Ct32, Ct24, Ct16, Indexed };

std::optional<TexClass> TextureClassOf(uint32_t psmCode) {
    switch (psmCode) {
        case 0x00:
        case 0x30:
            return TexClass::Ct32;
        case 0x01:
        case 0x31:
            return TexClass::Ct24;
        case 0x02:
        case 0x0A:
        case 0x32:
        case 0x3A:
            return TexClass::Ct16;
        case 0x13:
        case 0x14:
        case 0x1B:
        case 0x24:
        case 0x2C:
            return TexClass::Indexed;
        default:
            return std::nullopt;
    }
}

pixel::DepthCompare DepthCompareOf(uint32_t ztst) {
    switch (ztst) {
        case kZtstNever:
            return pixel::DepthCompare::Never;
        case 1:
            return pixel::DepthCompare::Always;
        case kZtstGequal:
            return pixel::DepthCompare::GreaterEqual;
        default:
            return pixel::DepthCompare::Greater;
    }
}

pixel::AlphaCompare AlphaCompareOf(uint32_t atst) { return static_cast<pixel::AlphaCompare>(atst & 7u); }
pixel::AlphaFail AlphaFailOf(uint32_t afail) { return static_cast<pixel::AlphaFail>(afail & 3u); }

uint32_t PackRgba(const Colour& c) {
    return static_cast<uint32_t>(c.r & 0xFF) | (static_cast<uint32_t>(c.g & 0xFF) << 8) |
           (static_cast<uint32_t>(c.b & 0xFF) << 16) | (static_cast<uint32_t>(c.a & 0xFF) << 24);
}

// Texture unit as the CPU reference builds it (cpuref_draw.cpp BuildTexture).
struct TextureUnit {
    memory::BufferView view{0, 0, memory::Psm::Ct32};
    TexClass cls = TexClass::Ct32;
    uint32_t twLog2 = 0;
    uint32_t thLog2 = 0;
    pixel::WrapAxis wrapS;
    pixel::WrapAxis wrapT;
    bool linear = false;
    bool fst = false;
    bool tcc = false;
    pixel::Tfx tfx = pixel::Tfx::Modulate;
    clut::TexaState texa;
    clut::Palette palette;
};

bool BuildTexture(const BatchState& state, const clut::ClutUnit& clut, TextureUnit& out) {
    const uint64_t tex0Register = state.ctx.tex0;
    const uint64_t tex1Register = state.ctx.tex1;
    const uint64_t clampRegister = state.ctx.clamp;
    const uint32_t psmCode = tex0::Psm(tex0Register);
    const std::optional<TexClass> cls = TextureClassOf(psmCode);
    const auto psm = memory::decodePsm(psmCode);
    if (!cls || !psm) return false;
    out.cls = *cls;
    out.view = memory::BufferView{tex0::Tbp0(tex0Register), tex0::Tbw(tex0Register), *psm};
    out.twLog2 = tex0::Tw(tex0Register);
    out.thLog2 = tex0::Th(tex0Register);
    out.wrapS = pixel::WrapAxis{static_cast<pixel::WrapMode>(clampreg::Wms(clampRegister)),
                                static_cast<int32_t>(clampreg::Minu(clampRegister)),
                                static_cast<int32_t>(clampreg::Maxu(clampRegister))};
    out.wrapT = pixel::WrapAxis{static_cast<pixel::WrapMode>(clampreg::Wmt(clampRegister)),
                                static_cast<int32_t>(clampreg::Minv(clampRegister)),
                                static_cast<int32_t>(clampreg::Maxv(clampRegister))};
    if (!pixel::texture_axis_usable(static_cast<int32_t>(out.twLog2), out.wrapS.mode) ||
        !pixel::texture_axis_usable(static_cast<int32_t>(out.thLog2), out.wrapT.mode)) {
        return false;
    }
    out.linear = tex1::Mmag(tex1Register) != 0;
    out.tcc = tex0::Tcc(tex0Register) != 0;
    out.tfx = static_cast<pixel::Tfx>(tex0::Tfx(tex0Register) & 3u);
    out.texa = clut::TexaState::FromRegister(state.env.texa);
    return true;
}

// The CPU palette of an indexed texture (CLUT buffer through CPSM, CSA and TEXA). Only the CPU decode and the
// palette-ring GLM decode need it; with the GPU CLUT (IN-349) the GPU expands the indices (IN-353: not decoded).
void DecodePalette(const BatchState& state, const clut::ClutUnit& clut, TextureUnit& texture) {
    const uint64_t tex0Register = state.ctx.tex0;
    texture.palette = clut.Decode(tex0::Psm(tex0Register), tex0::Cpsm(tex0Register), tex0::Csa(tex0Register),
                                  state.env.texa);
}

// Colour of one stored texel value after TEXA and palette expansion, packed RGBA8 (the GPU's texel format).
uint32_t ExpandTexel(const TextureUnit& t, uint32_t value) {
    switch (t.cls) {
        case TexClass::Ct32:
            return value;
        case TexClass::Ct24:
            return PackRgba(pixel::expand_texel24(value, static_cast<uint8_t>(t.texa.ta0), t.texa.aem));
        case TexClass::Ct16:
            return PackRgba(pixel::expand_texel16(static_cast<uint16_t>(value), static_cast<uint8_t>(t.texa.ta0),
                                                  static_cast<uint8_t>(t.texa.ta1), t.texa.aem));
        case TexClass::Indexed:
            return t.palette.entries[value & 255u];
    }
    return 0;
}

gpu::GpuVertex ToGpuVertex(const Vertex& v) {
    gpu::GpuVertex g{};
    g.s = v.s;
    g.t = v.t;
    g.rgba[0] = v.r;
    g.rgba[1] = v.g;
    g.rgba[2] = v.b;
    g.rgba[3] = v.a;
    g.q = v.q;
    g.x = static_cast<uint16_t>(static_cast<uint32_t>(v.x4) & 0xFFFFu);
    g.y = static_cast<uint16_t>(static_cast<uint32_t>(v.y4) & 0xFFFFu);
    g.z = v.z;
    g.u = static_cast<uint16_t>(v.u & 0xFFFFu);
    g.v = static_cast<uint16_t>(v.v & 0xFFFFu);
    g.fog = v.fog;
    return g;
}

using Translation = BatchTranslation;

// Texel rectangle needed by the region wrap modes may exceed 2^TW; the decoded image covers it.
bool TextureExtent(const TextureUnit& t, uint32_t& width, uint32_t& height) {
    width = 1u << t.twLog2;
    height = 1u << t.thLog2;
    if (t.wrapS.mode == pixel::WrapMode::RegionClamp || t.wrapS.mode == pixel::WrapMode::RegionRepeat) {
        width = std::max<uint32_t>(width, static_cast<uint32_t>(std::max(t.wrapS.maximum, 0)) + 1u);
    }
    if (t.wrapT.mode == pixel::WrapMode::RegionClamp || t.wrapT.mode == pixel::WrapMode::RegionRepeat) {
        height = std::max<uint32_t>(height, static_cast<uint32_t>(std::max(t.wrapT.maximum, 0)) + 1u);
    }
    return width <= gpu::kGpuMaxTextureSide && height <= gpu::kGpuMaxTextureSide;
}

// Wall-clock stage timer of one pass (roadmap requirement 4). CPU only: the GPU timestamp pool is a
// later step, and PassTimings::executeGpuUs stays 0 so a reader never mistakes it for a measurement.
class StageTimer {
public:
    explicit StageTimer(double& accumulator) : accumulator_(accumulator), start_(Clock::now()) {}
    ~StageTimer() {
        const std::chrono::duration<double, std::micro> elapsed = Clock::now() - start_;
        accumulator_ += elapsed.count();
    }
    StageTimer(const StageTimer&) = delete;
    StageTimer& operator=(const StageTimer&) = delete;

private:
    using Clock = std::chrono::steady_clock;
    double& accumulator_;
    Clock::time_point start_;
};

// Called just before the texels of a batch are read from the shadow memory (the M7 resident targets write
// back drawn pixels that overlap the texture first, IN-340).
using TextureReadHook = std::function<void(const memory::BufferView&, uint32_t width, uint32_t height)>;

// IN-346: asked first for a non-indexed texture; returns the GPU colour slot that holds the texture's memory
// (exact view, whole extent loaded), so the texels are copied on the GPU, or -1 to decode from the shadow memory.
using TextureFromTargetHook = std::function<int32_t(const memory::BufferView&, uint32_t width, uint32_t height)>;

// IN-347: asked next; returns true when the texture overlaps GPU-newer target data and the GLM was made
// current for it (pages uploaded, targets swizzled), so the texels are decoded from the GLM on the GPU.
using TextureFromGlmHook =
    std::function<bool(const memory::BufferView&, uint32_t width, uint32_t height, bool indexed, bool& gpuClut)>;

// Decoded-texture cache (M7 memory sources, chapter 04; IN-341). The key is everything the decode reads
// besides memory: TBP0, TBW, PSM, the decoded extent, TEXA for 16/24-bit texels and the palette contents
// for indexed ones. `find` returns texels only when no shadow-memory write touched the texture's pages since
// they were decoded; `store` keeps a fresh decode.
struct TextureKey {
    uint32_t tbp = 0, tbw = 0, psm = 0, width = 0, height = 0;
    uint32_t ta0 = 0, ta1 = 0, aem = 0;
    uint64_t paletteHash = 0;
    bool operator==(const TextureKey&) const = default;
};

class TextureCacheView {
public:
    virtual ~TextureCacheView() = default;
    virtual const std::vector<uint32_t>* find(const TextureKey& key, const memory::BufferView& view) = 0;
    virtual void store(const TextureKey& key, const memory::BufferView& view, const std::vector<uint32_t>& texels) = 0;
};

uint64_t HashPalette(const clut::Palette& palette) {
    uint64_t hash = 1469598103934665603ull;
    for (uint32_t i = 0; i < 256; ++i) {
        hash ^= palette.entries[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

void Translate(const Batch& batch, const memory::LocalMemory& local, const clut::ClutUnit& clut, Translation& out,
               Counters& counters, const TextureReadHook& beforeTextureRead = {},
               TextureCacheView* textureCache = nullptr, const TextureFromTargetHook& textureFromTarget = {},
               const TextureFromGlmHook& textureFromGlm = {}) {
    const BatchState& state = batch.state;
    const EnvironmentRegs& env = state.env;
    const ContextRegs& ctx = state.ctx;
    const uint64_t prim = state.effectivePrim;

    const bool flat = ((prim >> primbit::kIip) & 1u) == 0;
    const bool fogEnabled = ((prim >> primbit::kFge) & 1u) != 0;
    const bool blendEnabled = ((prim >> primbit::kAbe) & 1u) != 0;
    const Colour fogColour{static_cast<int32_t>(Field(env.fogcol, 0, 8)), static_cast<int32_t>(Field(env.fogcol, 8, 8)),
                           static_cast<int32_t>(Field(env.fogcol, 16, 8)), 0};
    const Clip scissor{static_cast<int32_t>(Field(ctx.scissor, 0, 11)), static_cast<int32_t>(Field(ctx.scissor, 16, 11)),
                       static_cast<int32_t>(Field(ctx.scissor, 32, 11)), static_cast<int32_t>(Field(ctx.scissor, 48, 11))};
    const Clip surface{0, gpu::kGpuSurface - 1, 0, gpu::kGpuSurface - 1};
    {
        const Clip visible = Intersect(scissor, surface);
        out.scissor = Empty(visible) ? PixelRect{} : PixelRect{visible.x0, visible.y0, visible.x1, visible.y1};
    }

    const uint32_t framePsmCode = frame::Psm(ctx.frame);
    const auto framePsm = memory::decodePsm(framePsmCode);
    out.frameView = memory::BufferView{memory::blockOfPage(frame::Fbp(ctx.frame)), frame::Fbw(ctx.frame),
                                       framePsm ? *framePsm : memory::Psm::Ct32};
    const uint32_t zbufPsmCode = 0x30u | zbuf::Psm(ctx.zbuf);
    const auto zPsm = memory::decodePsm(zbufPsmCode);
    out.zView = memory::BufferView{memory::blockOfPage(zbuf::Zbp(ctx.zbuf)), frame::Fbw(ctx.frame),
                                   zPsm ? *zPsm : memory::Psm::Z32};

    // Pixel state, computed exactly as the CPU reference sets it (01.5, 08).
    pixel::PixelState ps;
    ps.frame.format = FrameFormatOf(framePsmCode);
    ps.frame.fbmsk = frame::Fbmsk(ctx.frame);
    ps.frame.fba = (ctx.fba & 1u) != 0;
    ps.frame.dither = (env.dthe & 1u) != 0;
    ps.frame.dimx = env.dimx;
    ps.alpha_test.enabled = test::Ate(ctx.test) != 0;
    ps.alpha_test.compare = AlphaCompareOf(test::Atst(ctx.test));
    ps.alpha_test.reference = static_cast<uint8_t>(test::Aref(ctx.test));
    ps.alpha_test.fail = AlphaFailOf(test::Afail(ctx.test));
    ps.date.enabled = test::Date(ctx.test) != 0;
    ps.date.expected_msb = test::Datm(ctx.test) != 0;
    ps.depth.test_enabled = test::Zte(ctx.test) != 0;
    ps.depth.compare = DepthCompareOf(test::Ztst(ctx.test));
    ps.depth.write_masked = zbuf::Zmsk(ctx.zbuf) != 0;
    ps.z_format = ZFormatOf(zbufPsmCode);
    pixel::decode_alpha_register(ctx.alpha, ps.blend);
    ps.blend.enabled = blendEnabled;
    ps.blend.pabe = (env.pabe & 1u) != 0;
    ps.blend.colour_clamp = (env.colclamp & 1u) != 0;

    TextureUnit texture;
    const bool tme = ((prim >> primbit::kTme) & 1u) != 0;
    if (tme) {
        texture.fst = ((prim >> primbit::kFst) & 1u) != 0;
        if (!BuildTexture(state, clut, texture)) {
            out.skippedTexture = true;
            return;
        }
        uint32_t width = 0;
        uint32_t height = 0;
        if (!TextureExtent(texture, width, height)) {
            out.skippedTexture = true;
            return;
        }
        out.hasTexture = true;
        out.texWidth = width;
        out.texHeight = height;
        const bool rawKind = texture.cls == TexClass::Ct32 || texture.cls == TexClass::Ct24 || texture.cls == TexClass::Ct16;
        if (rawKind && textureFromTarget) out.textureSlot = textureFromTarget(texture.view, width, height);
        bool gpuClut = false;
        if (out.textureSlot < 0 && textureFromGlm &&
            textureFromGlm(texture.view, width, height, texture.cls == TexClass::Indexed, gpuClut)) {
            out.glmTexture = true;
            out.glmGpuClut = gpuClut && texture.cls == TexClass::Indexed;
            out.glmCpsm = tex0::Cpsm(state.ctx.tex0);
            out.glmCsa = tex0::Csa(state.ctx.tex0);
            out.glmTa0 = texture.texa.ta0;
            out.glmTa1 = texture.texa.ta1;
            out.glmAem = texture.texa.aem ? 1u : 0u;
            out.glmBp = texture.view.basePointer;
            out.glmBw = texture.view.bufferWidth;
            out.glmPsm = static_cast<uint32_t>(texture.view.psm);
            out.glmIndexed = texture.cls == TexClass::Indexed;
            if (out.glmIndexed && !out.glmGpuClut) {
                DecodePalette(state, clut, texture);
                out.palette.assign(texture.palette.entries, texture.palette.entries + 256);
            }
            counters.texturesFromGlm++;
        }
        if (out.textureSlot >= 0 || out.glmTexture) {
            // Raw stored words on the GPU; the pixel shader expands CT16/CT24 with TEXA (IN-346, IN-347).
            if (out.textureSlot >= 0) counters.texturesFromTargets++;
        } else {
        if (beforeTextureRead) beforeTextureRead(texture.view, width, height);
        if (texture.cls == TexClass::Indexed) DecodePalette(state, clut, texture);
        TextureKey key;
        key.tbp = texture.view.basePointer;
        key.tbw = texture.view.bufferWidth;
        key.psm = static_cast<uint32_t>(texture.view.psm);
        key.width = width;
        key.height = height;
        if (texture.cls == TexClass::Ct16 || texture.cls == TexClass::Ct24) {
            key.ta0 = texture.texa.ta0;
            key.ta1 = texture.texa.ta1;
            key.aem = texture.texa.aem ? 1u : 0u;
        }
        if (texture.cls == TexClass::Indexed) key.paletteHash = HashPalette(texture.palette);
        const std::vector<uint32_t>* cached = textureCache ? textureCache->find(key, texture.view) : nullptr;
        if (cached) {
            out.texels = *cached;
            counters.texturesCached++;
        } else {
            StageTimer decodeTimer(counters.textureDecodeUs);
            out.texels.resize(static_cast<size_t>(width) * height);
            local.readRect(texture.view, 0, 0, width, height, out.texels.data());  // stored values (IN-343)
            for (uint32_t& texel : out.texels) texel = ExpandTexel(texture, texel);
            counters.texturesDecoded++;
            counters.texelsDecoded += static_cast<uint64_t>(width) * height;
            if (textureCache) textureCache->store(key, texture.view, out.texels);
        }
        }
    }

    // GPU pixel-state block (hw_pixel_uber.frag). Field values equal the CPU reference's PixelState.
    gpu::GpuPixelState& st = out.state;
    st.fields[gpu::gpufield::Format] = static_cast<uint32_t>(ps.frame.format);
    st.fields[gpu::gpufield::Fbmsk] = ps.frame.fbmsk;
    st.fields[gpu::gpufield::Fba] = ps.frame.fba ? 1u : 0u;
    st.fields[gpu::gpufield::Dither] = ps.frame.dither ? 1u : 0u;
    st.fields[gpu::gpufield::DimxLo] = static_cast<uint32_t>(ps.frame.dimx);
    st.fields[gpu::gpufield::DimxHi] = static_cast<uint32_t>(ps.frame.dimx >> 32);
    st.fields[gpu::gpufield::Ate] = ps.alpha_test.enabled ? 1u : 0u;
    st.fields[gpu::gpufield::Atst] = static_cast<uint32_t>(ps.alpha_test.compare);
    st.fields[gpu::gpufield::Afail] = static_cast<uint32_t>(ps.alpha_test.fail);
    st.fields[gpu::gpufield::Aref] = ps.alpha_test.reference;
    st.fields[gpu::gpufield::DateOn] = ps.date.enabled ? 1u : 0u;
    st.fields[gpu::gpufield::Datm] = ps.date.expected_msb ? 1u : 0u;
    st.fields[gpu::gpufield::Ztest] = ps.depth.test_enabled ? 1u : 0u;
    st.fields[gpu::gpufield::Ztst] = static_cast<uint32_t>(ps.depth.compare);
    st.fields[gpu::gpufield::Zmsk] = ps.depth.write_masked ? 1u : 0u;
    st.fields[gpu::gpufield::Zformat] = static_cast<uint32_t>(ps.z_format);
    st.fields[gpu::gpufield::BlendOn] = ps.blend.enabled ? 1u : 0u;
    st.fields[gpu::gpufield::Pabe] = ps.blend.pabe ? 1u : 0u;
    st.fields[gpu::gpufield::BlendA] = static_cast<uint32_t>(ps.blend.a);
    st.fields[gpu::gpufield::BlendB] = static_cast<uint32_t>(ps.blend.b);
    st.fields[gpu::gpufield::BlendC] = static_cast<uint32_t>(ps.blend.c);
    st.fields[gpu::gpufield::BlendD] = static_cast<uint32_t>(ps.blend.d);
    st.fields[gpu::gpufield::Fix] = ps.blend.fix;
    st.fields[gpu::gpufield::Colclamp] = ps.blend.colour_clamp ? 1u : 0u;
    st.fields[gpu::gpufield::TexOn] = tme ? 1u : 0u;
    st.fields[gpu::gpufield::Tfx] = static_cast<uint32_t>(texture.tfx);
    st.fields[gpu::gpufield::Tcc] = texture.tcc ? 1u : 0u;
    // RGBA8 words expanded on the CPU, or raw words copied from a resident target and expanded in the shader
    // with the TEXA fields (IN-346; HW_TEXEL_* of hw_pixel_core.glsl: 0 RGBA8, 1 CT16, 2 CT24).
    st.fields[gpu::gpufield::TexelKind] = 0;
    if (out.textureSlot >= 0 || (out.glmTexture && !out.glmIndexed)) {
        st.fields[gpu::gpufield::TexelKind] = texture.cls == TexClass::Ct16 ? 1u : texture.cls == TexClass::Ct24 ? 2u : 0u;
    }
    st.fields[gpu::gpufield::Ta0] = texture.texa.ta0;
    st.fields[gpu::gpufield::Ta1] = texture.texa.ta1;
    st.fields[gpu::gpufield::Aem] = texture.texa.aem ? 1u : 0u;
    st.fields[gpu::gpufield::FogOn] = fogEnabled ? 1u : 0u;
    st.fields[gpu::gpufield::FogR] = static_cast<uint32_t>(fogColour.r);
    st.fields[gpu::gpufield::FogG] = static_cast<uint32_t>(fogColour.g);
    st.fields[gpu::gpufield::FogB] = static_cast<uint32_t>(fogColour.b);
    st.fields[gpu::gpufield::TexFst] = texture.fst ? 1u : 0u;
    st.fields[gpu::gpufield::TexLinear] = texture.linear ? 1u : 0u;
    st.fields[gpu::gpufield::WrapS] = static_cast<uint32_t>(texture.wrapS.mode);
    st.fields[gpu::gpufield::WrapT] = static_cast<uint32_t>(texture.wrapT.mode);
    st.fields[gpu::gpufield::WrapMinS] = static_cast<uint32_t>(texture.wrapS.minimum);
    st.fields[gpu::gpufield::WrapMaxS] = static_cast<uint32_t>(texture.wrapS.maximum);
    st.fields[gpu::gpufield::WrapMinT] = static_cast<uint32_t>(texture.wrapT.minimum);
    st.fields[gpu::gpufield::WrapMaxT] = static_cast<uint32_t>(texture.wrapT.maximum);
    st.fields[gpu::gpufield::TexLog2W] = texture.twLog2;
    st.fields[gpu::gpufield::TexLog2H] = texture.thLog2;
    out.blended = blendEnabled;

    // Primitives, in stream order (chapter 03.2). Each keeps its own scissor (the CPU clip).
    bool haveRect = false;
    for (const PrimRecord& record : batch.prims) {
        counters.primitives++;
        if (record.rect.Empty()) continue;
        const Clip clip = Intersect(Intersect(ToClip(record.rect), scissor), surface);
        if (Empty(clip)) continue;
        const Vertex* v = &batch.vertices[record.first];
        const uint32_t firstVertex = static_cast<uint32_t>(out.vertices.size());
        gpu::GpuDraw draw;
        draw.x0 = clip.x0;
        draw.y0 = clip.y0;
        draw.x1 = clip.x1;
        draw.y1 = clip.y1;
        draw.first = firstVertex;
        switch (record.type) {
            case PrimType::Point: {
                // Window pixel cell: the vertex is moved to the pixel corner (IN-300).
                Vertex p = v[0];
                p.x4 = (p.x4 >> 4) << 4;
                p.y4 = (p.y4 >> 4) << 4;
                out.vertices.push_back(ToGpuVertex(p));
                draw.topology = gpu::GpuTopology::Points;
                draw.count = 1;
                break;
            }
            case PrimType::Line:
            case PrimType::LineStrip: {
                Vertex a = v[0];
                Vertex b = v[1];
                if (flat) {
                    a.r = b.r; a.g = b.g; a.b = b.b; a.a = b.a;  // last vertex (de-index, IN-212)
                    counters.flatDeindexedPrimitives++;
                }
                out.vertices.push_back(ToGpuVertex(a));
                out.vertices.push_back(ToGpuVertex(b));
                draw.topology = gpu::GpuTopology::Lines;
                draw.count = 2;
                break;
            }
            case PrimType::Triangle:
            case PrimType::TriangleStrip:
            case PrimType::TriangleFan: {
                Vertex a = v[0];
                Vertex b = v[1];
                Vertex c = v[2];
                if (flat) {
                    a.r = c.r; a.g = c.g; a.b = c.b; a.a = c.a;  // last vertex (de-index, IN-212)
                    b.r = c.r; b.g = c.g; b.b = c.b; b.a = c.a;
                    counters.flatDeindexedPrimitives++;
                }
                out.vertices.push_back(ToGpuVertex(a));
                out.vertices.push_back(ToGpuVertex(b));
                out.vertices.push_back(ToGpuVertex(c));
                draw.topology = gpu::GpuTopology::Triangles;
                draw.count = 3;
                break;
            }
            case PrimType::Sprite: {
                // Chapter 03.2: corner c takes X and S/U from (c & 1 ? second : first), Y and T/V from
                // (c & 2 ? second : first); colour, Z, fog and Q always from the second vertex.
                const Vertex& first = v[0];
                const Vertex& second = v[1];
                Vertex corner[4];
                for (int c = 0; c < 4; ++c) {
                    const Vertex& horizontal = (c & 1) ? second : first;
                    const Vertex& vertical = (c & 2) ? second : first;
                    Vertex q = second;  // colour, Z, fog, Q
                    q.x4 = horizontal.x4;
                    q.s = horizontal.s;
                    q.u = horizontal.u;
                    q.y4 = vertical.y4;
                    q.t = vertical.t;
                    q.v = vertical.v;
                    corner[c] = q;
                }
                const int order[6] = {0, 1, 2, 1, 2, 3};
                for (int index : order) out.vertices.push_back(ToGpuVertex(corner[index]));
                draw.topology = gpu::GpuTopology::Triangles;
                draw.count = 6;
                counters.spritesExpanded++;
                break;
            }
            case PrimType::Reserved:
                continue;
        }
        out.draws.push_back(draw);
        if (!haveRect) {
            out.rect = PixelRect{clip.x0, clip.y0, clip.x1, clip.y1};
            haveRect = true;
        } else {
            out.rect = UnionRects(out.rect, PixelRect{clip.x0, clip.y0, clip.x1, clip.y1});
        }
    }
}

// ---- Block spans (M7 coherence, IN-340) ------------------------------------------------------------
// A conservative range of GS blocks (modulo 16384) that a rectangle of a buffer can touch: whole page rows
// from the row of y0 to the row of y1, plus one page when the base is not page aligned and one page row per
// buffer width the rectangle extends past (the row wraps). count >= 16384 means "all memory".
struct BlockSpan {
    uint32_t begin = 0;
    uint32_t count = 0;
};

constexpr uint32_t kAllBlocks = kBlocksTotal;

BlockSpan SpanOfRect(const memory::BufferView& view, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (x1 < x0 || y1 < y0) return BlockSpan{};
    if (x0 < 0 || y0 < 0 || x1 >= 2048 || y1 >= 2048) return BlockSpan{0, kAllBlocks};
    const memory::PsmInfo& info = memory::psmInfo(view.psm);
    const uint32_t bufferWidth = std::max<uint32_t>(view.bufferWidth, 1u) * 64u;
    const uint32_t pagesPerRow = std::max<uint32_t>(1u, (bufferWidth + info.pageWidth - 1u) / info.pageWidth);
    const uint32_t firstRow = static_cast<uint32_t>(y0) / info.pageHeight;
    uint32_t rows = static_cast<uint32_t>(y1) / info.pageHeight - firstRow + 1u;
    if (static_cast<uint32_t>(x1) >= bufferWidth) rows += 1u + static_cast<uint32_t>(x1) / bufferWidth;
    // One extra page only when the base is not page aligned (the rectangle then spills into the next page).
    const uint64_t spill = (view.basePointer % kBlocksPerPage) != 0 ? kBlocksPerPage : 0;
    const uint64_t count = static_cast<uint64_t>(rows) * pagesPerRow * kBlocksPerPage + spill;
    if (count >= kAllBlocks) return BlockSpan{0, kAllBlocks};
    const uint32_t begin = (view.basePointer + firstRow * pagesPerRow * kBlocksPerPage) % kAllBlocks;
    return BlockSpan{begin, static_cast<uint32_t>(count)};
}

BlockSpan SpanOfRect(const memory::BufferView& view, const PixelRect& r) { return SpanOfRect(view, r.x0, r.y0, r.x1, r.y1); }

bool SpansOverlap(const BlockSpan& a, const BlockSpan& b) {
    if (a.count == 0 || b.count == 0) return false;
    if (a.count >= kAllBlocks || b.count >= kAllBlocks) return true;
    const uint32_t ab = (b.begin + kAllBlocks - a.begin) % kAllBlocks;
    const uint32_t ba = (a.begin + kAllBlocks - b.begin) % kAllBlocks;
    return ab < a.count || ba < b.count;
}

// Span of a transfer rectangle (02.7): coordinates wrap at 2048, which the conservative span reports as all
// memory.
BlockSpan SpanOfTransfer(uint32_t bp, uint32_t bw, uint32_t psmCode, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const auto psm = memory::decodePsm(psmCode);
    if (!psm) return BlockSpan{0, kAllBlocks};
    if (w == 0 || h == 0) return BlockSpan{};
    return SpanOfRect(memory::BufferView{bp, bw, *psm}, static_cast<int32_t>(x), static_cast<int32_t>(y),
                      static_cast<int32_t>(x + w - 1u), static_cast<int32_t>(y + h - 1u));
}

bool SameView(const memory::BufferView& a, const memory::BufferView& b) {
    return a.basePointer == b.basePointer && a.bufferWidth == b.bufferWidth && a.psm == b.psm;
}

bool Contains(const PixelRect& outer, const PixelRect& inner) {
    return !outer.Empty() && outer.x0 <= inner.x0 && outer.y0 <= inner.y0 && outer.x1 >= inner.x1 && outer.y1 >= inner.y1;
}

// The batch reads or writes depth (01.5): a depth test other than ALWAYS, or depth writes not masked. A
// batch that does neither leaves the depth image untouched, so no depth target is needed for it.
bool DepthUsed(const gpu::GpuPixelState& st) {
    const bool testReads = st.fields[gpu::gpufield::Ztest] != 0 &&
                           st.fields[gpu::gpufield::Ztst] != static_cast<uint32_t>(pixel::DepthCompare::Always);
    return testReads || st.fields[gpu::gpufield::Zmsk] == 0;
}


} // namespace

bool ExecuteBatch(gpu::GpuDrawer &gpu, const BatchTranslation &t, memory::LocalMemory &local, Counters &counters,
                  std::string &error) {
    if (t.draws.empty()) return true;
    const std::int32_t rectX0 = t.rect.x0, rectY0 = t.rect.y0, rectX1 = t.rect.x1, rectY1 = t.rect.y1;
    const std::size_t words =
        static_cast<std::size_t>(rectX1 - rectX0 + 1) * static_cast<std::size_t>(rectY1 - rectY0 + 1);
    // Chunks of whole draws, each within the vertex buffer limit. Every chunk reads and writes the same
    // rectangle, so a later chunk sees the earlier chunk's pixels through the SLM.
    std::size_t begin = 0;
    while (begin < t.draws.size()) {
        std::size_t end = begin;
        std::uint32_t vertexCount = 0;
        while (end < t.draws.size() && vertexCount + t.draws[end].count <= gpu::kGpuMaxVertices) {
            vertexCount += t.draws[end].count;
            ++end;
        }
        if (end == begin) {
            error = "a primitive exceeds the vertex buffer";
            return false;
        }
        const std::uint32_t baseVertex = t.draws[begin].first;
        gpu::GpuChunk chunk;
        chunk.rectX0 = rectX0;
        chunk.rectY0 = rectY0;
        chunk.rectX1 = rectX1;
        chunk.rectY1 = rectY1;
        chunk.colourSlot = gpu::kGpuScratchColourSlot;  // the write-back mode never touches resident targets
        chunk.depthSlot = gpu::kGpuScratchDepthSlot;
        chunk.state = t.state;
        chunk.hasTexture = t.hasTexture;
        chunk.textureSlot = t.textureSlot;
        chunk.glmTexture = t.glmTexture;
        chunk.glmBp = t.glmBp;
        chunk.glmBw = t.glmBw;
        chunk.glmPsm = t.glmPsm;
        chunk.glmIndexed = t.glmIndexed;
        chunk.glmGpuClut = t.glmGpuClut;
        chunk.glmCpsm = t.glmCpsm;
        chunk.glmCsa = t.glmCsa;
        chunk.glmTa0 = t.glmTa0;
        chunk.glmTa1 = t.glmTa1;
        chunk.glmAem = t.glmAem;
        chunk.palette = t.palette;
        chunk.texWidth = t.texWidth;
        chunk.texHeight = t.texHeight;
        chunk.texels = t.texels;
        chunk.vertices.assign(t.vertices.begin() + baseVertex, t.vertices.begin() + baseVertex + vertexCount);
        for (std::size_t i = begin; i < end; ++i) {
            gpu::GpuDraw draw = t.draws[i];
            draw.first -= baseVertex;
            chunk.draws.push_back(draw);
        }
        chunk.frameWords.resize(words);
        chunk.depthWords.resize(words);
        const uint32_t rectWidth = static_cast<uint32_t>(rectX1 - rectX0 + 1);
        const uint32_t rectHeight = static_cast<uint32_t>(rectY1 - rectY0 + 1);
        local.readRect(t.frameView, static_cast<uint32_t>(rectX0), static_cast<uint32_t>(rectY0), rectWidth, rectHeight,
                       chunk.frameWords.data());
        local.readRect(t.zView, static_cast<uint32_t>(rectX0), static_cast<uint32_t>(rectY0), rectWidth, rectHeight,
                       chunk.depthWords.data());
        std::size_t index = 0;
        const std::vector<uint32_t> frameBefore = chunk.frameWords;
        const std::vector<uint32_t> depthBefore = chunk.depthWords;
        if (!gpu.Execute(chunk, error)) return false;
        counters.gpuChunks++;
        counters.gpuDraws += chunk.draws.size();
        // Write back only the words the draws changed (IN-335). The frame and depth buffers may share
        // local memory (a 2D pass with an unused ZBUF pointing at the other display buffer, or at the
        // frame itself): writing the untouched pre-draw depth words over the rectangle would put the
        // old pixels back over what this chunk just drew. Per pixel the frame word is stored before the
        // depth word, the order the CPU reference uses (cpuref_draw.cpp), so a changed depth word that
        // aliases a changed frame word wins as it does there.
        uint64_t frameChanged = 0;
        uint64_t depthChanged = 0;
        index = 0;
        for (int32_t y = rectY0; y <= rectY1; ++y) {
            for (int32_t x = rectX0; x <= rectX1; ++x, ++index) {
                if (chunk.frameWords[index] != frameBefore[index]) {
                    local.writePixel(t.frameView, static_cast<uint32_t>(x), static_cast<uint32_t>(y),
                                     chunk.frameWords[index]);
                    ++frameChanged;
                }
                if (chunk.depthWords[index] != depthBefore[index]) {
                    local.writePixel(t.zView, static_cast<uint32_t>(x), static_cast<uint32_t>(y),
                                     chunk.depthWords[index]);
                    ++depthChanged;
                }
            }
        }
        counters.writebackPixels += frameChanged;
        counters.writebackDepthWords += depthChanged;
        begin = end;
    }
    return true;
}

void TranslateBatch(const Batch &batch, const memory::LocalMemory &local, const clut::ClutUnit &clut,
                    BatchTranslation &out, Counters &counters) {
    Translate(batch, local, clut, out, counters);
}

// ---- Renderer -----------------------------------------------------------------------------------

// Why resident targets are written back (counters only; the M7 tuning needs to know).
// Why a resident target was written back (profile counters; Other = host reads of the whole memory).
enum class WritebackReason : uint8_t {
    Other, Texture, Clut, Transfer, Display, Eviction, LoadOverlap, Fallback, CpuSprite, Count
};

struct ReasonScope {
    WritebackReason& slot;
    WritebackReason saved;
    ReasonScope(WritebackReason& target, WritebackReason reason) : slot(target), saved(target) { slot = reason; }
    ~ReasonScope() { slot = saved; }
};

class Renderer::Impl final : public BatchSink, public SyncSink, public TransferSink, public TextureCacheView {
public:
    // ---- Shadow-memory write tracking and the decoded-texture cache (IN-341) ----------------------------
    // Every write into the shadow memory bumps the sequence number of the pages it may touch (conservative
    // spans). A cached decode is reused only when none of its pages changed after it was made.
    void MarkWritten(const BlockSpan& span) {
        if (span.count == 0) return;
        ++writeSequence_;
        if (span.count >= kAllBlocks) {
            pageWrites_.fill(writeSequence_);
            return;
        }
        const uint32_t firstPage = span.begin / kBlocksPerPage;
        const uint32_t lastBlock = span.begin + span.count - 1u;
        for (uint32_t page = firstPage; page <= lastBlock / kBlocksPerPage; ++page) {
            pageWrites_[page % memory::kPagesTotal] = writeSequence_;
        }
    }

    bool UnchangedSince(const BlockSpan& span, uint64_t sequence) const {
        if (span.count >= kAllBlocks) {
            for (uint64_t value : pageWrites_) {
                if (value > sequence) return false;
            }
            return true;
        }
        const uint32_t firstPage = span.begin / kBlocksPerPage;
        const uint32_t lastBlock = span.begin + span.count - 1u;
        for (uint32_t page = firstPage; page <= lastBlock / kBlocksPerPage; ++page) {
            if (pageWrites_[page % memory::kPagesTotal] > sequence) return false;
        }
        return true;
    }

    struct CachedTexture {
        TextureKey key;
        BlockSpan span;
        uint64_t sequence = 0;
        uint64_t lastUse = 0;
        std::vector<uint32_t> texels;
    };

    static uint64_t KeyHash(const TextureKey& k) {
        const uint64_t words[] = {k.tbp, k.tbw, k.psm, k.width, k.height, k.ta0, k.ta1, k.aem, k.paletteHash};
        uint64_t hash = 1469598103934665603ull;
        for (uint64_t w : words) {
            hash ^= w;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    const std::vector<uint32_t>* find(const TextureKey& key, const memory::BufferView&) override {
        if (!textureCacheEnabled_) return nullptr;
        auto it = textures_.find(KeyHash(key));
        if (it == textures_.end() || !(it->second.key == key)) return nullptr;
        if (!UnchangedSince(it->second.span, it->second.sequence)) {
            textures_.erase(it);
            return nullptr;
        }
        it->second.lastUse = ++textureUses_;
        return &it->second.texels;
    }

    void store(const TextureKey& key, const memory::BufferView& view, const std::vector<uint32_t>& texels) override {
        if (!textureCacheEnabled_) return;
        if (textures_.size() >= kTextureCacheEntries) {
            // Least recently used entry goes (the cache is small: GT4 menus reuse a few dozen textures).
            auto oldest = textures_.begin();
            for (auto it = textures_.begin(); it != textures_.end(); ++it) {
                if (it->second.lastUse < oldest->second.lastUse) oldest = it;
            }
            textures_.erase(oldest);
        }
        CachedTexture entry;
        entry.key = key;
        entry.span = SpanOfRect(view, 0, 0, static_cast<int32_t>(key.width) - 1, static_cast<int32_t>(key.height) - 1);
        entry.sequence = writeSequence_;
        entry.lastUse = ++textureUses_;
        entry.texels = texels;
        textures_[KeyHash(key)] = std::move(entry);
    }

    static FrontEndOptions RendererFrontEndOptions() {
        FrontEndOptions options;
        options.vertexTrace = false;  // no consumer yet (IN-353)
        return options;
    }

    // IN-353: the CPU CLUT copies entries only while the GPU CLUT is not authoritative. Leaving GPU CLUT mode
    // reads the GPU CLUT back into the CPU unit so it is exact again.
    void UpdateClutCopies() {
        clut::ClutUnit& unit = frontEnd_.MutableClut();
        const bool copies = !GpuClutActive();
        if (copies == unit.CopiesData()) return;
        if (copies && gpuClutSynced_ && gpu_.Ready()) {
            std::vector<uint16_t> halfwords;
            std::string error;
            if (!gpu_.ReadClut(halfwords, error)) {
                Fail(error);
                return;
            }
            unit.SetHalfwords(halfwords.data());
        }
        unit.SetCopiesData(copies);
    }

    Impl() : frontEnd_(RendererFrontEndOptions(), *this, *this, *this) {
        frontEnd_.SetLocalMemory(memory_.Local().bytes().data());
        InitSlots();
        frontEnd_.SetClutLoaded([this](const clut::Tex0Write& write) { OnClutLoaded(write); });
        frontEnd_.SetBeforeLocalRead([this](const BlockRange& range) {
            if (GpuClutActive()) return;  // IN-349: the GPU repeats the load from the GLM; no write-back
            {
                ReasonScope scope(writebackReason_, WritebackReason::Clut);
                CoherentForRead(BlockSpan{range.begin, range.count});
            }
        });
    }

    // ---- Transfers (02.7, 10.2): the resident targets are made coherent, then the shadow memory works ----
    void InvalidateDestination(const UploadCommit& c) override {
        ReasonScope scope(writebackReason_, WritebackReason::Transfer);
        // With GLM parking the slots park and CommitUpload merges into GLM-owned pages (IN-350).
        CoherentForWrite(SpanOfTransfer(c.dbp, c.dbw, c.dpsm, c.dsax, c.dsay, c.rrw, c.rrh), nullptr,
                         GlmParkingActive());
        memory_.InvalidateDestination(c);
    }
    void CommitUpload(const UploadCommit& c, const uint8_t* bytes, size_t byteCount) override {

        const BlockSpan span = SpanOfTransfer(c.dbp, c.dbw, c.dpsm, c.dsax, c.dsay, c.rrw, c.rrh);
        StageTimer timer(counters.uploadUs);
        if (GlmParkingActive()) {
            {
                ReasonScope scope(writebackReason_, WritebackReason::Transfer);
                CoherentForWrite(span, nullptr, true);  // resident slots park (no CPU wait)
            }
            if (gpuUploads_ && UploadOnGpu(c, bytes, byteCount, span)) return;
            if (AnyGlmOwned(span)) {
                MergeUploadIntoGlm(c, bytes, byteCount, span);
                return;
            }
            memory_.CommitUpload(c, bytes, byteCount);
            MarkWritten(span);
            return;
        }
        {
            ReasonScope scope(writebackReason_, WritebackReason::Transfer);
            CoherentForWrite(span);
        }
        memory_.CommitUpload(c, bytes, byteCount);
        MarkWritten(span);
    }

    // IN-351: the upload is written into the GLM by a kernel instead of into the shadow memory pixel by pixel on
    // the CPU (GT4 streams megabytes of textures per race frame). The stale pages of the destination are uploaded
    // to the GLM first, then the kernel writes the transfer's pixels, and the pages become GLM-owned: GPU users
    // read them there; a CPU user downloads them first. Only rectangles whose pixels have distinct addresses go
    // this way (no row wrap inside the buffer width, no coordinate wrap); others take the CPU path.
    bool UploadOnGpu(const UploadCommit& c, const uint8_t* bytes, size_t byteCount, const BlockSpan& span) {
        const auto psm = memory::decodePsm(c.dpsm);
        if (!psm || c.rrw == 0 || c.rrh == 0 || span.count >= kAllBlocks) return false;
        const memory::PsmInfo& info = memory::psmInfo(*psm);
        const uint32_t perRow = std::max<uint32_t>(1u, (c.dbw * 64u) / info.pageWidth);
        const uint32_t rowLimit = std::min<uint32_t>(perRow * info.pageWidth, memory::kTransferCoordinateWrap);
        if (c.dsax + c.rrw > rowLimit || c.dsay + c.rrh > memory::kTransferCoordinateWrap) return false;
        const uint64_t available = static_cast<uint64_t>(byteCount) * 8u / info.transferBpp;
        const uint32_t pixels =
            static_cast<uint32_t>(std::min<uint64_t>(available, static_cast<uint64_t>(c.rrw) * c.rrh));
        if (!UploadStalePages(span)) return true;
        std::string error;
        if (!gpu_.UploadHostToGlm(c.dbp, c.dbw, c.dpsm, c.dsax, c.dsay, c.rrw, pixels, info.transferBpp, bytes,
                                  byteCount, error)) {
            Fail(error);
            return true;
        }
        ForEachPage(span, [&](uint32_t page) {
            if (!glmOwned_[page]) {
                glmOwned_[page] = true;
                ++glmOwnedPages_;
            }
        });
        counters.glmHostUploads++;
        return true;
    }

    // IN-350: an upload whose destination has GLM-owned pages. The exact bytes it writes there are found by
    // writing it twice into those pages filled with 0x00 and with 0xFF (a bit the upload writes is equal in both
    // passes); they are merged into the GLM, so the pages stay GLM-owned and need no download. The shadow memory
    // gets the upload too (the other pages are then exact; GLM-owned pages are replaced on their next download).
    void MergeUploadIntoGlm(const UploadCommit& c, const uint8_t* bytes, size_t byteCount, const BlockSpan& span) {
        const auto psm = memory::decodePsm(c.dpsm);
        if (!psm) return;  // invalid PSM: the transfer is dropped (front-end rule), as in the shadow memory sink
        const memory::BufferView destination{c.dbp, c.dbw, *psm};
        const memory::TransferRect rect{c.dsax, c.dsay, c.rrw, c.rrh};
        std::vector<uint32_t> owned;
        ForEachPage(span, [&](uint32_t page) {
            if (glmOwned_[page]) owned.push_back(page);
        });
        uint8_t* local = memory_.Local().bytes().data();
        constexpr size_t kPageBytes = 8192;
        std::vector<uint8_t> saved(owned.size() * kPageBytes);
        std::vector<uint8_t> zeros(owned.size() * kPageBytes);
        for (size_t i = 0; i < owned.size(); ++i) {
            std::memcpy(saved.data() + i * kPageBytes, local + owned[i] * kPageBytes, kPageBytes);
            std::memset(local + owned[i] * kPageBytes, 0x00, kPageBytes);
        }
        memory::hostToLocal(memory_.Local(), destination, rect, std::span<const uint8_t>(bytes, byteCount));
        for (size_t i = 0; i < owned.size(); ++i) {
            std::memcpy(zeros.data() + i * kPageBytes, local + owned[i] * kPageBytes, kPageBytes);
            std::memset(local + owned[i] * kPageBytes, 0xFF, kPageBytes);
        }
        memory::hostToLocal(memory_.Local(), destination, rect, std::span<const uint8_t>(bytes, byteCount));
        std::vector<uint32_t> triples;
        for (size_t i = 0; i < owned.size(); ++i) {
            const uint8_t* ones = local + owned[i] * kPageBytes;
            const uint8_t* zero = zeros.data() + i * kPageBytes;
            for (uint32_t word = 0; word < kPageBytes / 4; ++word) {
                // Bit granularity: a T4 upload writes nibbles, so a written bit is one equal in both passes.
                uint32_t fromZeros = 0;
                uint32_t fromOnes = 0;
                std::memcpy(&fromZeros, zero + word * 4, 4);
                std::memcpy(&fromOnes, ones + word * 4, 4);
                const uint32_t mask = ~(fromZeros ^ fromOnes);
                const uint32_t value = fromZeros & mask;
                if (mask != 0) {
                    triples.push_back(owned[i] * static_cast<uint32_t>(kPageBytes / 4) + word);
                    triples.push_back(value);
                    triples.push_back(mask);
                }
            }
            std::memcpy(local + owned[i] * kPageBytes, saved.data() + i * kPageBytes, kPageBytes);
        }
        memory_.CommitUpload(c, bytes, byteCount);  // the real write (exact for the pages the GLM does not own)
        std::string error;
        if (!gpu_.MergeIntoGlm(triples, error)) {
            Fail(error);
            return;
        }
        MarkWritten(span);  // decoded-texture cache entries expire; pages the GLM does not own become stale there
        for (uint32_t page : owned) glmPageSequence_[page] = pageWrites_[page] + 1;  // the GLM holds the write
        counters.glmUploadMerges++;
        counters.glmMergedWords += triples.size() / 3;
    }
    void LocalToLocalMove(const LocalMoveCommit& m) override {

        {
            ReasonScope scope(writebackReason_, WritebackReason::Transfer);
            CoherentForRead(SpanOfTransfer(m.sbp, m.sbw, m.spsm, m.ssax, m.ssay, m.rrw, m.rrh));
        }
        const BlockSpan destination = SpanOfTransfer(m.dbp, m.dbw, m.dpsm, m.dsax, m.dsay, m.rrw, m.rrh);
        CoherentForWrite(destination);
        memory_.LocalToLocalMove(m);
        MarkWritten(destination);
    }
    void BeginLocalToHost(const LocalToHostSetup& r) override {
        pendingReadSpan_ = SpanOfTransfer(r.sbp, r.sbw, r.spsm, r.ssax, r.ssay, r.rrw, r.rrh);
        {
            ReasonScope scope(writebackReason_, WritebackReason::Transfer);
            CoherentForRead(SpanOfTransfer(r.sbp, r.sbw, r.spsm, r.ssax, r.ssay, r.rrw, r.rrh));
        }
        memory_.BeginLocalToHost(r);
    }
    void CancelLocalToHost() override { memory_.CancelLocalToHost(); }

    // ---- Resident targets (M7, IN-340, IN-344) ----------------------------------------------------------
    // Colour and depth targets stay in GPU target slots between batches (kGpuColourSlots - 1 colour slots and
    // kGpuDepthSlots - 1 depth slots; the last of each is the write-back mode's scratch pair). A target is an
    // exact (BP, BW, PSM) at 1x; `loaded` is the rectangle its image holds (equal to the shadow memory when it
    // was loaded), `drawn` the part the GPU changed since (newer than the shadow memory). Any shadow-memory read
    // of a drawn range writes it back first; any write over a loaded range writes back and drops the target; no
    // two slots ever hold overlapping memory (loading one writes back and drops the others it overlaps). A batch
    // whose colour and depth buffers share memory takes the write-back path, so the cache mode stores exactly
    // what the write-back mode stores (spec 15.2 M7 gate). The least recently used slot is evicted.
    struct ResidentSlot {
        bool depth = false;
        uint32_t gpuIndex = 0;     // GPU image of this slot
        bool hasIdentity = false;
        memory::BufferView view{0, 0, memory::Psm::Ct32};
        PixelRect loaded;  // empty = nothing loaded
        PixelRect drawn;   // empty = nothing newer than the shadow memory
        uint64_t lastUse = 0;
        // IN-350: the GLM already holds this slot's drawn pixels when no draw and no GLM page upload under them
        // happened since its last swizzle (GT4 draws palettes into a target and loads CLUTs from it many times).
        uint64_t drawSequence = 0;        // bumped by every draw into the slot
        uint64_t glmSwizzledDraw = ~0ull; // drawSequence at the last swizzle (~0: none)
        uint64_t glmSwizzledClock = 0;    // glmUploadClock_ at the last swizzle
    };

    static constexpr uint32_t kResidentColourSlots = gpu::kGpuColourSlots - 1;
    static constexpr uint32_t kResidentDepthSlots = gpu::kGpuDepthSlots - 1;

    void InitSlots() {
        for (uint32_t i = 0; i < kResidentColourSlots; ++i) slots_.push_back(ResidentSlot{false, i});
        for (uint32_t i = 0; i < kResidentDepthSlots; ++i) slots_.push_back(ResidentSlot{true, i});
    }

    BlockSpan LoadedSpan(const ResidentSlot& slot) const {
        return slot.loaded.Empty() ? BlockSpan{} : SpanOfRect(slot.view, slot.loaded);
    }

    void WriteBack(ResidentSlot& slot) {
        if (slot.drawn.Empty() || failed_) return;
        // The words around the drawn rectangle in GLM-owned pages are newer in the GLM (IN-350).
        Materialise(SpanOfRect(slot.view, slot.drawn));
        counters.cacheWritebackByReason[static_cast<size_t>(writebackReason_)]++;
        StageTimer timer(counters.cacheWritebackUs);
        const PixelRect r = slot.drawn;
        gpu::GpuChunk chunk;
        chunk.rectX0 = r.x0;
        chunk.rectY0 = r.y0;
        chunk.rectX1 = r.x1;
        chunk.rectY1 = r.y1;
        chunk.colourSlot = slot.depth ? gpu::kGpuScratchColourSlot : slot.gpuIndex;
        chunk.depthSlot = slot.depth ? slot.gpuIndex : gpu::kGpuScratchDepthSlot;
        chunk.uploadFrame = false;
        chunk.uploadDepth = false;
        chunk.readbackFrame = !slot.depth;
        chunk.readbackDepth = slot.depth;
        const size_t words = static_cast<size_t>(r.x1 - r.x0 + 1) * static_cast<size_t>(r.y1 - r.y0 + 1);
        std::vector<uint32_t>& data = slot.depth ? chunk.depthWords : chunk.frameWords;
        data.resize(words);
        std::string error;
        if (!gpu_.Execute(chunk, error)) {
            Fail(error);
            return;
        }
        memory_.Local().writeRect(slot.view, static_cast<uint32_t>(r.x0), static_cast<uint32_t>(r.y0),
                                  static_cast<uint32_t>(r.x1 - r.x0 + 1), static_cast<uint32_t>(r.y1 - r.y0 + 1),
                                  data.data());
        counters.cacheWritebacks++;
        counters.cacheWritebackWords += words;
        MarkWritten(SpanOfRect(slot.view, r));
        slot.drawn = PixelRect{};
    }

    void Drop(ResidentSlot& slot) {
        slot.loaded = PixelRect{};
        slot.drawn = PixelRect{};
        slot.hasIdentity = false;
        slot.glmSwizzledDraw = ~0ull;
    }

    // The shadow memory of `span` becomes current for a CPU reader: drawn slots are written back, GLM-owned
    // pages are downloaded.
    void CoherentForRead(const BlockSpan& span) {
        if (GlmParkingActive() && glmBatchedWritebacks_) {
            std::vector<ResidentSlot*> slots;
            for (ResidentSlot& slot : slots_) {
                if (!slot.drawn.Empty() && SpansOverlap(SpanOfRect(slot.view, slot.drawn), span)) {
                    slots.push_back(&slot);
                }
            }
            WriteBackViaGlm(slots, span);
            return;
        }
        for (ResidentSlot& slot : slots_) {
            if (!slot.drawn.Empty() && SpansOverlap(SpanOfRect(slot.view, slot.drawn), span)) WriteBack(slot);
        }
        Materialise(span);
    }

    // IN-357: the drawn rectangles of `slots` and the GLM-owned pages of `span` reach the shadow memory through
    // ONE GLM download (one submission and one wait) instead of one readback per slot: the stale pages are
    // uploaded, the slots swizzled into the GLM (skipped when it already holds them), then every page that may
    // differ from the shadow memory (the slots' drawn pages and the GLM-owned pages of `span`) is downloaded. The
    // slots stay resident with nothing GPU-newer, as after WriteBack. A slot whose drawn rectangle is wider than
    // its buffer (aliasing addresses) takes the per-slot WriteBack.
    void WriteBackViaGlm(const std::vector<ResidentSlot*>& slots, const BlockSpan& span) {
        if (failed_) return;
        std::vector<ResidentSlot*> batched;
        for (ResidentSlot* slot : slots) {
            if (slot->drawn.Empty()) continue;
            const int32_t bufferWidth = static_cast<int32_t>(std::max<uint32_t>(slot->view.bufferWidth, 1u) * 64u);
            if (slot->drawn.x1 >= bufferWidth) {
                WriteBack(*slot);
                continue;
            }
            batched.push_back(slot);
        }
        if (batched.empty() && !AnyGlmOwned(span)) return;
        StageTimer timer(counters.cacheWritebackUs);
        for (ResidentSlot* slot : batched) {
            if (!SwizzleSlotIntoGlm(*slot)) return;  // uploads the stale pages under it first
        }
        std::vector<bool> wanted(memory::kPagesTotal, false);
        for (ResidentSlot* slot : batched) {
            ForEachPage(SpanOfRect(slot->view, slot->drawn), [&](uint32_t page) { wanted[page] = true; });
        }
        ForEachPage(span, [&](uint32_t page) {
            if (glmOwned_[page]) wanted[page] = true;
        });
        // Every wanted page must be current in the GLM before it is downloaded (pages under a slot's drawn
        // rectangle were uploaded by its swizzle; GLM-owned pages are current by definition).
        std::vector<std::pair<uint32_t, uint32_t>> ranges;
        std::vector<uint32_t> pages;
        for (uint32_t page = 0; page < memory::kPagesTotal; ++page) {
            if (!wanted[page]) continue;
            if (!glmOwned_[page] && (glmPageSequence_[page] == 0 || glmPageSequence_[page] < pageWrites_[page] + 1)) {
                Fail("GLM page " + std::to_string(page) + " is stale at a batched write-back");
                return;
            }
            const uint32_t offset = page * 8192u;
            if (!ranges.empty() && ranges.back().first + ranges.back().second == offset) {
                ranges.back().second += 8192u;
            } else {
                ranges.push_back({offset, 8192u});
            }
            pages.push_back(page);
        }
        std::string error;
        if (!gpu_.DownloadGlm(ranges, memory_.Local().bytes().data(), error)) {
            Fail(error);
            return;
        }
        for (uint32_t page : pages) {
            if (glmOwned_[page]) {
                glmOwned_[page] = false;
                --glmOwnedPages_;
            }
            MarkWritten(BlockSpan{page * kBlocksPerPage, kBlocksPerPage});  // decoded-texture cache entries expire
            glmPageSequence_[page] = pageWrites_[page] + 1;                  // the GLM equals the shadow memory
        }
        for (ResidentSlot* slot : batched) {
            counters.cacheWritebackByReason[static_cast<size_t>(writebackReason_)]++;
            counters.cacheWritebacks++;
            slot->drawn = PixelRect{};
        }
        counters.glmBatchedWritebacks++;
        counters.glmPagesDownloaded += pages.size();
    }

    // Every slot that holds memory in `span` (except `keep`, may be null) gives it up. For a CPU writer
    // (`forGpu` false) the slots are written back and GLM-owned pages downloaded, so the shadow memory is
    // current before it changes. For a GPU consumer (a slot about to load `span`) the slots are parked in the
    // GLM instead (IN-350): no CPU stall; the consumer then loads those pages from the GLM.
    void CoherentForWrite(const BlockSpan& span, const ResidentSlot* keep = nullptr, bool forGpu = false) {
        const bool park = forGpu && GlmParkingActive();
        for (ResidentSlot& slot : slots_) {
            if (&slot == keep) continue;
            if (SpansOverlap(LoadedSpan(slot), span)) {
                if (park) {
                    Park(slot);
                } else {
                    WriteBack(slot);
                }
                Drop(slot);
            }
        }
        if (!forGpu) Materialise(span);
    }

    void WriteBackAll() {
        for (ResidentSlot& slot : slots_) WriteBack(slot);
        Materialise(BlockSpan{0, kAllBlocks});
    }

    // ---- GLM-owned pages (IN-350, chapters 04.9 and 05.3) ----------------------------------------------
    // A page is GLM-owned when its newest content lives only in the GLM: a slot's drawn rectangle was parked
    // there (swizzled into the GLM at its own layout) instead of being written back. The GLM copy of the rest of
    // such a page is current (its stale pages were uploaded before the swizzle), so the GLM holds the whole page.
    // GPU users (slot loads, GLM texture decodes, GPU CLUT loads) read it from the GLM directly; any CPU reader or
    // writer of the shadow memory downloads it first (Materialise). This is GT4's buffer reuse: the same memory
    // drawn under other widths and formats (a 640-wide depth buffer, then a 64-wide one at the same base; a
    // 128-wide target inside the frame buffer) moves between slots on the GPU.
    bool GlmParkingActive() const { return cacheMode_ && glmEnabled_ && glmParking_; }

    template <typename Visit>
    static void ForEachPage(const BlockSpan& span, Visit visit) {
        if (span.count == 0) return;
        if (span.count >= kAllBlocks) {
            for (uint32_t page = 0; page < memory::kPagesTotal; ++page) visit(page);
            return;
        }
        const uint32_t first = span.begin / kBlocksPerPage;
        const uint32_t last = (span.begin + span.count - 1u) / kBlocksPerPage;
        for (uint32_t page = first; page <= last; ++page) visit(page % memory::kPagesTotal);
    }

    bool AnyGlmOwned(const BlockSpan& span) const {
        if (glmOwnedPages_ == 0) return false;
        bool any = false;
        ForEachPage(span, [&](uint32_t page) { any = any || glmOwned_[page]; });
        return any;
    }

    // Makes the GLM hold the slot's drawn pixels: uploads the stale pages under them, then swizzles, unless the
    // GLM already holds them (no draw into the slot and no upload of those pages since its last swizzle).
    bool SwizzleSlotIntoGlm(ResidentSlot& slot) {
        const BlockSpan span = SpanOfRect(slot.view, slot.drawn);
        if (!UploadStalePages(span)) return false;
        bool current = slot.glmSwizzledDraw == slot.drawSequence;
        if (current) {
            ForEachPage(span, [&](uint32_t page) {
                current = current && glmPageUploadClock_[page] <= slot.glmSwizzledClock;
            });
        }
        if (current) {
            counters.glmSwizzlesSkipped++;
            return true;
        }
        std::string error;
        if (!gpu_.SwizzleToGlm(slot.depth, slot.gpuIndex, slot.view.basePointer, slot.view.bufferWidth,
                               static_cast<uint32_t>(slot.view.psm), slot.drawn.x0, slot.drawn.y0, slot.drawn.x1,
                               slot.drawn.y1, error)) {
            Fail(error);
            return false;
        }
        slot.glmSwizzledDraw = slot.drawSequence;
        slot.glmSwizzledClock = glmUploadClock_;
        counters.glmSwizzles++;
        return true;
    }

    // Swizzles the slot's drawn rectangle into the GLM and marks those pages GLM-owned (no CPU wait).
    void Park(ResidentSlot& slot) {
        if (slot.drawn.Empty() || failed_) return;
        const int32_t bufferWidth = static_cast<int32_t>(std::max<uint32_t>(slot.view.bufferWidth, 1u) * 64u);
        if (slot.drawn.x1 >= bufferWidth) {
            WriteBack(slot);  // wraps into the next page row: no defined last writer for a parallel swizzle
            return;
        }
        const BlockSpan span = SpanOfRect(slot.view, slot.drawn);
        if (!SwizzleSlotIntoGlm(slot)) return;
        ForEachPage(span, [&](uint32_t page) {
            if (!glmOwned_[page]) {
                glmOwned_[page] = true;
                ++glmOwnedPages_;
            }
        });
        counters.glmParks++;
        slot.drawn = PixelRect{};
    }

    // Downloads the GLM-owned pages of `span` into the shadow memory (submits and waits).
    void Materialise(const BlockSpan& span) {
        if (glmOwnedPages_ == 0 || failed_) return;
        std::vector<std::pair<uint32_t, uint32_t>> ranges;
        std::vector<uint32_t> pages;
        ForEachPage(span, [&](uint32_t page) {
            if (!glmOwned_[page]) return;
            const uint32_t offset = page * 8192u;
            if (!ranges.empty() && ranges.back().first + ranges.back().second == offset) {
                ranges.back().second += 8192u;
            } else {
                ranges.push_back({offset, 8192u});
            }
            pages.push_back(page);
        });
        if (pages.empty()) return;
        StageTimer timer(counters.cacheWritebackUs);
        std::string error;
        if (!gpu_.DownloadGlm(ranges, memory_.Local().bytes().data(), error)) {
            Fail(error);
            return;
        }
        for (uint32_t page : pages) {
            glmOwned_[page] = false;
            --glmOwnedPages_;
            MarkWritten(BlockSpan{page * kBlocksPerPage, kBlocksPerPage});  // decoded-texture cache entries expire
            glmPageSequence_[page] = pageWrites_[page] + 1;                  // the GLM equals the shadow memory
        }
        counters.glmDownloads++;
        counters.glmPagesDownloaded += pages.size();
    }

    // The memory a display circuit reads (13.2 read rectangle; a wrapping or unknown read reports all memory).
    static BlockSpan DisplayCircuitSpan(const display::CircuitRect& circuit) {
        const auto psm = memory::decodePsm(circuit.psm);
        if (!psm) return BlockSpan{0, kAllBlocks};
        return SpanOfRect(memory::BufferView{memory::blockOfPage(circuit.fbp), circuit.fbw, *psm}, circuit.dbx,
                          circuit.dby, circuit.dbx + static_cast<int32_t>(circuit.read_width) - 1,
                          circuit.dby + static_cast<int32_t>(circuit.read_height) - 1);
    }

    // This VBlank's display layout (13.2), computed once per VBlank (it advances the DBY history).
    void EnsureLayout() {
        if (layoutVblank_ != vblanks_ || !layoutValid_) {
            layout_ = display::compute_layout(displayRegs_, previousDby_);
            layoutVblank_ = vblanks_;
            layoutValid_ = true;
        }
    }

    // IN-355: records the download of the displayed memory into the current recording (before the frame's
    // submission). The GLM is made current for it first: stale pages uploaded, drawn targets swizzled.
    void CaptureDisplay() {
        captureShown_ = std::move(captureNext_);
        captureNext_ = DisplayCapture{};
        EnsureLayout();
        {
            display::MergePlan plan;
            std::array<bool, 2> available{};
            if (GpuComposeApplies(layout_, plan, available)) return;  // composed on the GPU at present (IN-364)
        }
        DisplayCapture capture;
        capture.layout = layout_;
        capture.regs = displayRegs_;
        capture.background = background_;
        capture.oddField = oddField_;
        std::vector<std::pair<uint32_t, uint32_t>> ranges;
        if (layout_.valid) {
            for (const display::CircuitRect& circuit : layout_.circuits) {
                if (!circuit.enabled || circuit.read_width == 0 || circuit.read_height == 0) continue;
                const BlockSpan span = DisplayCircuitSpan(circuit);
                if (!UploadStalePages(span)) return;
                for (ResidentSlot& slot : slots_) {
                    if (slot.drawn.Empty() || !SpansOverlap(SpanOfRect(slot.view, slot.drawn), span)) continue;
                    const int32_t bufferWidth =
                        static_cast<int32_t>(std::max<uint32_t>(slot.view.bufferWidth, 1u) * 64u);
                    if (slot.drawn.x1 >= bufferWidth) {
                        ReasonScope scope(writebackReason_, WritebackReason::Display);
                        WriteBack(slot);
                        if (!UploadStalePages(span)) return;
                        continue;
                    }
                    if (!SwizzleSlotIntoGlm(slot)) return;
                }
                ForEachPage(span, [&](uint32_t page) {
                    const uint32_t offset = page * 8192u;
                    for (const auto& range : ranges) {
                        if (offset >= range.first && offset < range.first + range.second) return;
                    }
                    if (!ranges.empty() && ranges.back().first + ranges.back().second == offset) {
                        ranges.back().second += 8192u;
                    } else {
                        ranges.push_back({offset, 8192u});
                    }
                });
            }
        }
        std::string error;
        capture.ticket = gpu_.RecordGlmDownload(ranges, error);
        if (!capture.ticket.valid) {
            Fail(error);
            return;
        }
        capture.valid = true;
        captureNext_ = std::move(capture);
        counters.displayCaptures++;
    }

    // The shadow memory replaced wholesale (auxiliary frame end, VRAM clear): GLM-owned content is discarded.
    void DiscardGlmOwned() {
        glmOwned_.fill(false);
        glmOwnedPages_ = 0;
    }

    void DropAll() {
        for (ResidentSlot& slot : slots_) Drop(slot);
    }

    // The slot holding `view`, or the least recently used slot of that kind (written back and dropped).
    ResidentSlot& Acquire(bool depth, const memory::BufferView& view) {
        ResidentSlot* oldest = nullptr;
        for (ResidentSlot& slot : slots_) {
            if (slot.depth != depth) continue;
            if (slot.hasIdentity && SameView(slot.view, view)) {
                slot.lastUse = ++slotUses_;
                return slot;
            }
        }
        for (ResidentSlot& slot : slots_) {
            if (slot.depth != depth || &slot == protectedSlot_) continue;  // never evict this batch's texture
            if (!oldest || (!slot.hasIdentity && oldest->hasIdentity) ||
                (slot.hasIdentity == oldest->hasIdentity && slot.lastUse < oldest->lastUse)) {
                oldest = &slot;
            }
        }
        if (oldest->hasIdentity) counters.cacheEvictions++;
        {
            ReasonScope scope(writebackReason_, WritebackReason::Eviction);
            if (GlmParkingActive()) {
                Park(*oldest);
            } else {
                WriteBack(*oldest);
            }
        }
        Drop(*oldest);
        oldest->hasIdentity = true;
        oldest->view = view;
        oldest->lastUse = ++slotUses_;
        return *oldest;
    }

    // Makes `slot` hold at least `rect` loaded from the shadow memory.
    // Fills `rect` of the slot's image from memory: from the GLM when a page under it is GLM-owned (IN-350),
    // otherwise from the shadow memory.
    void UploadIntoSlot(ResidentSlot& slot, const PixelRect& rect) {
        const BlockSpan span = SpanOfRect(slot.view, rect);
        if (GlmParkingActive() && (glmLoadsAlways_ || AnyGlmOwned(span))) {
            if (!UploadStalePages(span)) return;
            std::string error;
            if (!gpu_.LoadSlotFromGlm(slot.depth, slot.gpuIndex, slot.view.basePointer, slot.view.bufferWidth,
                                      static_cast<uint32_t>(slot.view.psm), rect.x0, rect.y0, rect.x1, rect.y1, error)) {
                Fail(error);
                return;
            }
            counters.glmSlotLoads++;
            return;
        }
        gpu::GpuChunk chunk;
        chunk.rectX0 = rect.x0;
        chunk.rectY0 = rect.y0;
        chunk.rectX1 = rect.x1;
        chunk.rectY1 = rect.y1;
        chunk.colourSlot = slot.depth ? gpu::kGpuScratchColourSlot : slot.gpuIndex;
        chunk.depthSlot = slot.depth ? slot.gpuIndex : gpu::kGpuScratchDepthSlot;
        chunk.uploadFrame = !slot.depth;
        chunk.uploadDepth = slot.depth;
        chunk.readbackFrame = false;
        chunk.readbackDepth = false;
        const size_t words = static_cast<size_t>(rect.x1 - rect.x0 + 1) * static_cast<size_t>(rect.y1 - rect.y0 + 1);
        std::vector<uint32_t>& data = slot.depth ? chunk.depthWords : chunk.frameWords;
        data.resize(words);
        memory_.Local().readRect(slot.view, static_cast<uint32_t>(rect.x0), static_cast<uint32_t>(rect.y0),
                                 static_cast<uint32_t>(rect.x1 - rect.x0 + 1), static_cast<uint32_t>(rect.y1 - rect.y0 + 1),
                                 data.data());
        std::string error;
        if (!gpu_.Execute(chunk, error)) {
            Fail(error);
            return;
        }
        counters.cacheLoadWords += words;
    }

    // Makes `slot` hold at least `rect` loaded from the shadow memory. The loaded rectangle grows to the union;
    // only the part outside the old rectangle is uploaded (IN-350: up to four strips), so the slot's own GPU-newer
    // pixels stay where they are and no write-back is needed. Every other slot that holds part of the new area is
    // written back and dropped first (two images never hold the same bytes).
    void Load(ResidentSlot& slot, const PixelRect& rect) {
        if (Contains(slot.loaded, rect)) return;
        StageTimer timer(counters.cacheLoadUs);
        const PixelRect old = slot.loaded;
        const PixelRect wanted = old.Empty() ? rect : UnionRects(old, rect);
        {
            ReasonScope scope(writebackReason_, WritebackReason::LoadOverlap);
            CoherentForWrite(SpanOfRect(slot.view, wanted), &slot, true);
        }
        if (old.Empty()) {
            UploadIntoSlot(slot, wanted);
        } else {
            // wanted minus old: the rows above and below old over the full width of wanted, then the columns
            // left and right of old inside old's rows.
            if (wanted.y0 < old.y0) UploadIntoSlot(slot, PixelRect{wanted.x0, wanted.y0, wanted.x1, old.y0 - 1});
            if (wanted.y1 > old.y1) UploadIntoSlot(slot, PixelRect{wanted.x0, old.y1 + 1, wanted.x1, wanted.y1});
            if (wanted.x0 < old.x0) UploadIntoSlot(slot, PixelRect{wanted.x0, old.y0, old.x0 - 1, old.y1});
            if (wanted.x1 > old.x1) UploadIntoSlot(slot, PixelRect{old.x1 + 1, old.y0, wanted.x1, old.y1});
        }
        slot.loaded = wanted;
        counters.cacheLoads++;
    }

    // The rectangle to load for a batch: its own rectangle, widened to the context's scissor when that is a
    // plausible buffer (inside the buffer width, at most 512 lines) and the widened area holds no other resident
    // target (IN-350: widening into a neighbour, such as GT4's depth buffer right after its colour buffer, would
    // drop that neighbour on every switch). Later batches of the same target then rarely need to grow it.
    PixelRect LoadRectFor(const Translation& t, const memory::BufferView& view, const ResidentSlot& slot) const {
        const int32_t bufferWidth = static_cast<int32_t>(std::max<uint32_t>(view.bufferWidth, 1u) * 64u);
        const PixelRect& sc = t.scissor;
        if (sc.Empty() || sc.x1 >= bufferWidth || sc.y1 - sc.y0 >= 512 || sc.y1 >= gpu::kGpuSurface) return t.rect;
        const PixelRect widened = UnionRects(t.rect, sc);
        const BlockSpan span = SpanOfRect(view, widened);
        for (const ResidentSlot& other : slots_) {
            if (&other != &slot && SpansOverlap(LoadedSpan(other), span)) return t.rect;
        }
        return widened;
    }

    // IN-346: the resident colour slot holding a texture (exact view, whole extent loaded), or -1. Only in
    // cache mode. The slot is protected from eviction for this batch.
    int32_t TextureSlotFor(const memory::BufferView& view, uint32_t width, uint32_t height) {
        if (!cacheMode_ || !textureFromTargets_) return -1;
        const PixelRect extent{0, 0, static_cast<int32_t>(width) - 1, static_cast<int32_t>(height) - 1};
        for (ResidentSlot& slot : slots_) {
            if (slot.depth || !slot.hasIdentity || !SameView(slot.view, view) || !Contains(slot.loaded, extent)) continue;
            protectedSlot_ = &slot;
            slot.lastUse = ++slotUses_;
            return static_cast<int32_t>(slot.gpuIndex);
        }
        return -1;
    }

    // ---- GLM (M10, chapter 04.9, IN-347) ------------------------------------------------------------------
    // The GPU mirror of local memory. glmPageSequence_[page] is the shadow-memory write sequence the GLM copy of
    // that page reflects (0: never uploaded). A GLM read of span R: upload every page of R (and of the drawn
    // rectangles of the targets that overlap R) whose shadow-memory content changed, then swizzle those
    // targets' drawn rectangles into the GLM; the GLM then holds what the shadow memory would hold after a
    // write-back, without one.
    bool UploadStalePages(const BlockSpan& span) {
        std::vector<std::pair<uint32_t, uint32_t>>& ranges = staleRanges_;  // reused (IN-365; ~2,000 calls per frame)
        ranges.clear();
        auto stale = [&](uint32_t page) {
            return glmPageSequence_[page] == 0 || glmPageSequence_[page] < pageWrites_[page] + 1;
        };
        auto add = [&](uint32_t page) {
            if (glmOwned_[page]) {
                // The shadow memory changed under a GLM-owned page without a download first: a coherence bug.
                Fail("GLM-owned page " + std::to_string(page) + " changed in the shadow memory");
                return;
            }
            const uint32_t offset = page * 8192u;
            if (!ranges.empty() && ranges.back().first + ranges.back().second == offset) {
                ranges.back().second += 8192u;
            } else {
                ranges.push_back({offset, 8192u});
            }
            glmPageSequence_[page] = pageWrites_[page] + 1;  // +1: 0 means "never uploaded"
            glmPageUploadClock_[page] = ++glmUploadClock_;
        };
        if (span.count >= kAllBlocks) {
            for (uint32_t page = 0; page < memory::kPagesTotal; ++page) {
                if (stale(page)) add(page);
            }
        } else if (span.count != 0) {
            const uint32_t first = span.begin / kBlocksPerPage;
            const uint32_t last = (span.begin + span.count - 1u) / kBlocksPerPage;
            for (uint32_t page = first; page <= last; ++page) {
                const uint32_t wrapped = page % memory::kPagesTotal;
                if (stale(wrapped)) add(wrapped);
            }
        }
        if (failed_) return false;
        if (ranges.empty()) return true;
        std::string error;
        if (!gpu_.UploadGlm(memory_.Local().bytes().data(), ranges, error)) {
            Fail(error);
            return false;
        }
        counters.glmPagesUploaded += ranges.size();
        return true;
    }

    // `force`: the texture must be decoded on the GPU even without GPU-newer data under it (indexed textures
    // while the GPU CLUT is authoritative, IN-349).
    bool PrepareGlm(const memory::BufferView& view, uint32_t width, uint32_t height, bool force = false) {
        if (!cacheMode_ || !glmEnabled_) return false;
        const BlockSpan span = SpanOfRect(view, 0, 0, static_cast<int32_t>(width) - 1, static_cast<int32_t>(height) - 1);
        std::vector<ResidentSlot*> sources;
        for (ResidentSlot& slot : slots_) {
            if (slot.drawn.Empty() || !SpansOverlap(SpanOfRect(slot.view, slot.drawn), span)) continue;
            // A drawn rectangle wider than its buffer wraps into the next page row: two pixels share an address
            // and a parallel swizzle has no defined last writer. Use the write-back path then.
            const int32_t bufferWidth = static_cast<int32_t>(std::max<uint32_t>(slot.view.bufferWidth, 1u) * 64u);
            if (slot.drawn.x1 >= bufferWidth) {
                // Write it back instead (the shadow memory then holds it; the GLM upload below picks it up).
                ReasonScope scope(writebackReason_, WritebackReason::Texture);
                WriteBack(slot);
                if (!force) return false;
                continue;
            }
            sources.push_back(&slot);
        }
        if (sources.empty() && !force && !AnyGlmOwned(span)) {
            return false;  // nothing GPU-newer: the shadow memory decode is exact
        }
        if (!UploadStalePages(span)) return false;
        for (ResidentSlot* slot : sources) {
            if (!SwizzleSlotIntoGlm(*slot)) return false;
        }
        return true;
    }

    // IN-348 candidates: small untextured sprite batches whose memory no resident target holds (IN-350: a small
    // sprite on a resident target, such as a HUD box on the frame buffer, stays on the GPU; drawing it on the CPU
    // would write back and reload the whole target).
    bool SmallUntexturedSprites(const Batch& batch, const Translation& t) const {
        if (t.hasTexture || t.rect.Empty()) return false;
        const int64_t area = static_cast<int64_t>(t.rect.x1 - t.rect.x0 + 1) * (t.rect.y1 - t.rect.y0 + 1);
        if (area > 64 * 64) return false;
        for (const PrimRecord& record : batch.prims) {
            if (record.type != PrimType::Sprite) return false;
        }
        const BlockSpan frameSpan = SpanOfRect(t.frameView, t.rect);
        const BlockSpan depthSpan = SpanOfRect(t.zView, t.rect);
        if (AnyGlmOwned(frameSpan) || AnyGlmOwned(depthSpan)) return false;  // would need a download (IN-350)
        for (const ResidentSlot& slot : slots_) {
            const BlockSpan loaded = LoadedSpan(slot);
            if (SpansOverlap(loaded, frameSpan) || SpansOverlap(loaded, depthSpan)) return false;
        }
        return true;
    }

    // ---- GPU CLUT (chapter 07.7, IN-349) --------------------------------------------------------------
    // In cache mode with the GLM, every performed CLUT load is repeated on the GPU from the GLM, the GPU CLUT is
    // authoritative and indexed textures are expanded from it on the GPU; the CPU CLUT unit still decides when
    // a load happens. So a palette the game drew on the GPU needs no write-back.
    bool GpuClutActive() const { return cacheMode_ && glmEnabled_ && gpuClutEnabled_; }

    void OnClutLoaded(const clut::Tex0Write& write) {
        if (!GpuClutActive() || failed_) return;
        StageTimer timer(counters.clutUs);
        if (!gpuClutSynced_) SyncGpuClut();
        const BlockSpan span = write.csm == 0 ? BlockSpan{write.cbp & ~31u, 64u} : BlockSpan{0, kAllBlocks};
        // Make the GLM current for the palette (stale pages, GPU-newer targets), as PrepareGlm does for textures.
        if (!UploadStalePages(span)) return;
        for (ResidentSlot& slot : slots_) {
            if (slot.drawn.Empty() || !SpansOverlap(SpanOfRect(slot.view, slot.drawn), span)) continue;
            const int32_t bufferWidth = static_cast<int32_t>(std::max<uint32_t>(slot.view.bufferWidth, 1u) * 64u);
            if (slot.drawn.x1 >= bufferWidth) {
                ReasonScope scope(writebackReason_, WritebackReason::Clut);
                WriteBack(slot);
                if (!UploadStalePages(SpanOfRect(slot.view, PixelRect{0, 0, bufferWidth - 1, 2047}))) return;
                continue;
            }
            if (!SwizzleSlotIntoGlm(slot)) return;
        }
        std::string error;
        if (!gpu_.LoadClutFromGlm(write.cbp, write.cpsm, write.csm, write.csa, write.psm, write.texclutCbw,
                                  write.texclutCou, write.texclutCov, error)) {
            Fail(error);
            return;
        }
        counters.gpuClutLoads++;
    }

    // The GPU CLUT starts as a copy of the CPU CLUT buffer (start-up, GS reset).
    void SyncGpuClut() {
        std::string error;
        if (!gpu_.UploadClut(frontEnd_.Clut().Halfwords(), error)) {
            Fail(error);
            return;
        }
        gpuClutSynced_ = true;
    }

    // One translated batch through the resident targets. Returns false (nothing done) when the batch must
    // take the write-back path instead.
    bool ExecuteResident(const Translation& t, std::string& error) {
        const bool depthUsed = DepthUsed(t.state);
        const BlockSpan frameSpan = SpanOfRect(t.frameView, t.rect);
        const BlockSpan depthSpan = depthUsed ? SpanOfRect(t.zView, t.rect) : BlockSpan{};
        if (depthUsed && SpansOverlap(frameSpan, depthSpan)) return false;  // colour and depth share memory
        ResidentSlot& colour = Acquire(false, t.frameView);
        ResidentSlot* depth = depthUsed ? &Acquire(true, t.zView) : nullptr;
        Load(colour, LoadRectFor(t, t.frameView, colour));
        if (depth) Load(*depth, LoadRectFor(t, t.zView, *depth));
        if (failed_) return true;
        // The colour slot's load may have dropped the depth slot (or the reverse) when they overlap; then the
        // batch takes the write-back path with both stored first.
        if (depth && (!colour.hasIdentity || !depth->hasIdentity || colour.loaded.Empty() || depth->loaded.Empty() ||
                      SpansOverlap(LoadedSpan(colour), LoadedSpan(*depth)))) {
            ReasonScope scope(writebackReason_, WritebackReason::Fallback);
            WriteBack(colour);
            Drop(colour);
            WriteBack(*depth);
            Drop(*depth);
            return false;
        }
        if (!Contains(colour.loaded, t.rect) || (depth && !Contains(depth->loaded, t.rect))) {
            error = "resident target does not cover the batch rectangle";
            return true;
        }
        size_t begin = 0;
        while (begin < t.draws.size()) {
            size_t end = begin;
            uint32_t vertexCount = 0;
            while (end < t.draws.size() && vertexCount + t.draws[end].count <= gpu::kGpuMaxVertices) {
                vertexCount += t.draws[end].count;
                ++end;
            }
            if (end == begin) {
                error = "a primitive exceeds the vertex buffer";
                return true;
            }
            const uint32_t baseVertex = t.draws[begin].first;
            gpu::GpuChunk chunk;
            chunk.rectX0 = t.rect.x0;
            chunk.rectY0 = t.rect.y0;
            chunk.rectX1 = t.rect.x1;
            chunk.rectY1 = t.rect.y1;
            chunk.colourSlot = colour.gpuIndex;
            chunk.depthSlot = depth ? depth->gpuIndex : gpu::kGpuScratchDepthSlot;
            chunk.uploadFrame = chunk.uploadDepth = chunk.readbackFrame = chunk.readbackDepth = false;
            chunk.state = t.state;
            chunk.hasTexture = t.hasTexture;
            chunk.textureSlot = t.textureSlot;
            chunk.glmTexture = t.glmTexture;
            chunk.glmBp = t.glmBp;
            chunk.glmBw = t.glmBw;
            chunk.glmPsm = t.glmPsm;
            chunk.glmIndexed = t.glmIndexed;
            chunk.glmGpuClut = t.glmGpuClut;
            chunk.glmCpsm = t.glmCpsm;
            chunk.glmCsa = t.glmCsa;
            chunk.glmTa0 = t.glmTa0;
            chunk.glmTa1 = t.glmTa1;
            chunk.glmAem = t.glmAem;
            chunk.palette = t.palette;
            chunk.texWidth = t.texWidth;
            chunk.texHeight = t.texHeight;
            chunk.texelSpan = t.texels.data();  // IN-365: borrowed, no copy per chunk
            chunk.texelSpanCount = t.texels.size();
            chunk.vertexSpan = t.vertices.data() + baseVertex;
            chunk.vertexSpanCount = vertexCount;
            chunk.draws.reserve(end - begin);
            for (size_t i = begin; i < end; ++i) {
                gpu::GpuDraw draw = t.draws[i];
                draw.first -= baseVertex;
                chunk.draws.push_back(draw);
            }
            if (!gpu_.Execute(chunk, error)) return true;
            counters.gpuChunks++;
            counters.gpuDraws += chunk.draws.size();
            begin = end;
        }
        colour.drawn = colour.drawn.Empty() ? t.rect : UnionRects(colour.drawn, t.rect);
        colour.drawSequence++;
        if (depth) depth->drawSequence++;
        if (depth) depth->drawn = depth->drawn.Empty() ? t.rect : UnionRects(depth->drawn, t.rect);
        counters.cacheResidentBatches++;
        return true;
    }

    void OnBatch(Batch&& batch) override {
        StageTimer batchTimer(counters.batchUs);
        counters.batches++;
        if (!gpu_.Ready()) {
            Fail("GPU drawer is not initialised");
            return;
        }
        Translation t;
        // IN-360: reuse the previous batch's vector storage (cleared, capacity kept).
        t.vertices.swap(spareVertices_);
        t.draws.swap(spareDraws_);
        t.texels.swap(spareTexels_);
        t.vertices.clear();
        t.draws.clear();
        t.texels.clear();
        struct ReturnStorage {
            Translation& t;
            Impl& s;
            ~ReturnStorage() {
                s.spareVertices_.swap(t.vertices);
                s.spareDraws_.swap(t.draws);
                s.spareTexels_.swap(t.texels);
            }
        } returnStorage{t, *this};
        {
            StageTimer timer(frameTimings_.translateUs);
            Translate(batch, memory_.Local(), frontEnd_.Clut(), t, counters,
                      [this](const memory::BufferView& view, uint32_t width, uint32_t height) {
                          {
                              ReasonScope scope(writebackReason_, WritebackReason::Texture);
                              CoherentForRead(SpanOfRect(view, 0, 0, static_cast<int32_t>(width) - 1,
                                                     static_cast<int32_t>(height) - 1));
                          }
                      },
                      this,
                      [this](const memory::BufferView& view, uint32_t width, uint32_t height) -> int32_t {
                          return TextureSlotFor(view, width, height);
                      },
                      [this](const memory::BufferView& view, uint32_t width, uint32_t height, bool indexed,
                             bool& gpuClut) {
                          gpuClut = GpuClutActive();
                          return PrepareGlm(view, width, height, indexed && gpuClut);
                      });
        }
        if (observer_) observer_(batch, t);
        struct ClearProtection {
            const ResidentSlot*& slot;
            ~ClearProtection() { slot = nullptr; }
        } clearProtection{protectedSlot_};
        if (t.skippedTexture) {
            counters.primitivesSkippedTexture += batch.prims.size();
            counters.batchesSkippedTexture++;
            return;
        }
        if (t.blended) counters.batchesBlended++;
        if (t.draws.empty()) {
            counters.batchesEmpty++;
            return;
        }
        std::string error;
        {
            StageTimer timer(frameTimings_.executeUs);
            bool done = false;
            if (cacheMode_ && cpuSmallSprites_ && SmallUntexturedSprites(batch, t)) {
                // IN-348: small flat sprite batches (GT4 draws its palettes this way, chapter 07.8) are drawn
                // by the CPU reference straight into the shadow memory, which is exact for untextured sprites
                // (the GPU path draws the same words). A CLUT load from them then needs no GPU write-back.
                ReasonScope scope(writebackReason_, WritebackReason::CpuSprite);
                CoherentForWrite(SpanOfRect(t.frameView, t.rect));
                CoherentForWrite(SpanOfRect(t.zView, t.rect));
                cpuref::DrawStats stats;
                cpuref::DrawBatch(batch, memory_.Local(), frontEnd_.Clut(), stats);
                MarkWritten(SpanOfRect(t.frameView, t.rect));
                MarkWritten(SpanOfRect(t.zView, t.rect));
                counters.cpuSpriteBatches++;
                done = true;
            }
            if (!done && cacheMode_) {
                done = ExecuteResident(t, error);
                if (done && !error.empty()) Fail(error);
            }
            if (!done) {
                if (cacheMode_) {
                    // Write-back path for this batch (scratch GPU slots): no resident target may hold its memory.
                    ReasonScope scope(writebackReason_, WritebackReason::Fallback);
                    CoherentForWrite(SpanOfRect(t.frameView, t.rect));
                    CoherentForWrite(SpanOfRect(t.zView, t.rect));
                    counters.cacheFallbackBatches++;
                }
                if (!ExecuteBatch(gpu_, t, memory_.Local(), counters, error)) Fail(error);
                MarkWritten(SpanOfRect(t.frameView, t.rect));
                MarkWritten(SpanOfRect(t.zView, t.rect));
            }
        }
    }

    bool OnSync(SyncKind, uint32_t, uint32_t) override { return false; }

    void Fail(const std::string& message) {
        failed_ = true;
        if (lastError_.empty()) lastError_ = message;
    }

    cpuref::LocalMemorySink memory_;
    FrontEnd frontEnd_;
    gpu::GpuDrawer gpu_;
    Counters counters;
    PassTimings timings_;        // last completed frame
    PassTimings frameTimings_;   // being accumulated
    PassTimings timingTotals_;   // session sums
    std::uint64_t timedFrames_ = 0;
    std::vector<std::uint8_t> auxSnapshot_;  // guest SLM at BeginAuxiliaryFrame()
    bool auxiliary_ = false;
    display::DisplayRegisters displayRegs_;
    std::array<uint32_t, 2> previousDby_{};
    std::array<uint8_t, 3> background_{};
    std::optional<display::Image> lastImage_;      // last reference image (KeepPrevious, 13.3)
    std::optional<display::Image> lastPresented_;  // last presentation image (KeepPrevious, 13.3)
    display::Deinterlacer deinterlacer_;           // 13.4 history (presentation only)
    OutputSettings settings_;
    bool oddField_ = false;                        // field of the VBlank being closed (12.4)
    std::uint64_t vblanks_ = 0;                    // Flush() count: one per VBlank
    std::uint64_t layoutVblank_ = 0;               // VBlank whose layout is cached in layout_
    bool layoutValid_ = false;
    display::DisplayLayout layout_;

    // ---- Asynchronous presentation source (IN-355) ----
    // With a window, each Flush records a download of the displayed memory (made current in the GLM) into the
    // frame's own submission; the presentation compose of the next VBlank composes that capture once the GPU has
    // finished the frame (one frame of presentation latency), so the CPU never waits for the frame it just
    // submitted. Reference composes (dumps) stay synchronous and exact for their own VBlank.
    struct DisplayCapture {
        bool valid = false;
        display::DisplayLayout layout;
        display::DisplayRegisters regs;
        std::array<uint8_t, 3> background{};
        bool oddField = false;
        gpu::GpuDrawer::DownloadTicket ticket;
        bool completed = false;
    };
    bool asyncPresentation_ = false;
    bool gpuCompose_ = true;  // IN-364 (off for A/B runs)

    // IN-364: the GPU compose applies to this VBlank's layout (progressive, a composite plan, GLM mode).
    bool GpuComposeApplies(const display::DisplayLayout& layout, display::MergePlan& plan,
                           std::array<bool, 2>& available) const {
        if (!gpuCompose_ || !cacheMode_ || !glmEnabled_ || auxiliary_ || !layout.valid || layout.really_interlaced) {
            return false;
        }
        for (std::size_t i = 0; i < 2; ++i) {
            const display::CircuitRect& circuit = layout.circuits[i];
            available[i] = circuit.enabled && circuit.read_width != 0 && circuit.read_height != 0;
        }
        const display::PmodeFields pmode = display::decode_pmode(displayRegs_.pmode);
        plan = display::plan_merge(pmode, layout.circuits[0], layout.circuits[1], available[0], available[1]);
        return plan.outcome == display::MergeOutcome::Composite;
    }
    DisplayCapture captureNext_;   // recorded at the last Flush
    DisplayCapture captureShown_;  // recorded at the Flush before; composed by this VBlank's presentation
    memory::LocalMemory displayMemory_;
    bool failed_ = false;
    std::string lastError_;
    Renderer::BatchObserver observer_;
    bool cacheMode_ = false;       // GT4_HW_SYNC_MODE: false = write-back reference mode
    // Decoded-texture cache (IN-341). On in both sync modes: it only skips re-decoding unchanged memory.
    static constexpr size_t kTextureCacheEntries = 256;
    bool textureCacheEnabled_ = true;
    std::array<uint64_t, memory::kPagesTotal> pageWrites_{};
    uint64_t writeSequence_ = 0;
    uint64_t textureUses_ = 0;
    std::unordered_map<uint64_t, CachedTexture> textures_;
    std::vector<ResidentSlot> slots_;  // resident colour and depth targets (IN-344)
    WritebackReason writebackReason_ = WritebackReason::Other;  // why the next write-backs happen (counters)
    const ResidentSlot* protectedSlot_ = nullptr;  // texture source of the current batch (IN-346)
    bool textureFromTargets_ = true;               // IN-346 (off for A/B runs)
    bool cpuSmallSprites_ = true;                  // IN-348 (off for A/B runs)
    bool glmEnabled_ = true;                       // IN-347 GT4_HW_GLM (off for A/B runs: write-back path)
    bool gpuClutEnabled_ = true;                   // IN-349 (off for A/B runs)
    bool glmParking_ = true;                       // IN-350 (off for A/B runs: evictions and overlaps write back)
    bool glmLoadsAlways_ = false;                  // IN-350 fixtures: every slot load goes through the GLM
    BlockSpan pendingReadSpan_{0, kAllBlocks};    // source of the last LOCAL->HOST transfer (10.2)
    bool glmBatchedWritebacks_ = true;
    std::vector<std::pair<uint32_t, uint32_t>> staleRanges_;
    std::vector<Qword> gifScratch_;              // IN-360: unaligned GIF packets copied here (reused)
    std::vector<gpu::GpuVertex> spareVertices_;  // IN-360: storage reused by the next batch's translation
    std::vector<gpu::GpuDraw> spareDraws_;
    std::vector<uint32_t> spareTexels_;             // IN-357 (off for A/B runs: one readback per slot)
    bool gpuUploads_ = true;                       // IN-351 (off for A/B runs: uploads written on the CPU)
    std::array<bool, memory::kPagesTotal> glmOwned_{};  // newest content only in the GLM (IN-350)
    uint32_t glmOwnedPages_ = 0;
    std::array<uint64_t, memory::kPagesTotal> glmPageUploadClock_{};  // glmUploadClock_ at the page's last upload
    uint64_t glmUploadClock_ = 0;
    bool gpuClutSynced_ = false;                   // the GPU CLUT was initialised from the CPU CLUT
    std::array<uint64_t, memory::kPagesTotal> glmPageSequence_{};
    uint64_t slotUses_ = 0;
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}

Renderer::~Renderer() = default;

bool Renderer::Init(const gpu::VulkanHandles& handles, std::string& error) {
    if (!impl_->gpu_.Init(handles, error)) return false;
    impl_->UpdateClutCopies();
    return true;
}

bool Renderer::GifTransfer(std::uint32_t path, const std::uint8_t* bytes, std::size_t byteCount) {
    if (path == 0 || path > 3) return false;  // IN-312
    if (byteCount % 16 != 0) return false;
    StageTimer timer(impl_->counters.gifUs);
    // IN-360: on a little-endian host the stream bytes are Qwords already; they are used in place when aligned,
    // otherwise copied once into a reused buffer (no allocation per packet).
    const std::size_t count = byteCount / 16;
    if constexpr (std::endian::native == std::endian::little && sizeof(Qword) == 16) {
        if (reinterpret_cast<std::uintptr_t>(bytes) % alignof(Qword) == 0) {
            impl_->frontEnd_.GifTransfer(path, reinterpret_cast<const Qword*>(bytes), count);
            return true;
        }
        impl_->gifScratch_.resize(count);
        std::memcpy(impl_->gifScratch_.data(), bytes, byteCount);
    } else {
        impl_->gifScratch_.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            std::memcpy(&impl_->gifScratch_[i].lo, bytes + 16 * i, 8);  // as before IN-360
            std::memcpy(&impl_->gifScratch_[i].hi, bytes + 16 * i + 8, 8);
        }
    }
    impl_->frontEnd_.GifTransfer(path, impl_->gifScratch_.data(), count);
    return true;
}

bool Renderer::GifBusy(std::uint32_t path) const { return impl_->frontEnd_.GifBusy(path); }

void Renderer::GifReset(std::uint32_t pathMask) {
    impl_->frontEnd_.GifReset(pathMask);
    impl_->memory_.ResetRead();  // a GIF reset cancels the pending LOCAL->HOST read (10.2)
}

bool Renderer::WritePrivileged(std::uint32_t address, std::uint64_t value) {
    using namespace priv;
    auto& regs = impl_->displayRegs_;
    switch (address) {
        case kPmode: regs.pmode = value; return true;
        case kSmode1: regs.smode1 = value; return true;
        case kSmode2: regs.smode2 = value; return true;
        case kSyncv: regs.syncv = value; return true;
        case kDispfb1: regs.circuits[0].dispfb = value; return true;
        case kDisplay1: regs.circuits[0].display = value; return true;
        case kDispfb2: regs.circuits[1].dispfb = value; return true;
        case kDisplay2: regs.circuits[1].display = value; return true;
        case kBgcolor:
            impl_->background_[0] = static_cast<uint8_t>(Field(value, 0, 8));
            impl_->background_[1] = static_cast<uint8_t>(Field(value, 8, 8));
            impl_->background_[2] = static_cast<uint8_t>(Field(value, 16, 8));
            return true;
        case kCsr:
            // CSR.RESET (bit 9) resets the GS as in the CPU reference; CSR.FIELD is read-only, the
            // VSYNC record supplies the display field.
            if (Field(value, 9, 1) != 0) ResetGs();
            return true;
        default:
            return false;
    }
}

void Renderer::ResetGs() {
    Impl& s = *impl_;
    s.WriteBackAll();  // a GS reset keeps local memory, so resident draws are stored first
    s.DropAll();
    s.frontEnd_.ResetGs();
    s.gpuClutSynced_ = false;  // the GPU CLUT is re-initialised from the CPU CLUT on its next use (IN-349)
    s.frontEnd_.GifReset(7u);
    s.memory_.ResetRead();
    s.displayRegs_ = {};
    s.displayRegs_.anti_blur = s.settings_.antiBlur;
    s.previousDby_ = {};
    s.background_ = {};
    s.lastImage_.reset();
    s.lastPresented_.reset();
    s.deinterlacer_.clear_history();
    s.layoutValid_ = false;
}

bool Renderer::BeginAuxiliaryFrame() {
    Impl& s = *impl_;
    if (s.auxiliary_) return false;
    s.WriteBackAll();  // the snapshot is the guest's memory including resident draws
    const auto bytes = s.memory_.Local().bytes();
    s.auxSnapshot_.assign(bytes.begin(), bytes.end());
    s.auxiliary_ = true;
    return true;
}

void Renderer::EndAuxiliaryFrame() {
    Impl& s = *impl_;
    if (!s.auxiliary_) return;
    s.DropAll();  // resident draws of the auxiliary frame are discarded with it
    s.DiscardGlmOwned();
    const auto bytes = s.memory_.Local().bytes();
    std::copy(s.auxSnapshot_.begin(), s.auxSnapshot_.end(), bytes.begin());
    s.MarkWritten(BlockSpan{0, kAllBlocks});
    s.auxSnapshot_.clear();
    s.auxSnapshot_.shrink_to_fit();
    s.memory_.ResetRead();  // no partial LOCAL->HOST state survives the auxiliary frame
    s.auxiliary_ = false;
    s.counters.auxiliaryFrames++;
}

bool Renderer::AuxiliaryActive() const { return impl_->auxiliary_; }

const PassTimings& Renderer::Timings() const { return impl_->timings_; }

const PassTimings& Renderer::TimingTotals() const { return impl_->timingTotals_; }

std::uint64_t Renderer::TimedFrames() const { return impl_->timedFrames_; }

void Renderer::ClearVram() {
    impl_->DropAll();
    impl_->DiscardGlmOwned();
    auto bytes = impl_->memory_.Local().bytes();
    std::fill(bytes.begin(), bytes.end(), std::uint8_t{0});
    impl_->MarkWritten(BlockSpan{0, kAllBlocks});
}

std::size_t Renderer::ReadFifo(void* destination, std::size_t byteCount) {
    Impl& s = *impl_;
    if (s.auxiliary_) {
        // A guest readback is never answered from an auxiliary frame (roadmap requirement 2).
        s.counters.auxiliaryReadRefused++;
        return 0;
    }
    if (byteCount == 0) return 0;
    // The first FIFO access observes every earlier draw and upload (10.2).
    s.frontEnd_.FlushBatch(FlushReason::Transfer);
    if (s.memory_.LocalToHostPending()) {
        // IN-357: the first FIFO access takes the transfer's data from the shadow memory; only its source
        // rectangle must be current there (draws since TRXDIR included). It used to write back every resident
        // target on every FIFO read (GT4's lens flare: about 7 per frame, each a submission and a wait).
        ReasonScope scope(s.writebackReason_, WritebackReason::Transfer);
        s.CoherentForRead(s.pendingReadSpan_);
    }
    bool overrun = false;
    const std::vector<uint8_t> data = s.memory_.ReadLocalToHost(byteCount, overrun);
    if (overrun) s.counters.fifoOverruns++;
    if (s.memory_.ReadDrained()) s.frontEnd_.WriteRegister(reg::kTrxdir, 3);
    std::memcpy(destination, data.data(), data.size());
    return data.size();
}

void Renderer::Flush() {
    Impl& s = *impl_;
    s.frontEnd_.VBlank(&kEverything, 1);
    s.frontEnd_.FlushBatch(FlushReason::VBlank);
    // Frame end: publish the CPU cost of the passes that just ran (roadmap requirement 4), so a run
    // without a window still reports translate and execute.
    s.timings_ = s.frameTimings_;
    s.timingTotals_.translateUs += s.frameTimings_.translateUs;
    s.timingTotals_.executeUs += s.frameTimings_.executeUs;
    s.frameTimings_ = PassTimings{};
    ++s.timedFrames_;
    ++s.vblanks_;  // the next compose computes this VBlank's display layout (13.2)
    if (s.displayRegs_.smode2 & 1u) {  // SMODE2.INT: which video mode the guest ran this VBlank
        ++s.counters.vblanksInterlaced;
    } else {
        ++s.counters.vblanksProgressive;
    }
    if (s.asyncPresentation_ && s.cacheMode_ && s.glmEnabled_ && s.gpu_.Ready() && !s.failed_ && !s.auxiliary_) {
        s.CaptureDisplay();  // IN-355: into this frame's submission, composed at the next VBlank
    }
    // Submit the frame's recorded chunks without waiting (IN-342): the GPU draws while the CPU composes and
    // starts the next frame; the next recording waits for it before reusing the rings.
    std::string error;
    if (s.gpu_.Ready() && !s.gpu_.Flush(false, error)) s.Fail(error);
}

FrameOutput Renderer::Compose(ComposeKind kind) {
    FrameOutput out;
    ComposeFrame(out, kind);
    return out;
}

void Renderer::SetDisplayField(bool odd) { impl_->oddField_ = odd; }

bool Renderer::DisplayField() const { return impl_->oddField_; }

void Renderer::SetOutputSettings(const OutputSettings& settings) {
    impl_->settings_ = settings;
    impl_->displayRegs_.anti_blur = settings.antiBlur;
}

const OutputSettings& Renderer::GetOutputSettings() const { return impl_->settings_; }

namespace {

// Row field of 13.3/13.4 (0 = the field on even output rows, unshifted) for a display field as the VSYNC
// record carries it (CSR.FIELD, 1 = odd). IN-339 (Confirmed on GT4 menus by measurement, against the IN-35
// reading): CSR.FIELD = 1 is the upper (even-row) field, CSR.FIELD = 0 the lower one.
int RowFieldOfDisplayField(bool csrFieldOdd) { return csrFieldOdd ? 0 : 1; }

void CopyImage(const display::Image& image, FrameOutput& out) {
    out.valid = image.width > 0 && image.height > 0;
    out.width = image.width;
    out.height = image.height;
    out.pixels.resize(image.pixels.size());
    uint32_t* dst = out.pixels.data();
    for (const display::Rgba8& p : image.pixels) {
        *dst++ = static_cast<uint32_t>(p.r) | (static_cast<uint32_t>(p.g) << 8) | (static_cast<uint32_t>(p.b) << 16) |
                 (static_cast<uint32_t>(p.a) << 24);
    }
}

// One circuit's source rectangle read from the SLM (13.2, 13.3). CT24 alpha is 0x80 (IN-313).
display::Image ReadCircuit(const memory::LocalMemory& local, const display::CircuitRect& circuit) {
    display::Image image(circuit.read_width, circuit.read_height);
    const auto psm = memory::decodePsm(circuit.psm);
    if (!psm) return image;
    const memory::BufferView view{memory::blockOfPage(circuit.fbp), circuit.fbw, *psm};
    const pixel::FrameFormat format = [&] {
        switch (circuit.psm) {
            case 0x01: return pixel::FrameFormat::Ct24;
            case 0x02:
            case 0x0A: return pixel::FrameFormat::Ct16;
            default: return pixel::FrameFormat::Ct32;
        }
    }();
    // Stored words of the read rectangle; readRect when it does not wrap at 2048 (IN-24), else per pixel.
    std::vector<uint32_t> words(static_cast<size_t>(circuit.read_width) * circuit.read_height);
    const bool wraps = circuit.dbx < 0 || circuit.dby < 0 ||
                       static_cast<uint32_t>(circuit.dbx) + circuit.read_width > 2048u ||
                       static_cast<uint32_t>(circuit.dby) + circuit.read_height > 2048u;
    if (!wraps) {
        local.readRect(view, static_cast<uint32_t>(circuit.dbx), static_cast<uint32_t>(circuit.dby), circuit.read_width,
                       circuit.read_height, words.data());
    }
    display::Rgba8* pixels = image.pixels.data();
    for (uint32_t y = 0; y < circuit.read_height; ++y) {
        for (uint32_t x = 0; x < circuit.read_width; ++x) {
            const uint32_t sx = static_cast<uint32_t>(circuit.dbx + static_cast<int32_t>(x)) & 2047u;
            const uint32_t sy = static_cast<uint32_t>(circuit.dby + static_cast<int32_t>(y)) & 2047u;
            const size_t index = static_cast<size_t>(y) * circuit.read_width + x;
            const uint32_t stored = wraps ? local.readPixel(view, sx, sy) : words[index];
            const pixel::Colour colour = pixel::frame_word_to_colour(pixel::unpack_frame_word(format, stored));
            display::Rgba8& dst = pixels[index];
            dst.r = static_cast<uint8_t>(colour.r);
            dst.g = static_cast<uint8_t>(colour.g);
            dst.b = static_cast<uint8_t>(colour.b);
            dst.a = format == pixel::FrameFormat::Ct24 ? 0x80 : static_cast<uint8_t>(colour.a);
        }
    }
    return image;
}

}  // namespace

void Renderer::ComposeFrame(FrameOutput& out, ComposeKind kind) {
    Impl& s = *impl_;
    out.valid = false;
    out.width = 0;
    out.height = 0;
    double composeUs = 0.0;
    {
        StageTimer timer(composeUs);
        // 13.2 once per VBlank: compute_layout advances the DBY history, so a second compose of the
        // same VBlank (a dump next to a present) must reuse the layout instead of recomputing it.
        s.EnsureLayout();
        const bool presentation = kind == ComposeKind::Presentation;
        // IN-355: a presentation compose with asynchronous presentation composes the previous VBlank's capture
        // (its download is complete once the GPU finished that frame; nothing waits for the frame just submitted).
        Impl::DisplayCapture& capture = s.captureShown_;
        bool fromCapture = presentation && s.asyncPresentation_ && capture.valid;
        if (fromCapture && !capture.completed) {
            std::string error;
            if (!s.gpu_.CompleteGlmDownload(capture.ticket, s.displayMemory_.bytes().data(), error)) {
                s.Fail(error);
                fromCapture = false;
            } else {
                capture.completed = true;
            }
        }
        if (presentation && s.asyncPresentation_ && !fromCapture && s.captureNext_.valid) {
            // The first capture is recorded but not composable yet (the first VBlank of asynchronous
            // presentation): nothing to show this VBlank. The deinterlacer and the presentation history are not
            // touched, so the composed frames are the synchronous sequence one VBlank later. Without any capture
            // (presents without a Flush between them) the compose stays synchronous.
            s.timings_.composeUs = 0.0;
            return;
        }
        const display::DisplayLayout& layout = fromCapture ? capture.layout : s.layout_;
        const display::DisplayRegisters& regs = fromCapture ? capture.regs : s.displayRegs_;
        const std::array<uint8_t, 3>& background = fromCapture ? capture.background : s.background_;
        const bool displayField = fromCapture ? capture.oddField : s.oddField_;
        const memory::LocalMemory& source = fromCapture ? s.displayMemory_ : s.memory_.Local();
        if (fromCapture) s.counters.presentationsFromCapture++;
        const auto blank = [&] {
            if (presentation) {
                s.counters.blankFrames++;
                s.deinterlacer_.clear_history();  // 13.4: no source, no stale history next time
            }
        };
        const auto body = [&] {
            if (!layout.valid) {
                blank();
                return;
            }
            const display::PmodeFields pmode = display::decode_pmode(regs.pmode);
            std::array<display::Image, 2> sources{};
            std::array<bool, 2> available{false, false};
            for (std::size_t i = 0; i < 2; ++i) {
                const display::CircuitRect& circuit = layout.circuits[i];
                if (!circuit.enabled || circuit.read_width == 0 || circuit.read_height == 0) continue;
                if (!fromCapture) {
                    // The display reads this circuit's rectangle from the shadow memory: GPU-newer targets there
                    // are written back first (only those; a wrapping read rectangle reports all memory).
                    ReasonScope scope(s.writebackReason_, WritebackReason::Display);
                    s.CoherentForRead(Impl::DisplayCircuitSpan(circuit));
                }
                sources[i] = ReadCircuit(source, circuit);
                available[i] = true;
            }
            const display::MergePlan plan =
                display::plan_merge(pmode, layout.circuits[0], layout.circuits[1], available[0], available[1]);
            if (plan.outcome == display::MergeOutcome::KeepPrevious) {
                // 13.3: a circuit is enabled without a source; nothing new this VBlank, previous image kept.
                const std::optional<display::Image>& kept = presentation ? s.lastPresented_ : s.lastImage_;
                if (kept) CopyImage(*kept, out);
                blank();
                return;
            }
            // Effective deinterlace (13.4). The reference image never deinterlaces (it is the donor decode).
            display::DeinterlaceMode effective = display::DeinterlaceMode::Off;
            if (presentation) {
                effective = s.settings_.deinterlace == display::DeinterlaceMode::Auto
                                ? display::resolve_auto_deinterlace(layout, layout.ffmd, regs.scanmsk_active)
                                : s.settings_.deinterlace;
            }
            // IN-339: the display field of a frame (VSYNC field = CSR.FIELD, 1 = odd) maps to the LOWER row
            // phase when it is 0 and to the upper one when it is 1: GT4 draws its CSR.FIELD = 1 frames half a
            // line higher, so with the IN-35 reading (field 1 shifted down) every woven menu line landed two
            // rows from its partner. `_bff` still inverts the result.
            const int vblankField = RowFieldOfDisplayField(displayField);
            const int fieldPhase = vblankField ^ (s.settings_.bottomFieldFirst ? 1 : 0);
            const int32_t offsetRows =
                presentation ? display::interlace_offset_rows(layout.really_interlaced, layout.ffmd, effective, 1,
                                                              fieldPhase)
                             : 0;
            const display::OutputSize size =
                display::merge_output_size(layout, effective == display::DeinterlaceMode::Off, 1);
            const display::SourcePlacement rc1{available[0] ? &sources[0] : nullptr, layout.circuits[0].screen_x,
                                               layout.circuits[0].screen_y,
                                               static_cast<int32_t>(layout.circuits[0].display_width),
                                               static_cast<int32_t>(layout.circuits[0].display_height)};
            const display::SourcePlacement rc2{available[1] ? &sources[1] : nullptr, layout.circuits[1].screen_x,
                                               layout.circuits[1].screen_y,
                                               static_cast<int32_t>(layout.circuits[1].display_width),
                                               static_cast<int32_t>(layout.circuits[1].display_height)};
            display::Image merged = display::compose_merge(plan, pmode, background[0], background[1],
                                                           background[2], size.width, size.height, 1, offsetRows,
                                                           rc1, rc2);
            if (presentation) {
                if (plan.use_rc1 && plan.use_rc2 && !pmode.mmod) s.counters.mergeAlphaFromTexel++;  // V-01-12
                if (offsetRows != 0) s.counters.interlaceOffsetFrames++;
                const bool anySource = available[0] || available[1];
                const bool runs = anySource && layout.really_interlaced && effective != display::DeinterlaceMode::Off;
                if (runs) {
                    merged = s.deinterlacer_.process(merged, vblankField, effective,
                                                     s.settings_.bottomFieldFirst, 1);
                    s.counters.deinterlacedFrames++;
                } else if (!anySource) {
                    s.deinterlacer_.clear_history();
                }
                s.counters.presentationFrames++;
            }
            CopyImage(merged, out);
            if (!s.auxiliary_) {
                // An auxiliary frame never replaces the guest's display history.
                if (presentation) {
                    s.lastPresented_ = std::move(merged);
                } else {
                    s.lastImage_ = std::move(merged);
                }
            }
        };
        body();
    }
    s.timings_.composeUs = composeUs;
    s.counters.composeUs += composeUs;
    s.timings_.executeGpuUs = 0.0;  // reserved for GPU timestamps (not measured yet)
}

void Renderer::SetBatchObserver(BatchObserver observer) { impl_->observer_ = std::move(observer); }

const std::uint8_t* Renderer::Slm() const {
    impl_->WriteBackAll();  // a caller reading the shadow memory sees every draw (M7 coherence)
    return impl_->memory_.Local().bytes().data();
}

void Renderer::SetSyncMode(bool cache) {
    if (!cache) {
        impl_->WriteBackAll();
        impl_->DropAll();
    }
    impl_->cacheMode_ = cache;
    impl_->UpdateClutCopies();
}

bool Renderer::CacheMode() const { return impl_->cacheMode_; }

void Renderer::SetTextureFromTargets(bool enabled) { impl_->textureFromTargets_ = enabled; }

void Renderer::SetGlm(bool enabled) {
    impl_->glmEnabled_ = enabled;
    impl_->UpdateClutCopies();
}

void Renderer::SetGpuClut(bool enabled) {
    impl_->gpuClutEnabled_ = enabled;
    impl_->UpdateClutCopies();
}

void Renderer::SetGlmParking(bool enabled) {
    if (!enabled) impl_->WriteBackAll();  // GLM-owned pages go back to the shadow memory
    impl_->glmParking_ = enabled;
}

void Renderer::SetGlmLoadsAlways(bool enabled) { impl_->glmLoadsAlways_ = enabled; }

void Renderer::SetGpuUploads(bool enabled) { impl_->gpuUploads_ = enabled; }

void Renderer::SetRuns(bool enabled) { impl_->gpu_.SetRuns(enabled); }

void Renderer::SetBatchedWritebacks(bool enabled) { impl_->glmBatchedWritebacks_ = enabled; }

void Renderer::SetGpuTiming(bool enabled) { impl_->gpu_.SetGpuTiming(enabled); }
void Renderer::SetDecodeReuse(bool enabled) { impl_->gpu_.SetDecodeReuse(enabled); }

void Renderer::SetChunksPerSubmission(std::uint32_t chunks) { impl_->gpu_.SetChunksPerSubmission(chunks); }

void Renderer::SetDoubleBuffering(bool enabled) { impl_->gpu_.SetDoubleBuffering(enabled); }

bool Renderer::ComposeFrameGpu(GpuFrame& out) {
    Impl& s = *impl_;
    out = GpuFrame{};
    double composeUs = 0.0;
    {
        StageTimer timer(composeUs);
        s.EnsureLayout();
        const display::DisplayLayout& layout = s.layout_;
        display::MergePlan plan;
        std::array<bool, 2> available{};
        if (!s.GpuComposeApplies(layout, plan, available)) return false;
        const display::PmodeFields pmode = display::decode_pmode(s.displayRegs_.pmode);
        // The same choices as the CPU presentation compose for a progressive frame: no field offset, deinterlace
        // never runs (not really interlaced), output size from 13.3.
        const display::DeinterlaceMode effective =
            s.settings_.deinterlace == display::DeinterlaceMode::Auto
                ? display::resolve_auto_deinterlace(layout, layout.ffmd, s.displayRegs_.scanmsk_active)
                : s.settings_.deinterlace;
        const display::OutputSize size =
            display::merge_output_size(layout, effective == display::DeinterlaceMode::Off, 1);
        if (size.width == 0 || size.height == 0) return false;
        // The GLM is made current for the circuits (stale pages uploaded, drawn targets swizzled).
        for (std::size_t i = 0; i < 2; ++i) {
            if (!available[i]) continue;
            const BlockSpan span = Impl::DisplayCircuitSpan(layout.circuits[i]);
            if (!s.UploadStalePages(span)) return false;
            for (auto& slot : s.slots_) {
                if (slot.drawn.Empty() || !SpansOverlap(SpanOfRect(slot.view, slot.drawn), span)) continue;
                const int32_t bufferWidth = static_cast<int32_t>(std::max<uint32_t>(slot.view.bufferWidth, 1u) * 64u);
                if (slot.drawn.x1 >= bufferWidth) {
                    ReasonScope scope(s.writebackReason_, WritebackReason::Display);
                    s.WriteBack(slot);
                    if (!s.UploadStalePages(span)) return false;
                    continue;
                }
                if (!s.SwizzleSlotIntoGlm(slot)) return false;
            }
        }
        std::vector<uint32_t> parameters(29, 0u);
        parameters[0] = uint32_t{s.background_[0]} | (uint32_t{s.background_[1]} << 8) |
                        (uint32_t{s.background_[2]} << 16) | (std::min<uint32_t>(pmode.alp, 255u) << 24);
        parameters[1] = std::min<uint32_t>(pmode.alp, 255u);
        parameters[2] = pmode.mmod ? 1u : 0u;
        parameters[3] = plan.use_rc1 ? 1u : 0u;
        parameters[4] = plan.use_rc2 ? 1u : 0u;
        for (std::size_t i = 0; i < 2; ++i) {
            const display::CircuitRect& circuit = layout.circuits[i];
            const std::size_t at = i == 0 ? 5u : 17u;
            const auto psm = memory::decodePsm(circuit.psm);
            if (!available[i] || !psm) continue;
            parameters[at + 0] = memory::blockOfPage(circuit.fbp);
            parameters[at + 1] = circuit.fbw;
            parameters[at + 2] = circuit.psm;
            parameters[at + 3] = circuit.psm == 0x01u ? 1u : (circuit.psm == 0x02u || circuit.psm == 0x0Au) ? 2u : 0u;
            parameters[at + 4] = static_cast<uint32_t>(circuit.dbx);
            parameters[at + 5] = static_cast<uint32_t>(circuit.dby);
            parameters[at + 6] = circuit.read_width;
            parameters[at + 7] = circuit.read_height;
            parameters[at + 8] = static_cast<uint32_t>(circuit.screen_x);
            parameters[at + 9] = static_cast<uint32_t>(circuit.screen_y);
            parameters[at + 10] = circuit.display_width;
            parameters[at + 11] = circuit.display_height;
        }
        std::string error;
        if (!s.gpu_.ComposeDisplay(parameters, size.width, size.height, error) || !s.gpu_.Flush(false, error)) {
            s.Fail(error);
            return false;
        }
        if (plan.use_rc1 && plan.use_rc2 && !pmode.mmod) s.counters.mergeAlphaFromTexel++;  // V-01-12
        s.counters.presentationFrames++;
        s.counters.gpuComposedFrames++;
        s.lastPresented_.reset();  // a later KeepPrevious falls back to the CPU history (none: blank)
        out.valid = true;
        out.width = size.width;
        out.height = size.height;
        out.image = s.gpu_.DisplayImage();
    }
    s.timings_.composeUs = composeUs;
    s.counters.composeUs += composeUs;
    return true;
}

void Renderer::SetGpuCompose(bool enabled) { impl_->gpuCompose_ = enabled; }

bool Renderer::ReadGpuFrame(const GpuFrame& frame, std::vector<std::uint32_t>& pixels) {
    std::string error;
    if (!frame.valid || !impl_->gpu_.ReadDisplay(frame.width, frame.height, pixels, error)) return false;
    return true;
}

void Renderer::SetAsyncPresentation(bool enabled) {
    impl_->asyncPresentation_ = enabled;
    impl_->captureNext_ = Impl::DisplayCapture{};
    impl_->captureShown_ = Impl::DisplayCapture{};
}

void Renderer::SetCpuSmallSprites(bool enabled) { impl_->cpuSmallSprites_ = enabled; }

const Counters& Renderer::GetCounters() const { return impl_->counters; }

const gpu::GpuStats& Renderer::GpuCounters() const { return impl_->gpu_.Stats(); }

const std::string& Renderer::LastError() const { return impl_->lastError_; }

bool Renderer::Failed() const { return impl_->failed_; }

} // namespace hwgs::writeback
