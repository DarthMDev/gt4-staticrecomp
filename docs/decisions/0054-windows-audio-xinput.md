# 0054 — Windows audio and XInput in the isolated execution pilot

Status: accepted for host integration, 2026-10-13; owner requests audio/XInput before Vulkan
customization and explicitly accepts proceeding without a physical controller.
Baseline d7f25fe; branch codex/audio-xinput-pilot-112.

Connect the frozen MIT runtime's actual SPU2 PCM sink to Windows waveOut:
48kHz, stereo, signed16, interleaved. Four owned512-frame buffers copy the
runtime's stack-local32-frame batches immediately. A manual-reset event signals
completion; reset then rescan all headers before waiting. Reuse only completed
headers. Backpressure waits pump window messages without advancing guest time.
Normal completion drains the partial buffer; close/error cancels, unprepares
and closes before releasing storage. Detach the sink on every exit. A failed
driver cleanup terminates rather than freeing a buffer the driver still owns.

Use XInputGetState/SetState with fixed DualShock2 bit/axis/pressure/motor mapping,
standard deadzones, wide signed Y inversion, focus neutralization and disconnected
slot rescan. Keyboard remains available. Scripted --press takes exclusive
precedence for repeatable probes. Xbox A maps Cross and X maps Square; triggers
map L2/R2 rather than silently changing GT4's configured driving controls.

The manual launcher verifies runtime/shaders/CORE/ELF/disc, uses a persistent
private card initialized once from the qualified profile L seed, and preserves
a card backup per session. Parent byte lock prevents duplicate launchers; the
native process separately holds an exclusive card lock until it exits, including
an orphaned child. No trace/capture overhead during manual play. Inherited
PS2_DUMP_AUDIO is cleared; explicit capture stays in the new private run.

Trade-offs: WinMM is sufficient for this pilot and adds no external dependency;
Microsoft recommends WASAPI for modern applications. This host connection does
not establish SPU2 fidelity, physical controller behavior or acoustic audibility.
The synchronous renderer/runtime currently generates audio too slowly in some
segments: empty-queue observations are counted, not hidden with guessed clock changes.
Full race/stability/save coverage is a separate acceptance milestone.

Race loading exposed a Vulkan guard that rejected zero workgroups. The API's
group-count contract permits zero; zero raster work leaves VRAM unchanged in the
synthetic GPU control, while invalid pipeline/parameter/device counts still stop.
The6000-frame game repeat finishes normally;48 common pre-failure captures are
byte-identical and the first actual zero raster dispatch is pipeline5/x0/y0.
Longer driving controls qualify response separately from host acceptance.
This functional repair is necessary for the requested test, not graphics
customization. Production time/IRQ/kernel and frozen111 artifacts remain untouched.

Sources: [waveOutOpen](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutopen),
[header lifetime](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutunprepareheader),
[XInput](https://learn.microsoft.com/en-us/windows/win32/xinput/getting-started-with-xinput),
[vkCmdDispatch](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDispatch.html).

Evidence: [slice112](../reverse-engineering/slice112-native-input-audio.md),
[metadata](../inputs/slice112-native-input-audio.json).
