#include "host_io.hpp"

#include "perf_probe.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

namespace {
void check_audio(MMRESULT result, const char *operation) {
  if (result == MMSYSERR_NOERROR)
    return;
  char description[MAXERRORLENGTH]{};
  waveOutGetErrorTextA(result, description, MAXERRORLENGTH);
  throw std::runtime_error(std::string(operation) + ": " + description + " (" +
                           std::to_string(result) + ")");
}

std::uint8_t axis_value(SHORT raw, int deadzone, bool invert) {
  const int value = raw;
  const int magnitude = value < 0 ? -value : value;
  if (magnitude <= deadzone)
    return 128;
  const int range = (value < 0 ? 32768 : 32767) - deadzone;
  const int distance = magnitude - deadzone;
  const bool negative = (value < 0) != invert;
  return static_cast<std::uint8_t>(negative ? 128 - distance * 128 / range
                                            : 128 + distance * 127 / range);
}
} // namespace

PadState map_gamepad(const XINPUT_GAMEPAD &gamepad) {
  PadState pad;
  const struct {
    WORD host;
    int guest;
  } bindings[] = {{XINPUT_GAMEPAD_BACK, 0},
                  {XINPUT_GAMEPAD_LEFT_THUMB, 1},
                  {XINPUT_GAMEPAD_RIGHT_THUMB, 2},
                  {XINPUT_GAMEPAD_START, 3},
                  {XINPUT_GAMEPAD_DPAD_UP, 4},
                  {XINPUT_GAMEPAD_DPAD_RIGHT, 5},
                  {XINPUT_GAMEPAD_DPAD_DOWN, 6},
                  {XINPUT_GAMEPAD_DPAD_LEFT, 7},
                  {XINPUT_GAMEPAD_LEFT_SHOULDER, 10},
                  {XINPUT_GAMEPAD_RIGHT_SHOULDER, 11},
                  {XINPUT_GAMEPAD_Y, 12},
                  {XINPUT_GAMEPAD_B, 13},
                  {XINPUT_GAMEPAD_A, 14},
                  {XINPUT_GAMEPAD_X, 15}};
  for (const auto &binding : bindings)
    if (gamepad.wButtons & binding.host) {
      pad.buttons |= static_cast<std::uint16_t>(1u << binding.guest);
      pad.pressures[binding.guest] = 255;
    }
  const BYTE triggers[] = {gamepad.bLeftTrigger, gamepad.bRightTrigger};
  for (int trigger = 0; trigger < 2; ++trigger)
    if (triggers[trigger] > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
      pad.buttons |= static_cast<std::uint16_t>(1u << (8 + trigger));
      pad.pressures[8 + trigger] = static_cast<std::uint8_t>(
          (triggers[trigger] - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) * 255 /
          (255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD));
    }
  pad.axes = {
      axis_value(gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, false),
      axis_value(gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, true),
      axis_value(gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, false),
      axis_value(gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, true)};
  return pad;
}

void merge_keyboard(PadState &pad, std::uint16_t buttons) {
  pad.buttons |= buttons;
  for (int bit = 0; bit < 16; ++bit)
    if (buttons & (1u << bit))
      pad.pressures[bit] = 255;
  if (buttons & (1u << 7))
    pad.axes[2] = 0;
  if (buttons & (1u << 5))
    pad.axes[2] = 255;
  if (buttons & (1u << 4))
    pad.axes[3] = 0;
  if (buttons & (1u << 6))
    pad.axes[3] = 255;
}

XINPUT_VIBRATION map_rumble(std::uint16_t rumble) {
  return {static_cast<WORD>((rumble >> 8) * 257),
          static_cast<WORD>((rumble & 1) ? 65535 : 0)};
}

ControllerInput::~ControllerInput() { stop(); }

PadState ControllerInput::poll(bool focused, std::uint16_t keyboard_buttons) {
  PadState pad;
  if (!focused) {
    stop();
    return pad;
  }
  XINPUT_STATE state{};
  if (slot_ == XUSER_MAX_COUNT && GetTickCount64() >= next_scan_) {
    next_scan_ = GetTickCount64() + 2000;
    for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
      const DWORD result = XInputGetState(slot, &state);
      if (result == ERROR_SUCCESS) {
        slot_ = slot;
        std::fprintf(stderr, "[input] XInput controller slot %lu connected\n",
                     slot);
        break;
      }
      if (result != ERROR_DEVICE_NOT_CONNECTED)
        throw std::runtime_error("XInputGetState failed: " +
                                 std::to_string(result));
    }
  }
  if (slot_ != XUSER_MAX_COUNT) {
    const DWORD result = XInputGetState(slot_, &state);
    if (result == ERROR_SUCCESS)
      pad = map_gamepad(state.Gamepad);
    else if (result == ERROR_DEVICE_NOT_CONNECTED) {
      std::fprintf(stderr, "[input] XInput controller disconnected\n");
      slot_ = XUSER_MAX_COUNT;
      motors_ = 0;
      next_scan_ = 0;
    } else
      throw std::runtime_error("XInputGetState failed: " +
                               std::to_string(result));
  }
  merge_keyboard(pad, keyboard_buttons);
  return pad;
}

void ControllerInput::rumble(std::uint16_t value) {
  if (slot_ == XUSER_MAX_COUNT || motors_ == value)
    return;
  auto vibration = map_rumble(value);
  const DWORD result = XInputSetState(slot_, &vibration);
  if (result == ERROR_DEVICE_NOT_CONNECTED) {
    slot_ = XUSER_MAX_COUNT;
    motors_ = 0;
    next_scan_ = 0;
    return;
  }
  if (result != ERROR_SUCCESS)
    throw std::runtime_error("XInputSetState failed: " +
                             std::to_string(result));
  motors_ = value;
}

void ControllerInput::stop() {
  if (slot_ != XUSER_MAX_COUNT && motors_) {
    XINPUT_VIBRATION vibration{};
    const DWORD result = XInputSetState(slot_, &vibration);
    if (result != ERROR_SUCCESS && result != ERROR_DEVICE_NOT_CONNECTED) {
      std::fprintf(stderr, "[input] stopping rumble failed: %lu\n", result);
      return;
    }
  }
  motors_ = 0;
}

AudioOutput::AudioOutput(std::function<bool()> pump_messages)
    : pump_messages_(std::move(pump_messages)) {
  event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!event_)
    throw std::runtime_error("Cannot create audio completion event");
  try {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 192000;
    check_audio(waveOutOpen(&device_, WAVE_MAPPER, &format,
                            reinterpret_cast<DWORD_PTR>(event_), 0,
                            CALLBACK_EVENT),
                "waveOutOpen stereo16/48000");
    for (auto &buffer : buffers_) {
      buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.samples.data());
      buffer.header.dwBufferLength =
          static_cast<DWORD>(buffer.samples.size() * sizeof(std::int16_t));
      check_audio(
          waveOutPrepareHeader(device_, &buffer.header, sizeof(WAVEHDR)),
          "waveOutPrepareHeader");
      buffer.prepared = true;
    }
    std::fprintf(stderr,
                 "[audio] Windows PCM stereo16 48000 Hz, 4 x 512 frames\n");
  } catch (...) {
    try {
      close(false);
    } catch (const std::exception &error) {
      std::fprintf(stderr, "[audio] fatal construction cleanup failure: %s\n",
                   error.what());
      std::terminate();
    }
    throw;
  }
}

AudioOutput::~AudioOutput() {
  try {
    close(false);
  } catch (const std::exception &error) {
    // Never release storage still owned by a failed audio driver.
    std::fprintf(stderr, "[audio] fatal cleanup failure: %s\n", error.what());
    std::terminate();
  }
}

std::size_t AudioOutput::reap() {
  std::size_t queued = 0;
  for (auto &buffer : buffers_) {
    if (buffer.queued && (buffer.header.dwFlags & WHDR_DONE)) {
      const std::size_t frames = buffer.header.dwBufferLength / 4;
      completed_ += frames;
      gt4::perf::Probe::shared().add_audio(0, frames);
      buffer.queued = false;
    }
    queued += buffer.queued;
  }
  return queued;
}

bool AudioOutput::wait_for(Buffer *buffer) {
  const ULONGLONG deadline = GetTickCount64() + 5000;
  for (;;) {
    // Reset before rescanning headers: coalesced notifications cannot lose
    // completion.
    if (!ResetEvent(event_))
      throw std::runtime_error("Cannot reset audio completion event");
    const auto queued = reap();
    if (buffer ? !buffer->queued : queued == 0)
      return true;
    if (pump_messages_ && !pump_messages_())
      return false;
    if (GetTickCount64() >= deadline)
      throw std::runtime_error("Windows audio device stalled for five seconds");
    ++waits_;
    const auto wait_begin = gt4::perf::now_us();
    const DWORD result =
        MsgWaitForMultipleObjects(1, &event_, FALSE, 50, QS_ALLINPUT);
    gt4::perf::Probe::shared().note_wait(gt4::perf::Stage::audio_wait,
                                         gt4::perf::now_us() - wait_begin);
    if (result == WAIT_FAILED)
      throw std::runtime_error("Waiting for audio completion failed");
  }
}

bool AudioOutput::submit() {
  if (!pending_frames_)
    return true;
  auto &buffer = buffers_[next_buffer_];
  if (!wait_for(&buffer))
    return false;
  const auto queued = reap();
  if (!queued && submitted_)
    ++empty_queue_;
  std::copy_n(pending_.data(), pending_frames_ * 2, buffer.samples.data());
  buffer.header.dwBufferLength = static_cast<DWORD>(pending_frames_ * 4);
  check_audio(waveOutWrite(device_, &buffer.header, sizeof(WAVEHDR)),
              "waveOutWrite");
  buffer.queued = true;
  submitted_ += pending_frames_;
  peak_ = std::max(peak_, queued + 1);
  pending_frames_ = 0;
  next_buffer_ = (next_buffer_ + 1) % buffers_.size();
  return true;
}

bool AudioOutput::push(const std::int16_t *samples, std::size_t frames) {
  if (!device_ || (frames && !samples))
    throw std::runtime_error("Invalid audio sink input/lifetime");
  gt4::perf::Scope production(gt4::perf::Stage::audio_push);
  received_ += frames;
  gt4::perf::Probe::shared().add_audio(frames, 0);
  while (frames) {
    const auto count = std::min(frames, frames_per_buffer - pending_frames_);
    std::copy_n(samples, count * 2, pending_.data() + pending_frames_ * 2);
    samples += count * 2;
    frames -= count;
    pending_frames_ += count;
    if (pending_frames_ == frames_per_buffer && !submit())
      return false;
  }
  return true;
}

void AudioOutput::close(bool drain) {
  if (device_) {
    if (drain && submit())
      wait_for(nullptr);
    reap();
    check_audio(waveOutReset(device_), "waveOutReset");
    for (auto &buffer : buffers_) {
      if (buffer.queued) {
        canceled_ += buffer.header.dwBufferLength / 4;
        buffer.queued = false;
      }
      if (buffer.prepared) {
        check_audio(
            waveOutUnprepareHeader(device_, &buffer.header, sizeof(WAVEHDR)),
            "waveOutUnprepareHeader");
        buffer.prepared = false;
      }
    }
    check_audio(waveOutClose(device_), "waveOutClose");
    device_ = nullptr;
    std::fprintf(
        stderr,
        "[audio] received=%llu submitted=%llu completed=%llu canceled=%llu "
        "pending=%zu peak=%zu waits=%llu empty_queue=%llu\n",
        static_cast<unsigned long long>(received_),
        static_cast<unsigned long long>(submitted_),
        static_cast<unsigned long long>(completed_),
        static_cast<unsigned long long>(canceled_), pending_frames_, peak_,
        static_cast<unsigned long long>(waits_),
        static_cast<unsigned long long>(empty_queue_));
  }
  if (event_) {
    CloseHandle(event_);
    event_ = nullptr;
  }
}
