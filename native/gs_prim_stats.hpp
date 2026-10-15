// GS primitive statistics and owner frame capture (slice 186, TASK-028).
// Stats: GT4_GS_PRIM_STATS=1 adds one "[gs-stats]" line to each F8 report. Off by default; with the
// variable unset nothing is counted and no output changes.
// Capture: F10 writes the next presented frame at its scaled size and as a 1x box average (PNG), with
// the primitive statistics of that frame, into the newest private/manual-play/session-* folder.
// Scaled-frame switches (PS2_SCALE > 1 only; read once, folded into the setup flags word, bits 16-18):
//   GT4_GS_SPRITE_SUBSAMPLE=0  sprite coverage at the integer pixel (slice 176 behaviour)
//   GT4_GS_SPRITE_SNAP=1       sprite texel coordinate snapped to the integer pixel (slice 176 behaviour)
// Line sub-sample coverage is not implemented (see docs/BUILD.md slice 186).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace gs_prim_stats {

constexpr std::uint32_t kFlagSpriteSub = 1u << 16; // matches F_SPRITE_SUB in scripts/derive_upscale_gs_shader.py
constexpr std::uint32_t kFlagNoSnap = 1u << 18;    // matches F_NO_SNAP
constexpr std::uint32_t kSetupFlagTme = 2u, kSetupFlagFst = 16u, kSetupFlagAte = 64u, kSetupFlagBilinear = 16384u;
constexpr std::uint32_t kNoDecode = 0xFFFFFFFFu;

inline bool env_equals(const char *name, const char *value) {
    const char *v = std::getenv(name);
    return v && std::strcmp(v, value) == 0;
}

inline void print_totals_at_exit();

inline bool stats_enabled() {
    static const bool on = [] {
        const bool enabled = env_equals("GT4_GS_PRIM_STATS", "1");
        // Headless runs never present (no F8): print the run totals at exit.
        if (enabled) std::atexit(print_totals_at_exit);
        return enabled;
    }();
    return on;
}

// Flag bits folded into every setup word at PS2_SCALE > 1; zero at scale 1 (output unchanged).
inline std::uint32_t upscale_setup_bits(std::uint32_t scale) {
    if (scale <= 1) return 0;
    std::uint32_t bits = 0;
    if (!env_equals("GT4_GS_SPRITE_SUBSAMPLE", "0")) bits |= kFlagSpriteSub;
    if (!env_equals("GT4_GS_SPRITE_SNAP", "1")) bits |= kFlagNoSnap;
    return bits;
}

// Sprite box start: the first integer pixel that holds a sub-sample inside the sprite.
inline int sprite_box_min(std::uint32_t flags, float lo) {
    return (flags & kFlagSpriteSub) ? static_cast<int>(std::floor(lo)) : static_cast<int>(std::ceil(lo));
}

struct Counters {
    std::uint64_t batches = 0, primitives = 0;
    std::uint64_t triangles = 0, sprites = 0, lines = 0, points = 0;
    std::uint64_t textured = 0, untextured = 0, alpha_test = 0, bilinear = 0;
    std::uint64_t decoded = 0, direct = 0, stale_decode = 0;
    std::uint64_t sprite_snap_u = 0, sprite_snap_v = 0;
    std::uint64_t texture_format[256] = {};
};

inline Counters &interval_counters() { static Counters c; return c; }
inline Counters &frame_counters() { static Counters c; return c; }
inline std::uint64_t &frames_presented() { static std::uint64_t n = 0; return n; }
inline std::atomic<bool> &capture_requested() { static std::atomic<bool> r{false}; return r; }

inline float float_bits(std::uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

inline void add(Counters &c, const std::uint32_t *prim, const std::uint32_t *setup) {
    const std::uint32_t kind = prim[1];
    const std::uint32_t flags = setup[0];
    const bool textured = (flags & kSetupFlagTme) != 0;
    const bool decoded = setup[24] != kNoDecode;
    const std::uint32_t tpsm = setup[14] & 0xFFu;
    ++c.primitives;
    switch (kind) {
    case 0: ++c.triangles; break;
    case 1: ++c.sprites; break;
    case 2: ++c.lines; break;
    default: ++c.points; break;
    }
    if (textured) {
        ++c.textured;
        ++c.texture_format[tpsm];
        decoded ? ++c.decoded : ++c.direct;
    } else {
        ++c.untextured;
    }
    if (flags & kSetupFlagAte) ++c.alpha_test;
    if (flags & kSetupFlagBilinear) ++c.bilinear;
    if (kind == 1 && textured) {
        // Same test as shade_at case 1 in gs.wgsl: point sampling and texel rate >= pixel rate.
        const bool direct = tpsm == 0x00u || tpsm == 0x01u || tpsm == 0x02u || tpsm == 0x0Au || tpsm >= 0x30u;
        const bool point = (!(flags & kSetupFlagBilinear)) || direct;
        if (!point) return;
        const std::uint32_t tw = setup[15] & 0xFFu, th = setup[15] >> 8;
        float du = float_bits(prim[18]) - float_bits(prim[15]);
        float dv = float_bits(prim[19]) - float_bits(prim[16]);
        const float dx = float_bits(prim[6]), dy = float_bits(prim[7]);
        if (flags & kSetupFlagFst) {
            du /= 16.0f;
            dv /= 16.0f;
        } else {
            float qu = float_bits(prim[20]), qv = qu;
            if (qu == 0.0f) qu = qv = 1.0f;
            du = du / qu * static_cast<float>(1u << tw);
            dv = dv / qv * static_cast<float>(1u << th);
        }
        if (std::fabs(du) >= std::fabs(dx) * 0.999f) ++c.sprite_snap_u;
        if (std::fabs(dv) >= std::fabs(dy) * 0.999f) ++c.sprite_snap_v;
    }
}

// Called once per batch from flush_batch (see native/gs_prim_stats_hook.inc).
inline void note_batch(const std::uint32_t *prims, std::uint32_t count, const std::uint32_t *setups, std::uint32_t stride) {
    ++interval_counters().batches;
    ++frame_counters().batches;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t *prim = prims + i * 28u;
        const std::uint32_t *setup = setups + prim[0] * stride;
        add(interval_counters(), prim, setup);
        add(frame_counters(), prim, setup);
    }
}

inline void note_stale_decode() {
    ++interval_counters().stale_decode;
    ++frame_counters().stale_decode;
}

inline void print_counters(std::FILE *out, const char *label, const Counters &c, std::uint64_t frames) {
    const double per = frames ? static_cast<double>(frames) : 1.0;
    std::fprintf(out, "[gs-stats] %s frames=%llu batches=%llu prims/frame=%.1f tri=%.1f sprite=%.1f line=%.1f point=%.1f ",
                 label, static_cast<unsigned long long>(frames), static_cast<unsigned long long>(c.batches),
                 c.primitives / per, c.triangles / per, c.sprites / per, c.lines / per, c.points / per);
    std::fprintf(out, "textured=%.1f untextured=%.1f ate=%.1f bilinear=%.1f decoded=%.1f direct=%.1f stale_decode=%.1f ",
                 c.textured / per, c.untextured / per, c.alpha_test / per, c.bilinear / per, c.decoded / per,
                 c.direct / per, c.stale_decode / per);
    std::fprintf(out, "sprite_snap_u=%.1f sprite_snap_v=%.1f tpsm=", c.sprite_snap_u / per, c.sprite_snap_v / per);
    bool any = false;
    for (int psm = 0; psm < 256; ++psm)
        if (c.texture_format[psm]) {
            std::fprintf(out, "%s0x%02X:%.1f", any ? "," : "", psm, c.texture_format[psm] / per);
            any = true;
        }
    if (!any) std::fprintf(out, "none");
    std::fprintf(out, "\n");
}

// F8 report: prints the interval since the last report, then resets it. Silent unless stats are enabled.
inline void report(std::FILE *out) {
    if (!stats_enabled()) return;
    print_counters(out, "interval", interval_counters(), frames_presented());
    interval_counters() = Counters{};
    frames_presented() = 0;
}

inline void print_totals_at_exit() {
    print_counters(stderr, "totals", interval_counters(), 0);
}

inline void request_capture() { capture_requested().store(true); }
inline bool take_capture() { return capture_requested().exchange(false); }

// Minimal PNG writer (stored deflate blocks, no compression library).
inline std::uint32_t crc32_of(const std::uint8_t *data, std::size_t size, std::uint32_t crc = 0) {
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

inline void put_be32(std::vector<std::uint8_t> &out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

inline void png_chunk(std::vector<std::uint8_t> &out, const char tag[4], const std::vector<std::uint8_t> &body) {
    put_be32(out, static_cast<std::uint32_t>(body.size()));
    std::vector<std::uint8_t> block(tag, tag + 4);
    block.insert(block.end(), body.begin(), body.end());
    out.insert(out.end(), block.begin(), block.end());
    put_be32(out, crc32_of(block.data(), block.size()));
}

// rgba: 0xAABBGGRR words as produced by the display pass (R in the low byte).
inline bool write_png(const std::string &path, std::uint32_t width, std::uint32_t height, const std::vector<std::uint32_t> &rgba) {
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (width * 3 + 1));
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint32_t p = rgba[static_cast<std::size_t>(y) * width + x];
            raw.push_back(static_cast<std::uint8_t>(p));
            raw.push_back(static_cast<std::uint8_t>(p >> 8));
            raw.push_back(static_cast<std::uint8_t>(p >> 16));
        }
    }
    std::vector<std::uint8_t> zlib = {0x78, 0x01};
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    for (std::size_t at = 0; at < raw.size() || at == 0;) {
        const std::size_t size = std::min<std::size_t>(65535, raw.size() - at);
        const bool last = at + size >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<std::uint8_t>(size));
        zlib.push_back(static_cast<std::uint8_t>(size >> 8));
        zlib.push_back(static_cast<std::uint8_t>(~size));
        zlib.push_back(static_cast<std::uint8_t>(~size >> 8));
        zlib.insert(zlib.end(), raw.begin() + at, raw.begin() + at + size);
        at += size;
        if (last) break;
    }
    put_be32(zlib, (b << 16) | a);
    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<std::uint8_t> header;
    put_be32(header, width);
    put_be32(header, height);
    header.insert(header.end(), {8, 2, 0, 0, 0}); // 8-bit RGB
    png_chunk(png, "IHDR", header);
    png_chunk(png, "IDAT", zlib);
    png_chunk(png, "IEND", {});
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), file) == png.size();
    std::fclose(file);
    return ok;
}

// Newest private/manual-play/session-* folder under the working directory, created if missing.
inline std::filesystem::path capture_directory() {
    namespace fs = std::filesystem;
    const fs::path base = fs::path("private") / "manual-play";
    std::error_code error;
    fs::path newest;
    if (fs::is_directory(base, error)) {
        for (const auto &entry : fs::directory_iterator(base, error)) {
            const std::string name = entry.path().filename().string();
            if (entry.is_directory() && name.rfind("session-", 0) == 0 && (newest.empty() || name > newest.filename().string()))
                newest = entry.path();
        }
    }
    if (newest.empty()) newest = base / "capture";
    fs::create_directories(newest, error);
    return newest;
}

// Writes the capture for the presented frame. `native` is scale-sized RGBA, `box` the 1x average.
inline void save_capture(const std::vector<std::uint32_t> &native, std::uint32_t native_width, std::uint32_t native_height,
                         const std::vector<std::uint32_t> &box, std::uint32_t width, std::uint32_t height, std::uint32_t scale) {
    const std::filesystem::path dir = capture_directory();
    static std::uint32_t capture_index = 0;
    ++capture_index;
    char stem[64];
    std::snprintf(stem, sizeof stem, "frame-%04u", capture_index);
    const std::string native_path = (dir / (std::string(stem) + "-" + std::to_string(scale) + "x.png")).string();
    const std::string box_path = (dir / (std::string(stem) + "-1x.png")).string();
    const std::string text_path = (dir / (std::string(stem) + "-stats.txt")).string();
    const bool native_ok = write_png(native_path, native_width, native_height, native);
    const bool box_ok = write_png(box_path, width, height, box);
    std::FILE *text = std::fopen(text_path.c_str(), "w");
    if (text) {
        std::fprintf(text, "native=%s (%ux%u, scale %u) ok=%d\n", native_path.c_str(), native_width, native_height, scale, native_ok ? 1 : 0);
        std::fprintf(text, "1x=%s (%ux%u) ok=%d\n", box_path.c_str(), width, height, box_ok ? 1 : 0);
        if (stats_enabled()) print_counters(text, "frame", frame_counters(), 1);
        else std::fprintf(text, "primitive stats: disabled (set GT4_GS_PRIM_STATS=1)\n");
        std::fclose(text);
    }
    std::fprintf(stderr, "[capture] %s and %s\n", native_path.c_str(), box_path.c_str());
}

// Called once per presented frame.
inline void frame_presented() {
    ++frames_presented();
    // Headless runs have no F8: GT4_GS_PRIM_STATS_TRACE=1 (with GT4_GS_PRIM_STATS=1) prints each frame.
    if (stats_enabled() && env_equals("GT4_GS_PRIM_STATS_TRACE", "1")) print_counters(stderr, "frame", frame_counters(), 1);
    frame_counters() = Counters{};
}

} // namespace gs_prim_stats
