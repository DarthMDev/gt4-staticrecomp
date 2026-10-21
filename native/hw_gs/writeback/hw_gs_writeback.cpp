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
#include <array>
#include <chrono>
#include <cstring>
#include <optional>

#include "hw_batch.hpp"
#include "hw_clut.hpp"
#include "hw_front_end.hpp"
#include "hw_gs_display.hpp"
#include "hw_gs_pixel.hpp"
#include "hw_gs_registers.hpp"
#include "hw_gs_types.hpp"
#include "hw_memory.hpp"
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
    if (out.cls == TexClass::Indexed) {
        out.palette = clut.Decode(psmCode, tex0::Cpsm(tex0Register), tex0::Csa(tex0Register), state.env.texa);
    }
    return true;
}

// Colour of one texel after TEXA and palette expansion, packed RGBA8 (the GPU's texel format).
uint32_t DecodeTexel(const memory::LocalMemory& local, const TextureUnit& t, uint32_t x, uint32_t y) {
    const uint32_t value = local.readPixel(t.view, x, y);
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

void Translate(const Batch& batch, const memory::LocalMemory& local, const clut::ClutUnit& clut, Translation& out,
               Counters& counters) {
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
        out.texels.resize(static_cast<size_t>(width) * height);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                out.texels[static_cast<size_t>(y) * width + x] = DecodeTexel(local, texture, x, y);
            }
        }
        counters.texturesDecoded++;
        counters.texelsDecoded += static_cast<uint64_t>(width) * height;
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
    st.fields[gpu::gpufield::TexelKind] = 0;  // always RGBA8 (expanded on the CPU)
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
        chunk.state = t.state;
        chunk.hasTexture = t.hasTexture;
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
        std::size_t index = 0;
        for (int32_t y = rectY0; y <= rectY1; ++y) {
            for (int32_t x = rectX0; x <= rectX1; ++x, ++index) {
                chunk.frameWords[index] = local.readPixel(t.frameView, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                chunk.depthWords[index] = local.readPixel(t.zView, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
            }
        }
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

class Renderer::Impl final : public BatchSink, public SyncSink {
public:
    Impl() : frontEnd_(FrontEndOptions{}, *this, *this, memory_) {
        frontEnd_.SetLocalMemory(memory_.Local().bytes().data());
    }

    void OnBatch(Batch&& batch) override {
        counters.batches++;
        if (!gpu_.Ready()) {
            Fail("GPU drawer is not initialised");
            return;
        }
        Translation t;
        {
            StageTimer timer(frameTimings_.translateUs);
            Translate(batch, memory_.Local(), frontEnd_.Clut(), t, counters);
        }
        if (observer_) observer_(batch, t);
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
            if (!ExecuteBatch(gpu_, t, memory_.Local(), counters, error)) Fail(error);
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
    bool failed_ = false;
    std::string lastError_;
    Renderer::BatchObserver observer_;
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}

Renderer::~Renderer() = default;

bool Renderer::Init(const gpu::VulkanHandles& handles, std::string& error) { return impl_->gpu_.Init(handles, error); }

bool Renderer::GifTransfer(std::uint32_t path, const std::uint8_t* bytes, std::size_t byteCount) {
    if (path == 0 || path > 3) return false;  // IN-312
    if (byteCount % 16 != 0) return false;
    std::vector<Qword> qwords(byteCount / 16);
    for (std::size_t i = 0; i < qwords.size(); ++i) {
        std::memcpy(&qwords[i].lo, bytes + 16 * i, 8);
        std::memcpy(&qwords[i].hi, bytes + 16 * i + 8, 8);
    }
    impl_->frontEnd_.GifTransfer(path, qwords.data(), qwords.size());
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
    s.frontEnd_.ResetGs();
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
    const auto bytes = s.memory_.Local().bytes();
    s.auxSnapshot_.assign(bytes.begin(), bytes.end());
    s.auxiliary_ = true;
    return true;
}

void Renderer::EndAuxiliaryFrame() {
    Impl& s = *impl_;
    if (!s.auxiliary_) return;
    const auto bytes = s.memory_.Local().bytes();
    std::copy(s.auxSnapshot_.begin(), s.auxSnapshot_.end(), bytes.begin());
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
    auto bytes = impl_->memory_.Local().bytes();
    std::fill(bytes.begin(), bytes.end(), std::uint8_t{0});
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
    out.pixels.clear();
    out.pixels.reserve(image.pixels.size());
    for (const display::Rgba8& p : image.pixels) {
        out.pixels.push_back(static_cast<uint32_t>(p.r) | (static_cast<uint32_t>(p.g) << 8) |
                             (static_cast<uint32_t>(p.b) << 16) | (static_cast<uint32_t>(p.a) << 24));
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
    for (uint32_t y = 0; y < circuit.read_height; ++y) {
        for (uint32_t x = 0; x < circuit.read_width; ++x) {
            const uint32_t sx = static_cast<uint32_t>(circuit.dbx + static_cast<int32_t>(x)) & 2047u;
            const uint32_t sy = static_cast<uint32_t>(circuit.dby + static_cast<int32_t>(y)) & 2047u;
            const uint32_t stored = local.readPixel(view, sx, sy);
            const pixel::Colour colour = pixel::frame_word_to_colour(pixel::unpack_frame_word(format, stored));
            display::Rgba8& dst = image.at(x, y);
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
        if (s.layoutVblank_ != s.vblanks_ || !s.layoutValid_) {
            s.layout_ = display::compute_layout(s.displayRegs_, s.previousDby_);
            s.layoutVblank_ = s.vblanks_;
            s.layoutValid_ = true;
        }
        const display::DisplayLayout& layout = s.layout_;
        const bool presentation = kind == ComposeKind::Presentation;
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
            const display::PmodeFields pmode = display::decode_pmode(s.displayRegs_.pmode);
            std::array<display::Image, 2> sources{};
            std::array<bool, 2> available{false, false};
            for (std::size_t i = 0; i < 2; ++i) {
                const display::CircuitRect& circuit = layout.circuits[i];
                if (!circuit.enabled || circuit.read_width == 0 || circuit.read_height == 0) continue;
                sources[i] = ReadCircuit(s.memory_.Local(), circuit);
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
                                ? display::resolve_auto_deinterlace(layout, layout.ffmd, s.displayRegs_.scanmsk_active)
                                : s.settings_.deinterlace;
            }
            // IN-339: the display field of a frame (VSYNC field = CSR.FIELD, 1 = odd) maps to the LOWER row
            // phase when it is 0 and to the upper one when it is 1: GT4 draws its CSR.FIELD = 1 frames half a
            // line higher, so with the IN-35 reading (field 1 shifted down) every woven menu line landed two
            // rows from its partner. `_bff` still inverts the result.
            const int vblankField = RowFieldOfDisplayField(s.oddField_);
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
            display::Image merged = display::compose_merge(plan, pmode, s.background_[0], s.background_[1],
                                                           s.background_[2], size.width, size.height, 1, offsetRows,
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
    s.timings_.executeGpuUs = 0.0;  // reserved for GPU timestamps (not measured yet)
}

void Renderer::SetBatchObserver(BatchObserver observer) { impl_->observer_ = std::move(observer); }

const std::uint8_t* Renderer::Slm() const { return impl_->memory_.Local().bytes().data(); }

const Counters& Renderer::GetCounters() const { return impl_->counters; }

const gpu::GpuStats& Renderer::GpuCounters() const { return impl_->gpu_.Stats(); }

const std::string& Renderer::LastError() const { return impl_->lastError_; }

bool Renderer::Failed() const { return impl_->failed_; }

} // namespace hwgs::writeback
