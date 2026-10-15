"""Offline GS primitive interpolation oracle (TASK-031 Prototype A).

Reads GT4_PGS_CAPTURE streams, matches primitives between captured frames, builds
synthetic in-between frames by lerping screen-space geometry, and writes them back as
capture segments that the offline replay renders. See docs/HIGH-FPS-STUDY.md.
"""
