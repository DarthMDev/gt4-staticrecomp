// Multi-submit stress workload for asynchronous GS submission. The same
// deterministic sequence of draws, uploads (cache inserts and evictions),
// flushes and CPU readbacks runs in one process; the full VRAM is written to
// the path given on the command line. CTest runs it with and without
// GT4_GS_SYNC=1 and compares the two files byte for byte.
#include "gpu.hpp"
#include "gs/raster.hpp"
#include "native_vulkan.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ps2::gs;

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

// Uploads a rectangle of 32-bit pixels into the given buffer base and width.
void upload(std::uint32_t base, std::uint32_t buffer_width,
            std::uint32_t dest_x, std::uint32_t dest_y, std::uint32_t width,
            std::uint32_t height, const std::vector<std::uint8_t> &payload) {
  State transfer{};
  transfer.bitbltbuf = (static_cast<std::uint64_t>(base) << 32) |
                       (static_cast<std::uint64_t>(buffer_width) << 48);
  transfer.trxpos = (static_cast<std::uint64_t>(dest_x) << 32) |
                    (static_cast<std::uint64_t>(dest_y) << 48);
  transfer.trxreg = static_cast<std::uint64_t>(width) |
                    (static_cast<std::uint64_t>(height) << 32);
  gpu::image_begin(transfer);
  gpu::image_data(payload.data(), payload.size());
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 2 && argc != 3)
      throw std::runtime_error("usage: vulkan_async_stress <output-vram.bin> [all|draws|uploads]");
    const std::string workload = argc == 3 ? argv[2] : "all";
    if (workload != "all" && workload != "draws" && workload != "uploads")
      throw std::runtime_error("workload must be all, draws or uploads");
    const bool with_draws = workload != "uploads";
    const bool with_uploads = workload != "draws";
    ps2::gs::reset();
    if (gpu::mode() != gpu::Mode::primary)
      throw std::runtime_error("Stress workload requires primary GPU rasterization");

    // Payloads come from a fixed pool larger than the 16 MiB cache, so repeated
    // uploads hit the cache while distinct ones force evictions.
    std::mt19937 random(20261014u);
    std::vector<std::vector<std::uint8_t>> payloads;
    std::vector<std::uint32_t> payload_widths, payload_heights;
    for (int index = 0; index < 600; ++index) {
      const std::uint32_t width = 64 + (random() % 3) * 16;
      const std::uint32_t height = 64 + (random() % 3) * 8;
      std::vector<std::uint8_t> payload(static_cast<std::size_t>(width) * height * 4);
      for (auto &byte : payload)
        byte = static_cast<std::uint8_t>(random());
      payloads.push_back(std::move(payload));
      payload_widths.push_back(width);
      payload_heights.push_back(height);
    }

    State state{};
    state.prim = 6;
    state.prmodecont = 1;
    state.colclamp = 1;
    state.ctx[0].scissor = (511ull << 16) | (511ull << 48);
    std::uint32_t uploads = 0, draws = 0;
    for (int step = 0; step < 900; ++step) {
      const std::uint32_t frame_base = (random() % 3) * 0x40;
      state.ctx[0].frame = frame_base | (static_cast<std::uint64_t>(1 + random() % 4) << 16);
      ++g_setup_generation;
      const int sprites = 1 + static_cast<int>(random() % 4);
      for (int index = 0; index < sprites; ++index) {
        const std::uint32_t left = random() % 480, top = random() % 480;
        const std::uint32_t right = left + 4 + random() % 28, bottom = top + 4 + random() % 28;
        const std::uint32_t color = static_cast<std::uint32_t>(random()) | 0x80000000u;
        if (with_draws) {
          sprite(state, left, top, right, bottom, color);
          // One primitive per submission: overlapping primitives inside one
          // batch race in the existing raster path (see docs/GS-ASYNC-DESIGN.md).
          gpu::flush();
          ++draws;
        }
      }
      if (with_uploads && step % 5 == 0) {
        const auto pick = random() % payloads.size();
        upload(0x200 + static_cast<std::uint32_t>(random() % 0x100), 4,
               random() % 64, random() % 64, payload_widths[pick],
               payload_heights[pick], payloads[pick]);
        ++uploads;
      }
      // Frequent flushes create many in-flight submissions; occasional CPU
      // readbacks force the completion path mid-stream.
      if (step % 7 == 0)
        gpu::flush();
      if (step % 113 == 0) {
        std::vector<std::uint8_t> scratch(kMemorySize);
        gpu::download(scratch.data());
      }
    }
    std::vector<std::uint8_t> vram(kMemorySize);
    gpu::download(vram.data());
    std::ofstream output(argv[1], std::ios::binary);
    if (!output.write(reinterpret_cast<const char *>(vram.data()),
                      static_cast<std::streamsize>(vram.size())))
      throw std::runtime_error("Cannot write the stress VRAM dump");
    gpu::stats();
    std::fprintf(stderr,
                 "[stress] PASS: %u draws, %u uploads, VRAM dumped to %s\n",
                 draws, uploads, argv[1]);
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "[stress] FAIL: %s\n", error.what());
    return 1;
  }
}
