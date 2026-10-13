# 0055 — Native widescreen defaults and guest video modes

Status: accepted widescreen implementation and synthetic video bounds; real
480i/480p/1080i racing now owner-confirmed functionally, 2026-10-13.
Independent scan-mode fidelity remains open. Baseline f9346c8;
branch codex/widescreen-video-113. Owner prioritizes16:9/default and keeps4:3
as an option. Qualify480p/1080i after entering a real race, then open manual play.

Use the original GT4 Screen Settings option to enable native widescreen and
save through the game. The pinned ELF setter00437110 updates the option and
projection; race application004365F0 publishes wide flag006184F0 for HUD layout.
A Vulkan viewport alone cannot create this native camera/HUD behavior.
No translated game patch, guessed memory write or forced camera projection.

Presentation defaults to16:9, with a centered rectangle and black margins.
F6 alternates presentation16:9/4:3; the original game Aspect Ratio must match.
Jogar-GT4.cmd --aspect 4:3 selects the host option. For newly initialized private
cards, choose a hash-pinned original-UI widescreen seed for16:9 and the preserved
normal110 seed for4:3. Existing owner cards remain selected and are not reseeded
by the launcher. Each manual session retains its existing backup/exclusive locks.

The internal resolution stays1x. Decode DISPLAY/MAGH/MAGV and SMODE2 exactly
through the existing donor path. Expand display scratch to the GS2048x2048
dimension ceiling; do not assume nominal1080i means a1920x1080 game framebuffer.
Keep Vulkan image recreation and bounds checks during source-size transitions.
Do not remove the inherited half-height sampling or adopt a guessed parity policy.

Independent CPU VRAM reads compare every synthetic display pixel at640x448
half-height,640x480 progressive,640x1080 half-height,1920x1080 and2048x2048.
ActualGPU presents these shapes in both aspects; oversize is refused. Rectangle
controls cover centered bars, ultrawide host margins, empty/tiny sizes and64-bit
arithmetic; this is not native ultrawide gameplay. Original menu transitions,
CRT calls, resolved sizes and real race captures qualify the modes separately.

Trade-offs: minimal host integration and the game's existing settings preserve
camera/HUD intent without rebuilding the frozen game library. Full temporal
field reconstruction, custom internal scales/effects/textures, PS2 fidelity,
performance and finished-race/save coverage remain separate work.
The MIT donor and all112/111 producers stay frozen; production MSVC is unchanged.

Owner now requests leaving the game open and navigating personally, ending
automated menu/race exploration. Preserve the current card; it still matches
the original4:3 seed when manual session starts. Owner selects native16:9 and
video mode through original Options. Do not silently claim the existing card
has migrated to16:9. Newly created cards default to the qualified wide seed.
Owner performance screenshots are recorded only; no performance work now.
