#include "host_io.hpp"

#include <cstdio>
#include <stdexcept>

namespace {
void require(bool value, const char *label) {
  if (!value)
    throw std::runtime_error(label);
}
} // namespace

int main() {
  try {
    const WORD buttons[] = {XINPUT_GAMEPAD_BACK,
                            XINPUT_GAMEPAD_LEFT_THUMB,
                            XINPUT_GAMEPAD_RIGHT_THUMB,
                            XINPUT_GAMEPAD_START,
                            XINPUT_GAMEPAD_DPAD_UP,
                            XINPUT_GAMEPAD_DPAD_RIGHT,
                            XINPUT_GAMEPAD_DPAD_DOWN,
                            XINPUT_GAMEPAD_DPAD_LEFT,
                            XINPUT_GAMEPAD_LEFT_SHOULDER,
                            XINPUT_GAMEPAD_RIGHT_SHOULDER,
                            XINPUT_GAMEPAD_Y,
                            XINPUT_GAMEPAD_B,
                            XINPUT_GAMEPAD_A,
                            XINPUT_GAMEPAD_X};
    const int bits[] = {0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15};
    XINPUT_GAMEPAD gamepad{};
    for (int index = 0; index < 14; ++index) {
      gamepad.wButtons = buttons[index];
      const auto pad = map_gamepad(gamepad);
      require(pad.buttons == (1u << bits[index]), "digital button permutation");
      for (int bit = 0; bit < 16; ++bit)
        require(pad.pressures[bit] == (bit == bits[index] ? 255 : 0),
                "digital pressure");
    }
    gamepad.wButtons = 0x0c00;
    require(map_gamepad(gamepad).buttons == 0, "reserved host bits");
    gamepad = {};
    gamepad.bLeftTrigger = 30;
    require(map_gamepad(gamepad).buttons == 0, "trigger deadzone");
    gamepad.bLeftTrigger = 31;
    require(map_gamepad(gamepad).buttons == 256 &&
                map_gamepad(gamepad).pressures[8] == 1,
            "trigger first step");
    gamepad.bLeftTrigger = gamepad.bRightTrigger = 255;
    require(map_gamepad(gamepad).buttons == 768 &&
                map_gamepad(gamepad).pressures[8] == 255 &&
                map_gamepad(gamepad).pressures[9] == 255,
            "trigger endpoints");
    gamepad = {};
    gamepad.sThumbLX = -32768;
    gamepad.sThumbLY = 32767;
    gamepad.sThumbRX = 32767;
    gamepad.sThumbRY = -32768;
    require(map_gamepad(gamepad).axes ==
                std::array<std::uint8_t, 4>{255, 255, 0, 0},
            "signed endpoints and Y inversion");
    gamepad = {};
    gamepad.sThumbLX = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    gamepad.sThumbRY = -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
    require(map_gamepad(gamepad).axes == PadState{}.axes,
            "axis deadzone boundary");
    int previous = 0;
    for (int value = -32768; value <= 32767; ++value) {
      gamepad.sThumbLX = static_cast<SHORT>(value);
      const int axis = map_gamepad(gamepad).axes[2];
      require(axis >= previous, "axis monotonicity");
      previous = axis;
    }
    for (int bit = 0; bit < 16; ++bit) {
      auto pad = map_gamepad(XINPUT_GAMEPAD{});
      const auto keyboard_button = static_cast<std::uint16_t>(1u << bit);
      merge_keyboard(pad, keyboard_button);
      require(pad.buttons == keyboard_button,
              "keyboard bit maps to one digital button");
      for (int pressure = 0; pressure < 16; ++pressure)
        require(pad.pressures[pressure] == (pressure == bit ? 255 : 0),
                "keyboard pressure maps to matching button");
      require(pad.axes == PadState{}.axes,
              "keyboard button leaves analog axes neutral");
    }
    const std::uint16_t directions[] = {
        1u << 4, 1u << 5, 1u << 6, 1u << 7};
    for (const auto direction : directions) {
      auto pad = map_gamepad(XINPUT_GAMEPAD{});
      merge_keyboard(pad, direction);
      require(pad.buttons == direction && pad.axes == PadState{}.axes,
              "keyboard direction emits one digital direction only");
    }
    auto held = map_gamepad(XINPUT_GAMEPAD{});
    merge_keyboard(held, 1u << 7);
    require(held.buttons == (1u << 7), "keyboard direction held");
    auto held_again = map_gamepad(XINPUT_GAMEPAD{});
    merge_keyboard(held_again, 1u << 7);
    require(held_again.buttons == held.buttons &&
                held_again.pressures == held.pressures &&
                held_again.axes == held.axes,
            "keyboard direction remains stable while held");
    auto released = map_gamepad(XINPUT_GAMEPAD{});
    merge_keyboard(released, 0);
    require(released.buttons == 0 && released.axes == PadState{}.axes &&
                released.pressures == PadState{}.pressures,
            "keyboard direction release returns to neutral");
    auto chord = map_gamepad(XINPUT_GAMEPAD{});
    merge_keyboard(chord, (1u << 7) | (1u << 14));
    require(chord.buttons == 0x4080 && chord.pressures[7] == 255 &&
                chord.pressures[14] == 255 && chord.axes == PadState{}.axes,
            "keyboard direction and action chord");
    XINPUT_GAMEPAD analog_gamepad{};
    analog_gamepad.wButtons = XINPUT_GAMEPAD_A;
    analog_gamepad.sThumbLX = 20000;
    analog_gamepad.sThumbLY = -18000;
    analog_gamepad.sThumbRX = -16000;
    analog_gamepad.sThumbRY = 14000;
    auto combined = map_gamepad(analog_gamepad);
    const auto xinput_axes = combined.axes;
    const auto xinput_buttons = combined.buttons;
    merge_keyboard(combined, directions[0] | directions[3]);
    require(combined.axes == xinput_axes,
            "keyboard directions preserve XInput analog axes");
    require((combined.buttons & xinput_buttons) == xinput_buttons,
            "keyboard directions preserve XInput buttons");
    for (const std::uint16_t rumble : {0, 0x0100, 0xff00, 1, 0xff01}) {
      const auto vibration = map_rumble(rumble);
      require(vibration.wLeftMotorSpeed == (rumble >> 8) * 257 &&
                  vibration.wRightMotorSpeed == ((rumble & 1) ? 65535 : 0),
              "rumble channels");
    }
    ControllerInput input;
    const auto neutral = input.poll(false, 0xffff);
    require(neutral.buttons == 0 && neutral.axes == PadState{}.axes &&
                neutral.pressures == PadState{}.pressures,
            "focus loss neutral");
    std::fprintf(stderr, "PASS: XInput mapping, pressures, signed axes, "
                         "digital keyboard, focus, rumble\n");
    AudioOutput audio;
    std::array<std::int16_t, 64> samples{};
    for (int index = 0; index < 32; ++index) {
      samples[index * 2] = static_cast<std::int16_t>(index * 100);
      samples[index * 2 + 1] = static_cast<std::int16_t>(-index * 200);
    }
    for (int block = 0; block < 81; ++block)
      require(audio.push(samples.data(), 32), "audio fragmented submission");
    audio.close(true);
    require(audio.received_frames() == 2592 && audio.completed_frames() == 2592,
            "audio wrap and partial drain");
    AudioOutput canceled;
    require(canceled.push(samples.data(), 32), "audio partial cancellation");
    canceled.close(false);
    require(canceled.completed_frames() == 0,
            "pending PCM not falsely completed");
    AudioOutput queued_cancel;
    for (int block = 0; block < 64; ++block)
      require(queued_cancel.push(samples.data(), 32),
              "queued PCM before cancel");
    queued_cancel.close(false);
    require(queued_cancel.completed_frames() +
                    queued_cancel.canceled_frames() ==
                2048,
            "queued cancellation ownership accounting");
    queued_cancel.close(false);
    bool rejected = false;
    try {
      queued_cancel.push(samples.data(), 32);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    require(rejected, "closed sink refuses new samples");
    std::fprintf(stderr, "PASS: real Windows audio completion, ring wrap, "
                         "partial drain and cancel\n");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
