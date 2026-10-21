// MIT License. Copyright (c) 2026 João Alano and GT4Recomp contributors.
// CTest hw_stream_sink (task/027-coregs2): with GT4_RENDERER=hw the GS stream seam of main (gs_stream_client.hpp)
// sends its hooks to the in-process HWR backend. This drives the hooks in the order the spliced donor sources call
// them (reset, SetGsCrt, one GIF packet, a FIFO read, three VSYNCs, finish) and lets CTest check the session line.
// Headless: no window is attached, so each VSYNC closes one HWR frame. Written from docs/hw-renderer-spec (clean room).

#include "gs_stream/gs_stream_client.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

int main() {
  namespace stream = gt4::gs_stream;
  if (!stream::stream_renderer()) {
    std::printf("FAIL: GT4_RENDERER=hw did not select a non-donor renderer\n");
    return 1;
  }
  stream::on_gs_reset();
  stream::on_gif_reset();
  stream::on_set_gs_crt(1, 0x02, 0);  // NTSC interlaced, field mode: SMODE1, SYNCV and SMODE2 reach the backend
  stream::on_privileged_write(0x12000000u, 0x66u);  // PMODE

  // One PACKED GIF packet: tag NLOOP=1, EOP=1, NREG=1, REGS=A+D (0xE), then FRAME_1 (0x4C) = 0.
  alignas(16) std::uint8_t packet[32] = {};
  const std::uint64_t tag_low = 1u | (1ull << 15) | (1ull << 60);
  const std::uint64_t tag_high = 0xEu;
  const std::uint64_t ad_value = 0, ad_register = 0x4C;
  std::memcpy(packet + 0, &tag_low, 8);
  std::memcpy(packet + 8, &tag_high, 8);
  std::memcpy(packet + 16, &ad_value, 8);
  std::memcpy(packet + 24, &ad_register, 8);
  if (!stream::on_gif_render(1, packet, 2)) {
    std::printf("FAIL: the hw sink did not take the GIF chunk (the donor front end would run)\n");
    return 1;
  }

  std::uint8_t fifo[32];
  std::memset(fifo, 0xA5, sizeof fifo);
  if (!stream::read_fifo(fifo, 2)) {
    std::printf("FAIL: the hw sink did not answer the FIFO read\n");
    return 1;
  }
  for (std::uint8_t byte : fifo) {
    if (byte != 0) {  // M0 backend: no local-to-host data yet, so the reply is zero-filled
      std::printf("FAIL: FIFO reply not zero-filled at M0\n");
      return 1;
    }
  }

  for (int frame = 0; frame < 3; ++frame) stream::on_vsync(frame & 1);
  stream::finish(stdout);
  std::printf("PASS: hw stream sink\n");
  return 0;
}
