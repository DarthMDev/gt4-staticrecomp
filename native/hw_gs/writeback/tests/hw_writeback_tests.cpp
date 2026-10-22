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
#include "../../memory/hw_memory.hpp"
#include "../../clut/hw_clut.hpp"
#include "../../frontend/hw_gs_registers.hpp"
#include "../../pixel/hw_gs_pixel.hpp"
#include "../../frontend/hw_gs_types.hpp"
#include "../../hw_gs_vulkan_loader.hpp"
#include "../hw_gs_vulkan_device_table.hpp"
#include "../hw_gs_present_scheduler.hpp"
#include "../hw_gs_writeback.hpp"
#include "../hw_writeback_replay_target.hpp"
#include "../../cpuref/cpuref_renderer.hpp"
#include "../../cpuref/cpuref_replay.hpp"

namespace {

using namespace hwgs;

// ---- Headless device -----------------------------------------------------------------------------

struct Headless {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    bool wantInterlock = true;
    bool interlock = false;

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
        // IN-345: enable pixel interlock when the device has it (the batch-per-draw path), unless the
        // caller asked for the per-primitive path (A/B: --no-interlock / GT4_HW_TEST_NO_INTERLOCK=1).
        interlock = wantInterlock && gt4::hw::vulkan_loader::pixel_interlock_supported(physical);
        VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT interlockFeatures{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT};
        interlockFeatures.fragmentShaderPixelInterlock = VK_TRUE;
        const char* interlockExtension = VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME;
        VkDeviceCreateInfo dev{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dev.queueCreateInfoCount = 1;
        dev.pQueueCreateInfos = &queue;
        if (interlock) {
            dev.pNext = &interlockFeatures;
            dev.enabledExtensionCount = 1;
            dev.ppEnabledExtensionNames = &interlockExtension;
        }
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
    bool abe = false;        // PRIM.ABE: run the blend unit
    uint64_t alpha_reg = 0;  // ALPHA register (0x42), see pixel::encode_alpha_register
    bool pabe = false;       // skip the blend when source alpha MSB is 0
    bool colclamp = false;   // COLCLAMP: 0 wraps the low 8 bits
    bool fba = false;        // force the alpha byte to 0x80 on write
    bool dthe = false;       // dithering (16-bit frames)
    uint64_t dimx = 0;       // DIMX, the dither matrix
    bool date = false;       // DATE (destination alpha test)
    bool datm = false;       // DATE expected MSB
    PrimType type = PrimType::Triangle;
};

uint64_t Pack(uint64_t value, unsigned lo) { return value << lo; }

Batch MakeBatch(const Setup& s, const std::vector<Vertex>& vertices, const std::vector<PrimType>& prims) {
    Batch batch;
    batch.state.ctx.frame = Pack(s.fbp, 0) | Pack(s.fbw, 16) | Pack(s.fpsm, 24) | Pack(s.fbmsk, 32);
    batch.state.ctx.zbuf = Pack(s.zbp, 0) | Pack(s.zpsm, 24) | Pack(s.zmsk ? 1 : 0, 32);
    batch.state.ctx.test = Pack(s.ate ? 1 : 0, 0) | Pack(s.atst, 1) | Pack(s.aref, 4) | Pack(s.afail, 12) |
                           Pack(s.date ? 1 : 0, 14) | Pack(s.datm ? 1 : 0, 15) | Pack(s.zte ? 1 : 0, 16) |
                           Pack(s.ztst, 17);
    batch.state.ctx.alpha = s.alpha_reg;
    batch.state.ctx.fba = Pack(s.fba ? 1 : 0, 0);
    batch.state.env.pabe = s.pabe ? 1 : 0;
    batch.state.env.colclamp = s.colclamp ? 1 : 0;
    batch.state.env.dthe = s.dthe ? 1 : 0;
    batch.state.env.dimx = s.dimx;
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
           (s.fge ? 1ull << primbit::kFge : 0) | (s.abe ? 1ull << primbit::kAbe : 0) |
           (s.fst ? 1ull << primbit::kFst : 0);
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

// ---- M5: blend unit, alpha test, DATE, FBA/FBMASK/DIMX through the raster path ------------------
// Every case compares the GPU result with the CPU reference over the whole batch rectangle, like the
// M4 equivalence cases: the CPU side is the bit-exact pixel library of chapter 08.

uint64_t AlphaRegister(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint8_t fix) {
    return pixel::encode_alpha_register(static_cast<pixel::BlendSource>(a), static_cast<pixel::BlendSource>(b),
                                        static_cast<pixel::BlendFactor>(c), static_cast<pixel::BlendSource>(d), fix);
}

// 81 blend modes of chapter 08.2 (A, B, C, D over Cs/Cd/Zero and As/Ad/FIX), with COLCLAMP both ways.
// The destination is the deterministic frame pattern, so every mode blends against real destination
// colours and alphas.
int TestRasterBlendModes(gpu::GpuDrawer& drawer) {
    uint64_t compared = 0;
    uint64_t mismatches = 0;
    for (uint32_t clamp = 0; clamp < 2; ++clamp) {
        for (uint32_t a = 0; a < 3; ++a) {
            for (uint32_t b = 0; b < 3; ++b) {
                for (uint32_t c = 0; c < 3; ++c) {
                    for (uint32_t d = 0; d < 3; ++d) {
                        Setup s;
                        s.iip = false;
                        s.abe = true;
                        s.colclamp = clamp != 0;
                        s.alpha_reg = AlphaRegister(a, b, c, d, 0x40);
                        Batch batch = MakeBatch(s, FlatQuad(0, 0, 8, 8, 120, 60, 30, 0x90),
                                                {PrimType::Triangle, PrimType::Triangle});
                        CaseResult r = RunCase(drawer, s, batch, 21 + a * 27 + b * 9 + c * 3 + d);
                        if (r.gpuError) {
                            std::printf("hw_gpu_raster_blend: GPU error: %s\n", r.error.c_str());
                            return 1;
                        }
                        compared += r.compared;
                        mismatches += r.mismatches;
                    }
                }
            }
        }
    }
    std::printf("hw_gpu_raster_blend: compared %llu pixels over 162 modes, mismatches %llu\n",
                static_cast<unsigned long long>(compared), static_cast<unsigned long long>(mismatches));
    return mismatches == 0 ? 0 : 1;
}

// PABE: with PABE = 1 the blend is skipped while the source alpha MSB is 0 (08.2).
int TestRasterPabe(gpu::GpuDrawer& drawer) {
    int failures = 0;
    for (uint32_t alpha : {0x40u, 0xC0u}) {
        Setup s;
        s.iip = false;
        s.abe = true;
        s.pabe = true;
        s.alpha_reg = AlphaRegister(0, 1, 0, 1, 0x00);  // Cs*Ad + Cd*(1-Ad)
        Batch batch = MakeBatch(s, FlatQuad(0, 0, 8, 8, 200, 40, 90, static_cast<uint8_t>(alpha)),
                                {PrimType::Triangle, PrimType::Triangle});
        failures += Report("hw_gpu_raster_pabe", RunCase(drawer, s, batch, 31 + alpha));
    }
    return failures ? 1 : 0;
}

// Alpha test: the eight comparisons of 08.6 and the four AFAIL actions, with alphas on both sides of
// the reference so both the pass and the fail path run.
int TestRasterAlphaTest(gpu::GpuDrawer& drawer) {
    uint64_t compared = 0;
    uint64_t mismatches = 0;
    for (uint32_t atst = 0; atst < 8; ++atst) {
        for (uint32_t afail = 0; afail < 4; ++afail) {
            Setup s;
            s.iip = false;
            s.ate = true;
            s.atst = atst;
            s.afail = afail;
            s.aref = 0x50;
            s.zte = true;
            s.ztst = 1;
            std::vector<Vertex> quad = FlatQuad(0, 0, 8, 8, 70, 80, 90, 0x30);
            std::vector<Vertex> other = FlatQuad(8, 0, 16, 8, 20, 30, 40, 0x70);
            quad.insert(quad.end(), other.begin(), other.end());
            Batch batch = MakeBatch(s, quad, {PrimType::Triangle, PrimType::Triangle, PrimType::Triangle,
                                              PrimType::Triangle});
            CaseResult r = RunCase(drawer, s, batch, 41 + atst * 4 + afail);
            if (r.gpuError) {
                std::printf("hw_gpu_raster_atest: GPU error: %s\n", r.error.c_str());
                return 1;
            }
            compared += r.compared;
            mismatches += r.mismatches;
        }
    }
    std::printf("hw_gpu_raster_atest: compared %llu pixels over 32 combinations, mismatches %llu\n",
                static_cast<unsigned long long>(compared), static_cast<unsigned long long>(mismatches));
    return mismatches == 0 ? 0 : 1;
}

// DATE (08.7): the destination alpha MSB decides whether a pixel is written. A first quad writes,
// a second one with the opposite alpha tests against the pixels the first one produced.
int TestRasterDate(gpu::GpuDrawer& drawer) {
    uint64_t compared = 0;
    uint64_t mismatches = 0;
    for (uint32_t datm = 0; datm < 2; ++datm) {
        Setup s;
        s.iip = false;
        s.date = true;
        s.datm = datm != 0;
        std::vector<Vertex> quad = FlatQuad(0, 0, 8, 8, 90, 40, 10, 0x80);
        std::vector<Vertex> inner = FlatQuad(2, 2, 6, 6, 10, 200, 30, 0x40);
        quad.insert(quad.end(), inner.begin(), inner.end());
        Batch batch = MakeBatch(s, quad, {PrimType::Triangle, PrimType::Triangle, PrimType::Triangle,
                                          PrimType::Triangle});
        CaseResult r = RunCase(drawer, s, batch, 51 + datm);
        if (r.gpuError) {
            std::printf("hw_gpu_raster_date: GPU error: %s\n", r.error.c_str());
            return 1;
        }
        compared += r.compared;
        mismatches += r.mismatches;
    }
    std::printf("hw_gpu_raster_date: compared %llu pixels, mismatches %llu\n",
                static_cast<unsigned long long>(compared), static_cast<unsigned long long>(mismatches));
    return mismatches == 0 ? 0 : 1;
}

// FBA (alpha forced to 0x80), FBMSK on a CT24 target and DIMX dithering on a CT16 target (05.6, 08.8).
int TestRasterFrameWrites(gpu::GpuDrawer& drawer) {
    int failures = 0;
    {
        Setup s;
        s.iip = false;
        s.fba = true;
        Batch batch = MakeBatch(s, FlatQuad(0, 0, 8, 8, 30, 60, 90, 0x12),
                                {PrimType::Triangle, PrimType::Triangle});
        failures += Report("hw_gpu_raster_fba", RunCase(drawer, s, batch, 61));
    }
    {
        Setup s;
        s.iip = false;
        s.fpsm = 1;  // CT24
        s.fbmsk = 0x00FFFFFF;
        Batch batch = MakeBatch(s, FlatQuad(0, 0, 8, 8, 240, 120, 60, 0xFF),
                                {PrimType::Triangle, PrimType::Triangle});
        failures += Report("hw_gpu_raster_fbmsk", RunCase(drawer, s, batch, 62));
    }
    {
        Setup s;
        s.iip = false;
        s.fpsm = 2;  // CT16
        s.dthe = true;
        s.dimx = 0x13579BDF2468ACE0ull & 0xFFFFFFFFFFFFull;  // 16 four-bit entries, values -4..+3
        Batch batch = MakeBatch(s, FlatQuad(0, 0, 8, 8, 130, 70, 210, 0xC0),
                                {PrimType::Triangle, PrimType::Triangle});
        failures += Report("hw_gpu_raster_dither", RunCase(drawer, s, batch, 63));
    }
    return failures ? 1 : 0;
}

// ---- M6 interface: present scheduler, auxiliary frames, per-pass timings ------------------------

// Default policy: a frame end is presented once and then re-presented on later ticks without a queue.
int TestPresentScheduler() {
    present::Scheduler s;
    if (s.Tick() != present::Action::none) {
        std::printf("hw_present_scheduler: a tick without any frame presented something\n");
        return 1;
    }
    present::FrameStamp first;
    first.guestFrame = 7;
    first.hasImage = true;
    s.SubmitFrame(first);
    if (s.Tick() != present::Action::presentNew || s.lastShown().guestFrame != 7) {
        std::printf("hw_present_scheduler: the submitted frame was not presented\n");
        return 1;
    }
    if (s.Tick() != present::Action::presentHeld) {
        std::printf("hw_present_scheduler: the held image was not re-presented\n");
        return 1;
    }
    if (s.presentedNew() != 1 || s.presentedHeld() != 1 || s.dropped() != 0) {
        std::printf("hw_present_scheduler: counters %llu/%llu/%llu\n",
                    static_cast<unsigned long long>(s.presentedNew()),
                    static_cast<unsigned long long>(s.presentedHeld()),
                    static_cast<unsigned long long>(s.dropped()));
        return 1;
    }
    // A frame end without a guest image still reaches the display (chapter 12.4 shows black).
    present::FrameStamp blank;
    blank.guestFrame = 8;
    blank.hasImage = false;
    s.SubmitFrame(blank);
    if (s.Tick() != present::Action::presentNew || s.hasImage()) {
        std::printf("hw_present_scheduler: an image-less frame end was mishandled\n");
        return 1;
    }
    std::printf("hw_present_scheduler: new/held/dropped = 2/1/0, newest frame %llu\n",
                static_cast<unsigned long long>(s.lastShown().guestFrame));
    return 0;
}

// Holding disabled: nothing is presented until a new frame end arrives, and a backlog keeps only the
// newest frame (never a queue).
int TestPresentSchedulerHold() {
    present::Scheduler s;
    present::Policy policy;
    policy.hold_last_image = false;
    s.SetPolicy(policy);
    present::FrameStamp stamp;
    stamp.hasImage = true;
    stamp.guestFrame = 1;
    s.SubmitFrame(stamp);
    if (s.Tick() != present::Action::presentNew) {
        std::printf("hw_present_scheduler_hold: first frame not presented\n");
        return 1;
    }
    if (s.Tick() != present::Action::none) {
        std::printf("hw_present_scheduler_hold: presented again with holding disabled\n");
        return 1;
    }
    stamp.guestFrame = 2;
    s.SubmitFrame(stamp);
    stamp.guestFrame = 3;
    s.SubmitFrame(stamp);
    if (s.dropped() != 1) {
        std::printf("hw_present_scheduler_hold: backlog was not collapsed (dropped %llu)\n",
                    static_cast<unsigned long long>(s.dropped()));
        return 1;
    }
    if (s.Tick() != present::Action::presentNew || s.lastShown().guestFrame != 3) {
        std::printf("hw_present_scheduler_hold: the newest frame did not win\n");
        return 1;
    }
    std::printf("hw_present_scheduler_hold: hold=none, newest wins, 1 dropped\n");
    return 0;
}

// Host rate above the guest rate: two ticks per guest frame give one new present and one re-present,
// and the late-input hook runs on every tick.
int TestPresentSchedulerRate() {
    present::Scheduler s;
    int hookCalls = 0;
    s.SetLateInputHook([](void* context) { ++*static_cast<int*>(context); }, &hookCalls);
    uint64_t newPresents = 0;
    uint64_t heldPresents = 0;
    for (uint64_t frame = 10; frame < 13; ++frame) {
        present::FrameStamp stamp;
        stamp.guestFrame = frame;
        stamp.hasImage = true;
        s.SubmitFrame(stamp);
        for (int tick = 0; tick < 2; ++tick) {
            const present::Action action = s.Tick();
            if (action == present::Action::presentNew) ++newPresents;
            if (action == present::Action::presentHeld) ++heldPresents;
        }
    }
    if (newPresents != 3 || heldPresents != 3) {
        std::printf("hw_present_scheduler_rate: %llu new / %llu held at 2x\n",
                    static_cast<unsigned long long>(newPresents),
                    static_cast<unsigned long long>(heldPresents));
        return 1;
    }
    if (hookCalls != 6) {
        std::printf("hw_present_scheduler_rate: late input hook ran %d times, expected 6\n", hookCalls);
        return 1;
    }
    std::printf("hw_present_scheduler_rate: 3 frames over 6 ticks = 3 new + 3 held, 6 late-input calls\n");
    return 0;
}

// GIF packet helpers for the auxiliary-frame test: one A+D packet writing registers, then an IMAGE
// packet carrying host->local data (chapter 02.1).
Qword PackedTag(uint32_t nloop, bool eop, uint32_t format, const std::vector<uint8_t>& regs) {
    Qword tag;
    tag.lo = uint64_t{nloop} | (uint64_t{eop ? 1u : 0u} << 15) | (uint64_t{format} << 58);
    tag.hi = 0;
    for (size_t i = 0; i < regs.size() && i < 16; ++i) {
        tag.hi |= uint64_t{regs[i]} << (4 * i);
    }
    if (!regs.empty()) {
        tag.lo |= uint64_t{static_cast<uint32_t>(regs.size())} << 60;
    }
    return tag;
}

// Uploads `bytes` (qword multiple) to GS page 0 with an 8x8 CT32 transfer.
void UploadToPage0(writeback::Renderer& renderer, uint32_t value) {
    std::vector<Qword> stream;
    stream.push_back(PackedTag(4, false, 0, {0xE}));
    const uint64_t bitbltbuf = (uint64_t{1} << 16) | (uint64_t{1} << 48);  // SBW = DBW = 1, CT32
    const uint64_t trxpos = 0;
    const uint64_t trxreg = uint64_t{8} | (uint64_t{8} << 32);
    stream.push_back(Qword{bitbltbuf, 0x50});
    stream.push_back(Qword{trxpos, 0x51});
    stream.push_back(Qword{trxreg, 0x52});
    stream.push_back(Qword{0, 0x53});  // TRXDIR = 0: host -> local
    const uint32_t qwords = 16;        // 8 x 8 CT32 = 256 bytes
    stream.push_back(PackedTag(qwords, true, 2, {}));
    for (uint32_t i = 0; i < qwords; ++i) {
        Qword data;
        data.lo = uint64_t{value} | (uint64_t{value} << 32);
        data.hi = uint64_t{value} | (uint64_t{value} << 32);
        stream.push_back(data);
    }
    renderer.GifTransfer(1, reinterpret_cast<const uint8_t*>(stream.data()), stream.size() * sizeof(Qword));
    renderer.Flush();
}

uint64_t SlmHash(const writeback::Renderer& renderer) {
    const std::uint8_t* bytes = renderer.Slm();
    uint64_t hash = 1469598103934665603ull;  // FNV-1a
    for (size_t i = 0; i < 4u * 1024u * 1024u; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

// Requirement 2: an auxiliary frame may draw, upload and clear, but the guest's memory is restored and
// a guest readback is never answered from it.
int TestAuxFrameGuestUntouched() {
    writeback::Renderer renderer;
    UploadToPage0(renderer, 0x11223344u);
    const uint64_t before = SlmHash(renderer);
    if (!renderer.BeginAuxiliaryFrame()) {
        std::printf("hw_aux_frame_guest_untouched: begin refused\n");
        return 1;
    }
    UploadToPage0(renderer, 0xAABBCCDDu);
    if (SlmHash(renderer) == before) {
        std::printf("hw_aux_frame_guest_untouched: the auxiliary upload changed nothing (test is blind)\n");
        renderer.EndAuxiliaryFrame();
        return 1;
    }
    std::uint8_t scratch[64] = {};
    if (renderer.ReadFifo(scratch, 4) != 0 || renderer.GetCounters().auxiliaryReadRefused != 1) {
        std::printf("hw_aux_frame_guest_untouched: a readback during an auxiliary frame was answered\n");
        renderer.EndAuxiliaryFrame();
        return 1;
    }
    renderer.EndAuxiliaryFrame();
    if (SlmHash(renderer) != before) {
        std::printf("hw_aux_frame_guest_untouched: guest memory differs after the auxiliary frame\n");
        return 1;
    }
    if (renderer.AuxiliaryActive() || renderer.GetCounters().auxiliaryFrames != 1) {
        std::printf("hw_aux_frame_guest_untouched: state not restored\n");
        return 1;
    }
    if (!renderer.BeginAuxiliaryFrame() || renderer.BeginAuxiliaryFrame()) {
        std::printf("hw_aux_frame_guest_untouched: nested auxiliary frame was accepted\n");
        return 1;
    }
    renderer.EndAuxiliaryFrame();
    std::printf("hw_aux_frame_guest_untouched: guest SLM identical, readback refused, nested begin refused\n");
    return 0;
}

// Requirement 4: per-pass CPU timers exist and are honest. Without a Vulkan device the drawer refuses
// the batch before any GPU work, so nothing is timed and the failure is reported instead of a fake zero
// being presented as a measurement. The compose pass is measured on its own.
int TestPassTimings() {
    writeback::Renderer renderer;
    UploadToPage0(renderer, 0x55AA55AAu);
    writeback::FrameOutput frame;
    renderer.ComposeFrame(frame);
    const writeback::PassTimings timings = renderer.Timings();  // copy: a later flush overwrites it
    if (!(timings.composeUs > 0.0)) {
        std::printf("hw_pass_timings: compose was not measured (%.3f us)\n", timings.composeUs);
        return 1;
    }
    if (timings.executeGpuUs != 0.0) {
        std::printf("hw_pass_timings: the GPU field is reserved and must stay 0\n");
        return 1;
    }

    // A draw without a device must fail cleanly and leave the draw timers at zero.
    std::vector<Qword> draw;
    draw.push_back(PackedTag(7, true, 0, {0xE}));
    draw.push_back(Qword{3, 0x00});                                          // PRIM = triangle
    draw.push_back(Qword{uint64_t{639} << 16 | uint64_t{447} << 48, 0x40});  // SCISSOR_1
    draw.push_back(Qword{uint64_t{1} << 16, 0x4C});                          // FRAME_1: FBW 1
    draw.push_back(Qword{0, 0x18});                                          // XYOFFSET_1
    draw.push_back(Qword{uint64_t{20} | (uint64_t{20} << 16) | (uint64_t{0x100} << 32), 0x05});
    draw.push_back(Qword{uint64_t{80} | (uint64_t{20} << 16) | (uint64_t{0x100} << 32), 0x05});
    draw.push_back(Qword{uint64_t{20} | (uint64_t{80} << 16) | (uint64_t{0x100} << 32), 0x05});
    renderer.GifTransfer(1, reinterpret_cast<const std::uint8_t *>(draw.data()), draw.size() * sizeof(Qword));
    renderer.Flush();
    if (!renderer.Failed() || renderer.LastError().empty()) {
        std::printf("hw_pass_timings: a draw without a device was not reported as a failure\n");
        return 1;
    }
    const writeback::PassTimings afterDraw = renderer.Timings();
    if (afterDraw.translateUs != 0.0 || afterDraw.executeUs != 0.0) {
        std::printf("hw_pass_timings: draw timers moved without a device (%.3f/%.3f us)\n",
                    afterDraw.translateUs, afterDraw.executeUs);
        return 1;
    }
    if (renderer.TimedFrames() < 2) {
        std::printf("hw_pass_timings: %llu timed frames, expected at least 2\n",
                    static_cast<unsigned long long>(renderer.TimedFrames()));
        return 1;
    }
    std::printf("hw_pass_timings: compose %.1f us, draw timers 0 without a device, %llu timed frames\n",
                timings.composeUs, static_cast<unsigned long long>(renderer.TimedFrames()));
    return 0;
}

// IN-335: the frame and depth buffers may share local memory (a 2D pass whose unused ZBUF points at the
// other display buffer, or at the frame itself). The write-back must not put the pre-draw depth words of
// the rectangle back over the pixels the batch just drew. The GPU result must equal the CPU reference,
// which stores the frame word and then the depth word per pixel. Cases: ZBUF = FRAME with Z writes
// masked, ZBUF = FRAME with Z written, and a ZBUF that starts one page row (32 lines) above FRAME, so
// only the lower rows of the depth rectangle alias the upper rows of the drawn frame rectangle (rows the
// write-back visits earlier, which is what exposes a stale depth word written over them).
// Z written into a buffer that aliases the frame is reported, not asserted: Z32 and CT32 swizzle the same
// page differently, so the last word stored at an address depends on the order in which the rasteriser
// visits pixels (CPU reference: primitive by primitive; write-back: one raster pass over the rectangle),
// which the specification does not define (IN-335).
int TestWritebackAliasedDepth(gpu::GpuDrawer& drawer) {
    struct AliasCase {
        const char* label;
        int32_t zbpOffsetPages;
        bool zmsk;
        bool asserted;
    };
    const AliasCase cases[] = {{"same pages, Z masked", 0, true, true},
                               {"same pages, Z written (report only)", 0, false, false},
                               {"one page row above, Z masked", -10, true, true}};
    int failures = 0;
    for (const AliasCase& c : cases) {
        Setup s;
        s.iip = false;
        s.fbp = 30;
        s.zbp = static_cast<uint32_t>(static_cast<int32_t>(s.fbp) + c.zbpOffsetPages);  // FBW 10: 10 pages per row
        s.zmsk = c.zmsk;
        s.zte = true;
        s.ztst = 1;  // ALWAYS
        Batch batch = MakeBatch(s, FlatQuad(4, 20, 60, 60, 200, 30, 90, 255), {PrimType::Triangle, PrimType::Triangle});
        const CaseResult r = RunCase(drawer, s, batch, static_cast<uint32_t>(41 + c.zbpOffsetPages));
        std::printf("  case: %s\n", c.label);
        const int failed = Report("hw_gpu_writeback_aliased_depth", r);
        if (c.asserted || r.gpuError) failures += failed;
    }
    return failures == 0 ? 0 : 1;
}

// Chapter 10.2: a LOCAL->HOST read observes every earlier draw, including draws still waiting in the open
// batch, and the data is consumed sequentially across FIFO reads. Two sprites of different colours fill
// rows 0..3 and 4..7 of an 8 x 8 CT32 rectangle; it is read back in two halves of 128 bytes with no VBlank
// flush in between, so the first half must hold only the top colour and the second only the bottom one.
int TestReadbackObservesPendingDraw(const gpu::VulkanHandles& handles) {
    writeback::Renderer renderer;
    std::string error;
    if (!renderer.Init(handles, error)) {
        std::printf("hw_readback_pending_draw: GPU drawer init failed: %s\n", error.c_str());
        return 1;
    }
    const uint32_t top = 0x80402010u;     // A 0x80, B 0x40, G 0x20, R 0x10
    const uint32_t bottom = 0x7F0A0B0Cu;  // A 0x7F, B 0x0A, G 0x0B, R 0x0C
    auto rgbaq = [](uint32_t colour) { return uint64_t{colour} | (uint64_t{0x3F800000u} << 32); };  // Q = 1.0
    auto xyz = [](uint32_t x, uint32_t y) { return uint64_t{x * 16} | (uint64_t{y * 16} << 16); };
    std::vector<Qword> stream;
    stream.push_back(PackedTag(16, true, 0, {0xE}));
    stream.push_back(Qword{uint64_t{0} | (uint64_t{1} << 16), 0x4C});            // FRAME_1: FBP 0, FBW 1, CT32
    stream.push_back(Qword{uint64_t{64} | (uint64_t{1} << 32), 0x4E});           // ZBUF_1: ZBP 64, ZMSK
    stream.push_back(Qword{(uint64_t{63} << 16) | (uint64_t{63} << 48), 0x40});  // SCISSOR_1: 0..63 x 0..63
    stream.push_back(Qword{0, 0x18});                                             // XYOFFSET_1 = 0
    stream.push_back(Qword{(uint64_t{1} << 16) | (uint64_t{1} << 17), 0x47});    // TEST_1: ZTE, ZTST ALWAYS
    stream.push_back(Qword{6, 0x00});                                             // PRIM: sprite, context 1
    stream.push_back(Qword{rgbaq(top), 0x01});
    stream.push_back(Qword{xyz(0, 0), 0x05});
    stream.push_back(Qword{xyz(8, 4), 0x05});  // kick: rows 0..3
    stream.push_back(Qword{rgbaq(bottom), 0x01});
    stream.push_back(Qword{xyz(0, 4), 0x05});
    stream.push_back(Qword{xyz(8, 8), 0x05});                                     // kick: rows 4..7
    stream.push_back(Qword{uint64_t{0} | (uint64_t{1} << 16), 0x50});             // BITBLTBUF: SBP 0, SBW 1, CT32
    stream.push_back(Qword{0, 0x51});                                             // TRXPOS 0, 0
    stream.push_back(Qword{uint64_t{8} | (uint64_t{8} << 32), 0x52});             // TRXREG 8 x 8
    stream.push_back(Qword{1, 0x53});                                             // TRXDIR 1: local -> host
    if (!renderer.GifTransfer(1, reinterpret_cast<const uint8_t*>(stream.data()), stream.size() * sizeof(Qword))) {
        std::printf("hw_readback_pending_draw: GIF transfer refused\n");
        return 1;
    }
    std::vector<uint32_t> words(64, 0);
    const std::size_t first = renderer.ReadFifo(words.data(), 128);
    const std::size_t second = renderer.ReadFifo(words.data() + 32, 128);
    uint32_t pastEnd[4] = {};
    const std::size_t third = renderer.ReadFifo(pastEnd, sizeof(pastEnd));  // past the end: zeros, one overrun
    uint32_t wrong = 0;
    for (size_t i = 0; i < 32; ++i) wrong += words[i] != top ? 1u : 0u;
    for (size_t i = 32; i < 64; ++i) wrong += words[i] != bottom ? 1u : 0u;
    std::printf("hw_readback_pending_draw: read %zu + %zu bytes (+%zu past the end), %u of 64 words wrong, "
                "words 0/32 = %08X/%08X, overruns %llu\n",
                first, second, third, wrong, words[0], words[32],
                static_cast<unsigned long long>(renderer.GetCounters().fifoOverruns));
    if (renderer.Failed()) {
        std::printf("hw_readback_pending_draw: renderer failed: %s\n", renderer.LastError().c_str());
        return 1;
    }
    return (first == 128 && second == 128 && wrong == 0 && renderer.GetCounters().fifoOverruns == 1) ? 0 : 1;
}

// IN-335 replay control: a double-buffered 2D scene in which every frame draws into the back buffer
// while ZBUF (Z writes masked) stays at page 0, so on every other frame the unused depth buffer shares
// the pages of the buffer being drawn: the pattern of GT4's boot and menu screens (ZBUF = 0x100000000).
// The legacy capture is replayed through the CPU reference and through the GPU write-back renderer; every
// frame must give the same composed image and the same 4 MiB local memory hash. Only sprites are drawn,
// whose coverage and attributes both rasterisers are specified to agree on exactly, so any difference is
// a write-back or flush defect. Before the IN-335 fix the frames drawn into page 0 lost part of their draws here.
int TestReplayParityDoubleBuffer(const gpu::VulkanHandles& handles) {
    constexpr uint32_t kFrames = 6;
    constexpr uint32_t kBufferPages[2] = {0, 2};  // 64 x 64 CT32 buffers, FBW 1: two pages each
    cpuref::CaptureWriter capture;
    capture.Privileged(0x12000000u, 1u | (1u << 5) | (0xFFu << 8));                // PMODE: circuit 1, ALP 0xFF
    capture.Privileged(0x12000010u, uint64_t{2} << 13);                             // SMODE1: CMOD 2 (NTSC)
    capture.Privileged(0x12000080u, (uint64_t{63} << 32) | (uint64_t{63} << 44));  // DISPLAY1: 64 x 64
    auto rgbaq = [](uint32_t colour) { return uint64_t{colour} | (uint64_t{0x3F800000u} << 32); };  // Q = 1.0
    auto xyz = [](uint32_t x, uint32_t y) { return uint64_t{x * 16} | (uint64_t{y * 16} << 16); };
    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        const uint32_t back = kBufferPages[frame & 1u];
        const uint32_t background = 0x80000000u | (frame * 0x101010u + 0x203040u);
        const uint32_t text = 0x60000000u | (0xF0E0D0u - frame * 0x010203u);
        // A+D pairs: data, register address.
        const std::vector<uint64_t> words = {
            uint64_t{back} | (uint64_t{1} << 16), 0x4C,                         // FRAME_1: back buffer, FBW 1, CT32
            uint64_t{0} | (uint64_t{1} << 32), 0x4E,                            // ZBUF_1: page 0, ZMSK
            (uint64_t{63} << 16) | (uint64_t{63} << 48), 0x40,                  // SCISSOR_1: 0..63 x 0..63
            0, 0x18,                                                            // XYOFFSET_1 = 0
            (uint64_t{1} << 16) | (uint64_t{1} << 17), 0x47,                    // TEST_1: ZTE, ZTST ALWAYS
            (uint64_t{1} << 2) | (uint64_t{1} << 6), 0x42,                      // ALPHA_1: (Cs - Cd) * As + Cd
            6, 0x00,                                                            // PRIM: sprite
            rgbaq(background), 0x01, xyz(0, 0), 0x05, xyz(64, 64), 0x05,       // full-buffer clear
            6u | (1u << 6), 0x00,                                               // PRIM: sprite, ABE
            rgbaq(text), 0x01, xyz(4 + frame, 8), 0x05, xyz(40 + frame, 14), 0x05,  // a "text line"
        };
        capture.Gif(1, cpuref::CaptureWriter::Packet(static_cast<uint32_t>(words.size() / 2), true, false, 0, 0,
                                                     {0xE}, words));
        capture.Privileged(0x12000070u, uint64_t{back} | (uint64_t{1} << 9));  // DISPFB1: show the back buffer
        capture.FrameEnd(frame);
    }

    struct FrameRecord {
        bool valid = false;
        std::vector<uint8_t> rgb;
        uint64_t slmHash = 0;
    };
    std::vector<FrameRecord> cpuFrames;
    std::vector<FrameRecord> gpuFrames;
    auto record = [](std::vector<FrameRecord>& out, const cpuref::FrameImage& image, const uint8_t* slm) {
        FrameRecord r;
        r.valid = image.valid;
        if (image.valid) {
            for (const display::Rgba8& p : image.image.pixels) {
                r.rgb.push_back(p.r);
                r.rgb.push_back(p.g);
                r.rgb.push_back(p.b);
            }
        }
        r.slmHash = cpuref::HashLocalMemory(slm, 4u * 1024u * 1024u);
        out.push_back(std::move(r));
    };

    std::string error;
    cpuref::CpuRenderer cpu;
    cpuref::ReplaySummary cpuSummary;
    if (!cpuref::ReplayCapture(capture.Bytes(), cpu, cpuSummary,
                               [&](uint64_t, const cpuref::FrameImage& image) {
                                   record(cpuFrames, image, cpu.Memory().Local().bytes().data());
                               },
                               error)) {
        std::printf("hw_replay_parity_double_buffer: CPU replay failed: %s\n", error.c_str());
        return 1;
    }
    writeback::Renderer renderer;
    if (!renderer.Init(handles, error)) {
        std::printf("hw_replay_parity_double_buffer: GPU drawer init failed: %s\n", error.c_str());
        return 1;
    }
    writeback::WritebackReplayTarget target(renderer);
    cpuref::ReplaySummary gpuSummary;
    const bool replayed = cpuref::ReplayCapture(capture.Bytes(), target, gpuSummary,
                                                [&](uint64_t, const cpuref::FrameImage& image) {
                                                    record(gpuFrames, image, renderer.Slm());
                                                },
                                                error);
    if (!replayed || renderer.Failed()) {
        std::printf("hw_replay_parity_double_buffer: GPU replay failed: %s %s\n", error.c_str(),
                    renderer.LastError().c_str());
        return 1;
    }
    if (cpuFrames.size() != kFrames || gpuFrames.size() != kFrames) {
        std::printf("hw_replay_parity_double_buffer: frame counts %zu / %zu, expected %u\n", cpuFrames.size(),
                    gpuFrames.size(), kFrames);
        return 1;
    }
    int failures = 0;
    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        const FrameRecord& c = cpuFrames[frame];
        const FrameRecord& g = gpuFrames[frame];
        const bool imageEqual = c.valid == g.valid && c.rgb == g.rgb;
        const bool memoryEqual = c.slmHash == g.slmHash;
        std::printf("  frame %u (%s): image %s (valid %d/%d, %zu bytes), memory %016llx / %016llx %s\n", frame,
                    (frame & 1u) ? "odd" : "even", imageEqual ? "equal" : "DIFFERENT", c.valid ? 1 : 0,
                    g.valid ? 1 : 0, c.rgb.size(), static_cast<unsigned long long>(c.slmHash),
                    static_cast<unsigned long long>(g.slmHash), memoryEqual ? "equal" : "DIFFERENT");
        if (!imageEqual || !memoryEqual) ++failures;
        // The fixture must not be blind: every frame changes memory and shows a non-empty image.
        if (frame > 0 && c.slmHash == cpuFrames[frame - 1].slmHash) {
            std::printf("  frame %u left memory unchanged (fixture is blind)\n", frame);
            ++failures;
        }
        if (!c.valid || c.rgb.empty()) {
            std::printf("  frame %u has no composed image (fixture is blind)\n", frame);
            ++failures;
        }
    }
    std::printf("hw_replay_parity_double_buffer: %u frames, %d failures\n", kFrames, failures);
    return failures == 0 ? 0 : 1;
}

// M6 output stage (13.3 step 3, 13.4, 12.9): the presentation compose follows the VBlank field, the
// reference compose (the donor-matching `vram` image) never does. A static 64 x 32 field buffer (one colour
// per row) is shown through a 480i NTSC circuit with FFMD = 1, so every source row covers two output rows.
//   - Reference: identical for even and odd fields; no deinterlace counters move.
//   - Deinterlace off: the presentation equals the reference for both fields.
//   - Weave over an even/odd pair: the odd field is shifted down one row (13.3 step 3) and woven, which
//     for static content must rebuild exactly the line-doubled reference image (no one-line bob).
//   - Weave of two field buffers (each with its own lines) gives the progressive frame (IN-339 phase).
//   - Bob: the CSR.FIELD = 1 field is shifted down one row (13.4, IN-339); the other is not.
// No Vulkan device is needed: uploads go to the shadow memory and composition is CPU work.
int TestPresentationFieldPhase() {
    constexpr uint32_t kWidth = 64;
    constexpr uint32_t kRows = 32;
    // Colour of progressive frame line `line` (448-line frame; field buffers hold every other line).
    auto lineColour = [](uint32_t line) {
        return 0x80000000u | ((line * 7u + 3u) & 0xFFu) | (((line * 13u) & 0xFFu) << 8);
    };
    // Field buffer: row k holds frame line 2k + fieldLine (fieldLine 0 = even field, 1 = odd field).
    auto upload = [&](writeback::Renderer& renderer, uint32_t fieldLine = 0) {
        std::vector<Qword> stream;
        stream.push_back(PackedTag(4, false, 0, {0xE}));
        stream.push_back(Qword{(uint64_t{1} << 16) | (uint64_t{1} << 48), 0x50});  // BITBLTBUF: DBP 0, DBW 1, CT32
        stream.push_back(Qword{0, 0x51});                                           // TRXPOS 0, 0
        stream.push_back(Qword{uint64_t{kWidth} | (uint64_t{kRows} << 32), 0x52});  // TRXREG 64 x 32
        stream.push_back(Qword{0, 0x53});                                           // TRXDIR 0: host -> local
        const uint32_t qwords = kWidth * kRows / 4;
        stream.push_back(PackedTag(qwords, true, 2, {}));
        for (uint32_t i = 0; i < qwords; ++i) {
            const uint32_t row = (i * 4) / kWidth;
            const uint32_t colour = lineColour(2 * row + fieldLine);
            stream.push_back(Qword{uint64_t{colour} | (uint64_t{colour} << 32), uint64_t{colour} | (uint64_t{colour} << 32)});
        }
        renderer.GifTransfer(1, reinterpret_cast<const uint8_t*>(stream.data()), stream.size() * sizeof(Qword));
    };
    auto setup = [&](writeback::Renderer& renderer, display::DeinterlaceMode mode) {
        writeback::OutputSettings settings;
        settings.deinterlace = mode;
        renderer.SetOutputSettings(settings);
        upload(renderer);
        renderer.WritePrivileged(0x12000000u, 1u | (1u << 5) | (0xFFu << 8));  // PMODE: RC1, MMOD 1, ALP 255
        renderer.WritePrivileged(0x12000010u, uint64_t{2} << 13);              // SMODE1: NTSC
        renderer.WritePrivileged(0x12000020u, 3u);                             // SMODE2: INT, FFMD
        renderer.WritePrivileged(0x12000060u, 1u);                             // SYNCV: interlaced signal (13.1)
        renderer.WritePrivileged(0x12000070u, uint64_t{1} << 9);               // DISPFB1: FBP 0, FBW 1, CT32
        renderer.WritePrivileged(0x12000080u, (uint64_t{3} << 23) | (uint64_t{4 * kWidth - 1} << 32) |
                                                  (uint64_t{2 * kRows - 1} << 44));  // DISPLAY1: MAGH 3, 64 x 64
    };
    auto frame = [](writeback::Renderer& renderer, bool odd, writeback::ComposeKind kind) {
        renderer.SetDisplayField(odd);
        renderer.Flush();
        return renderer.Compose(kind);
    };
    using writeback::ComposeKind;
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s: %s\n", what, ok ? "ok" : "FAILED");
        if (!ok) ++failures;
    };

    writeback::Renderer off;
    setup(off, display::DeinterlaceMode::Off);
    const writeback::FrameOutput refEven = frame(off, false, ComposeKind::Reference);
    const writeback::FrameOutput refOdd = frame(off, true, ComposeKind::Reference);
    const writeback::FrameOutput offEven = frame(off, false, ComposeKind::Presentation);
    const writeback::FrameOutput offOdd = frame(off, true, ComposeKind::Presentation);
    std::printf("  reference %ux%u valid %d\n", refEven.width, refEven.height, refEven.valid ? 1 : 0);
    check(refEven.valid && refEven.height >= 2 * kRows, "reference image exists and is full height");
    check(refEven.pixels == refOdd.pixels, "reference is independent of the field");
    check(offEven.pixels == refEven.pixels && offOdd.pixels == refEven.pixels, "deinterlace off presents the reference");
    check(off.GetCounters().deinterlacedFrames == 0 && off.GetCounters().interlaceOffsetFrames == 0,
          "deinterlace off moves no deinterlace counter");

    writeback::Renderer weave;
    setup(weave, display::DeinterlaceMode::Weave);
    frame(weave, false, ComposeKind::Presentation);
    const writeback::FrameOutput woven = frame(weave, true, ComposeKind::Presentation);
    check(woven.pixels == refEven.pixels, "weave of an even/odd pair rebuilds the line-doubled image");
    check(weave.GetCounters().interlaceOffsetFrames == 1 && weave.GetCounters().deinterlacedFrames == 2,
          "weave: one field offset (the CSR.FIELD = 0 field), two deinterlaced fields");

    // Field phase (IN-339): each field buffer holds its own lines, as GT4's field rendering does. The frame
    // whose display field is CSR.FIELD = 1 holds the upper lines (2k), the CSR.FIELD = 0 frame the lower
    // lines (2k + 1). Weaving the pair must give the progressive frame, line y = lineColour(y); the IN-35
    // reading (field 1 shifted down) puts every line two rows from its partner and fails here.
    writeback::Renderer phase;
    setup(phase, display::DeinterlaceMode::Weave);
    upload(phase, 1);
    frame(phase, false, ComposeKind::Presentation);
    upload(phase, 0);
    const writeback::FrameOutput progressive = frame(phase, true, ComposeKind::Presentation);
    bool progressiveOk = progressive.valid && progressive.height >= 2 * kRows && progressive.width >= kWidth;
    uint32_t wrongRows = 0;
    for (uint32_t y = 0; progressiveOk && y < 2 * kRows; ++y) {
        const uint32_t expected = lineColour(y) & 0x00FFFFFFu;
        const uint32_t got = progressive.pixels[static_cast<size_t>(y) * progressive.width] & 0x00FFFFFFu;
        if (got != expected) ++wrongRows;
    }
    std::printf("  progressive weave: %u of %u rows wrong\n", wrongRows, 2 * kRows);
    check(progressiveOk && wrongRows == 0, "weave of two field buffers gives the progressive frame");

    writeback::Renderer bob;
    setup(bob, display::DeinterlaceMode::Bob);
    // 13.4 bob shifts the row-0 field (CSR.FIELD = 1, IN-339) down by one row and leaves the other as is.
    const writeback::FrameOutput bobUpper = frame(bob, true, ComposeKind::Presentation);
    const writeback::FrameOutput bobLower = frame(bob, false, ComposeKind::Presentation);
    check(bobUpper.valid && bobUpper.pixels != refEven.pixels, "bob shifts the CSR.FIELD = 1 field by one row");
    check(bobLower.pixels == refEven.pixels, "bob leaves the CSR.FIELD = 0 field in place");

    std::printf("hw_presentation_field_phase: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}

// M7 gate (15.2: cache mode equals write-back mode), IN-340. A synthetic capture drives both sync modes of the
// GPU renderer through the same replay reader; every frame must give the same composed image and the same
// 4 MiB local memory hash. The frames exercise every coherence path of the resident targets:
//   - depth-tested draws with a separate depth buffer (colour and depth targets resident),
//   - a colour target switch (buffer A to buffer B) and back,
//   - a texture read from a target drawn in the same frame (feedback: written back before decoding),
//   - a host -> local upload into a drawn target (write back, then drop),
//   - a local -> local move out of a drawn target into another,
//   - a local -> host readback of a drawn target,
//   - a CLUT load from memory a draw just wrote, used by a T8 texture,
//   - a draw whose ZBUF shares the colour buffer's pages (the IN-335 case: write-back fallback).
// It also proves the cache is in use (resident batches, loads, write-backs) so a silently disabled cache fails.
int TestCacheEqualsWriteback(const gpu::VulkanHandles& handles) {
    constexpr uint32_t kFrames = 5;
    constexpr uint64_t kA = 0, kB = 2, kZ = 8, kT8 = 12, kClut = 14;  // pages
    auto rgbaq = [](uint32_t colour) { return uint64_t{colour} | (uint64_t{0x3F800000u} << 32); };
    auto xyz = [](uint32_t x, uint32_t y, uint32_t z = 0) {
        return uint64_t{x * 16} | (uint64_t{y * 16} << 16) | (uint64_t{z} << 32);
    };
    auto uv = [](uint32_t u, uint32_t v) { return uint64_t{u * 16} | (uint64_t{v * 16} << 16); };
    auto frameReg = [](uint64_t page) { return page | (uint64_t{1} << 16); };  // FBW 1, CT32
    auto zbufReg = [](uint64_t page, bool masked) { return page | (masked ? (uint64_t{1} << 32) : 0); };  // Z32
    auto tex0 = [](uint64_t blocks, uint64_t psm, uint64_t log2, uint64_t cbpBlocks, bool load) {
        return blocks | (uint64_t{1} << 14) | (psm << 20) | (log2 << 26) | (log2 << 30) | (uint64_t{1} << 34) |
               (cbpBlocks << 37) | (uint64_t{load ? 1u : 0u} << 61);  // TBW 1, TCC, TFX modulate, CPSM CT32, CSM1
    };
    const uint64_t kSprite = 6, kTriangle = 3, kIip = 1u << 3, kTme = 1u << 4, kFst = 1u << 8;

    cpuref::CaptureWriter capture;
    capture.Privileged(0x12000000u, 1u | (1u << 5) | (0xFFu << 8));                // PMODE: RC1, MMOD 1, ALP 255
    capture.Privileged(0x12000010u, uint64_t{2} << 13);                             // SMODE1: NTSC
    capture.Privileged(0x12000070u, uint64_t{kA * 32} | (uint64_t{1} << 9));        // DISPFB1: buffer A
    capture.Privileged(0x12000080u, (uint64_t{63} << 32) | (uint64_t{63} << 44));  // DISPLAY1: 64 x 64
    auto ad = [&](const std::vector<uint64_t>& words) {
        capture.Gif(1, cpuref::CaptureWriter::Packet(static_cast<uint32_t>(words.size() / 2), true, false, 0, 0, {0xE},
                                                     words));
    };
    auto image = [&](const std::vector<uint64_t>& data) {
        capture.Gif(1, cpuref::CaptureWriter::Packet(static_cast<uint32_t>(data.size() / 2), true, false, 0, 2, {}, data));
    };
    auto blit = [](uint64_t sbp, uint64_t spsm, uint64_t dbp, uint64_t dpsm) {
        return sbp | (uint64_t{1} << 16) | (spsm << 24) | (dbp << 32) | (uint64_t{1} << 48) | (dpsm << 56);
    };
    // Common state: scissor 0..63, no offset, depth test GEQUAL with writes (separate Z buffer).
    const std::vector<uint64_t> common = {(uint64_t{63} << 16) | (uint64_t{63} << 48), 0x40, 0, 0x18,
                                          (uint64_t{1} << 16) | (uint64_t{2} << 17), 0x47,  // TEST: ZTE, GEQUAL
                                          0, 0x42};                                          // ALPHA_1 (unused)
    // T8 texture data (64 x 64, one page) uploaded once: index = (x + 3y) & 255.
    {
        std::vector<uint64_t> words = {blit(0, 0, kT8 * 32, 0x13), 0x50, 0, 0x51, uint64_t{64} | (uint64_t{64} << 32), 0x52,
                                       0, 0x53};
        ad(words);
        std::vector<uint64_t> data;
        for (uint32_t i = 0; i < 64 * 64 / 8; ++i) {
            uint64_t word = 0;
            for (uint32_t b = 0; b < 8; ++b) {
                const uint32_t p = i * 8 + b;
                word |= uint64_t{((p % 64) + 3 * (p / 64)) & 255u} << (8 * b);
            }
            data.push_back(word);
        }
        image(data);
    }
    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        std::vector<uint64_t> w = common;
        // 1. Buffer A, depth buffer Z: a clear sprite and two depth-tested Gouraud triangles.
        w.insert(w.end(), {frameReg(kA), 0x4C, zbufReg(kZ, false), 0x4E, kSprite, 0x00,
                           rgbaq(0x80102030u + frame), 0x01, xyz(0, 0, 0), 0x05, xyz(64, 64, 0), 0x05,
                           kTriangle | kIip, 0x00, rgbaq(0x80FF0000u), 0x01, xyz(3, 2, 500 + frame), 0x05,
                           rgbaq(0x8000FF00u), 0x01, xyz(60, 10, 900), 0x05, rgbaq(0x800000FFu), 0x01,
                           xyz(20 + frame, 61, 700), 0x05});
        // 2. Buffer B: clear, then a sprite textured from buffer A (drawn above, this frame).
        w.insert(w.end(), {frameReg(kB), 0x4C, zbufReg(kZ, true), 0x4E, kSprite, 0x00, rgbaq(0x80405060u), 0x01,
                           xyz(0, 0), 0x05, xyz(64, 64), 0x05, tex0(kA * 32, 0x00, 6, 0, false), 0x06, 0, 0x08,
                           kSprite | kTme | kFst, 0x00, rgbaq(0x80808080u), 0x01, uv(0, 0), 0x03, xyz(8, 8), 0x05,
                           uv(48, 48), 0x03, xyz(56, 56), 0x05});
        // 3. A palette drawn into memory (CT32 16 x 16 at page kClut), then a T8 texture using it, into A.
        w.insert(w.end(), {frameReg(kClut), 0x4C, kSprite | kIip, 0x00, rgbaq(0x80000000u | (frame * 0x111111u)), 0x01,
                           xyz(0, 0), 0x05, rgbaq(0x80FFFFFFu - frame), 0x01, xyz(16, 16), 0x05,
                           frameReg(kA), 0x4C, tex0(kT8 * 32, 0x13, 6, kClut * 32, true), 0x06,
                           kSprite | kTme | kFst, 0x00, rgbaq(0x80808080u), 0x01, uv(0, 0), 0x03, xyz(30, 30), 0x05,
                           uv(30, 30), 0x03, xyz(60, 60), 0x05});
        // 3b. Seven more small colour targets (pages 20, 24, ..., 44): more targets than resident slots, so the
        //     least recently used ones are evicted (written back) and reloaded; then one is read as a texture.
        for (uint64_t k = 0; k < 7; ++k) {
            w.insert(w.end(), {frameReg(20 + 4 * k), 0x4C, kSprite, 0x00, rgbaq(0x80000000u + 0x0F0F0Fu * (k + 1) + frame),
                               0x01, xyz(2 * k, 1), 0x05, xyz(20 + 2 * k, 20), 0x05});
        }
        w.insert(w.end(), {frameReg(kA), 0x4C, tex0(20 * 32, 0x00, 5, 0, false), 0x06, kSprite | kTme | kFst, 0x00,
                           rgbaq(0x80808080u), 0x01, uv(0, 0), 0x03, xyz(40, 40), 0x05, uv(24, 24), 0x03,
                           xyz(64, 64), 0x05});
        ad(w);
        // 4. Host -> local: 8 x 8 CT32 block into buffer A at (4 + frame, 4).
        ad({blit(0, 0, kA * 32, 0x00), 0x50, (uint64_t{4 + frame} << 32) | (uint64_t{4} << 48), 0x51,
            uint64_t{8} | (uint64_t{8} << 32), 0x52, 0, 0x53});
        std::vector<uint64_t> block;
        for (uint32_t i = 0; i < 32; ++i) block.push_back((uint64_t{0x11223344u + i + frame} << 32) | (0x55667788u - i));
        image(block);
        // 5. Local -> local: A (0,0) 16 x 16 to B (40, 40).
        ad({blit(kA * 32, 0x00, kB * 32, 0x00), 0x50, uint64_t{0} | (uint64_t{40} << 32) | (uint64_t{40} << 48), 0x51,
            uint64_t{16} | (uint64_t{16} << 32), 0x52, 2, 0x53});
        // 6. Local -> host: 8 x 8 of B at (36, 36) (drawn by the textured sprite).
        ad({blit(kB * 32, 0x00, 0, 0x00), 0x50, uint64_t{36} | (uint64_t{36} << 16), 0x51,
            uint64_t{8} | (uint64_t{8} << 32), 0x52, 1, 0x53});
        capture.FifoRead(16);
        // 7. A draw into A whose ZBUF is A itself with Z written (test ALWAYS): the write-back fallback.
        ad({frameReg(kA), 0x4C, zbufReg(kA, false), 0x4E, uint64_t{1} << 16 | (uint64_t{1} << 17), 0x47, kSprite, 0x00,
            rgbaq(0x80EEDDCCu), 0x01, xyz(50, 2), 0x05, xyz(62, 12), 0x05});
        // 8. GT4's buffer reuse (IN-350): page 48 drawn 128 wide (FBW 2), then 64 wide at the same base (the first
        //    target is parked in the GLM and the second loads from it), then partial CT16 and T4 uploads into it
        //    (merged into the GLM-owned pages), then read as a CT32 texture into A.
        ad({(uint64_t{1} << 16) | (uint64_t{2} << 17), 0x47, frameReg(48) | (uint64_t{1} << 16), 0x4C,
            zbufReg(kZ, true), 0x4E, kSprite, 0x00, rgbaq(0x80123456u + frame), 0x01, xyz(0, 0), 0x05, xyz(64, 40), 0x05,
            frameReg(48), 0x4C, kSprite, 0x00, rgbaq(0x80654321u), 0x01, xyz(5 + frame, 3), 0x05, xyz(30, 50), 0x05});
        ad({blit(0, 0, 48 * 32, 0x02), 0x50, (uint64_t{10} << 32) | (uint64_t{3 + frame} << 48), 0x51,
            uint64_t{8} | (uint64_t{4} << 32), 0x52, 0, 0x53});
        image({0x1111222233334444ull + frame, 0x5555666677778888ull, 0x99990000AAAABBBBull, 0xCCCCDDDDEEEEFFFFull,
               0x0123456789ABCDEFull, 0xFEDCBA9876543210ull, 0x0F0F0F0FF0F0F0F0ull, 0x1357924680ACE13Bull});
        ad({blit(0, 0, 48 * 32, 0x14), 0x50, (uint64_t{6} << 32) | (uint64_t{40} << 48), 0x51,
            uint64_t{8} | (uint64_t{4} << 32), 0x52, 0, 0x53});
        image({0x89ABCDEF01234567ull ^ frame, 0x76543210FEDCBA98ull});
        ad({frameReg(kA), 0x4C, tex0(48 * 32, 0x00, 6, 0, false), 0x06, kSprite | kTme | kFst, 0x00,
            rgbaq(0x80808080u), 0x01, uv(0, 0), 0x03, xyz(0, 32), 0x05, uv(64, 32), 0x03, xyz(64, 64), 0x05});
        capture.FrameEnd(frame);
    }

    struct FrameRecord {
        bool valid = false;
        std::vector<uint8_t> rgb;
        uint64_t slmHash = 0;
        std::vector<uint8_t> slm;
    };
    auto replay = [&](bool cacheMode, bool cpuSprites, std::vector<FrameRecord>& out, writeback::Counters& counters,
                      std::string& error) -> bool {
        writeback::Renderer renderer;
        if (!renderer.Init(handles, error)) return false;
        renderer.SetSyncMode(cacheMode);
        renderer.SetCpuSmallSprites(cpuSprites);
        renderer.SetGlmLoadsAlways(cpuSprites);  // the second cache run loads every slot through the GLM (IN-350)
        renderer.SetGpuUploads(!cpuSprites);     // ... and writes uploads on the CPU (GLM merges, IN-350/351)
        writeback::WritebackReplayTarget target(renderer);
        cpuref::ReplaySummary summary;
        const bool ok = cpuref::ReplayCapture(capture.Bytes(), target, summary,
                                              [&](uint64_t, const cpuref::FrameImage& image) {
                                                  FrameRecord r;
                                                  r.valid = image.valid;
                                                  for (const display::Rgba8& p : image.image.pixels) {
                                                      r.rgb.push_back(p.r);
                                                      r.rgb.push_back(p.g);
                                                      r.rgb.push_back(p.b);
                                                  }
                                                  const uint8_t* slm = renderer.Slm();
                                                  r.slmHash = cpuref::HashLocalMemory(slm, 4u * 1024u * 1024u);
                                                  r.slm.assign(slm, slm + 4u * 1024u * 1024u);
                                                  out.push_back(std::move(r));
                                              },
                                              error);
        counters = renderer.GetCounters();
        if (renderer.Failed()) error += " " + renderer.LastError();
        return ok && !renderer.Failed();
    };
    // The cache run without the CPU sprite path (IN-348) keeps every small target on the GPU (evictions); the
    // second cache run with it must give the same images and memory too.
    std::vector<FrameRecord> reference;
    std::vector<FrameRecord> cached;
    std::vector<FrameRecord> cachedCpuSprites;
    writeback::Counters referenceCounters;
    writeback::Counters cacheCounters;
    writeback::Counters cpuSpriteCounters;
    std::string error;
    if (!replay(false, false, reference, referenceCounters, error) || !replay(true, false, cached, cacheCounters, error) ||
        !replay(true, true, cachedCpuSprites, cpuSpriteCounters, error)) {
        std::printf("hw_cache_equals_writeback: replay failed: %s\n", error.c_str());
        return 1;
    }
    int failures = 0;
    if (reference.size() != kFrames || cached.size() != kFrames) {
        std::printf("hw_cache_equals_writeback: frame counts %zu / %zu\n", reference.size(), cached.size());
        return 1;
    }
    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        const bool image = reference[frame].valid == cached[frame].valid && reference[frame].rgb == cached[frame].rgb;
        const bool memory = reference[frame].slmHash == cached[frame].slmHash &&
                            cachedCpuSprites.size() == kFrames &&
                            reference[frame].slmHash == cachedCpuSprites[frame].slmHash &&
                            reference[frame].rgb == cachedCpuSprites[frame].rgb;
        std::printf("  frame %u: image %s, memory %016llx / %016llx %s\n", frame, image ? "equal" : "DIFFERENT",
                    static_cast<unsigned long long>(reference[frame].slmHash),
                    static_cast<unsigned long long>(cached[frame].slmHash), memory ? "equal" : "DIFFERENT");
        if (!memory) {
            // Name the 8 KiB GS pages that differ, so a failure points at the buffer involved.
            std::string pages;
            for (uint32_t page = 0; page < 512; ++page) {
                const size_t base = static_cast<size_t>(page) * 8192u;
                if (std::memcmp(reference[frame].slm.data() + base, cached[frame].slm.data() + base, 8192) != 0) {
                    pages += " " + std::to_string(page);
                }
            }
            std::printf("    differing pages:%s\n", pages.c_str());
        }
        if (!image || !memory) ++failures;
        if (frame > 0 && reference[frame].slmHash == reference[frame - 1].slmHash) {
            std::printf("  frame %u left memory unchanged (fixture is blind)\n", frame);
            ++failures;
        }
    }
    std::printf("  cache: resident %llu, fallback %llu, loads %llu, write-backs %llu\n",
                static_cast<unsigned long long>(cacheCounters.cacheResidentBatches),
                static_cast<unsigned long long>(cacheCounters.cacheFallbackBatches),
                static_cast<unsigned long long>(cacheCounters.cacheLoads),
                static_cast<unsigned long long>(cacheCounters.cacheWritebacks));
    std::printf("  cache: GLM upload merges %llu (%llu words, CPU-upload run), GPU host uploads %llu\n",
                static_cast<unsigned long long>(cpuSpriteCounters.glmUploadMerges),
                static_cast<unsigned long long>(cpuSpriteCounters.glmMergedWords),
                static_cast<unsigned long long>(cacheCounters.glmHostUploads));
    std::printf("  cache: GLM parks %llu, GLM slot loads %llu / %llu (always), GLM downloads %llu\n",
                static_cast<unsigned long long>(cacheCounters.glmParks),
                static_cast<unsigned long long>(cacheCounters.glmSlotLoads),
                static_cast<unsigned long long>(cpuSpriteCounters.glmSlotLoads),
                static_cast<unsigned long long>(cacheCounters.glmDownloads));
    std::printf("  cache: evictions %llu, GPU CLUT loads %llu, GLM textures %llu, CPU sprite batches %llu\n",
                static_cast<unsigned long long>(cacheCounters.cacheEvictions),
                static_cast<unsigned long long>(cacheCounters.gpuClutLoads),
                static_cast<unsigned long long>(cacheCounters.texturesFromGlm),
                static_cast<unsigned long long>(cpuSpriteCounters.cpuSpriteBatches));
    if (cacheCounters.cacheResidentBatches == 0 || cacheCounters.cacheFallbackBatches == 0 ||
        cacheCounters.cacheLoads == 0 || cacheCounters.cacheWritebacks == 0 || cacheCounters.cacheEvictions == 0 ||
        referenceCounters.cacheResidentBatches != 0 || cpuSpriteCounters.cpuSpriteBatches == 0 ||
        cacheCounters.gpuClutLoads == 0 || cacheCounters.texturesFromGlm == 0 || cacheCounters.glmParks == 0 ||
        cacheCounters.glmSlotLoads == 0 || cacheCounters.glmDownloads == 0 || cpuSpriteCounters.glmUploadMerges == 0 ||
        cacheCounters.glmHostUploads == 0 ||
        cpuSpriteCounters.glmSlotLoads < cpuSpriteCounters.cacheLoads) {
        std::printf("  the cache paths were not all exercised (fixture is blind)\n");
        ++failures;
    }
    // IN-355: presentation from the asynchronous capture equals the synchronous presentation one VBlank later
    // (nothing is shown at the first VBlank), and memory is unchanged by it.
    {
        auto present = [&](bool async, std::vector<FrameRecord>& out, writeback::Counters& counters,
                           std::string& error) -> bool {
            writeback::Renderer renderer;
            if (!renderer.Init(handles, error)) return false;
            renderer.SetSyncMode(true);
            renderer.SetAsyncPresentation(async);
            writeback::WritebackReplayTarget target(renderer, writeback::ComposeKind::Presentation);
            cpuref::ReplaySummary summary;
            const bool ok = cpuref::ReplayCapture(capture.Bytes(), target, summary,
                                                  [&](uint64_t, const cpuref::FrameImage& image) {
                                                      FrameRecord r;
                                                      r.valid = image.valid;
                                                      for (const display::Rgba8& p : image.image.pixels) {
                                                          r.rgb.push_back(p.r);
                                                          r.rgb.push_back(p.g);
                                                          r.rgb.push_back(p.b);
                                                      }
                                                      r.slmHash = cpuref::HashLocalMemory(renderer.Slm(),
                                                                                          4u * 1024u * 1024u);
                                                      out.push_back(std::move(r));
                                                  },
                                                  error);
            counters = renderer.GetCounters();
            if (renderer.Failed()) error += " " + renderer.LastError();
            return ok && !renderer.Failed();
        };
        std::vector<FrameRecord> sync;
        std::vector<FrameRecord> async;
        writeback::Counters syncCounters;
        writeback::Counters asyncCounters;
        if (!present(false, sync, syncCounters, error) || !present(true, async, asyncCounters, error)) {
            std::printf("  async presentation: replay failed: %s\n", error.c_str());
            ++failures;
        } else if (sync.size() != kFrames || async.size() != kFrames) {
            std::printf("  async presentation: frame counts %zu / %zu\n", sync.size(), async.size());
            ++failures;
        } else {
            if (async[0].valid) {
                std::printf("  async presentation: frame 0 shows an image (expected none)\n");
                ++failures;
            }
            for (uint32_t frame = 0; frame < kFrames; ++frame) {
                const bool memory = sync[frame].slmHash == async[frame].slmHash;
                const bool image = frame == 0 || (async[frame].valid == sync[frame - 1].valid &&
                                                  async[frame].rgb == sync[frame - 1].rgb);
                std::printf("  async presentation frame %u: image %s previous VBlank's, memory %s\n", frame,
                            image ? "equals" : "DIFFERS from", memory ? "equal" : "DIFFERS");
                if (!image || !memory) ++failures;
            }
            uint32_t changing = 0;  // the shift is only visible when consecutive frames differ
            for (uint32_t frame = 1; frame < kFrames; ++frame) changing += sync[frame].rgb != sync[frame - 1].rgb ? 1u : 0u;
            if (changing < kFrames - 1 || asyncCounters.displayCaptures == 0 ||
                asyncCounters.presentationsFromCapture + 1 != kFrames ||
                syncCounters.presentationsFromCapture != 0) {
                std::printf("  async presentation: captures %llu, composes from captures %llu (fixture is blind)\n",
                            static_cast<unsigned long long>(asyncCounters.displayCaptures),
                            static_cast<unsigned long long>(asyncCounters.presentationsFromCapture));
                ++failures;
            }
        }
    }
    std::printf("hw_cache_equals_writeback: %u frames, %d failures\n", kFrames, failures);
    return failures == 0 ? 0 : 1;
}

// hw_glm_unswizzle (IN-350): random local memory in the GLM is loaded into a colour or depth slot at a target
// layout; the slot must then hold LocalMemory::readRect of the same rectangle (what a load from the shadow memory
// would upload). Then a GLM download of a few page ranges must give back the GLM bytes at the same offsets.
int TestGlmUnswizzle(gpu::GpuDrawer& drawer) {
    struct Case {
        memory::Psm psm;
        bool depth;
        uint32_t bp;
        uint32_t bw;
    };
    const Case cases[] = {{memory::Psm::Ct32, false, 64, 10},  {memory::Psm::Ct24, false, 320, 4},
                          {memory::Psm::Ct16, false, 1000, 3}, {memory::Psm::Ct16S, false, 37, 3},
                          {memory::Psm::Z32, true, 640, 10},   {memory::Psm::Z24, true, 2048, 4},
                          {memory::Psm::Z16, true, 5000, 3},   {memory::Psm::Z16S, true, 7001, 3}};
    uint64_t state = 0x0B5Eu;
    auto next = [&] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>(state >> 32);
    };
    int failures = 0;
    memory::LocalMemory local;
    for (const Case& c : cases) {
        for (uint32_t address = 0; address < 4u * 1024u * 1024u; address += 4) {
            const uint32_t word = next();
            for (uint32_t b = 0; b < 4; ++b) local.setByte(address + b, static_cast<uint8_t>(word >> (8 * b)));
        }
        std::string error;
        if (!drawer.UploadGlm(local.bytes().data(), {{0u, 4u * 1024u * 1024u}}, error)) {
            std::printf("hw_glm_unswizzle: upload failed: %s\n", error.c_str());
            return 1;
        }
        const int32_t x0 = 3, y0 = 5, x1 = 140, y1 = 77;
        const uint32_t width = static_cast<uint32_t>(x1 - x0 + 1);
        const uint32_t height = static_cast<uint32_t>(y1 - y0 + 1);
        gpu::GpuChunk chunk;
        chunk.rectX0 = x0;
        chunk.rectY0 = y0;
        chunk.rectX1 = x1;
        chunk.rectY1 = y1;
        chunk.colourSlot = 2;
        chunk.depthSlot = 1;
        chunk.uploadFrame = chunk.uploadDepth = false;
        chunk.readbackFrame = !c.depth;
        chunk.readbackDepth = c.depth;
        (c.depth ? chunk.depthWords : chunk.frameWords).resize(static_cast<size_t>(width) * height);
        if (!drawer.LoadSlotFromGlm(c.depth, c.depth ? 1u : 2u, c.bp, c.bw, static_cast<uint32_t>(c.psm), x0, y0, x1, y1,
                                    error) ||
            !drawer.Execute(chunk, error)) {
            std::printf("hw_glm_unswizzle: GPU error: %s\n", error.c_str());
            return 1;
        }
        std::vector<uint32_t> expected(static_cast<size_t>(width) * height);
        local.readRect(memory::BufferView{c.bp, c.bw, c.psm}, static_cast<uint32_t>(x0), static_cast<uint32_t>(y0), width,
                       height, expected.data());
        const std::vector<uint32_t>& got = c.depth ? chunk.depthWords : chunk.frameWords;
        size_t differing = 0;
        for (size_t i = 0; i < expected.size(); ++i) differing += got[i] != expected[i] ? 1u : 0u;
        std::printf("  psm %02x (%s slot): %zu of %zu pixels differ from readRect\n", static_cast<unsigned>(c.psm),
                    c.depth ? "depth" : "colour", differing, expected.size());
        if (differing != 0) ++failures;
    }
    // Download round trip: three page ranges (one at the end of memory) into a zeroed buffer.
    std::vector<uint8_t> downloaded(4u * 1024u * 1024u, 0);
    const std::vector<std::pair<uint32_t, uint32_t>> ranges = {
        {0u, 8192u}, {37u * 8192u, 3u * 8192u}, {4u * 1024u * 1024u - 8192u, 8192u}};
    std::string error;
    if (!drawer.DownloadGlm(ranges, downloaded.data(), error)) {
        std::printf("hw_glm_unswizzle: download failed: %s\n", error.c_str());
        return 1;
    }
    size_t wrong = 0;
    for (size_t i = 0; i < downloaded.size(); ++i) {
        bool inside = false;
        for (const auto& range : ranges) inside = inside || (i >= range.first && i < range.first + range.second);
        const uint8_t expected = inside ? local.bytes()[i] : uint8_t{0};
        wrong += downloaded[i] != expected ? 1u : 0u;
    }
    std::printf("  download: %zu of %zu bytes wrong\n", wrong, downloaded.size());
    if (wrong != 0) ++failures;
    std::printf("hw_glm_unswizzle: %d failing cases\n", failures);
    return failures == 0 ? 0 : 1;
}

// hw_glm_upload_merge (IN-350): random (word, value, bit mask) triples merged into a random GLM must give
// glm = (glm & ~mask) | (value & mask) for exactly those words, every other word unchanged.
int TestGlmUploadMerge(gpu::GpuDrawer& drawer) {
    uint64_t state = 0x3E26u;
    auto next = [&] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>(state >> 32);
    };
    std::vector<uint8_t> memory(4u * 1024u * 1024u);
    for (uint8_t& byte : memory) byte = static_cast<uint8_t>(next());
    std::string error;
    if (!drawer.UploadGlm(memory.data(), {{0u, 4u * 1024u * 1024u}}, error)) {
        std::printf("hw_glm_upload_merge: upload failed: %s\n", error.c_str());
        return 1;
    }
    std::vector<uint32_t> expected(memory.size() / 4);
    std::memcpy(expected.data(), memory.data(), memory.size());
    std::vector<uint32_t> triples;
    std::vector<bool> used(expected.size(), false);
    for (int i = 0; i < 20000; ++i) {
        const uint32_t word = next() % static_cast<uint32_t>(expected.size());
        if (used[word]) continue;
        used[word] = true;
        const uint32_t masks[] = {0xFFFFFFFFu, 0x0000FFFFu, 0xFFFF0000u, 0x0000000Fu, 0xF0F00F0Fu, next()};
        const uint32_t mask = masks[next() % 6u];
        const uint32_t value = next();
        triples.insert(triples.end(), {word, value, mask});
        expected[word] = (expected[word] & ~mask) | (value & mask);
    }
    std::vector<uint32_t> glm;
    if (!drawer.MergeIntoGlm(triples, error) || !drawer.ReadGlm(glm, error)) {
        std::printf("hw_glm_upload_merge: GPU error: %s\n", error.c_str());
        return 1;
    }
    size_t wrong = 0;
    for (size_t i = 0; i < expected.size(); ++i) wrong += glm[i] != expected[i] ? 1u : 0u;
    std::printf("  %zu merged words, %zu of %zu GLM words wrong\n", triples.size() / 3, wrong, expected.size());
    std::printf("hw_glm_upload_merge: %s\n", wrong == 0 ? "pass" : "FAIL");
    return wrong == 0 ? 0 : 1;
}

// hw_glm_host_upload (IN-351): EE->GS uploads written into the GLM by the upload kernel must equal
// memory::hostToLocal on the same bytes, for every transfer size (32, 24, 16, 8, 4 bits, the high-byte and
// high/low-nibble 32-bit formats, depth formats), odd origins and widths, and a partial transfer that ends inside
// the rectangle. The whole 4 MiB is compared after each upload.
int TestGlmHostUpload(gpu::GpuDrawer& drawer) {
    struct Case {
        uint32_t psm;
        uint32_t bp;
        uint32_t bw;
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
        bool partial;
    };
    const Case cases[] = {{0x00, 64, 10, 3, 5, 61, 17, false},   {0x01, 320, 4, 1, 2, 33, 9, false},
                          {0x02, 1000, 3, 7, 1, 25, 40, false},  {0x0A, 37, 3, 2, 3, 30, 31, true},
                          {0x13, 2048, 4, 5, 9, 77, 21, false},  {0x14, 5000, 2, 3, 7, 101, 13, false},
                          {0x1B, 7001, 2, 9, 4, 50, 19, false},  {0x24, 7200, 2, 1, 1, 63, 11, false},
                          {0x2C, 7400, 2, 4, 6, 64, 8, true},    {0x30, 9000, 10, 6, 3, 300, 12, false},
                          {0x31, 9400, 4, 2, 2, 129, 7, false},  {0x3A, 9800, 2, 3, 1, 90, 30, false}};
    uint64_t state = 0x7A11u;
    auto next = [&] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>(state >> 32);
    };
    memory::LocalMemory local;
    for (uint32_t address = 0; address < 4u * 1024u * 1024u; address += 4) {
        const uint32_t word = next();
        for (uint32_t b = 0; b < 4; ++b) local.setByte(address + b, static_cast<uint8_t>(word >> (8 * b)));
    }
    std::string error;
    if (!drawer.UploadGlm(local.bytes().data(), {{0u, 4u * 1024u * 1024u}}, error)) {
        std::printf("hw_glm_host_upload: upload failed: %s\n", error.c_str());
        return 1;
    }
    int failures = 0;
    for (const Case& c : cases) {
        const auto psm = memory::decodePsm(c.psm);
        if (!psm) {
            std::printf("  psm %02x: not a PSM\n", c.psm);
            ++failures;
            continue;
        }
        const uint32_t bpp = memory::psmInfo(*psm).transferBpp;
        size_t bytes = (static_cast<size_t>(c.width) * c.height * bpp + 7) / 8;
        if (c.partial) bytes = bytes / 2 + 3;  // ends inside the rectangle, not on a texel boundary
        std::vector<uint8_t> data(bytes);
        for (uint8_t& byte : data) byte = static_cast<uint8_t>(next());
        const uint32_t pixels = static_cast<uint32_t>(
            std::min<uint64_t>(static_cast<uint64_t>(bytes) * 8u / bpp, static_cast<uint64_t>(c.width) * c.height));
        if (!drawer.UploadHostToGlm(c.bp, c.bw, c.psm, c.x, c.y, c.width, pixels, bpp, data.data(), data.size(), error)) {
            std::printf("hw_glm_host_upload: GPU error: %s\n", error.c_str());
            return 1;
        }
        memory::hostToLocal(local, memory::BufferView{c.bp, c.bw, *psm}, memory::TransferRect{c.x, c.y, c.width, c.height},
                            std::span<const uint8_t>(data.data(), data.size()));
        std::vector<uint32_t> glm;
        if (!drawer.ReadGlm(glm, error)) {
            std::printf("hw_glm_host_upload: read failed: %s\n", error.c_str());
            return 1;
        }
        size_t differing = 0;
        for (size_t i = 0; i < glm.size(); ++i) {
            uint32_t expected = 0;
            std::memcpy(&expected, local.bytes().data() + 4 * i, 4);
            differing += glm[i] != expected ? 1u : 0u;
        }
        std::printf("  psm %02x %ux%u at (%u,%u)%s: %zu GLM words differ from hostToLocal\n", c.psm, c.width, c.height,
                    c.x, c.y, c.partial ? " partial" : "", differing);
        if (differing != 0) ++failures;
    }
    std::printf("hw_glm_host_upload: %d failing cases\n", failures);
    return failures == 0 ? 0 : 1;
}

// IN-347 GLM kernels against the CPU memory model (chapter 04.9 fixture hw_glm_equals_writeback, split in two).
// hw_glm_decode: random local memory uploaded to the GLM; for every PSM, several bases and widths, the GPU
// decode of a 96 x 80 extent equals LocalMemory::readPixel (indexed formats: through a random palette).
int TestGlmDecode(gpu::GpuDrawer& drawer) {
    memory::LocalMemory local;
    uint64_t state = 0x61A5u;
    auto next = [&] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>(state >> 32);
    };
    for (uint32_t address = 0; address < 4u * 1024u * 1024u; address += 4) {
        const uint32_t word = next();
        for (uint32_t b = 0; b < 4; ++b) local.setByte(address + b, static_cast<uint8_t>(word >> (8 * b)));
    }
    std::string error;
    if (!drawer.UploadGlm(local.bytes().data(), {{0u, 4u * 1024u * 1024u}}, error)) {
        std::printf("hw_glm_decode: upload failed: %s\n", error.c_str());
        return 1;
    }
    std::vector<uint32_t> palette(256);
    for (uint32_t& entry : palette) entry = next();
    const memory::Psm psms[] = {memory::Psm::Ct32, memory::Psm::Ct24, memory::Psm::Ct16, memory::Psm::Ct16S,
                                memory::Psm::T8,   memory::Psm::T4,   memory::Psm::T8H,  memory::Psm::T4Hl,
                                memory::Psm::T4Hh, memory::Psm::Z32,  memory::Psm::Z24,  memory::Psm::Z16,
                                memory::Psm::Z16S};
    const uint32_t bases[] = {0u, 37u, 9000u, 16370u};
    const uint32_t widths[] = {1u, 2u, 10u};
    constexpr uint32_t kW = 96, kH = 80;
    uint64_t compared = 0, mismatches = 0;
    for (memory::Psm psm : psms) {
        const bool indexed = memory::psmInfo(psm).isIndexed;
        for (uint32_t bp : bases) {
            for (uint32_t bw : widths) {
                gpu::GpuChunk chunk;
                chunk.rectX0 = chunk.rectY0 = 0;
                chunk.rectX1 = chunk.rectY1 = 0;
                chunk.uploadFrame = chunk.uploadDepth = chunk.readbackFrame = chunk.readbackDepth = false;
                chunk.hasTexture = true;
                chunk.glmTexture = true;
                chunk.glmBp = bp;
                chunk.glmBw = bw;
                chunk.glmPsm = static_cast<uint32_t>(psm);
                chunk.glmIndexed = indexed;
                chunk.palette = palette;
                chunk.texWidth = kW;
                chunk.texHeight = kH;
                std::vector<uint32_t> texels;
                if (!drawer.Execute(chunk, error) || !drawer.ReadTexels(kW, kH, texels, error)) {
                    std::printf("hw_glm_decode: GPU error: %s\n", error.c_str());
                    return 1;
                }
                const memory::BufferView view{bp, bw, psm};
                for (uint32_t y = 0; y < kH; ++y) {
                    for (uint32_t x = 0; x < kW; ++x) {
                        uint32_t expected = local.readPixel(view, x, y);
                        if (indexed) expected = palette[expected & 255u];
                        const uint32_t got = texels[static_cast<size_t>(y) * kW + x];
                        ++compared;
                        if (got != expected) {
                            if (mismatches < 4) {
                                std::printf("  mismatch psm=%02x bp=%u bw=%u at %u,%u: gpu %08X cpu %08X\n",
                                            static_cast<unsigned>(psm), bp, bw, x, y, got, expected);
                            }
                            ++mismatches;
                        }
                    }
                }
            }
        }
    }
    std::printf("hw_glm_decode: compared %llu texels over 13 PSMs, mismatches %llu\n",
                static_cast<unsigned long long>(compared), static_cast<unsigned long long>(mismatches));
    return mismatches == 0 ? 0 : 1;
}

// hw_glm_swizzle: random target words in a colour or depth slot are swizzled into the GLM at the target's own
// (BP, BW, PSM); the whole 4 MiB GLM must then equal the local memory after LocalMemory::writeRect of the same
// words (only the pixel bits change; neighbouring sub-word pixels survive).
// The rectangle stays inside the buffer width: wider rectangles wrap into the next page row, two pixels then
// share an address, and the last writer depends on the visiting order (a parallel swizzle has none).
int TestGlmSwizzle(gpu::GpuDrawer& drawer) {
    struct Case {
        memory::Psm psm;
        bool depth;
        uint32_t bp;
        uint32_t bw;
    };
    const Case cases[] = {{memory::Psm::Ct32, false, 64, 10},  {memory::Psm::Ct24, false, 320, 4},
                          {memory::Psm::Ct16, false, 1000, 3}, {memory::Psm::Ct16S, false, 37, 3},
                          {memory::Psm::Z32, true, 640, 10},   {memory::Psm::Z24, true, 2048, 4},
                          {memory::Psm::Z16, true, 5000, 3},   {memory::Psm::Z16S, true, 7001, 3}};
    uint64_t state = 0x5A11u;
    auto next = [&] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>(state >> 32);
    };
    int failures = 0;
    for (const Case& c : cases) {
        memory::LocalMemory local;
        for (uint32_t address = 0; address < 4u * 1024u * 1024u; address += 4) {
            const uint32_t word = next();
            for (uint32_t b = 0; b < 4; ++b) local.setByte(address + b, static_cast<uint8_t>(word >> (8 * b)));
        }
        std::string error;
        if (!drawer.UploadGlm(local.bytes().data(), {{0u, 4u * 1024u * 1024u}}, error)) {
            std::printf("hw_glm_swizzle: upload failed: %s\n", error.c_str());
            return 1;
        }
        const int32_t x0 = 3, y0 = 5, x1 = 140, y1 = 77;
        const size_t words = static_cast<size_t>(x1 - x0 + 1) * static_cast<size_t>(y1 - y0 + 1);
        std::vector<uint32_t> values(words);
        for (uint32_t& v : values) v = next();
        gpu::GpuChunk chunk;
        chunk.rectX0 = x0;
        chunk.rectY0 = y0;
        chunk.rectX1 = x1;
        chunk.rectY1 = y1;
        chunk.colourSlot = 1;
        chunk.depthSlot = 1;
        chunk.uploadFrame = !c.depth;
        chunk.uploadDepth = c.depth;
        chunk.readbackFrame = chunk.readbackDepth = false;
        (c.depth ? chunk.depthWords : chunk.frameWords) = values;
        std::vector<uint32_t> glm;
        if (!drawer.Execute(chunk, error) ||
            !drawer.SwizzleToGlm(c.depth, 1, c.bp, c.bw, static_cast<uint32_t>(c.psm), x0, y0, x1, y1, error) ||
            !drawer.ReadGlm(glm, error)) {
            std::printf("hw_glm_swizzle: GPU error: %s\n", error.c_str());
            return 1;
        }
        local.writeRect(memory::BufferView{c.bp, c.bw, c.psm}, static_cast<uint32_t>(x0), static_cast<uint32_t>(y0),
                        static_cast<uint32_t>(x1 - x0 + 1), static_cast<uint32_t>(y1 - y0 + 1), values.data());
        size_t differing = 0;
        for (size_t i = 0; i < glm.size(); ++i) {
            uint32_t expected = 0;
            std::memcpy(&expected, local.bytes().data() + 4 * i, 4);
            if (glm[i] != expected) ++differing;
        }
        std::printf("  psm %02x (%s slot): %zu of %zu GLM words differ from the write-back\n",
                    static_cast<unsigned>(c.psm), c.depth ? "depth" : "colour", differing, glm.size());
        if (differing != 0) ++failures;
    }
    std::printf("hw_glm_swizzle: %d failing cases\n", failures);
    return failures == 0 ? 0 : 1;
}

// IN-349 GPU CLUT (chapter 07.7) against the CPU CLUT unit: a sequence of 40 random CLUT loads (CT32, CT16,
// CT16S; CSM1 and CSM2; T8 and T4 palettes; CSA 0..31; aligned and unaligned CBP) is applied to the CPU
// clut::ClutUnit from local memory and to the GPU CLUT buffer from the GLM; after every load the 512 halfwords
// must be equal. Then a T8 and a T4 texture decoded on the GPU through the GPU CLUT (with TEXA for CT16) must
// equal the CPU decode through ClutUnit::Decode.
int TestGlmClut(gpu::GpuDrawer& drawer) {
    memory::LocalMemory local;
    uint64_t state = 0xC107u;
    auto next = [&] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>(state >> 32);
    };
    for (uint32_t address = 0; address < 4u * 1024u * 1024u; address += 4) {
        const uint32_t word = next();
        for (uint32_t b = 0; b < 4; ++b) local.setByte(address + b, static_cast<uint8_t>(word >> (8 * b)));
    }
    std::string error;
    if (!drawer.UploadGlm(local.bytes().data(), {{0u, 4u * 1024u * 1024u}}, error)) {
        std::printf("hw_glm_clut: upload failed: %s\n", error.c_str());
        return 1;
    }
    CounterSet counterSet;
    clut::ClutUnit cpu(counterSet);
    if (!drawer.UploadClut(cpu.Halfwords(), error)) {
        std::printf("hw_glm_clut: CLUT upload failed: %s\n", error.c_str());
        return 1;
    }
    const uint32_t cpsms[] = {0x00u, 0x02u, 0x0Au};
    int failures = 0;
    clut::Tex0Write last;
    for (int load = 0; load < 40; ++load) {
        clut::Tex0Write write;
        write.psm = (next() & 1u) ? 0x13u : 0x14u;  // T8 or T4
        write.cpsm = cpsms[next() % 3u];
        write.cbp = (load % 3 == 0) ? (next() % 16000u) : (next() % 500u) * 32u;
        write.csm = (load % 5 == 4) ? 1u : 0u;
        write.csa = next() & 31u;
        write.cld = 1;
        write.texclutCbw = 1u + next() % 4u;
        write.texclutCou = next() % 4u;
        write.texclutCov = next() % 300u;
        cpu.ProcessWrite(write, local.bytes().data(), {});
        if (!drawer.LoadClutFromGlm(write.cbp, write.cpsm, write.csm, write.csa, write.psm, write.texclutCbw,
                                    write.texclutCou, write.texclutCov, error)) {
            std::printf("hw_glm_clut: GPU load failed: %s\n", error.c_str());
            return 1;
        }
        std::vector<uint16_t> gpu;
        if (!drawer.ReadClut(gpu, error)) {
            std::printf("hw_glm_clut: read failed: %s\n", error.c_str());
            return 1;
        }
        uint32_t differing = 0;
        for (uint32_t i = 0; i < 512; ++i) differing += gpu[i] != cpu.Halfwords()[i] ? 1u : 0u;
        if (differing != 0) {
            std::printf("  load %d (cpsm %02x csm %u csa %u psm %02x cbp %u): %u halfwords differ\n", load, write.cpsm,
                        write.csm, write.csa, write.psm, write.cbp, differing);
            ++failures;
        }
        last = write;
    }
    // Indexed decode through the GPU CLUT equals the CPU palette for both index sizes and both palette formats.
    const uint64_t texa = uint64_t{0x55} | (uint64_t{1} << 15) | (uint64_t{0xAA} << 32);  // TA0, AEM, TA1
    const struct {
        uint32_t psm;
        uint32_t cpsm;
        uint32_t csa;
    } decodes[] = {{0x13u, 0x00u, 3u}, {0x14u, 0x00u, 9u}, {0x13u, 0x02u, 7u}, {0x14u, 0x0Au, 21u}};
    for (const auto& d : decodes) {
        clut::Tex0Write write;
        write.psm = d.psm;
        write.cpsm = d.cpsm;
        write.cbp = 4096u;
        write.csa = d.csa;
        write.cld = 1;
        cpu.ProcessWrite(write, local.bytes().data(), {});
        if (!drawer.LoadClutFromGlm(write.cbp, write.cpsm, 0, write.csa, write.psm, 0, 0, 0, error)) return 1;
        const clut::Palette palette = cpu.Decode(d.psm, d.cpsm, d.csa, texa);
        const clut::TexaState texaState = clut::TexaState::FromRegister(texa);
        gpu::GpuChunk chunk;
        chunk.rectX0 = chunk.rectY0 = chunk.rectX1 = chunk.rectY1 = 0;
        chunk.uploadFrame = chunk.uploadDepth = chunk.readbackFrame = chunk.readbackDepth = false;
        chunk.hasTexture = true;
        chunk.glmTexture = true;
        chunk.glmBp = 8000u;
        chunk.glmBw = 2u;
        chunk.glmPsm = d.psm;
        chunk.glmIndexed = true;
        chunk.glmGpuClut = true;
        chunk.glmCpsm = d.cpsm;
        chunk.glmCsa = d.csa;
        chunk.glmTa0 = texaState.ta0;
        chunk.glmTa1 = texaState.ta1;
        chunk.glmAem = texaState.aem ? 1u : 0u;
        chunk.texWidth = 64;
        chunk.texHeight = 32;
        std::vector<uint32_t> texels;
        if (!drawer.Execute(chunk, error) || !drawer.ReadTexels(64, 32, texels, error)) {
            std::printf("hw_glm_clut: decode failed: %s\n", error.c_str());
            return 1;
        }
        uint32_t wrong = 0;
        const memory::BufferView view{8000u, 2u, *memory::decodePsm(d.psm)};
        for (uint32_t y = 0; y < 32; ++y) {
            for (uint32_t x = 0; x < 64; ++x) {
                const uint32_t expected = palette.entries[local.readPixel(view, x, y) & 255u];
                wrong += texels[static_cast<size_t>(y) * 64 + x] != expected ? 1u : 0u;
            }
        }
        std::printf("  decode psm %02x cpsm %02x csa %u: %u of 2048 texels differ\n", d.psm, d.cpsm, d.csa, wrong);
        if (wrong != 0) ++failures;
    }
    (void)last;
    std::printf("hw_glm_clut: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
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
    if (name == "hw_present_scheduler") return TestPresentScheduler();
    if (name == "hw_present_scheduler_hold") return TestPresentSchedulerHold();
    if (name == "hw_present_scheduler_rate") return TestPresentSchedulerRate();
    if (name == "hw_aux_frame_guest_untouched") return TestAuxFrameGuestUntouched();
    if (name == "hw_pass_timings") return TestPassTimings();
    if (name == "hw_presentation_field_phase") return TestPresentationFieldPhase();

    Headless headless;
    std::string error;
    if (const char* off = std::getenv("GT4_HW_TEST_NO_INTERLOCK"); off && std::strcmp(off, "1") == 0) {
        headless.wantInterlock = false;  // IN-345: run the per-primitive path
    }
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
    handles.pixelInterlock = headless.interlock;
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
    if (name == "hw_gpu_raster_blend") return TestRasterBlendModes(drawer);
    if (name == "hw_gpu_raster_pabe") return TestRasterPabe(drawer);
    if (name == "hw_gpu_raster_atest") return TestRasterAlphaTest(drawer);
    if (name == "hw_gpu_raster_date") return TestRasterDate(drawer);
    if (name == "hw_gpu_raster_frame") return TestRasterFrameWrites(drawer);
    if (name == "hw_gpu_gouraud_report") return TestGouraudReport(drawer);
    if (name == "hw_glm_decode") return TestGlmDecode(drawer);
    if (name == "hw_glm_swizzle") return TestGlmSwizzle(drawer);
    if (name == "hw_glm_unswizzle") return TestGlmUnswizzle(drawer);
    if (name == "hw_glm_upload_merge") return TestGlmUploadMerge(drawer);
    if (name == "hw_glm_host_upload") return TestGlmHostUpload(drawer);
    if (name == "hw_glm_clut") return TestGlmClut(drawer);
    if (name == "hw_gpu_writeback_aliased_depth") return TestWritebackAliasedDepth(drawer);
    if (name == "hw_readback_pending_draw") return TestReadbackObservesPendingDraw(handles);
    if (name == "hw_replay_parity_double_buffer") return TestReplayParityDoubleBuffer(handles);
    if (name == "hw_cache_equals_writeback") return TestCacheEqualsWriteback(handles);
    std::printf("unknown test: %s\n", name.c_str());
    return 2;
}
