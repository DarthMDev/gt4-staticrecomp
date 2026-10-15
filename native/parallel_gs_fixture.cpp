// Headless paraLLEl-GS fixture (TASK-012, slice 162), ported from the slice 158 spike.
// Raw GIF qwords go through Backend::gif_transfer as the GS front end delivers them,
// privileged registers are mirrored by guest address, LOCAL->HOST reads use the transfer
// FIFO, signals arrive inline, and the scanout is copied back to host memory.
//
// Exit status: 0 all checks pass, 1 a check failed, 77 this machine cannot run
// paraLLEl-GS (CTest reports it as skipped, with the reason printed).

#include "gs_video_mode.hpp"
#include "parallel_gs_backend.hpp"
#include "png_stored.hpp"
#include "presented_scanout_dump.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {

using gt4::parallel_gs::Backend;
using gt4::parallel_gs::ScanoutImage;
using gt4::parallel_gs::ScanoutRequest;
using gt4::parallel_gs::SignalHandler;

constexpr int kSkipStatus = 77;
int failures = 0;
constexpr std::uint32_t kFrameWidth = 640;
constexpr std::uint32_t kFrameHeight = 448;
constexpr std::uint32_t kTextureBlock = 0x2000; // 2 MiB in 256-byte block units.
constexpr std::uint32_t kPmode = 0x12000000u;
constexpr std::uint32_t kSmode1 = 0x12000010u;
constexpr std::uint32_t kSmode2 = 0x12000020u;
constexpr std::uint32_t kDispfb1 = 0x12000070u;
constexpr std::uint32_t kDisplay1 = 0x12000080u;

struct Qword {
    std::uint64_t low, high;
};

// Builds a GIF packet stream: tags, A+D pairs and raw data qwords.
class GifStream {
public:
    void tag(std::uint32_t nloop, bool eop, std::uint32_t flg, std::uint32_t nreg, std::uint64_t regs,
             bool pre = false, std::uint32_t prim = 0) {
        const std::uint64_t low = (nloop & 0x7FFFull) | (std::uint64_t(eop) << 15) | (std::uint64_t(pre) << 46) |
                                  (std::uint64_t(prim & 0x7FF) << 47) | (std::uint64_t(flg & 3) << 58) |
                                  (std::uint64_t(nreg & 15) << 60);
        words.push_back({low, regs});
    }
    void address_data(std::uint32_t address, std::uint64_t value) { words.push_back({value, address}); }
    void raw(std::uint64_t low, std::uint64_t high) { words.push_back({low, high}); }
    const void *data() const { return words.data(); }
    std::size_t bytes() const { return words.size() * sizeof(Qword); }
    void clear() { words.clear(); }

private:
    std::vector<Qword> words;
};

// One A+D packet with N register writes.
void address_data_packet(GifStream &gif, const std::vector<std::pair<std::uint32_t, std::uint64_t>> &writes) {
    gif.tag(static_cast<std::uint32_t>(writes.size()), true, 0, 1, 0xE);
    for (const auto &[address, value] : writes)
        gif.address_data(address, value);
}

constexpr std::uint64_t frame_register(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm) {
    return fbp | (std::uint64_t(fbw) << 16) | (std::uint64_t(psm) << 24);
}
constexpr std::uint64_t scissor(std::uint32_t x0, std::uint32_t x1, std::uint32_t y0, std::uint32_t y1) {
    return x0 | (std::uint64_t(x1) << 16) | (std::uint64_t(y0) << 32) | (std::uint64_t(y1) << 48);
}
constexpr std::uint64_t xyz(std::uint32_t x, std::uint32_t y, std::uint32_t z = 0) {
    return ((x << 4) & 0xFFFF) | (std::uint64_t((y << 4) & 0xFFFF) << 16) | (std::uint64_t(z) << 32);
}
constexpr std::uint64_t rgbaq(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a) {
    return r | (g << 8) | (b << 16) | (std::uint64_t(a) << 24) | (std::uint64_t(0x3F800000) << 32);
}
constexpr std::uint64_t bitbltbuf(std::uint32_t sbp, std::uint32_t sbw, std::uint32_t spsm, std::uint32_t dbp,
                                  std::uint32_t dbw, std::uint32_t dpsm) {
    return sbp | (std::uint64_t(sbw) << 16) | (std::uint64_t(spsm) << 24) | (std::uint64_t(dbp) << 32) |
           (std::uint64_t(dbw) << 48) | (std::uint64_t(dpsm) << 56);
}
constexpr std::uint64_t trxpos(std::uint32_t sx, std::uint32_t sy, std::uint32_t dx, std::uint32_t dy) {
    return sx | (std::uint64_t(sy) << 16) | (std::uint64_t(dx) << 32) | (std::uint64_t(dy) << 48);
}
constexpr std::uint64_t trxreg(std::uint32_t w, std::uint32_t h) { return w | (std::uint64_t(h) << 32); }

struct SignalLog final : SignalHandler {
    int signals = 0;
    int finishes = 0;
    int labels = 0;
    std::uint64_t last_signal = 0;
    bool on_signal(std::uint64_t payload) override {
        ++signals;
        last_signal = payload;
        return false;
    }
    bool on_finish(std::uint64_t) override {
        ++finishes;
        return false;
    }
    bool on_label(std::uint64_t) override {
        ++labels;
        return false;
    }
};

std::vector<std::uint32_t> make_texture() {
    std::vector<std::uint32_t> texels(64 * 64);
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x)
            texels[y * 64 + x] = (((x / 8) ^ (y / 8)) & 1) ? 0x80FFFFFFu : 0x800000FFu; // white / red
    return texels;
}

// Context 1 setup, texture upload (host->local IMAGE), clear sprite, textured sprite,
// a PACKED Gouraud triangle, then SIGNAL / FINISH / LABEL.
void build_scene(GifStream &gif, std::uint32_t frame_index) {
    address_data_packet(gif, {
                                 {0x4C, frame_register(0, kFrameWidth / 64, 0)}, // FRAME_1
                                 {0x4E, (1ull << 32)},                           // ZBUF_1, ZMSK
                                 {0x40, scissor(0, kFrameWidth - 1, 0, kFrameHeight - 1)},
                                 {0x47, (1ull << 16) | (1ull << 17)}, // TEST_1: ZTE, ZTST=ALWAYS
                                 {0x18, 0},                           // XYOFFSET_1
                                 {0x1A, 1},                           // PRMODECONT AC=1
                                 {0x46, 1},                           // COLCLAMP
                             });
    address_data_packet(gif, {
                                 {0x50, bitbltbuf(0, 0, 0, kTextureBlock, 1, 0)},
                                 {0x51, trxpos(0, 0, 0, 0)},
                                 {0x52, trxreg(64, 64)},
                                 {0x53, 0}, // TRXDIR host->local
                             });
    const auto texels = make_texture();
    const std::uint32_t image_qwords = 64 * 64 * 4 / 16;
    gif.tag(image_qwords, true, 2, 0, 0);
    for (std::uint32_t i = 0; i < image_qwords; ++i) {
        std::uint64_t low, high;
        std::memcpy(&low, &texels[i * 4], 8);
        std::memcpy(&high, &texels[i * 4 + 2], 8);
        gif.raw(low, high);
    }
    const std::uint64_t tex0 = kTextureBlock | (1ull << 14) | (0ull << 20) | (6ull << 26) | (6ull << 30) |
                               (1ull << 34) | (1ull << 35); // TBW=1, CT32, 64x64, TCC, DECAL
    const std::uint8_t shade = static_cast<std::uint8_t>(0x20 + (frame_index & 0x1F));
    address_data_packet(gif, {
                                 {0x3F, 0},                  // TEXFLUSH
                                 {0x06, tex0},               // TEX0_1
                                 {0x14, 0},                  // TEX1_1 nearest
                                 {0x00, 6},                  // PRIM sprite
                                 {0x01, rgbaq(shade, 0x40, 0x80, 0x80)},
                                 {0x05, xyz(0, 0)},
                                 {0x05, xyz(kFrameWidth, kFrameHeight)},
                                 {0x00, 6 | (1 << 4) | (1 << 8)}, // sprite, TME, FST
                                 {0x03, 0},                       // UV (0,0)
                                 {0x05, xyz(100, 100)},
                                 {0x03, (64ull << 4) | ((64ull << 4) << 16)},
                                 {0x05, xyz(228, 228)},
                             });
    // PACKED Gouraud triangle: PRE sets PRIM = triangle | IIP; regs RGBAQ, XYZ2 per vertex.
    gif.tag(3, true, 0, 2, 0x51, true, 3 | (1 << 3));
    const std::uint32_t triangle[3][5] = {{255, 0, 0, 300, 300}, {0, 255, 0, 600, 300}, {0, 0, 255, 450, 440}};
    for (const auto &v : triangle) {
        gif.raw(v[0] | (std::uint64_t(v[1]) << 32), v[2] | (std::uint64_t(0x80) << 32));
        gif.raw((v[3] << 4) | (std::uint64_t(v[4] << 4) << 32), 0);
    }
    // PACKED ST, RGBAQ, XYZ2 sprite (paraLLEl-GS's optimized STQRGBAXYZ handler): 64x64 at (300, 20),
    // texture coordinates 0..1 with Q = 1, so pixel (300 + x, 20 + y) samples texel (x, y).
    gif.tag(2, true, 0, 3, 0x512, true, 6 | (1 << 4));
    const float st_corners[2] = {0.0f, 1.0f};
    const std::uint32_t xy_corners[2][2] = {{300, 20}, {364, 84}};
    for (int v = 0; v < 2; ++v) {
        std::uint32_t s_bits, q_bits;
        const float q = 1.0f;
        std::memcpy(&s_bits, &st_corners[v], 4);
        std::memcpy(&q_bits, &q, 4);
        gif.raw(s_bits | (std::uint64_t(s_bits) << 32), q_bits);
        gif.raw(0x80 | (std::uint64_t(0x80) << 32), 0x80 | (std::uint64_t(0x80) << 32));
        gif.raw((xy_corners[v][0] << 4) | (std::uint64_t(xy_corners[v][1] << 4) << 32), 0);
    }
    address_data_packet(gif, {
                                 {0x60, 0xFFFFFFFF00000000ull | (0x1234u + frame_index)}, // SIGNAL id, mask
                                 {0x61, 0},                                                // FINISH
                                 {0x62, 0xFFFFFFFF00000000ull | 0xABCDu},                  // LABEL
                             });
}

// LOCAL->HOST of a CT32 rectangle of the frame buffer through the transfer FIFO.
std::vector<std::uint32_t> read_frame_rect(Backend &backend, std::uint32_t x, std::uint32_t y, std::uint32_t w,
                                           std::uint32_t h) {
    GifStream gif;
    address_data_packet(gif, {
                                 {0x50, bitbltbuf(0, kFrameWidth / 64, 0, 0, 0, 0)},
                                 {0x51, trxpos(x, y, 0, 0)},
                                 {0x52, trxreg(w, h)},
                                 {0x53, 1}, // TRXDIR local->host
                             });
    backend.gif_transfer(3, gif.data(), gif.bytes());
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(w) * h);
    backend.read_transfer_fifo(pixels.data(), static_cast<std::uint32_t>(pixels.size() * 4 / 16));
    return pixels;
}

// Mirrors the NTSC interlaced frame registers. SMODE1/SMODE2 come from the SetGsCrt mapping,
// as the front end synthesises them for the donor's HLE SetGsCrt (slice 162 step 4). `field` is the
// SetGsCrt FFMD argument: 1 = frame mode (GT4's request), 0 = field mode.
void set_display_ntsc_interlaced_frame(Backend &backend, std::uint32_t field) {
    const auto video = gt4::gs::map_set_gs_crt({1, 0x02, field});
    if (video.status != gt4::gs::CrtMappingStatus::ok) {
        std::printf("[FAIL] SetGsCrt mapping for NTSC refused: %s\n", gt4::gs::describe(video.status));
        ++failures;
    }
    backend.write_privileged(kPmode, 1 | (1 << 2) | (1 << 5) | (0xFFull << 8)); // PMODE EN1 CRTMD=1 MMOD ALP
    backend.write_privileged(kSmode1, video.registers.smode1);
    backend.write_privileged(kSmode2, video.registers.smode2);
    backend.write_privileged(kDispfb1, (kFrameWidth / 64ull) << 9);             // FBP=0 FBW=10 CT32
    const std::uint64_t display = 636 | (50ull << 12) | (3ull << 23) | (std::uint64_t(kFrameWidth * 4 - 1) << 32) |
                                  (std::uint64_t(kFrameHeight - 1) << 44);
    backend.write_privileged(kDisplay1, display); // DX=636 DY=50 MAGH=3 DW=2559 DH=447
}

// A 480p display (SetGsCrt interlace 0, mode 0x50, as GT4 requests for races): progressive NTSC timing
// with the component clock (2 clocks per pixel), so DISPLAY uses MAGH=1, DW=1279, DX=318, DY=50 for a
// 640x448 picture. GT4's own 480p DISPLAY values are not logged yet (Unknown); this is the standard setup.
void set_display_480p_progressive(Backend &backend) {
    const auto video = gt4::gs::map_set_gs_crt({0, 0x50, 1});
    backend.write_privileged(kSmode1, video.registers.smode1);
    backend.write_privileged(kSmode2, video.registers.smode2);
    const std::uint64_t display = 318 | (50ull << 12) | (1ull << 23) | (std::uint64_t(kFrameWidth * 2 - 1) << 32) |
                                  (std::uint64_t(kFrameHeight - 1) << 44);
    backend.write_privileged(kDisplay1, display);
}

void expect(bool condition, const char *what) {
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition)
        ++failures;
}

// F10 PNG encoder (slice 188): check-value vectors for CRC-32 and Adler-32, the fixed IEND trailer, and a
// round trip through the stored deflate blocks (a frame above 64 KiB of scanlines, so two blocks).
void check_png_encoder() {
    const std::uint8_t digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    expect(gt4::png::crc32(0, digits, sizeof digits) == 0xCBF43926u, "PNG CRC-32 of \"123456789\" is 0xCBF43926");
    const std::uint8_t wiki[] = {'W', 'i', 'k', 'i', 'p', 'e', 'd', 'i', 'a'};
    expect(gt4::png::adler32(wiki, sizeof wiki) == 0x11E60398u, "zlib Adler-32 of \"Wikipedia\" is 0x11E60398");

    const std::uint32_t width = 100, height = 300;
    std::vector<std::uint8_t> pixels(std::size_t(width) * height * 4);
    for (std::size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = std::uint8_t(i * 7 + (i >> 9));
    const std::vector<std::uint8_t> file = gt4::png::encode_rgba(width, height, pixels.data());
    const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    expect(file.size() > 8 && std::equal(signature, signature + 8, file.begin()), "PNG signature");
    const std::uint8_t iend[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
    expect(file.size() >= 12 && std::equal(iend, iend + 12, file.end() - 12), "PNG ends with the IEND chunk");
    expect(file[16] == 0 && file[17] == 0 && file[18] == 0 && file[19] == 100 && file[20] == 0 && file[21] == 0 &&
               file[22] == 1 && file[23] == 44,
           "IHDR carries width 100 and height 300 (big-endian)");

    // Walk the chunks to the IDAT payload: zlib header, stored blocks, Adler-32.
    std::size_t at = 8;
    std::vector<std::uint8_t> zlib;
    while (at + 12 <= file.size()) {
        const std::uint32_t length = (std::uint32_t(file[at]) << 24) | (std::uint32_t(file[at + 1]) << 16) |
                                     (std::uint32_t(file[at + 2]) << 8) | file[at + 3];
        if (std::memcmp(&file[at + 4], "IDAT", 4) == 0)
            zlib.assign(file.begin() + std::ptrdiff_t(at + 8), file.begin() + std::ptrdiff_t(at + 8 + length));
        at += 12 + length;
    }
    std::vector<std::uint8_t> inflated;
    bool stored_ok = zlib.size() > 6 && zlib[0] == 0x78 && zlib[1] == 0x01;
    std::size_t position = 2;
    int blocks = 0;
    while (stored_ok && position + 5 <= zlib.size() - 4) {
        const bool final_block = (zlib[position] & 1u) != 0;
        const std::size_t size = zlib[position + 1] | (std::size_t(zlib[position + 2]) << 8);
        const std::size_t complement = zlib[position + 3] | (std::size_t(zlib[position + 4]) << 8);
        stored_ok = (zlib[position] & 6u) == 0 && (size ^ 0xFFFFu) == complement &&
                    position + 5 + size <= zlib.size() - 4;
        if (!stored_ok)
            break;
        inflated.insert(inflated.end(), zlib.begin() + std::ptrdiff_t(position + 5),
                        zlib.begin() + std::ptrdiff_t(position + 5 + size));
        position += 5 + size;
        ++blocks;
        if (final_block)
            break;
    }
    bool rows_ok = stored_ok && inflated.size() == std::size_t(height) * (width * 4 + 1);
    for (std::uint32_t y = 0; rows_ok && y < height; ++y) {
        const std::size_t row = std::size_t(y) * (width * 4 + 1);
        rows_ok = inflated[row] == 0 &&
                  std::equal(pixels.begin() + std::ptrdiff_t(std::size_t(y) * width * 4),
                             pixels.begin() + std::ptrdiff_t(std::size_t(y + 1) * width * 4),
                             inflated.begin() + std::ptrdiff_t(row + 1));
    }
    expect(rows_ok && blocks == 2, "IDAT inflates to the filtered scanlines (two stored blocks)");
}

bool has_non_zero_pixel(const ScanoutImage &image) {
    for (std::uint8_t byte : image.rgba)
        if (byte)
            return true;
    return false;
}

// Window presentation (slice 168, step 6) on a hidden test window: no game, nothing shown. Checks that
// the Granite WSI swapchain is created on an HWND, that present() succeeds frame after frame, that a
// resize and a zero-sized (minimised) client are handled, and that the 16:9 rectangle matches the
// donor presenter's arithmetic.
void check_window_presentation(Backend &backend) {
    using gt4::parallel_gs::widescreen_rect;
    constexpr auto full = widescreen_rect(960, 540);
    constexpr auto tall = widescreen_rect(1920, 1200);
    constexpr auto wide = widescreen_rect(2560, 1080);
    expect(full.left == 0 && full.top == 0 && full.width == 960 && full.height == 540, "16:9 rect fills a 960x540 client");
    expect(tall.left == 0 && tall.top == 60 && tall.width == 1920 && tall.height == 1080, "16:9 rect letterboxes 1920x1200");
    expect(wide.left == 320 && wide.top == 0 && wide.width == 1920 && wide.height == 1080, "16:9 rect pillarboxes 2560x1080");
    expect(widescreen_rect(0, 0).width == 0, "zero-sized client gives an empty rect");
    using gt4::parallel_gs::present_filter;
    using gt4::parallel_gs::PresentFilter;
    expect(present_filter(false, 640, 448, full) == PresentFilter::nearest, "1x path keeps nearest (donor behaviour)");
    expect(present_filter(true, 1280, 447, full) == PresentFilter::linear, "high-resolution downscale uses linear");
    expect(present_filter(true, 960, 540, full) == PresentFilter::nearest, "exact 1:1 high-resolution uses nearest");
    expect(present_filter(true, 960, 540, tall) == PresentFilter::nearest, "exact 2x upscale uses nearest");
    expect(present_filter(true, 1280, 896, tall) == PresentFilter::linear, "non-integer upscale uses linear");
    using gt4::parallel_gs::high_resolution_target;
    expect(high_resolution_target(full, 447, true).height == 539 && high_resolution_target(full, 447, true).top == 0,
           "447-line frame-mode image keeps the 448-line scale (539 of 540 rows)");
    expect(high_resolution_target(full, 896, true).height == 540 && high_resolution_target(full, 447, false).height == 540,
           "even heights and the 1x path fill the rectangle");

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"GT4ParallelGsFixture";
    RegisterClassW(&window_class);
    HWND window = CreateWindowExW(0, window_class.lpszClassName, L"paraLLEl-GS fixture", WS_OVERLAPPEDWINDOW, 0, 0, 960, 540,
                                  nullptr, nullptr, window_class.hInstance, nullptr);
    expect(window != nullptr, "hidden test window created");
    if (!window)
        return;
    gt4::parallel_gs::WindowTarget target;
    target.window = window;
    target.width = 960;
    target.height = 540;
    // FIFO, as the game window uses unless GT4_PRESENT_MODE says otherwise.
    target.mode = gt4::parallel_gs::PresentMode::fifo;
    std::string error;
    const bool attached = backend.attach_window(target, error);
    if (!attached)
        std::printf("       attach_window: %s\n", error.c_str());
    expect(attached && backend.has_window(), "Granite WSI swapchain created on the window (FIFO, UNORM)");
    std::string second_error;
    expect(!backend.attach_window(target, second_error), "a second attach_window is refused");
    if (attached) {
        GifStream gif;
        build_scene(gif, 1);
        backend.gif_transfer(3, gif.data(), gif.bytes());
        bool all_presented = true;
        for (std::uint32_t frame = 0; frame < 6; ++frame)
            all_presented = backend.present(frame & 1) && all_presented;
        expect(all_presented, "six consecutive presents with a scanout image");
        // F10 (slice 188): the request writes the presented image as scanout-NNNN.png at its native size.
        gt4::presented_scanout_dump_requested().store(true);
        expect(backend.present(0), "a present with the F10 request");
        expect(!gt4::presented_scanout_dump_requested().load(), "the F10 request is consumed by the present");
        if (std::FILE *dump = std::fopen("scanout-0001.png", "rb")) {
            std::uint8_t head[24] = {};
            const std::size_t read = std::fread(head, 1, sizeof head, dump);
            std::fclose(dump);
            const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
            const std::uint32_t dump_width = (std::uint32_t(head[16]) << 24) | (std::uint32_t(head[17]) << 16) |
                                             (std::uint32_t(head[18]) << 8) | head[19];
            const std::uint32_t dump_height = (std::uint32_t(head[20]) << 24) | (std::uint32_t(head[21]) << 16) |
                                              (std::uint32_t(head[22]) << 8) | head[23];
            expect(read == sizeof head && std::equal(signature, signature + 8, head) && dump_width == 640 &&
                       dump_height == 448,
                   "F10 writes scanout-0001.png: PNG signature, native 640x448 (1x)");
            std::remove("scanout-0001.png");
        } else {
            expect(false, "F10 writes scanout-0001.png in the working directory");
        }
        backend.resize_window(640, 360);
        expect(backend.present(0), "present after a resize to 640x360 rebuilds the swapchain");
        backend.resize_window(0, 0);
        expect(!backend.present(1), "a zero-sized (minimised) client presents nothing");
        backend.resize_window(960, 540);
        expect(backend.present(1), "present resumes after the client is restored");
        // High-resolution presentation (slice 169) in both GT4 display modes.
        expect(backend.set_super_sampling(4), "4x super-sampling for the high-resolution presents");
        backend.gif_transfer(3, gif.data(), gif.bytes());
        bool high_resolution_presented = backend.present(0, true) && backend.present(1, true);
        set_display_480p_progressive(backend);
        high_resolution_presented = backend.present(0, true) && backend.present(1, true) && high_resolution_presented;
        expect(high_resolution_presented, "high-resolution presents at 4x in 480i frame mode and 480p");
        set_display_ntsc_interlaced_frame(backend, 1);
        expect(backend.set_super_sampling(1), "back to 1x after the high-resolution presents");
    }
    backend.wait_idle();
    // The swapchain stays with the backend (never destroyed while the process runs, as in the game);
    // the window outlives it until process exit.
}

} // namespace

int main() {
    // Unbuffered: a crash must not hide the checks that already ran.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Backend backend;
    std::string error;
    if (!backend.init(error)) {
        std::printf("[SKIP] %s\n", error.c_str());
        return kSkipStatus;
    }
    expect(true, "headless Granite device and paraLLEl-GS initialised (feature check passed)");

    SignalLog signals;
    backend.set_signal_handler(&signals);
    expect(backend.set_super_sampling(1), "1x super-sampling accepted");
    expect(!backend.set_super_sampling(3), "unsupported super-sampling factor 3 refused");
    expect(!backend.write_privileged(0x11000000u, 1) && backend.read_privileged(0x11000000u) == 0,
           "privileged writes outside 0x12000000-0x12001FFF are refused");

    set_display_ntsc_interlaced_frame(backend, 1);
    const std::uint64_t pmode = backend.read_privileged(kPmode);
    const std::uint64_t display = backend.read_privileged(kDisplay1);
    expect((pmode & 1) == 1 && ((pmode >> 2) & 1) == 1 && ((display >> 32) & 0xFFF) == kFrameWidth * 4 - 1,
           "privileged register mirroring by guest address keeps the field layout");

    GifStream gif;
    build_scene(gif, 0);
    backend.gif_transfer(3, gif.data(), gif.bytes());
    expect(signals.signals == 1 && signals.finishes == 1 && signals.labels == 1,
           "SIGNAL/FINISH/LABEL delivered synchronously during gif_transfer");
    expect((signals.last_signal & 0xFFFFFFFFu) == 0x1234u, "SIGNAL payload carries ID and mask");

    const auto clear = read_frame_rect(backend, 8, 8, 8, 8);
    expect(clear[0] == 0x80804020u, "LOCAL->HOST FIFO readback returns the clear colour");
    const auto textured = read_frame_rect(backend, 100, 100, 8, 2);
    expect(textured[0] == 0x800000FFu, "textured sprite sampled the uploaded IMAGE data (texel 0,0 = red)");
    const auto textured_white = read_frame_rect(backend, 116, 100, 4, 1);
    expect(textured_white[0] == 0x80FFFFFFu, "2x scaled checker: x=116 maps to texel 8 (white)");

    const auto st_red = read_frame_rect(backend, 300, 20, 4, 1);
    const auto st_white = read_frame_rect(backend, 308, 20, 4, 1);
    expect(st_red[0] == 0x800000FFu && st_white[0] == 0x80FFFFFFu,
           "PACKED ST/RGBAQ/XYZ2 sprite (optimized handler) samples texels 0 and 8");

    const auto *vram = static_cast<const std::uint8_t *>(backend.map_vram(kTextureBlock * 256, 64 * 64 * 4));
    std::uint32_t first_texel = 0;
    if (vram)
        std::memcpy(&first_texel, vram, 4);
    expect(vram && first_texel == 0x800000FFu, "map_vram sees the uploaded texture (block 0 column order)");

    ScanoutRequest request;
    request.phase = 0;
    request.force_progressive = true;
    request.anti_blur = true;
    backend.flush();
    const ScanoutImage frame = backend.scanout(request);
    expect(frame.valid && frame.width == kFrameWidth && frame.height == kFrameHeight,
           "1x scanout of the NTSC frame is 640x448");
    expect(frame.valid && has_non_zero_pixel(frame), "1x scanout contains drawn pixels");
    backend.next_frame();

    // Unknown video format: SMODE1 left at zero must yield no image rather than garbage.
    backend.write_privileged(kSmode1, 0);
    backend.flush();
    const ScanoutImage unknown = backend.scanout(request);
    expect(!unknown.valid, "SMODE1=0 yields no scanout image");
    // The scene is one 448-line buffer read on alternate lines, a field-mode layout. In frame mode
    // (GT4's FFMD=1) paraLLEl-GS expects one 224-line buffer per field and its 4x high-resolution
    // scanout of this buffer is 1280x447 (slice 167), so the 4x check uses field mode.
    set_display_ntsc_interlaced_frame(backend, 0);
    backend.next_frame();

    // The front end delivers GIF data per DMA chunk, VIF DIRECT or XGKICK, so packets span calls.
    // The same scene sent in 1-, 2-, 5- and 7-qword pieces must give the same frame buffer as one call.
    gif.clear();
    build_scene(gif, 0);
    backend.clear_vram();
    backend.gif_transfer(3, gif.data(), gif.bytes());
    const auto whole_frame = read_frame_rect(backend, 0, 0, kFrameWidth, kFrameHeight);
    for (const std::size_t piece_qwords : {std::size_t(1), std::size_t(2), std::size_t(5), std::size_t(7)}) {
        backend.clear_vram();
        const auto *bytes = static_cast<const std::uint8_t *>(gif.data());
        for (std::size_t offset = 0; offset < gif.bytes(); offset += piece_qwords * sizeof(Qword)) {
            const std::size_t piece_bytes = std::min(piece_qwords * sizeof(Qword), gif.bytes() - offset);
            // A private copy, so a reader that runs past the piece sees poison, not the next piece.
            std::vector<std::uint8_t> piece(bytes + offset, bytes + offset + piece_bytes);
            piece.resize(piece_bytes + 64 * sizeof(Qword), 0xCD);
            backend.gif_transfer(3, piece.data(), piece_bytes);
        }
        const auto split_frame = read_frame_rect(backend, 0, 0, kFrameWidth, kFrameHeight);
        char what[96];
        std::snprintf(what, sizeof what, "GIF packets split into %zu-qword transfers give the one-call frame buffer",
                      piece_qwords);
        expect(split_frame == whole_frame, what);
    }
    expect(!backend.gif_busy(3), "path 3 is idle after the last split packet");

    expect(backend.set_super_sampling(4), "4x super-sampling accepted");
    gif.clear();
    build_scene(gif, 0);
    backend.gif_transfer(3, gif.data(), gif.bytes());
    const auto clear_4x = read_frame_rect(backend, 8, 8, 8, 8);
    expect(clear_4x[0] == 0x80804020u, "4x SSAA: FIFO readback still returns single-sample VRAM values");
    ScanoutRequest high_resolution = request;
    high_resolution.high_resolution = true;
    backend.flush();
    const ScanoutImage frame_4x = backend.scanout(high_resolution);
    expect(frame_4x.valid && frame_4x.width == 2 * kFrameWidth && frame_4x.height == 2 * kFrameHeight,
           "4x SSAA high-resolution scanout is 1280x896");
    backend.next_frame();

    // Slice 169: GT4's two display modes at 4x, 8x and 16x. Menus are 480i frame mode (FFMD=1, one
    // 224-line field per VBlank, DISPLAY DH=447); races at 480p are progressive (448 lines). In frame
    // mode paraLLEl-GS uses field-aware rendering: both output fields come from the super samples of
    // the one field buffer, and it drops the last line to avoid a one-line flicker, hence 1280x447
    // (upstream design, gs_renderer.cpp "Avoid a one line flicker due to field rendering").
    for (const std::uint32_t factor : {4u, 8u, 16u}) {
        expect(backend.set_super_sampling(factor), "super-sampling factor accepted");
        gif.clear();
        build_scene(gif, 0);
        backend.gif_transfer(3, gif.data(), gif.bytes());
        char what[128];
        set_display_ntsc_interlaced_frame(backend, 1);
        backend.flush();
        const ScanoutImage frame_mode = backend.scanout(high_resolution);
        std::snprintf(what, sizeof what, "%ux SSAA, 480i frame mode: high-resolution scanout is 1280x447 (got %ux%u)", factor,
                      frame_mode.width, frame_mode.height);
        expect(frame_mode.valid && frame_mode.width == 2 * kFrameWidth && frame_mode.height == 2 * 224 - 1, what);
        const ScanoutImage frame_mode_1x = backend.scanout(request);
        std::snprintf(what, sizeof what, "%ux SSAA, 480i frame mode: 1x scanout is still 640x448", factor);
        expect(frame_mode_1x.valid && frame_mode_1x.width == kFrameWidth && frame_mode_1x.height == kFrameHeight, what);
        set_display_480p_progressive(backend);
        backend.flush();
        const ScanoutImage progressive = backend.scanout(high_resolution);
        std::snprintf(what, sizeof what, "%ux SSAA, 480p progressive: high-resolution scanout is 1280x896 (got %ux%u)", factor,
                      progressive.width, progressive.height);
        expect(progressive.valid && progressive.width == 2 * kFrameWidth && progressive.height == 2 * kFrameHeight, what);
        backend.next_frame();
    }

    // Slice 176: 3x and 4x high-resolution scanouts from the 16x ordered grid (4x4 super samples per pixel).
    // Progressive 480p: the 4x image holds one super sample per output pixel, the 3x image the nearest
    // sample to each output centre, and the 2x image the average of each 2x2 quadrant (donor shader).
    // Both checks are exact: they compare the same render at the same sample positions.
    expect(backend.set_super_sampling(16), "16x super-sampling for the 3x and 4x scanouts");
    set_display_480p_progressive(backend);
    gif.clear();
    build_scene(gif, 0);
    backend.gif_transfer(3, gif.data(), gif.bytes());
    backend.flush();
    ScanoutRequest scaled = request;
    scaled.high_resolution = true;
    scaled.high_resolution_scale = 2;
    const ScanoutImage scanout_2x = backend.scanout(scaled);
    scaled.high_resolution_scale = 3;
    const ScanoutImage scanout_3x = backend.scanout(scaled);
    scaled.high_resolution_scale = 4;
    const ScanoutImage scanout_4x = backend.scanout(scaled);
    expect(scanout_2x.valid && scanout_2x.width == 2 * kFrameWidth && scanout_2x.height == 2 * kFrameHeight &&
               scanout_2x.high_resolution_scale == 2,
           "16x SSAA, 480p: the scale 2 request still gives the donor 1280x896 image");
    expect(scanout_3x.valid && scanout_3x.width == 3 * kFrameWidth && scanout_3x.height == 3 * kFrameHeight &&
               scanout_3x.high_resolution_scale == 3,
           "16x SSAA, 480p: the scale 3 request gives 1920x1344");
    expect(scanout_4x.valid && scanout_4x.width == 4 * kFrameWidth && scanout_4x.height == 4 * kFrameHeight &&
               scanout_4x.high_resolution_scale == 4,
           "16x SSAA, 480p: the scale 4 request gives 2560x1792");

    if (scanout_2x.valid && scanout_4x.valid) {
        // Output pixel (x, y) at 2x is the mean of the 2x2 block (2x..2x+1, 2y..2y+1) at 4x.
        std::uint64_t mismatches_2x = 0;
        int worst_2x = 0;
        for (std::uint32_t y = 0; y < 2 * kFrameHeight; ++y)
            for (std::uint32_t x = 0; x < 2 * kFrameWidth; ++x)
                for (std::uint32_t channel = 0; channel < 4; ++channel) {
                    int sum = 0;
                    for (std::uint32_t dy = 0; dy < 2; ++dy)
                        for (std::uint32_t dx = 0; dx < 2; ++dx)
                            sum += scanout_4x.rgba[((2 * y + dy) * scanout_4x.width + 2 * x + dx) * 4 + channel];
                    const int mean = (sum + 2) / 4;
                    const int delta = std::abs(mean - int(scanout_2x.rgba[(y * scanout_2x.width + x) * 4 + channel]));
                    worst_2x = std::max(worst_2x, delta);
                    if (delta > 1)
                        ++mismatches_2x;
                }
        char what[160];
        std::snprintf(what, sizeof what,
                      "4x samples average to the donor 2x quadrants (mismatches over 1: %llu, worst %d)",
                      static_cast<unsigned long long>(mismatches_2x), worst_2x);
        expect(mismatches_2x == 0, what);
    }

    if (scanout_3x.valid && scanout_4x.valid) {
        // 3x output pixel (x, y): PS2 pixel (x / 3, y / 3), grid sample g = floor((2l + 1) * 4 / 6) for local l.
        std::uint64_t mismatches_3x = 0;
        for (std::uint32_t y = 0; y < 3 * kFrameHeight; ++y)
            for (std::uint32_t x = 0; x < 3 * kFrameWidth; ++x) {
                const std::uint32_t gx = ((2 * (x % 3) + 1) * 4) / 6;
                const std::uint32_t gy = ((2 * (y % 3) + 1) * 4) / 6;
                const std::uint32_t source_x = (x / 3) * 4 + gx;
                const std::uint32_t source_y = (y / 3) * 4 + gy;
                const std::size_t a = (y * scanout_3x.width + x) * 4;
                const std::size_t b = (source_y * scanout_4x.width + source_x) * 4;
                if (std::memcmp(&scanout_3x.rgba[a], &scanout_4x.rgba[b], 4) != 0)
                    ++mismatches_3x;
            }
        char what[160];
        std::snprintf(what, sizeof what, "3x pixels are the nearest 4x super samples (mismatching pixels: %llu)",
                      static_cast<unsigned long long>(mismatches_3x));
        expect(mismatches_3x == 0, what);
    }

    // Field-aware 480i frames keep the donor 2x image when 3x or 4x is requested (slice 176 fallback).
    set_display_ntsc_interlaced_frame(backend, 1);
    gif.clear();
    build_scene(gif, 0);
    backend.gif_transfer(3, gif.data(), gif.bytes());
    backend.flush();
    scaled.high_resolution_scale = 4;
    const ScanoutImage frame_mode_4x = backend.scanout(scaled);
    expect(frame_mode_4x.valid && frame_mode_4x.width == 2 * kFrameWidth && frame_mode_4x.height == 2 * 224 - 1 &&
               frame_mode_4x.high_resolution_scale == 2,
           "16x SSAA, 480i frame mode: a scale 4 request falls back to the 1280x447 field-aware image");
    backend.next_frame();
    backend.wait_idle();

    // Back to 1x and the GT4 frame-mode registers before the window checks.
    expect(backend.set_super_sampling(1), "1x super-sampling restored");
    set_display_ntsc_interlaced_frame(backend, 1);
    check_window_presentation(backend);

    check_png_encoder();

    std::printf("failures: %d\n", failures);
    return failures ? 1 : 0;
}
