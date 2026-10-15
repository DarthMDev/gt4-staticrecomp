# Draft upstream report: `GSInterface::gif_transfer` with GIF packets split across calls

Status: draft only, not sent. Upstream (github.com/Arntzen-Software/parallel-gs) has GitHub issues
disabled and does not accept external contributions (README, "Contributions"). The owner decides whether
and how to send this (for example by e-mail to the maintainer). GT4Recomp carries the fix as a build-tree
patch (`native/parallel_gs.cmake`, `native/THIRD_PARTY.md`), slice 167.

Checked against: pinned commit `cc6184af7e0c03da603045ca371ffa5dae9b0655` and the default branch on
2026-10-15 (latest commit touching `gs/gs_interface.cpp`: `3a66c1976170`, 2026-09-02). Both contain the code
quoted below.

---

**Title:** `gif_transfer`: optimized PACKED path over-reads, or never advances, when a GIF packet spans calls

**Summary.** In `GSInterface::gif_transfer`, the optimized PACKED handler is sized from the whole call,
not from the qwords that remain after the current index:

```cpp
if (path.reg == 0 && optimized_draw_handler[path_index])
{
    uint32_t nloops_to_run = std::min<uint32_t>(size / nreg, path.tag.NLOOP - path.loop);
    (this->*optimized_draw_handler[path_index])(&qwords[i], nloops_to_run);
    i += nloops_to_run * nreg;
    path.loop += nloops_to_run;
}
```

`size` is the number of 128-bit words in the call and `i` is the current index, so after a GIF tag
(`i >= 1`) the handler can be asked for more loops than the call holds.

**Effects.**
1. Over-read: a call that carries a tag and only part of its body (or the tail of one packet and the
   start of the next) makes the handler read `nreg * nloops_to_run` words starting at `i`, past the end of
   the caller's buffer, and advance `path.loop` beyond what was delivered. The next call then resumes in
   the wrong place.
2. No progress: when fewer than `nreg` words are left at a loop boundary and the whole call is also
   shorter than `nreg` (for example a 2-word call into a 3-register STQ/RGBAQ/XYZ2 packet), `size / nreg`
   is 0, `i` does not advance and the loop never ends.

Hosts that always pass whole GIF packets per call do not hit this. A host that forwards GIF data per DMA
chunk (PATH3 chains with REF tags) or per VIF DIRECT command (PATH2) does.

**Reproduction (headless).** Build a PATH3 stream with A+D setup packets, an IMAGE upload and a PACKED
`ST, RGBAQ, XYZ2` sprite (which selects the optimized handler). Send it once as a single call and once in
1-, 2-, 5- or 7-word pieces, each piece in its own buffer padded with garbage, then read the frame buffer
back with a LOCAL->HOST transfer. With the current code our split delivery test did not terminate (killed
after 5 minutes; pieces shorter than NREG at a loop boundary cannot advance). With the change below all
four piece sizes give the single-call frame buffer.

**Suggested change.**

```cpp
if (path.reg == 0 && optimized_draw_handler[path_index] && size - i >= nreg)
{
    uint32_t nloops_to_run = std::min<uint32_t>(uint32_t((size - i) / nreg), path.tag.NLOOP - path.loop);
```

A partial loop then falls through to the generic per-register PACKED path, which already handles one
word at a time and keeps `path.reg` across calls.
