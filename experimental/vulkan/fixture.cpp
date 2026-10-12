#include "gpu.hpp"
#include "gs/raster.hpp"
#include "native_vulkan.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
using namespace ps2::gs;

void compare_memory(const std::vector<std::uint8_t> &actual,
                    const char *label) {
  const auto *expected = memory();
  for (std::size_t index = 0; index < actual.size(); ++index) {
    if (actual[index] != expected[index]) {
      throw std::runtime_error(std::string(label) + " differs at VRAM byte " +
                               std::to_string(index) +
                               ": GPU=" + std::to_string(actual[index]) +
                               ", CPU=" + std::to_string(expected[index]));
    }
  }
  std::fprintf(stderr, "[fixture] %s: full 4MiB VRAM identical\n", label);
}

void sprite(State &state, std::uint32_t left, std::uint32_t top,
            std::uint32_t right, std::uint32_t bottom, std::uint32_t color) {
  Vertex vertices[2]{};
  for (auto &vertex : vertices) {
    vertex.r = static_cast<std::uint8_t>(color);
    vertex.g = static_cast<std::uint8_t>(color >> 8);
    vertex.b = static_cast<std::uint8_t>(color >> 16);
    vertex.a = static_cast<std::uint8_t>(color >> 24);
    vertex.z = 1;
  }
  vertices[0].x = left * 16;
  vertices[0].y = top * 16;
  vertices[1].x = right * 16;
  vertices[1].y = bottom * 16;
  raster::draw(state, vertices, 2);
  gpu::draw(state, vertices, 2);
}

void fragmented_upload(std::uint32_t format, unsigned bits,
                       std::vector<std::uint8_t> &downloaded) {
  State transfer{};
  transfer.bitbltbuf = (0x400ull << 32) | (1ull << 48) |
                       (static_cast<std::uint64_t>(format) << 56);
  transfer.trxpos = (3ull << 32) | (7ull << 48);
  transfer.trxreg = 5ull | (3ull << 32);
  std::vector<std::uint8_t> payload((15 * bits + 7) / 8);
  for (unsigned index = 0; index < 15; ++index) {
    const std::uint32_t value = (0x81234567u + index * 137) &
                                (bits == 32 ? UINT32_MAX : (1u << bits) - 1);
    write_pixel(format, 0x400, 1, 3 + index % 5, 7 + index / 5, value);
    for (unsigned bit = 0; bit < bits; ++bit)
      if (value & (1u << bit))
        payload[(index * bits + bit) / 8] |=
            static_cast<std::uint8_t>(1u << ((index * bits + bit) % 8));
  }
  gpu::image_begin(transfer);
  for (std::size_t offset = 0; offset < payload.size(); ++offset)
    gpu::image_data(payload.data() + offset, 1);
  gpu::download(downloaded.data());
  compare_memory(downloaded,
                 ("fragmented upload PSM " + std::to_string(format)).c_str());
}
} // namespace

int main() {
  try {
    ps2::gs::reset();
    if (gpu::mode() != gpu::Mode::primary)
      throw std::runtime_error("Fixture requires primary GPU rasterization");
    State state{};
    state.prim = 6;
    state.prmodecont = 1;
    state.colclamp = 1;
    state.ctx[0].frame = 1ull << 16;
    state.ctx[0].zbuf = 1ull << 32;
    state.ctx[0].scissor = (63ull << 16) | (63ull << 48);
    sprite(state, 0, 0, 32, 32, 0x80112233);
    sprite(state, 16, 16, 48, 48, 0x80445566);
    std::vector<std::uint8_t> downloaded(kMemorySize);
    gpu::download(downloaded.data());
    compare_memory(downloaded, "ordered overlapping sprites across tiles");
    state.ctx[0].frame |= 0x00FF0000ull << 32;
    ++g_setup_generation;
    sprite(state, 20, 20, 28, 28, 0x80FFAA99);
    gpu::download(downloaded.data());
    compare_memory(downloaded, "framebuffer channel mask");
    std::vector<std::uint32_t> pixels;
    std::uint32_t width = 0, height = 0;
    gpu::display({0, 1, PSMCT32, 0, 0, 64, 64, false}, pixels, width, height);
    if (width != 64 || height != 64 || pixels.size() != 4096)
      throw std::runtime_error("Invalid compute display shape");
    if ((pixels[0] & 0xFFFFFFu) != 0x112233u ||
        (pixels[40 * 64 + 40] & 0xFFFFFFu) != 0x445566u)
      throw std::runtime_error(
          "Compute display pixels differ from known colors");
    std::vector<std::uint32_t> vram, clut;
    std::uint32_t scale = 0;
    gpu::read_state(vram, scale, clut);
    if (scale != 1 || vram.size() != kMemorySize / 4 || clut.size() != 512)
      throw std::runtime_error("Invalid GPU state readback shape");
    gpu::write_state(vram, scale, clut);
    gpu::download_async(downloaded.data(), 0, kMemorySize, 47);
    if (gpu::finish_download(46) || !gpu::finish_download(47) ||
        gpu::finish_download(47))
      throw std::runtime_error("Readback serial ownership differs");
    compare_memory(downloaded, "state restore and readback serial");
    const struct {
      std::uint32_t format;
      unsigned bits;
    } formats[] = {{PSMCT32, 32},  {PSMCT24, 24}, {PSMCT16, 16},
                   {PSMCT16S, 16}, {PSMT8, 8},    {PSMT4, 4},
                   {PSMT8H, 8},    {PSMT4HL, 4},  {PSMT4HH, 4}};
    for (const auto &format : formats)
      fragmented_upload(format.format, format.bits, downloaded);
    HWND window = CreateWindowExW(0, L"STATIC", L"Vulkan presentation fixture",
                                  WS_OVERLAPPEDWINDOW, 0, 0, 160, 120, nullptr,
                                  nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window)
      throw std::runtime_error("Cannot create fixture window");
    gpu::attach_surface({gpu::SurfaceTarget::Win32, nullptr,
                         reinterpret_cast<std::uint64_t>(window), nullptr,
                         nullptr, 160, 120});
    const gpu::DisplaySource display_source{0, 1, PSMCT32, 0, 0, 64, 64, false};
    for (int repeat = 0; repeat < 12; ++repeat)
      if (!gpu::present(display_source))
        throw std::runtime_error("Repeated Win32 presentation refused");
    gpu::resize_surface(0, 0);
    if (gpu::present(display_source))
      throw std::runtime_error("Zero-sized surface presented unexpectedly");
    SetWindowPos(window, nullptr, 0, 0, 240, 180, SWP_NOACTIVATE | SWP_NOMOVE);
    gpu::resize_surface(240, 180);
    if (!gpu::present(display_source))
      throw std::runtime_error("Restored resized surface refused presentation");
    DestroyWindow(window);
    std::fprintf(stderr,
                 "[fixture] Win32 repeat/zero-size/resize restore passed\n");
    gpu::stats();
    std::fprintf(stderr,
                 "[fixture] PASS: real Vulkan raster/display/readback\n");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "[fixture] FAIL: %s\n", error.what());
    return 1;
  }
}
