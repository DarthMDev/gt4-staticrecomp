# Active Requirements

2026-10-13; supersedes the original engine summary, which is preserved on the archive branch.

1. Hash-pinned GT4 USA v2.00, Windows x86-64 / C++20 / Clang, ahead-of-time (AOT) main game code,
   and required PS2 runtime. First make the game functional and stable; subsequently decompile
   for readability and extensibility. Never claim zero emulation of subsystems.
2. No game payloads, translated code, captures, save files, or binaries distributed via Git.
   All inputs and producers must have hashes; pin updates require qualification.
3. Real Vulkan on GPU; do not substitute guest output with replays or mock frames. Maintain
   audio, input, persistent memory card / backups / locks, and clean shutdown validation.
4. 16:9 default with 4:3 option, coherent camera and HUD; respect original video modes
   until future progressive-scan / internal resolution changes are qualified.
5. Keep unknowns explicit: performance overhead, duplicated input, audio
   continuity, PS2 fidelity / scan modes, and extended stability. No profiling, auditing, or fixes
   for performance/input while the owner's instruction to record only remains in effect.
6. Future 144 FPS / monitor refresh must preserve 100% game speed and decouple simulation from rendering;
   repeating frames or raising the guest clock does not demonstrate true high-refresh support.
7. Explicit guest state, human-readable code, and no silent fallbacks.
   New changes require automated controls; evidence and documentation are mandatory parts of delivery.
8. The original engine is not a second active product; restoring code from the
   preserved archive branch requires a documented technical decision.
