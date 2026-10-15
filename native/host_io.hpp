#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <mmsystem.h>
#include <Xinput.h>
// clang-format on

#include "pacing.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

struct PadState {
  std::uint16_t buttons = 0;
  std::array<std::uint8_t, 4> axes{128, 128, 128, 128};
  std::array<std::uint8_t, 16> pressures{};
};

PadState map_gamepad(const XINPUT_GAMEPAD &gamepad);
void merge_keyboard(PadState &pad, std::uint16_t buttons);
XINPUT_VIBRATION map_rumble(std::uint16_t rumble);

class ControllerInput {
public:
  ~ControllerInput();
  PadState poll(bool focused, std::uint16_t keyboard_buttons);
  void rumble(std::uint16_t value);
  void stop();

private:
  DWORD slot_ = XUSER_MAX_COUNT;
  ULONGLONG next_scan_ = 0;
  std::uint16_t motors_ = 0;
};

class AudioOutput {
public:
  // `mode` selects the blocking WinMM path (audio) or the non-blocking paced
  // ring with rate control (paced); see pacing.hpp.
  explicit AudioOutput(std::function<bool()> pump_messages = {},
                       gt4::pacing::Mode mode = gt4::pacing::Mode::audio);
  ~AudioOutput();
  AudioOutput(const AudioOutput &) = delete;
  AudioOutput &operator=(const AudioOutput &) = delete;
  // Audio mode waits for a free device buffer. Paced mode never waits: the
  // frames are resampled into the ring.
  bool push(const std::int16_t *samples, std::size_t frames);
  // Paced mode: reaps finished buffers, updates the rate ratio and submits full
  // blocks while the device queue is below the latency target. Never waits.
  // Does nothing in audio mode.
  void pump();
  // Frames queued on the device plus the ring (paced) or pending (audio).
  std::uint64_t queued_frames();
  gt4::pacing::Mode mode() const { return mode_; }
  double resampler_ratio() const { return resampler_.ratio(); }
  std::uint64_t underruns() const { return underruns_; }
  std::uint64_t overflow_frames() const { return overflow_frames_; }
  std::uint64_t received_frames() const { return received_; }
  std::uint64_t completed_frames() const { return completed_; }
  std::uint64_t canceled_frames() const { return canceled_; }
  void close(bool drain);

private:
  static constexpr std::size_t max_frames_per_buffer = 512;
  static constexpr std::size_t max_buffers = 16;
  struct Buffer {
    std::array<std::int16_t, max_frames_per_buffer * 2> samples{};
    WAVEHDR header{};
    bool prepared = false;
    bool queued = false;
  };
  std::array<Buffer, max_buffers> buffers_{};
  std::array<std::int16_t, max_frames_per_buffer * 2> pending_{};
  std::size_t pending_frames_ = 0;
  std::size_t frames_per_buffer_ = 512;
  std::size_t buffer_count_ = 4;
  std::size_t next_buffer_ = 0;
  gt4::pacing::Mode mode_ = gt4::pacing::Mode::audio;
  bool started_ = false;
  gt4::pacing::FrameRing ring_{gt4::pacing::paced_ring_frames};
  gt4::pacing::LinearResampler resampler_;
  gt4::pacing::RateController controller_{
      static_cast<double>(gt4::pacing::target_latency_frames),
      1.0 / static_cast<double>(gt4::pacing::guest_vblank_rate)};
  std::vector<std::int16_t> converted_;
  HWAVEOUT device_ = nullptr;
  HANDLE event_ = nullptr;
  std::function<bool()> pump_messages_;
  std::uint64_t received_ = 0, submitted_ = 0, completed_ = 0, canceled_ = 0;
  std::uint64_t empty_queue_ = 0, waits_ = 0;
  std::uint64_t underruns_ = 0, overflow_frames_ = 0;
  std::size_t peak_ = 0;
  std::size_t reap();
  std::size_t queued_device_buffers() const;
  bool wait_for(Buffer *buffer);
  bool submit();
  bool push_paced(const std::int16_t *samples, std::size_t frames);
  void submit_paced_block();
};
