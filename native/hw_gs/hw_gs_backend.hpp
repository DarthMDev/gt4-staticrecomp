// MIT License. Copyright (c) 2026 João Alano and GT4Recomp contributors.
// Written from the TASK-026 specification (docs/hw-renderer-spec), clean room per decision 0069.
// Implements chapter 12.3 (backend interface) at milestone M0: every operation exists,
// the GS data path is a stub, and the device, swapchain and present path are real.
//
// This header is Vulkan-free. Only the translation units under native/hw_gs/ that
// implement the backend include Vulkan headers.
#pragma once

#include "hw_gs_options.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gt4::hw {

// Guest-side stream events, delivered inline during gif_transfer (chapter 12.3).
enum class StreamEvent { signal, finish, label };

// Returning true asks the caller to flush.
using StreamEventHandler = bool (*)(void *context, StreamEvent event, std::uint32_t value);

// Display phase requested at a guest VBlank (chapter 12.5).
enum class PresentPhase { field_even, field_odd, progressive };

// scanout(): the output image as host RGBA8 (red in the low byte), at internal resolution.
struct ScanoutRequest {
  PresentPhase phase = PresentPhase::progressive;
  std::uint32_t width = 0;   // filled by the backend
  std::uint32_t height = 0;  // filled by the backend
  std::vector<std::uint32_t> pixels;
};

// Counters for the F8 report (chapter 12.7). M0 fills the subset that exists.
struct Stats {
  std::uint64_t frames = 0;             // next_frame() calls
  std::uint64_t gif_bytes = 0;          // bytes accepted by gif_transfer
  std::uint64_t privileged_writes = 0;  // accepted by write_privileged
  std::uint64_t flushes = 0;            // flush() calls
  std::uint64_t presents = 0;           // present() calls (window or headless)
  std::uint64_t black_presents = 0;     // presents that displayed a black frame
  std::uint64_t swapchain_recreations = 0;
  std::uint64_t faults = 0;             // out-of-range reads and writes

  // M6 interface: presentation decisions, auxiliary passes and the per-pass CPU budget of the last
  // frame (docs/COREGS2-ROADMAP.md requirements 1, 2 and 4). GPU timestamps are a later step; there is
  // no field here that could be mistaken for one.
  std::uint64_t held_presents = 0;      // re-presented images (no new frame this VBlank)
  std::uint64_t dropped_frames = 0;     // frame ends replaced before they reached the display
  std::uint64_t auxiliary_frames = 0;   // render-only passes that restored guest memory
  double translate_us = 0;              // batch translation, last frame
  double execute_us = 0;                // GPU drawer submission, last frame
  double compose_us = 0;                // display composition, last frame
  double upload_us = 0;                 // composed image upload, last present
  double present_us = 0;                // swapchain acquire, submit and present, last present
};

// Result of the feature report (chapter 12.6).
struct DeviceReport {
  std::string device_name;
  std::uint32_t vendor_id = 0;
  bool amd = false;
  std::vector<std::string> missing_required;  // refusal list when non-empty
  std::vector<std::string> optional_found;
  std::vector<std::string> optional_missing;
  std::uint32_t max_image_dimension_2d = 0;
  std::uint32_t max_compute_workgroup_count_x = 0;
};

class Backend {
public:
  Backend();
  ~Backend();
  Backend(const Backend &) = delete;
  Backend &operator=(const Backend &) = delete;

  // Creates instance, device, queues, allocator-free persistent state. Never presents.
  // On failure `error` names the missing requirement and the function returns false.
  bool init(const Options &options, std::string &error);

  void set_stream_event_handler(StreamEventHandler handler, void *context);

  // Feeds raw GIF qwords for path 1, 2 or 3. Transfers may split at any qword boundary.
  // M0 stub: counts bytes and returns true.
  bool gif_transfer(int path, const std::uint8_t *bytes, std::size_t byte_count);
  bool gif_busy(int path) const;
  void reset_gif_paths();

  // Stores display-register writes in 0x12000000..0x12001FFF. Returns false outside that range.
  bool write_privileged(std::uint32_t address, std::uint64_t value);
  std::uint64_t read_privileged(std::uint32_t address) const;

  void reset_context();
  void clear_vram();

  // Copies LOCAL->HOST data. M0 returns zeros and counts a fault.
  std::size_t read_transfer_fifo(void *destination, std::size_t qwords);

  // CPU view of GS memory coherent with recorded work. M0 returns nullptr.
  const std::uint8_t *map_vram(std::uint32_t offset, std::uint32_t size);

  void flush();       // submits recorded work, never waits
  void next_frame(); // headless frame boundary
  void wait_idle();

  // Builds the display image as present would. M0: no image, returns false (black).
  bool scanout(ScanoutRequest &request);

  // Swapchain on an existing HWND (passed as void* to keep this header Win32-free).
  bool attach_window(void *hwnd, std::string &error);
  void detach_window();
  // Resize and fullscreen only set a flag; recreation happens before the next present.
  void resize_window(std::uint32_t width, std::uint32_t height);
  // One call per guest VBlank: a black 16:9 frame. Headless (no window) only counts.
  void present(PresentPhase phase);

  // True while a window (swapchain) is attached; headless runs advance frames themselves.
  bool has_window() const;

  const Options &options() const;
  const DeviceReport &device_report() const;
  Stats stats() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace gt4::hw
