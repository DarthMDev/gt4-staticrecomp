// SPDX-License-Identifier: MIT
// Game-side GS stream backend (TASK-035; docs/GS-STREAM-PROTOCOL.md section 7, docs/GSDX-PLAN.md section C.3).
//
// Renderer selection, GT4_RENDERER:
//   unset or "donor"  the donor GS front end and the native Vulkan renderer (unchanged; every hook returns at once)
//   "pcsx2"           the GS stream: no donor Vulkan device; GIF, privileged writes, VSYNC and readbacks go to a GS
//                     process (GT4_GS_PROCESS=<executable>) over shared memory (gs_stream_ipc.hpp)
// Any other value stops the process before the game starts.
//
// GT4_GS_CAPTURE=<file> writes the same records to a .gscap version 2 file in either renderer mode (the donor output is
// unchanged). GT4_GS_CAPTURE_FRAMES=first:last limits it to VSYNC frames first..last.
//
// The hooks are called by the spliced donor sources (scripts/prepare_external_vulkan.py --gs-stream-hooks for gs.cpp,
// scripts/prepare_perf_runtime.py --gs-stream for gif.cpp and hw.cpp), always on the EE thread.
#pragma once

#include <cstdint>
#include <cstdio>

namespace gt4::gs_stream {

enum class Renderer { donor, pcsx2 };

// Parsed once from GT4_RENDERER. An unknown value prints a message and exits with EXIT_FAILURE.
Renderer renderer();
// True when GT4_RENDERER=pcsx2: the donor GS front end and Vulkan renderer are not used.
bool stream_renderer();

// --- hooks (EE thread) ---
// One accepted GIF chunk of `path`, in acceptance order (the gif render point, after the MTVU journal drain).
// Returns stream_renderer(): when true the caller does not run the donor GS front end for this chunk.
bool on_gif_render(int path, const std::uint8_t *data, std::uint32_t qwc);
// Privileged GS register write (the donor keeps its own copy for CSR/IMR/SIGLBLID reads).
void on_privileged_write(std::uint32_t address, std::uint64_t value);
// Local-to-host FIFO read. Returns true when the stream renderer answered it (data filled); false in donor mode,
// where the capture only records the request and the donor serves the data.
bool read_fifo(std::uint8_t *data, std::uint32_t qwords);
// Start of the vertical blank, after the donor toggled the field: odd_field is the CSR FIELD bit the EE now reads.
void on_vsync(bool odd_field);
// GS hard reset (donor render_reset) and GIF path reset.
void on_gs_reset();
void on_gif_reset();
// BIOS SetGsCrt: posts the SMODE1/SMODE2 writes the BIOS performs (the donor HLE only records the arguments).
void on_set_gs_crt(std::uint32_t interlace, std::uint32_t mode, std::uint32_t field);
// Window: the GS process renders into a child of this window (CONTROL attach_window / resize).
bool attach_window(std::uint64_t window, std::uint32_t width, std::uint32_t height);
void resize_window(std::uint32_t width, std::uint32_t height);
// Called when the GS process dies, with a reason (the spliced gs.cpp passes ps2::halt).
void set_halt_handler(void (*halt)(const char *reason));
// End of the run (donor gs::stats): CONTROL shutdown, wait for the GS process, print the session summary.
void finish(std::FILE *output);

// SetGsCrt (interlace, mode, field) to the SMODE1/SMODE2 values the BIOS writes; false for an unknown mode.
// Same table as the TASK-012 seam (native/gs_video_mode.cpp on task/012-parallel-gs).
bool map_set_gs_crt(std::uint32_t interlace, std::uint32_t mode, std::uint32_t field, std::uint64_t &smode1,
                    std::uint64_t &smode2);

}  // namespace gt4::gs_stream
