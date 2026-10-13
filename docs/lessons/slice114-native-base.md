# Slice114 — An Active Path and a Recoverable Archive

A dedicated branch preserves versioned state without forcing the project to build two distinct engines on every change. For this reason, we published the archive branch first, verified its dependencies, and only then removed the tools and tests unique to the original engine. Private files are not covered by this Git protection: inputs, saves, and outputs remained in local paths, verified by hashes.

The term "native" describes where the executable and translated code execute. It does not automatically describe every subsystem. Our EE executes AOT-compiled code; the IOP and vector units still interpret instructions and microprograms, and PS2 hardware devices are software implementations. Maintaining this distinction allows planning genuine improvements without starting from the false premise of zero emulation.

Tests must follow the implementation they verify. Archiving the original engine archives its corresponding tests; the active test suite continues verifying the GPU, audio/input, input verification, launcher, and persistence. A smaller test count must be justified by reduced scope, without lowering verification thresholds to let bugs pass.

Building this base reuses a private, pre-translated static library. Recompiling the host/runtime is not equivalent to regenerating the entire game from an ISO. Documentation must state this explicitly so subsequent engineers understand what can be reproduced independently and which pinned artifacts must be preserved.
