// SPDX-License-Identifier: MIT
// In-process consumer of the GS stream seam (TASK-027, CoreGS 2.0 development builds only).
//
// The TASK-035 seam (gs_stream_client.hpp) sends the game's GS input (GIF chunks at the render point, privileged
// writes in stream order, VSYNC with the field, resets, local-to-host FIFO reads, window attach and resize) to a GS
// process in pcsx2 mode. With GT4_RENDERER=hw the client sends the same calls, on the EE thread and in the same order,
// to the sink below instead: the GS renderer runs in the game process and no stream records are written.
//
// The client knows this sink only when it is compiled with GT4_GS_STREAM_HW (CMake GT4_HW_GS=ON); the HWR adapter
// (native/hw_gs/hw_gs_stream_sink.cpp) provides it. This header is Vulkan-free and Win32-free.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace gt4::gs_stream {

struct InProcessSink {
  // Called once, when GT4_RENDERER=hw is parsed (before the first frame). False: `error` says why; the client refuses
  // to start the game.
  bool (*start)(std::string &error) = nullptr;
  // One accepted GIF chunk of `path` (qwc quadwords).
  void (*gif)(int path, const std::uint8_t *data, std::uint32_t qwc) = nullptr;
  // One privileged GS register write (0x12000000-0x12001FFF), already in stream order with the GIF chunks.
  void (*privileged)(std::uint32_t address, std::uint64_t value) = nullptr;
  // Local-to-host FIFO read of `qwords` quadwords into `data` (always filled; zeros when nothing is available).
  void (*read_fifo)(std::uint8_t *data, std::uint32_t qwords) = nullptr;
  // Start of the vertical blank; odd_field is the CSR FIELD bit the EE now reads (GS-STREAM-PROTOCOL 6.1).
  void (*vsync)(bool odd_field) = nullptr;
  void (*gs_reset)() = nullptr;
  void (*gif_reset)() = nullptr;
  // Win32 HWND of the game window (as an integer) and its client size. False: no window output.
  bool (*attach_window)(std::uint64_t window, std::uint32_t width, std::uint32_t height) = nullptr;
  void (*resize_window)(std::uint32_t width, std::uint32_t height) = nullptr;
  // End of the run: wait for the GPU and print the session summary.
  void (*finish)(std::FILE *output) = nullptr;
};

// Provided by the HWR adapter when the build has GT4_HW_GS=ON.
const InProcessSink &hw_in_process_sink();

}  // namespace gt4::gs_stream
