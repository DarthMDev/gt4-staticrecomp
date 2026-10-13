#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <mmsystem.h>
#include <Xinput.h>
// clang-format on

#include <array>
#include <cstdint>
#include <functional>

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
  explicit AudioOutput(std::function<bool()> pump_messages = {});
  ~AudioOutput();
  AudioOutput(const AudioOutput &) = delete;
  AudioOutput &operator=(const AudioOutput &) = delete;
  bool push(const std::int16_t *samples, std::size_t frames);
  void close(bool drain);
  std::uint64_t received_frames() const { return received_; }
  std::uint64_t completed_frames() const { return completed_; }
  std::uint64_t canceled_frames() const { return canceled_; }

private:
  static constexpr std::size_t frames_per_buffer = 512;
  struct Buffer {
    std::array<std::int16_t, frames_per_buffer * 2> samples{};
    WAVEHDR header{};
    bool prepared = false;
    bool queued = false;
  };
  std::array<Buffer, 4> buffers_{};
  std::array<std::int16_t, frames_per_buffer * 2> pending_{};
  std::size_t pending_frames_ = 0;
  std::size_t next_buffer_ = 0;
  HWAVEOUT device_ = nullptr;
  HANDLE event_ = nullptr;
  std::function<bool()> pump_messages_;
  std::uint64_t received_ = 0, submitted_ = 0, completed_ = 0, canceled_ = 0;
  std::uint64_t empty_queue_ = 0, waits_ = 0;
  std::size_t peak_ = 0;
  std::size_t reap();
  bool wait_for(Buffer *buffer);
  bool submit();
};
