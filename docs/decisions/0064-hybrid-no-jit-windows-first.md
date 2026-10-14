# 0064 — Optimize the hybrid no-JIT base before scene-renderer replacement

Date: 2026-10-14. Status: Accepted planning direction on the dedicated hybrid branch.

The owner prioritizes stable 60 FPS/correct speed, PCSX2-style GS graphics options,
and future Android/iOS portability over replacing the complete graphics pipeline.
Windows comes first; named mobile targets are S23 Ultra/Snapdragon8 Gen 2 and
iPhone 15 Pro Max. Preserve the native-scene research at slice 140/main.

Retain necessary PS2 models and explicit interpretation where qualified. EE/VU
machine-code compilation is offline per target; guest JIT is excluded. GPU shader
compilation is distinct. Selective PCSX2 GS implementation reuse is authorized
with attribution and a GPL-compatible derivative strategy; no code is imported
or license changed by this decision.

First attribute Windows costs, consume verified OpenCode findings, and optimize
demonstrated hotspots. Qualify renderer interfaces before adopting a hardware-GS
backend or scaling targets. Do not assume VU is the bottleneck, enlarge the final
image and call it internal resolution, or treat dithering as deinterlacing.
Mobile packages need ARM64 recompilation, host integration and sustained-device
qualification; an existing x86-64 library and no-JIT label are insufficient.

See [Hybrid Port Plan](../HYBRID-PORT-PLAN.md) for phases and exit gates. This plan
does not report new measurements or authorize wholesale, unverified imports.
