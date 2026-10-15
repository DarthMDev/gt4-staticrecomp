// Internal-resolution control (slice176, TASK-028). Runs the primary GS backend at PS2_SCALE = argv[1]
// and checks the guest-visible results that must not depend on the scale:
//   1. write_state at scale 1 followed by download (resolve) returns the same VRAM bytes;
//   2. read_state reports the scale and replicated sub-samples, and sub-sample 0 is the 1x VRAM;
//   3. integer-aligned sprites give identical pixel counts in VRAM and in the 1x frame dump
//      (display averages each scale x scale block; the words of the downloaded framebuffer match).
// Run once per scale (1, 2, 3, 4); CTest compares with the same expected values.
#include "gpu.hpp"
#include "gs/raster.hpp"
#include "native_vulkan.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ps2::gs;

constexpr std::uint32_t kWords = kMemorySize / 4;
constexpr std::uint32_t kClutEntries = 512; // the GS CLUT holds 512 entries (gpu.cpp kClutEntries)
constexpr std::uint32_t kBackColor = 0x80112233;  // sprite A: (0,0)-(32,32)
constexpr std::uint32_t kFrontColor = 0x80445566; // sprite B: (16,16)-(48,48), drawn last
constexpr std::uint32_t kFrontPixels = 32 * 32;
constexpr std::uint32_t kBackOnlyPixels = 32 * 32 - 16 * 16;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
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
  gpu::draw(state, vertices, 2);
}

void check_round_trip(std::uint32_t scale) {
  const std::uint32_t samples = scale * scale;
  std::vector<std::uint32_t> vram(kWords);
  std::uint32_t seed = 0x13579BDFu;
  for (auto &word : vram) {
    seed = seed * 1664525u + 1013904223u;
    word = seed;
  }
  std::vector<std::uint32_t> clut(kClutEntries);
  for (std::uint32_t index = 0; index < kClutEntries; ++index)
    clut[index] = index * 0x01010101u;
  gpu::write_state(vram, 1, clut);
  std::vector<std::uint8_t> bytes(kMemorySize);
  gpu::download(bytes.data());
  for (std::uint32_t word = 0; word < kWords; ++word) {
    const std::uint32_t value = bytes[word * 4] | (bytes[word * 4 + 1] << 8) |
                                (bytes[word * 4 + 2] << 16) |
                                (static_cast<std::uint32_t>(bytes[word * 4 + 3]) << 24);
    if (value != vram[word])
      throw std::runtime_error("download after write_state differs at word " + std::to_string(word));
  }
  std::vector<std::uint32_t> state_vram, state_clut;
  std::uint32_t reported_scale = 0;
  gpu::read_state(state_vram, reported_scale, state_clut);
  require(reported_scale == scale, "read_state reports scale " + std::to_string(reported_scale));
  require(state_vram.size() == static_cast<std::size_t>(kWords) * samples,
          "read_state VRAM size differs from scale squared");
  for (std::uint32_t word = 0; word < kWords; ++word)
    for (std::uint32_t k = 0; k < samples; ++k)
      if (state_vram[static_cast<std::size_t>(word) * samples + k] != vram[word])
        throw std::runtime_error("read_state sub-sample differs at word " + std::to_string(word));
  require(state_clut == clut, "read_state CLUT differs");
  std::fprintf(stderr, "[upscale] scale %u: write_state/download/read_state round trip identical (%u words)\n",
               scale, kWords);
}

void check_sprites(std::uint32_t scale) {
  // Clear the frame buffer region (words 0..4095 hold the 64x64 PSMCT32 frame at FBP 0).
  std::vector<std::uint32_t> vram(kWords, 0);
  std::vector<std::uint32_t> clut(kClutEntries, 0);
  gpu::write_state(vram, 1, clut);
  State state{};
  state.prim = 6;
  state.prmodecont = 1;
  state.colclamp = 1;
  state.ctx[0].frame = 1ull << 16;
  state.ctx[0].zbuf = 1ull << 32;
  state.ctx[0].scissor = (63ull << 16) | (63ull << 48);
  sprite(state, 0, 0, 32, 32, kBackColor);
  sprite(state, 16, 16, 48, 48, kFrontColor);
  gpu::flush();
  gpu::wait_idle();

  std::vector<std::uint8_t> bytes(kMemorySize);
  gpu::download(bytes.data());
  std::uint32_t front = 0, back = 0;
  for (std::uint32_t word = 0; word < kWords; ++word) {
    const std::uint32_t value = bytes[word * 4] | (bytes[word * 4 + 1] << 8) |
                                (bytes[word * 4 + 2] << 16) |
                                (static_cast<std::uint32_t>(bytes[word * 4 + 3]) << 24);
    if (value == kFrontColor)
      ++front;
    else if (value == kBackColor)
      ++back;
  }
  // The VRAM words of PSMCT32 pixels are one-per-pixel; these counts do not depend on the page layout.
  require(front == kFrontPixels, "scale " + std::to_string(scale) + ": front sprite words " + std::to_string(front));
  require(back == kBackOnlyPixels, "scale " + std::to_string(scale) + ": back sprite words " + std::to_string(back));

  // The 1x frame dump averages each scale x scale block; integer-aligned edges keep the colours exact.
  const gpu::DisplaySource source{0, 1, PSMCT32, 0, 0, 64, 64, false};
  std::vector<std::uint32_t> rgba;
  std::uint32_t width = 0, height = 0;
  gpu::display(source, rgba, width, height);
  require(width == 64 && height == 64, "display size differs from 64x64 at scale " + std::to_string(scale));
  std::uint32_t display_front = 0, display_back = 0;
  for (std::uint32_t pixel : rgba) {
    if ((pixel & 0xFFFFFFu) == (kFrontColor & 0xFFFFFFu))
      ++display_front;
    else if ((pixel & 0xFFFFFFu) == (kBackColor & 0xFFFFFFu))
      ++display_back;
  }
  require(display_front == kFrontPixels, "display front pixels " + std::to_string(display_front));
  require(display_back == kBackOnlyPixels, "display back pixels " + std::to_string(display_back));
  std::fprintf(stderr, "[upscale] scale %u: sprites: VRAM front=%u back=%u; 1x frame front=%u back=%u\n", scale, front,
               back, display_front, display_back);
}
// Presentation at the scaled size: the swapchain blit maps the scale x image to the 16:9 rectangle
// (linear filter when scaled). Presentation must be accepted at every scale and resolution.
void check_presentation(std::uint32_t scale) {
  HWND window = CreateWindowExW(0, L"STATIC", L"Upscale presentation control", WS_OVERLAPPEDWINDOW, 0, 0, 960,
                                540, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window)
    throw std::runtime_error("Cannot create upscale control window");
  gpu::attach_surface({gpu::SurfaceTarget::Win32, nullptr, reinterpret_cast<std::uint64_t>(window), nullptr, nullptr,
                       960, 540});
  gpu::set_widescreen(true);
  const gpu::DisplaySource sources[] = {{0, 1, PSMCT32, 0, 0, 64, 64, false}, {0, 1, PSMCT32, 0, 0, 640, 448, false}};
  for (const auto &source : sources)
    require(gpu::present(source), "scale " + std::to_string(scale) + ": presentation refused");
  DestroyWindow(window);
  std::fprintf(stderr, "[upscale] scale %u: presentation of 64x64 and 640x448 frames accepted\n", scale);
}
} // namespace

int main(int argc, char **argv) {
  try {
    const char *requested = argc > 1 ? argv[1] : "2";
    if (_putenv_s("PS2_SCALE", requested) != 0)
      throw std::runtime_error("cannot set PS2_SCALE");
    ps2::gs::init();
    ps2::gs::reset();
    if (gpu::mode() != gpu::Mode::primary)
      throw std::runtime_error("Upscale control requires primary GPU rasterization");
    const auto scale = static_cast<std::uint32_t>(std::atoi(requested));
    check_round_trip(scale);
    check_sprites(scale);
    check_presentation(scale);
    std::fprintf(stderr, "[upscale] PASS scale %u\n", scale);
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "[upscale] FAIL: %s\n", error.what());
    return 1;
  }
}
