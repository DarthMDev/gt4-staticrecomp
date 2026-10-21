# Controller mapping study for the GT4Recomp launcher (en-US)

Read-only study, 2026-10-15. No repository file, build, or game launch was touched. Labels: **Confirmed** (read in code or docs), **High confidence**, **Hypothesis**, **Unknown**.

## 0. Owner question: are R2/L2 analog in GT4? (answered first)

Owner report: in PCSX2 he maps LT/RT to L2/R2, then in GT4's own controller settings assigns accelerate/brake to R2/L2, and the triggers act as analog throttle/brake. Sticks did not work for him.

| Claim | Status | Evidence |
|---|---|---|
| The DualShock 2 L2/R2 are pressure-sensitive (0-255) in pressure mode | **Confirmed** (protocol) | Donor `private/references/ps2-web-recomp/runtime/src/iop/sio2.cpp:96-98`: in mode 0x79, pressure bytes 9..20 are sent in the PS2 order Right, Left, Up, Down, Triangle, Circle, Cross, Square, L1, R1, L2, R2, using `kBits = {5,7,4,6,12,13,14,15,10,11,8,9}`. L2 = bit 8, R2 = bit 9 (`sio2.cpp:97`). Mode 0x79 is entered only by command 0x4F (`sio2.cpp:152-158`). PCSX2 does the same: `pcsx2/SIO/Pad/PadDualshock2.cpp:300-302` returns `GetPressure(PAD_L2/PAD_R2)` in pressure mode. |
| PCSX2 feeds an XInput trigger into L2/R2 pressure | **Confirmed** (PCSX2 code, GPL, design reference only) | `pcsx2/Input/XInputSource.cpp:41-48`: `AXIS_TRIGGERLEFT -> GenericInputBinding::L2`, `AXIS_TRIGGERRIGHT -> R2`. `pcsx2/SIO/Pad/PadDualshock2.cpp:18-33`: L2 and R2 are `InputBindingInfo::Type::HalfAxis`. `PadDualshock2.cpp:680-691` (`Set`, trigger branch): value clamped to 0..1, `ButtonDeadzone` applied, `rawInputs[index] = value * 255` (this is the pressure byte), and the digital bit is set when the value is above the deadzone. `pcsx2/Input/InputManager.cpp:903-914`: every pad binding goes through `ApplySingleBindingScale` (scale, deadzone) and then `Pad::SetControllerState`. So PCSX2 has no special case for GT4: a trigger is a pressure-sensitive L2/R2 by design. |
| GT4 uses pressure for L2/R2 when they are assigned to accelerate/brake | **Hypothesis, plausible** | No GT4 pad code is visible. The decomp (`private/slice151-research/gt4-decomp`) has no libpad/padman symbols or pad-class names, and the private ELF has no libpad symbol strings (`grep -a` for `scePadEnterPressMode`, `scePadInfoPressMode` and similar: 0 hits). Pad access is in the IOP padman IRX, which we interpret and do not read. Community reports say GT4 pressure-modulates the face buttons (GTPlanet, via a web search; primary pages returned HTTP 403 when fetched). |
| GT4 lets you assign accelerate/brake to R2/L2 in its own menu | **Hypothesis (community)**; this conflicts with some forum posts | A web search summary cites a PCSX2 forum post: in Options > Controllers choose "DualShock 2 button config 1P", then reassign L2/R2 to gas and brake. A different summary says the stock game does not offer R2/L2 for throttle. The primary pages were not retrievable (403). **Unknown** which is right for the owner's build. The owner's own report (he did it) is the best evidence and should be trusted over forum summaries. |
| Analog stick throttle works in GT4 | **Hypothesis / Unknown** | Forum sources say the right stick works for throttle/brake on stock controls (GTPlanet, via search summary); another source says it cannot be remapped. The stick reaches the game only in analog mode 0x73 (`sio2.cpp:90-95`, `104-107`, `126`: mode 0x73 is set by command 0x44 with byte 3 = 1). If GT4 does not switch to analog mode, sticks are ignored. This is the likeliest reason the owner's sticks did nothing (Hypothesis). Not verified on a real PS2 or in our build. |

**Verdict.** The R2/L2 pressure path is native PS2 hardware behavior (Confirmed for the protocol). PCSX2's only adaptation is the trigger-axis-to-pressure bridge, and we already implement that same bridge (section 3). Whether GT4 reads L2/R2 pressure is **not proven**. It is community-reported and matches the owner's experience, so treat it as High-confidence lore until the owner confirms in our build. The in-game menu path claimed by the community is "Options > Controllers > DualShock 2 button config 1P" (**Hypothesis**, not verifiable here).

**Can we preset it?** Not verified. GT4's button assignment is probably stored in the game's own save data on the memory card (**Hypothesis**). Writing a preset into the card would mean editing the owner's private card, which our rules forbid, and it would be fragile. Recommended: the launcher ships a documented "R2/L2 pedals" preset that tells the user which menu item to pick, plus a launch-time check that the pad is in pressure mode (see section 5). Do not write the card.

**Recommended default mapping for the owner's setup:** LT -> PS2 L2 pressure (`pressures[8]`, bit 8 digital), RT -> PS2 R2 pressure (`pressures[9]`, bit 9 digital). This is what `native/host_io.cpp:57-63` already does today (see section 1).

## 1. Current runtime input (Confirmed, by file)

Files: `native/host_io.hpp`, `native/host_io.cpp`, `native/window.cpp`, `native/host_io_fixture.cpp`.

- **Backend:** XInput only (`native/host_io.hpp:8`, `native/host_io.cpp:93-123`). Polls `XInputGetState` on the slot in use, rescans slots on disconnect, and throws on unexpected errors.
- **Button map** (`host_io.cpp:38-51`): XInput Back -> PS2 bit 0 (Select); LThumb -> 1 (L3); RThumb -> 2 (R3); Start -> 3; D-pad Up/Right/Down/Left -> 4/5/6/7; LB -> 10 (L1); RB -> 11 (R1); Y -> 12 (Triangle); B -> 13 (Circle); A -> 14 (Cross); X -> 15 (Square). Pressure is fixed at 255 for any pressed digital button (`host_io.cpp:55`).
- **Triggers today** (`host_io.cpp:57-63`): threshold 30 (`XINPUT_GAMEPAD_TRIGGER_THRESHOLD`) is a digital bit (8 = L2, 9 = R2), and the pressure byte is linear from the threshold to 255: `(raw - 30) * 255 / 225`. So the trigger is **both** a digital bit and an analog pressure, which matches PCSX2 behavior except that PCSX2 uses the value above `ButtonDeadzone` with no threshold of 30/255 (about 12%). Note the 30 deadzone (`host_io_fixture.cpp:44-53`: raw 30 -> no press, 31 -> pressure 1).
- **Axes** (`host_io.cpp:20-31`, `65-69`): deadzone for right stick and left stick (XInput constants). Byte order is `{RX, RY, LX, LY}` (matches PS2 bytes 5..8: `sio2.cpp:91-94`). Center 128, Y inverted. Signed endpoints handled with 64-bit-safe arithmetic. Fixture-tested (`host_io_fixture.cpp:56-72`).
- **Keyboard: hard-coded, not remappable.** `native/window.cpp:205-210`: Backspace = Select, C = L3, V = R3, Enter = Start, arrows = D-pad, X = Circle, Z = Cross, A = Square, S = Triangle, Q = L1, W = R1, 1 = L2, 2 = R2. Alt+Enter is excluded from Start (`window.cpp:212-218`). Keyboard buttons merge into the XInput pad with `merge_keyboard` (`host_io.cpp:73-78`) and set pressure 255 (digital). Keyboard never sets axes (`docs/BUILD.md:165`).
- **Focus:** `window.cpp:144-148` releases all buttons on `WM_KILLFOCUS`. Polling skips XInput when unfocused (`host_io.cpp:87-91`, `window.cpp:201`).
- **Rumble:** `host_io.cpp:80-82` and `126-150`: PS2 small motor is binary (bit 0, to the right motor at 65535), large motor is 0-255 (to the left motor, scaled x257). Stopped on exit.
- **Where the pad goes:** `window.cpp:48-54` `apply_pad` calls `ps2::set_pad_buttons`, `set_pad_axis`, `set_pad_pressure` (`ps2-web-recomp` `runtime/src/system.cpp:525-528` -> `iop/sio2.cpp:399-400`). Script-driven `--press` overrides the controller (`window.cpp:219`).
- **Gap:** no SDL, no DualSense/DS4 support, no config file, no remapping.

## 2. PS2 facts (for the mapping)

- **Pressure mode:** command 0x4F (`sio2.cpp:152-158`) enters pressure mode (0x79) and sets the poll mask. In mode 0x79 the response is 21 bytes, with pressure bytes 9..20 only for pressed digital buttons; the rest are 0 (`sio2.cpp:96-98`). Analog mode 0x73 (command 0x44 with byte 3 = 1, `sio2.cpp:124-130`) adds the two stick bytes 5..8. Digital 0x41 has no stick bytes.
- **Pressure order (PS2):** Right, Left, Up, Down, Triangle, Circle, Cross, Square, L1, R1, L2, R2 (**Confirmed**, `sio2.cpp:97`).
- **Rumble:** command 0x4D/0x42 maps motor indexes; small motor binary, large motor 0-255 (`sio2.cpp:109-118`, `139-151`).
- **GT4 pad config:** no game-side libpad symbols were found in our ELF or the decomp (section 0). The exact GT4 options are **Unknown** here; community names are "DualShock 2 button config 1P" and presets (**Hypothesis**).

## 3. PCSX2 approach (GPL-3.0+, design reference only; no code copied)

Files read: `private/tooling/pcsx2-slice101-source/pcsx2/SIO/Pad/PadDualshock2.cpp`, `pcsx2/Input/XInputSource.cpp`, `pcsx2/Input/SDLInputSource.cpp`, `pcsx2/Input/InputManager.cpp`.

- **Bindings:** a list of `InputBindingInfo` per input, with a type of `Button`, `Axis` or `HalfAxis` (`PadDualshock2.cpp:18-47`). L2 and R2 are `HalfAxis`. Analog sticks are four `HalfAxis` each.
- **Per-input settings:** `<Input>Scale` and `<Input>Deadzone` for each binding (`InputManager.cpp:903-914`). Global settings: `Deadzone`, `AxisScale`, `UseDiagonalScaleCorrection`, `ButtonDeadzone`, `PressureModifier`, `InvertL/R`, motor scales (`PadDualshock2.cpp:55-89`, `InputManager.cpp:140-146`).
- **Trigger path:** trigger value 0..1 -> `ButtonDeadzone` -> `rawInputs = value*255` (pressure) and the digital bit set above zero (`PadDualshock2.cpp:680-691`).
- **Face-button pressure:** the "Apply Pressure" button (`PAD_PRESSURE`) scales all non-analog, non-trigger buttons by `PressureModifier` (`PadDualshock2.cpp:693-718`). L2/R2 are excluded (`IsTriggerKey`), so triggers stay at full pressure.
- **Auto-mapping:** XInput and SDL report generic bindings (`XInputSource.cpp:41-48`, `108-123`; `SDLInputSource.cpp:137`). `InputManager::GetGenericBindingMapping` (`InputManager.cpp:1808-1816`) feeds the Qt binding widget (`pcsx2-qt/Settings/ControllerBindingWidget.cpp:268,1233`). So XInput A = Cross, B = Circle, X = Square, Y = Triangle, LT/RT = L2/R2 (**Confirmed**).
- **Gap:** PCSX2 has no GT4-specific preset that I could find (**Unknown**); per-game settings are in its GameDB, not in this tree. The forum fix for modern pads (trigger sensitivity values about 0.33/0.18) is a per-game plugin tweak, not a PCSX2 default (community, unverified).

## 4. Wheels overlap (TASK-018)

`docs/MENUS-AND-WHEEL.md:23-37` covers Logitech G27 and SDL3 haptics for real wheels (throttle/brake/clutch). If the launcher adopts SDL3 for pads, the same layer can read wheels, DualSense and DS4. The wheel's 8-bit pedals would then use the same analog-to-pressure bridge as triggers.

## 5. Recommendation

### 5.1 Backend (Windows)
- **Keep XInput as the first backend** (no new dependency, already in use, fixture-tested, Confirmed), and add **SDL3 GameController** as the second backend for DualSense, DS4, Switch Pro and wheels. SDL3 also covers Android later (phase 2) and gives one codepath.
- Keep the XInput mapping as the default for Xbox-style pads and the fallback if SDL3 is absent.

### 5.2 Binding model and config file (launcher)
- Logical actions, not raw PS2 bits: `cross, circle, square, triangle, l1, r1, l2, r2, l3, r3, select, start, dpad_*, lstick_*, rstick_*, pressure_modifier`.
- Each action is bound to one or more sources: `xinput:button:<name>`, `xinput:trigger:left|right`, `xinput:axis:<name>`, `key:<VK name>`, `sdl:<name>`.
- Per-binding options: `scale`, `deadzone`, `invert`, `curve` (linear/quadratic), `pressure_mode` (`digital` or `analog`, where `analog` drives pressure from the trigger value).
- File: `%APPDATA%\GT4Recomp\controls.toml` (TOML, like the rest of the launcher plan). Example:

```toml
schema = 1
backend = "xinput"            # "xinput" | "sdl3"

[pad.default]
cross    = ["xinput:button:A", "key:Z"]
square   = ["xinput:button:X", "key:A"]
triangle = ["xinput:button:Y", "key:S"]
circle   = ["xinput:button:B", "key:X"]
l1 = ["xinput:button:LB", "key:Q"]
r1 = ["xinput:button:RB", "key:W"]
l2 = ["xinput:trigger:left", "key:1"]    # pressure from the trigger value
r2 = ["xinput:trigger:right", "key:2"]
select = ["xinput:button:Back", "key:Backspace"]
start  = ["xinput:button:Start", "key:Enter"]
lstick_x = ["xinput:axis:LX"]
lstick_y = ["xinput:axis:LY"]
rstick_x = ["xinput:axis:RX"]
rstick_y = ["xinput:axis:RY"]
trigger.deadzone = 0.10
trigger.curve = "linear"
stick.deadzone = 0.12
```

### 5.3 Default auto-mapping (XInput)
- The owner's working setup, which matches PCSX2 and our current build: **LT -> L2 pressure (bit 8), RT -> R2 pressure (bit 9)**, both as digital bit and pressure byte, and the rest as now (A = Cross, B = Circle, X = Square, Y = Triangle, LB/RB = L1/R1, Back = Select, Start = Start, D-pad = D-pad, sticks = analog).
- To make GT4 treat the triggers as analog, the **pressure byte is the signal**: GT4 reads pressure only in mode 0x79 (enabled by its own 0x4F command), and reads it only for pressed bits. Our runtime already sends pressure bytes for L2/R2 (`host_io.cpp:57-63`, `window.cpp:48-54`, `sio2.cpp:96-98`).
- For GT4 to use them as throttle/brake, the owner must pick, in GT4's controller menu, R2 for accelerate and L2 for brake (**Hypothesis**; menu name to be confirmed). Sticks are a second option (right stick Y, if the game accepts analog mode).
- **Launcher UX:** show a "Pedals on triggers" hint on the controller page that tells the user this menu step; do not write any card or save.

### 5.4 Keyboard defaults and remapping
- Defaults: the table in section 1 (unchanged), with keyboard keys moved from `window.cpp` into the config file.
- Remapping UI: a list of PS2 actions (rows), each with a "Keyboard" and "Controller" column. Press to capture; Esc clears; conflicts are shown inline. Keyboard uses `VK_*` codes (Win32), not raw scan codes.
- Keyboard digital only (pressure 255) unless the binding sets `pressure_mode = "analog"` with a hold-to-ramp option (Hypothesis, optional).

### 5.5 Deadzones and curves
- Trigger: deadzone 0.10 default (PCSX2 default is 0, and XInput fixed threshold is 30/255 = 0.12). Curve linear; optional "gentle" (quadratic) for light throttle.
- Stick: circular deadzone, `stick.deadzone = 0.12` (XInput recommends 7849/32767 = 0.24 for left and 8689/32767 = 0.27 for right, which is too big for a throttle stick; **Hypothesis**: tune in test).
- Y inversion stays fixed (PS2 stick up = negative, Confirmed in `host_io.cpp:67-69`).

### 5.6 Rumble
- Keep XInput rumble as now: small motor binary to the right motor, large motor 0-255 to the left motor (`host_io.cpp:80-82`). Add a `rumble.scale` option (PCSX2 has motor scales). Stop on focus loss (`window.cpp:144-147`).

### 5.7 Runtime changes (where)
- `native/host_io.cpp` / `host_io.hpp`: generalize `map_gamepad` into the binding table (no hard-coded PS2 bits).
- `native/window.cpp:205-218`: move keyboard bindings out of code into the config table.
- `native/host_io.cpp`: add SDL3 backend (new file `native/host_sdl3.cpp`); keep `PadState` as the common output.
- No change needed in `ps2-web-recomp` `sio2.cpp` for L2/R2 pressure (already served). Verify the 0x4F/0x44 handshake from the launcher's run log (new diagnostic counter for pad mode 0x41/0x73/0x79).

## 6. Effort estimates (rough, my judgment)

| Item | Effort | Note |
|---|---|---|
| Binding table + TOML loader + keyboard out of code | 3-4 days | Pure C++ and launcher side; fixture-testable |
| Keyboard remapping UI (Slint) | 3-4 days | Depends on launcher M5 |
| SDL3 backend (XInput parity + DualSense/DS4) | 4-6 days | New dependency (zlib license); pins needed |
| Trigger analog verification on owner's build | 0.5 day | Owner test with pressure diagnostics |
| Preset for "pedals on triggers" | 0.5 day | Hint text only; no card writes |

## 7. Test plan

- **Fixtures (CTest, `host_io_fixture`):** trigger 0..255 -> pressure monotone; deadzone boundary; LT/RT digital and pressure both set; keyboard table loaded from config; SDL3 and XInput give the same `PadState` for the same logical press.
- **Python:** config round-trip; invalid binding rejected with a readable error.
- **Owner checks (manual):** (1) LT/RT -> L2/R2 pressure while the car is in the game; (2) in GT4's controller menu, assign R2/L2 to accelerate/brake and confirm throttle and brake respond proportionally; (3) right stick Y for throttle in analog mode; (4) keyboard remap round trip; (5) rumble on a real pad.
- Logs: count of pad mode commands received (0x41/0x73/0x79) in the probe log, to show whether GT4 requested pressure mode.

## 8. Open questions (Unknown)

1. Exact GT4 menu name and location for R2/L2 accelerate/brake (owner to confirm).
2. Whether GT4 reads L2/R2 pressure or only the digital bits (needs a pressure-mode probe or an owner test).
3. Whether GT4 accepts stick throttle only in analog mode, and why the owner's sticks did nothing.
4. Whether GT4 stores the controller assignment on the memory card (affects whether a preset can be shipped).

## 9. Sources

Repo: `native/host_io.cpp:20-150`, `native/host_io.hpp:8-32`, `native/window.cpp:48-54,144-148,201-225`, `native/host_io_fixture.cpp:17-140`, `docs/lessons/slice112-native-input-audio.md`, `docs/decisions/0054-windows-audio-xinput.md`, `docs/BUILD.md:165`, `docs/MENUS-AND-WHEEL.md:23-37`, `docs/reverse-engineering/slice112-native-input-audio.md:14-36`, `git show origin/task/019-launcher:docs/launcher/LAUNCHER-PLAN.md` (lines 261, 488, 605).
Donor: `private/references/ps2-web-recomp/runtime/src/iop/sio2.cpp:20-165,399-400`, `runtime/src/system.cpp:525-528`, `host/window.cpp:159-184` (donor SDL host, for comparison).
PCSX2 (GPL-3.0+, read only): `private/tooling/pcsx2-slice101-source/pcsx2/SIO/Pad/PadDualshock2.cpp:18-89,152-163,300-302,571-718,809-812,826-899`, `pcsx2/Input/XInputSource.cpp:41-48,108-123`, `pcsx2/Input/SDLInputSource.cpp:31-137`, `pcsx2/Input/InputManager.cpp:845-849,903-940,1808-1816`, `pcsx2-qt/Settings/ControllerBindingWidget.cpp:268,1233`.
Web (community, not primary): GameFAQs GT4 board thread 41621908; GTPlanet "DUALSHOCK2 - Handbrake Setup" (83383) and "Question regarding using the controllers for GT4" (56667); PCSX2 forum "Use DS4 triggers in GT4 help", "Right analog stick - GT4 Spec II", "GT4 configuration guide+shader" (page 11); skec.site GT1-4 mappings (blog/2022/gt.html); Silent's mod page cookieplmonster.github.io/mods/gran-turismo-4/. The forum pages returned HTTP 403 (or refused connection) to direct fetch, so their content is from search-result summaries and is **Hypothesis**.
Not verified: no ELF or decomp pad code was found, and no game launch, build, or card access was done (per rules).
