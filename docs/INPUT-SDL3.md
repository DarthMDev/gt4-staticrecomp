# Input layer on SDL3 (TASK-075 game side, TASK-076 keyboard)

Status: slice210 (branch `task/075-sdl3-input`). Owner test pending. Labels: **Confirmed** (code, tests or a
measurement here), **High confidence**, **Hypothesis** (not tested on hardware), **Unknown**.

## What changed

- **Gamepads through SDL 3.4.18** (`native/host_io.cpp`, `ControllerInput`). `SDL_Init(SDL_INIT_GAMEPAD)` only;
  the game window stays the Win32 window of `native/window.cpp`.
  - **Confirmed (probe, slice210-probe):** `SDL_Init(SDL_INIT_GAMEPAD)` creates no top-level window. It creates four
    message-only windows of the process (not visible, never focused, not the game window). One of them stays after
    `SDL_QuitSubSystem`. Disabling DirectInput (`SDL_JOYSTICK_DIRECTINPUT=0`) leaves two. RawInput and WGI made no
    difference. The message-only windows share the game's message loop without interfering with it. Not changed
    (open item for the owner, see below).
- **One mapping for every backend** (`native/pad_mapping.{hpp,cpp}`, pure, no SDL or Windows dependency). The SDL host
  reads a `GamepadReading` in the XInput convention and calls `map_reading`, the same function the XInput path uses.
  The controls table, dead zones, trigger curves and the 30/255 threshold are the ones of `native/controls.cpp`.
- **Player 1:** the connected pad with the lowest SDL instance id. Hot-plug is polled: the pad list is read every
  two seconds while no pad is open, and a disconnect is noticed on the next poll. The log line
  `[input] SDL gamepad "<name>" is controller 1 of N` matches the launcher's count.
- **Backend switch:** `GT4_INPUT=sdl` (default) or `GT4_INPUT=xinput` (the previous path, kept for one release).
  Any other value stops the start with `[input] GT4_INPUT must be sdl or xinput ..., game not started`.
- **Rumble:** the same motor values as XInput (`rumble_motors`), sent with `SDL_RumbleGamepad` for 500 ms and
  refreshed every 250 ms while the value holds. A pad without rumble support is not an error.
- **Keyboard (TASK-076):** the hard-coded table of `native/controls.cpp` (`default_controls`), table below.

### Pad mapping (SDL 3.4.18 gamepad API to the DS2 layout)

| SDL gamepad input | Controls source (`controls.toml`) | DS2 input | Notes |
| --- | --- | --- | --- |
| `SDL_GAMEPAD_BUTTON_SOUTH` | `xinput:A` | Cross | Bottom face: A on Xbox, Cross on PlayStation, B on Switch Pro (by position) |
| `SDL_GAMEPAD_BUTTON_EAST` | `xinput:B` | Circle | Right face |
| `SDL_GAMEPAD_BUTTON_WEST` | `xinput:X` | Square | Left face |
| `SDL_GAMEPAD_BUTTON_NORTH` | `xinput:Y` | Triangle | Top face |
| `SDL_GAMEPAD_BUTTON_LEFT_SHOULDER` / `RIGHT_SHOULDER` | `xinput:LB` / `RB` | L1 / R1 | |
| `SDL_GAMEPAD_AXIS_LEFT_TRIGGER` / `RIGHT_TRIGGER` | `xinput:LT` / `RT` | L2 / R2 | Analog pressure: SDL 0..32767 to the byte 0..255 (`>> 7`), then the 30/255 threshold and the pressure curve |
| `SDL_GAMEPAD_BUTTON_BACK` | `xinput:Back` | Select | DualShock "Share", DualSense "Create" |
| `SDL_GAMEPAD_BUTTON_START` | `xinput:Start` | Start | DualShock "Options" |
| `SDL_GAMEPAD_BUTTON_LEFT_STICK` / `RIGHT_STICK` | `xinput:LThumb` / `RThumb` | L3 / R3 | |
| `SDL_GAMEPAD_BUTTON_DPAD_*` | `xinput:Dpad*` | D-pad | |
| `SDL_GAMEPAD_AXIS_LEFTX` / `RIGHTX` | `xinput:LX` / `RX` | left / right stick X | Dead zones of the controls table (7849 / 8689 of 32767) |
| `SDL_GAMEPAD_AXIS_LEFTY` / `RIGHTY` | `xinput:LY` / `RY` | stick Y | SDL reports down as positive; the host negates it (`negate_axis`) to the XInput sign, so the `invert=true` defaults are unchanged |

The `xinput:` names in `controls.toml` are kept for compatibility. They name the physical positions, which SDL
reports the same way on every pad, so one file serves both backends. The launcher's file format does not change.

**Confirmed (tests `native_pad_mapping`):** the default mapping of readings equals the XInput mapping; the 30/255
threshold and `read_trigger` agree for all 256 trigger values; stick dead zones and directions; the SDL conversions
(`negate_axis`, `trigger_byte`); a `controls.toml` remap (Cross on B, Circle unbound, quadratic R2 curve, stick dead
zone) through the same code; rumble words; `GT4_INPUT` values; the lowest instance id as player 1.

**Not tested here:** a real pad. This machine was not run windowed (owner rule); the owner's test covers Xbox,
DualShock 4, DualSense and Switch Pro pads, and the launcher's controller count.

## Keyboard (TASK-076)

The table is hard-coded for now (not remappable from the launcher). The same table is in `docs/CONTROLS.md`
(section `keyboard-default`). Several keys per DS2 button.

| Key | DS2 input | Action | Evidence (see below) |
| --- | --- | --- | --- |
| Up, E | D-pad up | ghost (up) | owner design |
| Down | D-pad down | | owner design |
| Left, A | D-pad left | | owner design |
| Right, D | D-pad right | | owner design |
| W, Enter | Cross | accelerate, confirm | owner design |
| Space | Circle | handbrake | owner design; ludo.guide "Emergency Brake" = Circle (Hypothesis) |
| S | Square | brake | owner design; ludo.guide Brake = Square (Hypothesis) |
| Backspace, Esc | Triangle | back | owner design |
| Shift | R1 | nitro (boost) | ludo.guide Boost = R1 (Hypothesis) |
| R | R2 | gear up (upshift) | ludo.guide Upshift = R2 (Hypothesis) |
| F | L2 | gear down (downshift) | ludo.guide Downshift = L2 (Hypothesis) |
| Q | L1 | look back (rear view) | ludo.guide Rear View = L1 (Hypothesis) |
| C, Tab | Select | camera (driver views) | ludo.guide "Toggle Driver Views" = Select (Hypothesis) |
| P | Start | pause, Start | **Hypothesis, owner choice needed**: the owner's table gives no Start key (Enter is Cross) |

Not bound by default: L3 and R3 on the keyboard (the owner's table does not list them; `controls.toml` can add them).

F7, F8, F9 and F11 stay with the host (profiler and fullscreen); the table uses none of them.

## GT4 button evidence for nitro, gears, rear view and camera

**Confidence: Hypothesis.** Two sources, neither tested on a PS2 pad in this work:

1. Public control guide (ludo.guide, Gran Turismo 4 "Controls" page, fetched 2026-10-20). Its table, PlayStation
   column: Boost (R1), Upshift (manual transmission only, R2), Downshift (manual transmission only, L2), Rear View (L1),
   Toggle Driver Views (Select), Emergency Brake (Circle), Brake (Square), Accelerate (X). The page does not say whether
   these are the defaults. Used as the basis for nitro, gears, rear view, camera and the handbrake.
2. Decomp (`private/slice151-research/gt4-decomp`, commit e0b70c4d6). **Confirmed** names: `RaceInput::getButtons`
   (0x00426A00, the pad word), `getButtonDown` (0x00426A08, edge: new presses), `getExtendButtonDown` (0x00426B60, the
   second word, used by `RacePS2Base` and `RaceSolitaire`). Callers test bits 0x20, 0x40, 0x80 (pause and menus) and
   0x30. The decomp does not name the individual bits of the pad word, so it does not confirm the nitro, gear or camera
   bits. The bit layout used here is the PS2 pad layout of `controls.cpp` (Select bit 0, L3 1, R3 2, Start 3, up 4,
   right 5, down 6, left 7, L2 8, R2 9, L1 10, R1 11, Triangle 12, Circle 13, Cross 14, Square 15).

**Unknown:** which DS2 bit GT4 reads for the camera change and for rear view in each mode, and whether the game keeps
its own pad assignment on the memory card. The owner test (below) checks nitro, gears, rear view and camera.

## Owner test (slice210)

`private\owner-test-launcher\game` is slice210 (`game-209-old` is the previous build). Start it through the launcher
(or `python scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode pcsx2`).

1. Log shows `[input] backend sdl` and, with a pad, `[input] SDL gamepad "..." is controller 1 of N`.
2. Pad: Cross, Circle, Square, Triangle, L1, R1, L2 and R2 (pressure), both sticks, D-pad, Start, Select, stick clicks.
   Try an Xbox pad, a DualShock 4 and a DualSense if available. Unplug and replug: the pad must come back.
3. Keyboard: the table above. Check nitro (Shift), gear up (R), gear down (F), rear view (Q), camera (C, Tab).
   Report which of them GT4 really uses; the table can then change.
4. `GT4_INPUT=xinput` restores the old path (one release only).

## Open items

- Start has no keyboard key in the owner's table; P is a placeholder (Hypothesis).
- SDL creates four message-only helper windows (see above). Harmless to the game; the owner decides whether the
  DirectInput backend (two windows left) is worth dropping.
- The launcher's Controls page reads pads through its own SDL3 build (launcher agent, TASK-019 Rust side).
