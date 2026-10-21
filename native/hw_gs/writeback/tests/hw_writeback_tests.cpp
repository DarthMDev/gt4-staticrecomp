// MIT License. Copyright (c) 2026 João Alano and GT4Recomp contributors.
// Written from docs/hw-renderer-spec (TASK-027 M4, chapters 01.4, 03.2, 03.3, 03.4, 03.5, 03.7, 03.8,
// 15), clean room (decision 0069). CTest entry points of the M4 write-back reference renderer.
//
// Usage: hw_writeback_tests <test name>. GPU tests need a Vulkan 1.2 device with graphics and compute
// queues; without one they exit 77 (CTest reports Skipped, never a pass).
// Equivalence tests compare the GPU result with the CPU reference (cpuref::DrawBatch) pixel by pixel.
// Edges are placed on pixel boundaries where the two rasterisers are specified to agree; the diagonal
// and Gouraud cases are reported, not asserted (IN-500).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../cpuref/cpuref_draw.hpp"
#include "../../cpuref/cpuref_memory.hpp"
#include "../../frontend/hw_gs_registers.hpp"
#include "../../frontend/hw_gs_types.hpp"
#include "../../hw_gs_vulkan_loader.hpp"
#include "../hw_gs_vulkan_device_table.hpp"
#include "../hw_gs_writeback.hpp"

namespace {

using namespace hwgs;

// ---- Headless device -----------------------------------------------------------------------------

struct Headless {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;

    bool Create(std::string& error) {
        if (!gt4::hw::vulkan_loader::load_loader(error)) return false;
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "hw_writeback_tests";
        app.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        if (gt4_hw_vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS) {
            error = "vkCreateInstance failed";
            return false;
        }
        std::vector<std::string> missing;
        if (!gt4::hw::vulkan_loader::load_headless_entry_points(instance, missing)) {
            error = "instance entry points missing:";
            for (const std::string& name : missing) error += " " + name;
            return false;
        }
        uint32_t count = 0;
        gt4_hw_vkEnumeratePhysicalDevices(instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        gt4_hw_vkEnumeratePhysicalDevices(instance, &count, devices.data());
        for (VkPhysicalDevice candidate : devices) {
            VkPhysicalDeviceProperties props{};
            gt4_hw_vkGetPhysicalDeviceProperties(candidate, &props);
            if (props.apiVersion < VK_API_VERSION_1_2) continue;
            uint32_t families = 0;
            gt4_hw_vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
            std::vector<VkQueueFamilyProperties> props_q(families);
            gt4_hw_vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, props_q.data());
            for (uint32_t f = 0; f < families; ++f) {
                const VkQueueFlags wanted = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
                if ((props_q[f].queueFlags & wanted) != wanted) continue;
                physical = candidate;
                family = f;
                break;
            }
            if (physical) break;
        }
        if (!physical) {
            error = "no Vulkan 1.2 device with graphics and compute";
            return false;
        }
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        VkDeviceCreateInfo dev{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dev.queueCreateInfoCount = 1;
        dev.pQueueCreateInfos = &queue;
        if (gt4_hw_vkCreateDevice(physical, &dev, nullptr, &device) != VK_SUCCESS) {
            error = "vkCreateDevice failed";
            return false;
        }
        gt4_hw_vkGetDeviceQueue(device, family, 0, &queue_handle());
        return gt4::hw::vulkan_device::load_device_entry_points(instance, missing);
    }

    VkQueue &queue_handle() { return queue; }
};

// ---- Batch construction --------------------------------------------------------------------------

struct Setup {
    uint32_t fbp = 0;     // FBP in pages
    uint32_t fbw = 10;    // FBW in 64-pixel units
    uint32_t fpsm = 0;    // CT32
    uint32_t fbmsk = 0;
    uint32_t zbp = 70;    // ZBP in pages
    uint32_t zpsm = 0;    // Z32 low bits (0x30 implied)
    bool zmsk = true;
    bool zte = false;
    uint32_t ztst = 1;    // ALWAYS
    bool ate = false;
    uint32_t atst = 1;    // ALWAYS
    uint32_t aref = 0x80;
    uint32_t afail = 0;   // KEEP
    int scax0 = 0, scax1 = 639, scay0 = 0, scay1 = 447;
    bool tme = false;
    bool fst = false;
    bool iip = true;      // Gouraud
    bool fge = false;
    uint32_t fogcol = 0;
    uint32_t tbp = 40;    // texture base in pages
    uint32_t tbw = 4;
    uint32_t tpsm = 0;
    uint32_t tw = 3, th = 3;
    uint32_t tfx = 0;
    bool tcc = false;
    bool mmag = false;
    uint32_t wms = 0, wmt = 0;
    uint32_t minu = 0, maxu = 0, minv = 0, maxv = 0;
    PrimType type = PrimType::Triangle;
};

uint64_t Pack(uint64_t value, unsigned lo) { return value << lo; }

Batch MakeBatch(const Setup& s, const std::vector<Vertex>& vertices, const std::vector<PrimType>& prims) {
    Batch batch;
    batch.state.ctx.frame = Pack(s.fbp, 0) | Pack(s.fbw, 16) | Pack(s.fpsm, 24) | Pack(s.fbmsk, 32);
    batch.state.ctx.zbuf = Pack(s.zbp, 0) | Pack(s.zpsm, 24) | Pack(s.zmsk ? 1 : 0, 32);
    batch.state.ctx.test = Pack(s.ate ? 1 : 0, 0) | Pack(s.atst, 1) | Pack(s.aref, 4) | Pack(s.afail, 12) |
                           Pack(s.zte ? 1 : 0, 16) | Pack(s.ztst, 17);
    batch.state.ctx.scissor = Pack(static_cast<uint64_t>(s.scax0), 0) | Pack(static_cast<uint64_t>(s.scax1), 16) |
                              Pack(static_cast<uint64_t>(s.scay0), 32) | Pack(static_cast<uint64_t>(s.scay1), 48);
    batch.state.ctx.tex0 = Pack(s.tbp, 0) | Pack(s.tbw, 14) | Pack(s.tpsm, 20) | Pack(s.tw, 26) | Pack(s.th, 30) |
                           Pack(s.tcc ? 1 : 0, 34) | Pack(s.tfx, 35);
    batch.state.ctx.tex1 = Pack(s.mmag ? 1 : 0, 5);
    batch.state.ctx.clamp = Pack(s.wms, 0) | Pack(s.wmt, 2) | Pack(s.minu, 4) | Pack(s.maxu, 14) | Pack(s.minv, 24) |
                            Pack(s.maxv, 34);
    batch.state.env.fogcol = s.fogcol;
    uint64_t prim = static_cast<uint64_t>(s.type == PrimType::Triangle ? 3 : static_cast<uint8_t>(s.type));
    prim = prim | (s.iip ? 1ull << primbit::kIip : 0) | (s.tme ? 1ull << primbit::kTme : 0) |
           (s.fge ? 1ull << primbit::kFge : 0) | (s.fst ? 1ull << primbit::kFst : 0);
    batch.state.effectivePrim = prim;
    batch.cls = ClassOfType(static_cast<uint32_t>(prim & 7u));
    batch.vertices = vertices;
    uint32_t first = 0;
    for (PrimType type : prims) {
        const uint32_t count = type == PrimType::Point ? 1 : (type == PrimType::Line || type == PrimType::LineStrip) ? 2
                             : type == PrimType::Sprite ? 2 : 3;
        PrimRecord record;
        record.type = type;
        record.first = first;
        record.count = count;
        int32_t x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
        for (uint32_t i = 0; i < count; ++i) {
            const Vertex& v = vertices[first + i];
            x0 = std::min(x0, v.x4 >> 4);
            y0 = std::min(y0, v.y4 >> 4);
            x1 = std::max(x1, v.x4 >> 4);
            y1 = std::max(y1, v.y4 >> 4);
        }
        record.rect = PixelRect{x0, y0, x1, y1};
        batch.prims.push_back(record);
        first += count;
    }
    return batch;
}

Vertex MakeVertex(int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float s = 0.0f, float t = 0.0f,
                  uint32_t u = 0, uint32_t v = 0, uint32_t z = 0, uint8_t fog = 255) {
    Vertex out;
    out.x4 = x * 16;
    out.y4 = y * 16;
    out.r = r;
    out.g = g;
    out.b = b;
    out.a = a;
    out.s = s;
    out.t = t;
    out.q = 1.0f;
    out.u = u;
    out.v = v;
    out.z = z;
    out.fog = fog;
    return out;
}

// ---- Equivalence harness -------------------------------------------------------------------------

// Deterministic fill of local memory: a texture pattern and a frame pattern in the given views.
void FillPattern(memory::LocalMemory& local, const Setup& s, uint32_t seed) {
    uint32_t state = seed * 2654435761u + 1u;
    auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    const auto tpsm = memory::decodePsm(s.tpsm);
    if (tpsm) {
        const memory::BufferView view{memory::blockOfPage(s.tbp), s.tbw, *tpsm};
        for (uint32_t y = 0; y < (1u << s.th); ++y) {
            for (uint32_t x = 0; x < (1u << s.tw); ++x) local.writePixel(view, x, y, next() | 0x40000000u);
        }
    }
    const auto fpsm = memory::decodePsm(s.fpsm);
    if (fpsm) {
        const memory::BufferView view{memory::blockOfPage(s.fbp), s.fbw, *fpsm};
        for (uint32_t y = 0; y < 448; ++y) {
            for (uint32_t x = 0; x < 640; ++x) local.writePixel(view, x, y, next());
        }
    }
    const auto zpsm = memory::decodePsm(0x30u | s.zpsm);
    if (zpsm) {
        const memory::BufferView view{memory::blockOfPage(s.zbp), s.fbw, *zpsm};
        for (uint32_t y = 0; y < 448; ++y) {
            for (uint32_t x = 0; x < 640; ++x) local.writePixel(view, x, y, next() & 0x00FFFFFFu);
        }
    }
}

struct CaseResult {
    uint64_t compared = 0;
    uint64_t mismatches = 0;
    uint64_t firstX = 0, firstY = 0;
    bool gpuError = false;
    std::string error;
};

// Runs the batch through the CPU reference and through the GPU path, then compares frame and depth words
// over the batch rectangle. Both start from identical memory.
CaseResult RunCase(gpu::GpuDrawer& drawer, const Setup& s, const Batch& batch, uint32_t seed) {
    CaseResult result;
    memory::LocalMemory cpuMemory;
    memory::LocalMemory gpuMemory;
    FillPattern(cpuMemory, s, seed);
    FillPattern(gpuMemory, s, seed);
    CounterSet counterSet;
    clut::ClutUnit clut(counterSet);

    cpuref::DrawStats stats;
    cpuref::DrawBatch(batch, cpuMemory, clut, stats);

    writeback::BatchTranslation translation;
    writeback::Counters counters;
    writeback::TranslateBatch(batch, gpuMemory, clut, translation, counters);
    if (translation.skippedTexture) {
        result.error = "texture skipped";
        return result;
    }
    if (translation.draws.empty()) {
        // Nothing drawn: the reference must not have drawn either.
        return result;
    }
    if (!writeback::ExecuteBatch(drawer, translation, gpuMemory, counters, result.error)) {
        result.gpuError = true;
        return result;
    }
    const memory::BufferView frameCpu{memory::blockOfPage(s.fbp), s.fbw, *memory::decodePsm(s.fpsm)};
    const memory::BufferView zCpu{memory::blockOfPage(s.zbp), s.fbw, *memory::decodePsm(0x30u | s.zpsm)};
    const PixelRect& r = translation.rect;
    for (int32_t y = r.y0; y <= r.y1; ++y) {
        for (int32_t x = r.x0; x <= r.x1; ++x) {
            const uint32_t fc = cpuMemory.readPixel(frameCpu, x, y);
            const uint32_t fg = gpuMemory.readPixel(frameCpu, x, y);
            const uint32_t zc = cpuMemory.readPixel(zCpu, x, y);
            const uint32_t zg = gpuMemory.readPixel(zCpu, x, y);
            result.compared++;
            if (fc != fg || zc != zg) {
                if (result.mismatches == 0) {
                    result.firstX = static_cast<uint64_t>(x);
                    result.firstY = static_cast<uint64_t>(y);
                }
                // Bounded value report: the first few differences name the stage that diverged.
                if (result.mismatches < 4) {
                    std::printf("  mismatch at %d,%d: frame cpu=%08X gpu=%08X | depth cpu=%08X gpu=%08X\n", x, y, fc,
                                fg, zc, zg);
                }
                result.mismatches++;
            }
        }
    }
    return result;
}

int Report(const char* name, const CaseResult& r) {
    std::printf("%s: compared %llu pixels, mismatches %llu", name, static_cast<unsigned long long>(r.compared),
                static_cast<unsigned long long>(r.mismatches));
    if (r.mismatches) {
        std::printf(" (first at %llu,%llu)", static_cast<unsigned long long>(r.firstX),
                    static_cast<unsigned long long>(r.firstY));
    }
    std::printf("\n");
    if (r.gpuError) std::printf("%s: GPU error: %s\n", name, r.error.c_str());
    return (r.gpuError || r.mismatches != 0) ? 1 : 0;
}

// Quad on pixel boundaries: two triangles over [x0, x1) x [y0, y1) with one flat colour.
std::vector<Vertex> FlatQuad(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t r, uint8_t g, uint8_t b,
                             uint8_t a, float s0 = 0, float t0 = 0, float s1 = 0, float t1 = 0) {
    return {MakeVertex(x0, y0, r, g, b, a, s0, t0), MakeVertex(x1, y0, r, g, b, a, s1, t0),
            MakeVertex(x0, y1, r, g, b, a, s0, t1), MakeVertex(x1, y0, r, g, b, a, s1, t0),
            MakeVertex(x0, y1, r, g, b, a, s0, t1), MakeVertex(x1, y1, r, g, b, a, s1, t1)};
}

// ---- CPU translation tests (no GPU) --------------------------------------------------------------

int TestFlatProvoking() {
    Setup s;
    s.iip = false;  // flat: the last vertex supplies the colour (chapter 03.2)
    const std::vector<Vertex> v = {MakeVertex(0, 0, 10, 20, 30, 40), MakeVertex(16, 0, 50, 60, 70, 80),
                                   MakeVertex(0, 16, 90, 100, 110, 120)};
    Batch batch = MakeBatch(s, v, {PrimType::Triangle});
    writeback::BatchTranslation t;
    writeback::Counters counters;
    memory::LocalMemory local;
    CounterSet counterSet;
    clut::ClutUnit clut(counterSet);
    writeback::TranslateBatch(batch, local, clut, t, counters);
    if (t.vertices.size() != 3) return 1;
    for (const gpu::GpuVertex& g : t.vertices) {
        if (g.rgba[0] != 90 || g.rgba[1] != 100 || g.rgba[2] != 110 || g.rgba[3] != 120) {
            std::printf("hw_flat_provoking: triangle vertex not de-indexed to the last colour\n");
            return 1;
        }
    }
    // A line in flat mode takes the second vertex's colour.
    Setup line = s;
    line.type = PrimType::Line;
    Batch lineBatch = MakeBatch(line, {MakeVertex(0, 0, 1, 2, 3, 4), MakeVertex(8, 8, 5, 6, 7, 8)}, {PrimType::Line});
    writeback::BatchTranslation tl;
    writeback::TranslateBatch(lineBatch, local, clut, tl, counters);
    for (const gpu::GpuVertex& g : tl.vertices) {
        if (g.rgba[0] != 5 || g.rgba[3] != 8) {
            std::printf("hw_flat_provoking: line vertex not de-indexed\n");
            return 1;
        }
    }
    // Gouraud keeps every vertex colour.
    Setup smooth = s;
    smooth.iip = true;
    Batch smoothBatch = MakeBatch(smooth, v, {PrimType::Triangle});
    writeback::BatchTranslation ts;
    writeback::TranslateBatch(smoothBatch, local, clut, ts, counters);
    if (ts.vertices[0].rgba[0] != 10 || ts.vertices[2].rgba[0] != 90) {
        std::printf("hw_flat_provoking: smooth colours changed\n");
        return 1;
    }
    std::printf("hw_flat_provoking: triangle and line de-indexed to the last vertex, Gouraud unchanged\n");
    return 0;
}

int TestSpriteCorners() {
    Setup s;
    s.type = PrimType::Sprite;
    // First vertex at (2, 3) with S/T/U/V (4, 6)/(40, 60); second at (10, 9) with S/T (8, 12)/(80, 120).
    std::vector<Vertex> v = {MakeVertex(2, 3, 1, 1, 1, 1, 4, 6, 40, 60), MakeVertex(10, 9, 200, 201, 202, 203, 8, 12, 80, 120)};
    Batch batch = MakeBatch(s, v, {PrimType::Sprite});
    memory::LocalMemory local;
    CounterSet counterSet;
    clut::ClutUnit clut(counterSet);
    writeback::BatchTranslation t;
    writeback::Counters counters;
    writeback::TranslateBatch(batch, local, clut, t, counters);
    if (t.vertices.size() != 6 || counters.spritesExpanded != 1) {
        std::printf("hw_sprite_corners: expected 6 vertices, got %zu\n", t.vertices.size());
        return 1;
    }
    // Corner order of the two triangles: 0, 1, 2, 1, 2, 3 (chapter 03.2).
    const int32_t expectX[4] = {2, 10, 2, 10};
    const int32_t expectY[4] = {3, 3, 9, 9};
    const int order[6] = {0, 1, 2, 1, 2, 3};
    for (int i = 0; i < 6; ++i) {
        const gpu::GpuVertex& g = t.vertices[i];
        const int c = order[i];
        if (static_cast<int32_t>(g.x) != expectX[c] * 16 || static_cast<int32_t>(g.y) != expectY[c] * 16) {
            std::printf("hw_sprite_corners: corner %d at the wrong position\n", c);
            return 1;
        }
        if (g.rgba[0] != 200 || g.rgba[3] != 203) {
            std::printf("hw_sprite_corners: colour not taken from the second vertex\n");
            return 1;
        }
    }
    // S comes from the first vertex at X of the first corner and the second at the second corner.
    if (t.vertices[0].s != 4.0f || t.vertices[1].s != 8.0f || t.vertices[2].t != 12.0f) {
        std::printf("hw_sprite_corners: texture coordinates not per corner\n");
        return 1;
    }
    std::printf("hw_sprite_corners: six vertices, corner rule and second-vertex colour as chapter 03.2\n");
    return 0;
}

// ---- GPU tests -----------------------------------------------------------------------------------

int TestCoverageVectors(gpu::GpuDrawer& drawer) {
    // Chapter 03.3 vectors at 1x (sprite spans, pixel n covered when its centre is inside [min, max)):
    // 0.0..2.0 covers 0 and 1; 0.5..1.5 covers 1; 0.0625..1.0 covers nothing.
    Setup s;
    s.type = PrimType::Sprite;
    s.iip = false;
    const std::vector<Vertex> v = {
        MakeVertex(0, 0, 255, 0, 0, 128), MakeVertex(2, 1, 255, 0, 0, 128),   // row 0: x 0..2
        MakeVertex(0, 2, 0, 255, 0, 128), MakeVertex(1, 3, 0, 255, 0, 128),   // row 2: x 0..1
    };
    // Sprites with sub-pixel edges (12.4 units): 0.5..1.5 on row 4, 0.0625..1.0 on row 6.
    std::vector<Vertex> w = {MakeVertex(0, 4, 0, 0, 255, 128), MakeVertex(1, 5, 0, 0, 255, 128)};
    w[0].x4 = 8;
    w[1].x4 = 24;
    std::vector<Vertex> z = {MakeVertex(0, 6, 9, 9, 9, 128), MakeVertex(1, 7, 9, 9, 9, 128)};
    z[0].x4 = 1;
    z[1].x4 = 16;
    std::vector<Vertex> all;
    all.insert(all.end(), v.begin(), v.end());
    all.insert(all.end(), w.begin(), w.end());
    all.insert(all.end(), z.begin(), z.end());
    Batch batch = MakeBatch(s, all, {PrimType::Sprite, PrimType::Sprite, PrimType::Sprite});
    CaseResult result = RunCase(drawer, s, batch, 7);
    // Expected coverage from the vectors, checked against the GPU output directly.
    memory::LocalMemory gpuMemory;
    FillPattern(gpuMemory, s, 7);
    writeback::BatchTranslation t;
    writeback::Counters counters;
    CounterSet counterSet;
    clut::ClutUnit clut(counterSet);
    writeback::TranslateBatch(batch, gpuMemory, clut, t, counters);
    std::string error;
    if (!writeback::ExecuteBatch(drawer, t, gpuMemory, counters, error)) {
        std::printf("hw_gpu_coverage_vectors: GPU error %s\n", error.c_str());
        return 1;
    }
    const memory::BufferView frame{memory::blockOfPage(s.fbp), s.fbw, memory::Psm::Ct32};
    auto covered = [&](uint32_t x, uint32_t y) {
        return (gpuMemory.readPixel(frame, x, y) & 0xFFu) == 255u;  // red channel from the first sprite
    };
    auto coveredBlue = [&](uint32_t x, uint32_t y) { return ((gpuMemory.readPixel(frame, x, y) >> 16) & 0xFFu) == 255u; };
    bool ok = covered(0, 0) && covered(1, 0) && !covered(2, 0);
    ok = ok && !covered(0, 1) && !covered(1, 1);
    ok = ok && coveredBlue(1, 4) && !coveredBlue(0, 4) && !coveredBlue(2, 4);
    ok = ok && !coveredBlue(0, 6) && !coveredBlue(1, 6);
    if (!ok) {
        std::printf("hw_gpu_coverage_vectors: coverage differs from the chapter 03.3 vectors\n");
        return 1;
    }
    return Report("hw_gpu_coverage_vectors", result);
}

int TestEquivalenceFlat(gpu::GpuDrawer& drawer) {
    Setup s;
    s.iip = false;
    Batch batch = MakeBatch(s, FlatQuad(0, 0, 32, 32, 12, 200, 40, 255), {PrimType::Triangle, PrimType::Triangle});
    return Report("hw_gpu_equivalence_flat", RunCase(drawer, s, batch, 1));
}

int TestEquivalenceTexture(gpu::GpuDrawer& drawer) {
    // Textured quad, STQ coordinates on the pixel grid (decal, modulate, nearest, repeat).
    int failures = 0;
    for (uint32_t tfx = 0; tfx < 2; ++tfx) {
        Setup s;
        s.iip = false;
        s.tme = true;
        s.tpsm = 0;  // CT32 texture
        s.tw = 3;
        s.th = 3;
        s.tfx = tfx;
        s.fst = true;  // FST: UV in 1/16 texel units (chapter 03.5)
        std::vector<Vertex> q = {MakeVertex(0, 0, 128, 128, 128, 128, 0, 0, 0, 0),
                                 MakeVertex(40, 0, 128, 128, 128, 128, 0, 0, 40 * 16, 0),
                                 MakeVertex(0, 40, 128, 128, 128, 128, 0, 0, 0, 40 * 16),
                                 MakeVertex(40, 0, 128, 128, 128, 128, 0, 0, 40 * 16, 0),
                                 MakeVertex(0, 40, 128, 128, 128, 128, 0, 0, 0, 40 * 16),
                                 MakeVertex(40, 40, 128, 128, 128, 128, 0, 0, 40 * 16, 40 * 16)};
        Batch batch = MakeBatch(s, q, {PrimType::Triangle, PrimType::Triangle});
        failures += Report(tfx == 0 ? "hw_gpu_equivalence_texture_modulate" : "hw_gpu_equivalence_texture_decal",
                           RunCase(drawer, s, batch, 2 + tfx));
    }
    return failures ? 1 : 0;
}

int TestEquivalenceWrapRegion(gpu::GpuDrawer& drawer) {
    // STQ sprite over a 8x8 texture with region repeat and region clamp (chapter 03.5 table).
    int failures = 0;
    for (uint32_t mode = 2; mode <= 3; ++mode) {
        Setup s;
        s.tme = true;
        s.tw = 3;
        s.th = 3;
        s.wms = mode;
        s.wmt = mode;
        s.minu = mode == 3 ? 3 : 1;   // region repeat mask / region clamp minimum
        s.maxu = mode == 3 ? 1 : 6;   // region repeat fixed bits / region clamp maximum
        s.minv = s.minu;
        s.maxv = s.maxu;
        s.type = PrimType::Sprite;
        s.iip = false;
        s.fst = true;
        std::vector<Vertex> q = {MakeVertex(0, 0, 0, 0, 0, 0, 0, 0, 0, 0), MakeVertex(40, 40, 0, 0, 0, 0, 0, 0, 40 * 16, 40 * 16)};
        Batch batch = MakeBatch(s, q, {PrimType::Sprite});
        failures += Report(mode == 2 ? "hw_gpu_equivalence_region_clamp" : "hw_gpu_equivalence_region_repeat",
                           RunCase(drawer, s, batch, 4 + mode));
    }
    return failures ? 1 : 0;
}

int TestEquivalenceBilinear(gpu::GpuDrawer& drawer) {
    Setup s;
    s.tme = true;
    s.mmag = true;
    s.fst = true;
    s.iip = false;
    s.tw = 4;
    s.th = 4;
    std::vector<Vertex> q = {MakeVertex(0, 0, 0, 0, 0, 0, 0, 0, 8, 8), MakeVertex(32, 32, 0, 0, 0, 0, 0, 0, 8 + 32 * 16, 8 + 32 * 16)};
    s.type = PrimType::Sprite;
    Batch batch = MakeBatch(s, q, {PrimType::Sprite});
    return Report("hw_gpu_equivalence_bilinear", RunCase(drawer, s, batch, 9));
}

int TestEquivalenceTfxFog(gpu::GpuDrawer& drawer) {
    Setup s;
    s.iip = false;
    s.tme = false;
    s.tw = 3;
    s.th = 3;
    s.fst = true;
    s.tfx = 1;  // decal
    s.fge = true;
    s.fogcol = 0x402010;
    std::vector<Vertex> q = FlatQuad(0, 0, 40, 40, 200, 100, 50, 128, 0, 0, 40 * 16, 40 * 16);
    for (Vertex& v : q) v.fog = 90;
    Batch batch = MakeBatch(s, q, {PrimType::Triangle, PrimType::Triangle});
    return Report("hw_gpu_equivalence_tfx_fog", RunCase(drawer, s, batch, 11));
}

int TestEquivalenceDepthAlpha(gpu::GpuDrawer& drawer) {
    // Depth GEQUAL with a stored Z, then ALWAYS with Z writes, then the alpha test (KEEP).
    Setup s;
    s.iip = false;
    s.zte = true;
    s.ztst = 2;  // GEQUAL
    s.zmsk = false;
    Batch batch = MakeBatch(s, FlatQuad(0, 0, 32, 32, 90, 90, 90, 90), {PrimType::Triangle, PrimType::Triangle});
    int failures = Report("hw_gpu_equivalence_depth", RunCase(drawer, s, batch, 13));
    Setup a;
    a.iip = false;
    a.ate = true;
    a.atst = 6;   // GREATER
    a.aref = 0x20;
    a.afail = 0;  // KEEP
    Batch alpha = MakeBatch(a, FlatQuad(16, 16, 48, 48, 1, 2, 3, 0x40), {PrimType::Triangle, PrimType::Triangle});
    failures += Report("hw_gpu_equivalence_alpha_keep", RunCase(drawer, a, alpha, 14));
    return failures ? 1 : 0;
}

int TestEquivalenceScissor(gpu::GpuDrawer& drawer) {
    Setup s;
    s.iip = false;
    s.scax0 = 8;
    s.scax1 = 20;
    s.scay0 = 4;
    s.scay1 = 12;
    Batch batch = MakeBatch(s, FlatQuad(0, 0, 40, 40, 77, 66, 55, 255), {PrimType::Triangle, PrimType::Triangle});
    return Report("hw_gpu_equivalence_scissor", RunCase(drawer, s, batch, 15));
}

int TestGouraudReport(gpu::GpuDrawer& drawer) {
    // Report only (IN-500): Gouraud interpolation rounding is not asserted yet.
    Setup s;
    s.iip = true;
    std::vector<Vertex> tri = {MakeVertex(0, 0, 255, 0, 0, 255), MakeVertex(40, 0, 0, 255, 0, 255),
                               MakeVertex(0, 40, 0, 0, 255, 255)};
    Batch batch = MakeBatch(s, tri, {PrimType::Triangle});
    CaseResult r = RunCase(drawer, s, batch, 16);
    Report("hw_gpu_gouraud_report", r);
    return r.gpuError ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::string name = argc > 1 ? argv[1] : "";
    if (name == "hw_flat_provoking") return TestFlatProvoking();
    if (name == "hw_sprite_corners") return TestSpriteCorners();

    Headless headless;
    std::string error;
    if (!headless.Create(error)) {
        std::printf("%s: skipped (%s)\n", name.c_str(), error.c_str());
        return 77;
    }
    gpu::VulkanHandles handles;
    handles.instance = headless.instance;
    handles.physical = headless.physical;
    handles.device = headless.device;
    handles.queue = headless.queue;
    handles.queueFamily = headless.family;
    gpu::GpuDrawer drawer;
    if (!drawer.Init(handles, error)) {
        std::printf("%s: GPU drawer init failed: %s\n", name.c_str(), error.c_str());
        return 1;
    }
    if (name == "hw_gpu_coverage_vectors") return TestCoverageVectors(drawer);
    if (name == "hw_gpu_equivalence_flat") return TestEquivalenceFlat(drawer);
    if (name == "hw_gpu_equivalence_texture") return TestEquivalenceTexture(drawer);
    if (name == "hw_gpu_equivalence_region") return TestEquivalenceWrapRegion(drawer);
    if (name == "hw_gpu_equivalence_bilinear") return TestEquivalenceBilinear(drawer);
    if (name == "hw_gpu_equivalence_tfx_fog") return TestEquivalenceTfxFog(drawer);
    if (name == "hw_gpu_equivalence_depth_alpha") return TestEquivalenceDepthAlpha(drawer);
    if (name == "hw_gpu_equivalence_scissor") return TestEquivalenceScissor(drawer);
    if (name == "hw_gpu_gouraud_report") return TestGouraudReport(drawer);
    std::printf("unknown test: %s\n", name.c_str());
    return 2;
}
