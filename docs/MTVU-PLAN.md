# TASK-011 Plan: VU1 on a Worker Thread (MTVU)

2026-10-15. Read-only study; nothing was built, launched or benchmarked. Sources:
donor runtime `private/references/ps2-web-recomp` (frozen MIT, `1b48087`), our overlays
and `native/`, PCSX2 `aa7ab4306e` (`private/tooling/pcsx2-slice101-source`, GPL-3.0,
design reference only, no code copied), gt4-decomp `b5995291` (read only), the
reconstructed ELF and owner probe logs. Labels: **[C]** Confirmed (read in code or
counted in logs), **[H]** High confidence, **[Hy]** Hypothesis, **[U]** Unknown.
Donor paths are relative to `runtime/src/` unless stated.

## 0. Summary

- Goal: move VU1 microprogram execution (owner F8: 5.2 ms night race, 7.6 ms rally,
  7.8 ms showroom on a Ryzen 7 5700X) off the EE thread, on by default, `GT4_MTVU=0`
  restores today's path, results exact (headless frames byte-identical to slice166).
- Chosen design (section 5): **one VU1 worker thread; the EE keeps a shadow VIF1
  parser (donor design) and an ordered GS-operation journal with placeholders
  (PCSX2 MTGS-style ordering); the GS front end and Vulkan stay on the EE thread and
  drain the journal in serial order.** Present stays at the same program point, so
  no added latency. A separate render thread is a later, independent task.
- The donor `start_vu_thread` is **not usable as-is** [C]: it requires the donor render
  thread, and that threaded mode uses inexact download caches (section 1.4).
- Hard sync points (EE blocks until the worker is idle): VBlank before present,
  GS→host downloads, EE access to VU1 code/data memory, state save/load, frame dumps,
  shutdown, VIF1 reset. Everything else the EE observes (VIF1 STAT/MARK/ROW/TOPS,
  DMA ch1 completion and interrupts, GS CSR/SIGLBLID from path 2/3) is computed
  exactly on the EE thread without waiting.
- One invariant cannot be proven statically: **PATH1 (XGKICK) packets must not
  carry SIGNAL/FINISH/LABEL** (their interrupt would arrive late). Step 0 measures it;
  the runtime checks it on every drained packet.
- Effort: about 9-13 agent-days in 7 steps, plus owner sessions.

## 1. How VU1 runs today

### 1.1 Single-thread path (current main, slice166) [C]

1. DMA ch1 (or VIF1 FIFO writes) reach `vif::transfer(1, …)`: `dmac.cpp:47-71`
   (`deliver`, `case 1` at :60), `hw.cpp:275` (FIFO), chain/normal modes
   `dmac.cpp:118-210`. The whole chain is processed synchronously inside the EE store
   that starts the channel.
2. `vif.cpp:271-283` parses commands; `execute` (`vif.cpp:220-269`): STCYCL/OFFSET/
   BASE/ITOP/STMOD/MARK update unit state; FLUSHE/FLUSH/FLUSHA (`0x10/0x11/0x13`) are
   **no-ops** (:234); MSCAL/MSCALF (`0x14/0x15`) and MSCNT (`0x17`) call `run_vu`
   (:57-71: double-buffer TOPS/TOP/ITOP, `vu::set_tops`, `vu::run`); MPG (`0x4A`) writes
   VU1 code and bumps `code_changed` (:240-254); DIRECT/DIRECTHL (`0x50/0x51`) call
   `gif::transfer(2, …)` (:255-258); UNPACK (`0x6x`) writes VU1 data (:126-203). An
   i-bit sets STAT INT, stalls the unit and raises INTC 5 (:264-268); `FBRST` STC
   resumes it and calls `dmac::resume` (:350-374).
3. `vu_micro.cpp:921` `run` → `interpret` (:765-786, chaining for jump-register
   dispatch) → `run_entry` (:788-813). Our overlay gate
   (`scripts/vu1_aot_overlay.inc:228`) runs the generated program first
   (`aot_run_if_registered`, :202-224; budget resume to `run_fast`, :93-99).
4. XGKICK (`vu_micro.cpp:413-434`, op `0x6C` at :525) copies the GIF packet from VU1
   memory into a stack buffer and calls `gif::transfer(1, …)`; the overlay replaces
   that call with the kick-sink hook (`vu1_aot_overlay.inc:22-24`). `gif::transfer`
   (`gs/gif.cpp:168-175`) renders immediately when no queue is active: the GS front
   end (`gs::write`, `write_packed`, `write_image`) runs inside the VU1 call.
5. VU1 is instant: no guest cycles are charged, VPU_STAT reads 0 (`cop.cpp:29`),
   CMSAR1 is not implemented (`cop.cpp:47-50`), VU1 registers are not reachable
   from the EE or VU0 (VU0 data wraps at 256 qwords).

Consequence [C]: the `vu1_aot` F8 category includes the GS front-end work of every
PATH1 packet (the GIF walk and vertex/primitive processing run inside the scope).
Its split is **[U]** and is measured in step 0.

### 1.2 Where our overlays hook [C]

`scripts/prepare_perf_runtime.py:20-47`: pinned donor files (`system.cpp`,
`vu_micro.cpp`, `vif.cpp`), perf scopes (`iop_run`, `ee_dispatch`, `vu_interpret`,
`vif_gif` on `vif::transfer`), and the VU1 overlay anchors (`includes`, `kick_sink`,
`xgkick_hook`, `helpers`, `run_gate`). The VU0 overlay hooks `run()` for unit 0.
MTVU needs new anchors in `vif.cpp`, `gs/gif.cpp`, `gs/gs.cpp`, `memory.cpp`,
`system.cpp` and `vu_micro.cpp`; `gif.cpp`, `gs.cpp` and `memory.cpp` are not pinned
yet and must be added to `PINS`.

### 1.3 The donor VU thread [C]

- `system.cpp:423` `start_vu_thread` allocates a 32 MiB `Ring` (`gs/queue.cpp:82-134`,
  SPSC, 16-byte headers, wrap records, futex/atomic-wait handshake).
- `queue::outbox()` (`queue.cpp:72-75`): while the VU ring is active, **every** GS
  post from the EE thread goes into the VU ring, so one ordered stream carries VIF1
  and GS traffic.
- EE side, VIF1 runs as a **shadow** (`vif.cpp:32-34`): commands are still parsed on
  the EE thread on `g_units[1]` so STAT, DBF, TOPS/TOP/ITOP, MARK, CYCLE, MODE, MASK,
  CODE and the i-bit stall stay exact; MSCAL returns before running VU1 (:64);
  UNPACK is a dry run except in difference mode 2, which must keep ROW (:126-132,
  writes to `g_dry_run`); MPG is skipped (:241); DIRECT only scans GS signals
  (`gif::scan_signals`, :256). `vif::transfer`/`vif::write` post the raw packet or
  register write to the worker (:309, :351).
- Worker (`system.cpp:430-453`, role `Vu`): replays `kVif1`/`kVif1Write` on its own
  `g_vu_side` state, runs VU1, handles `kVuReset` and `kVuFence`
  (`sync_vu_thread`, :224-230, used only by save/load state at :256). **Every other
  record is forwarded to the render ring (:448).**
- Signals: `gif::transfer` scans PATH2/3 for SIGNAL/FINISH/LABEL and TRX registers on
  the EE thread (`gif.cpp:87-140`, :173); `gs::write` skips `signal()` when the render
  queue is active (`gs/gs.cpp:614-616`). PATH1 packets posted by the worker are
  **not scanned** (role check at `gif.cpp:173`).

### 1.4 Why it is not usable as-is [C]

1. Without the render thread, `render().post` at `system.cpp:448` uses a ring of
   capacity 0 (`head % capacity_`, division by zero), and `gif::transfer` from the
   worker would render on the worker while the EE renders PATH3: a data race and a
   broken GIF order. The native build never starts the render thread
   (`docs/GS-ASYNC-DESIGN.md`, `native/window.cpp:185-235` presents on the EE thread).
2. The donor threaded GS path is inexact by design: a 16-entry download cache returns
   earlier pixels for a repeated key (`gs.cpp:682-705`), and repeated download ranges
   become asynchronous stale downloads (`gs.cpp:192-217`). The donor host disables
   threads for frame dumps and timing (`host/window.cpp:315`).
3. PATH1 SIGNAL/FINISH/LABEL are lost (not scanned, not applied).
4. On Windows `Ring::wait_for_work` sleeps 1 ms per idle poll (`queue.cpp:153-156`).
5. `g_top/g_itop` are one global pair shared by VIF0 and VIF1 (`vu_micro.cpp:40`,
   `:633-636`, read by XTOP/XITOP at :523-524 and by generated code via `vu::top()`,
   `tools/vu_recomp/src/vurecomp.cpp:383-384`): a VIF0 MSCAL on the EE thread would
   race the worker. GT4 issues no VIF0 MSCAL (owner logs: VIF0 only `00/10/4A`) [C],
   but the overlay must split the pair per unit.
6. Perf scopes on the worker would trip `wrong_thread` (`native/perf_probe.cpp:53-80`;
   `perf_accounting.hpp:45` "one host thread").

Reusable [C]: the shadow VIF1 parser, the EE-side signal scanner, the raw-packet
transport idea, `kVuReset`/fence records, the role mechanism.

## 2. Observation points (EE or other units depending on VU1)

Per-frame numbers are averages over whole owner sessions (menus and races mixed) from
the end-of-run donor counters [C]: session 084551 (`vu1-all`, 18,811 frames) and
091614 (`main`, 11,011 frames). Per-scene values are [U] until step 0.

| Point | Today | Frequency (per frame) | MTVU rule |
|---|---|---|---|
| VIF1 STAT/ERR/MARK/CYCLE/MODE/NUM/MASK/CODE/ITOPS/BASE/OFST/TOPS/ITOP/TOP/ROW/COL reads (`hw.cpp:186`, `vif.cpp:326-348`) | direct | ELF scan: 3 constant-address STAT reads (`0x4A20F4`, `0x4A2130`, `0x4AB55C`) [H]; the VIF1 interrupt handler at `0x4AB548` reads STAT.INT and CODE (`0x10003C80`) [C] | shadow state, **no wait** (shadow is exact, section 5.4) |
| VIF1 FBRST/ERR/MARK/ROW/COL writes (`vif.cpp:350-374`) | direct; STC resumes and calls `dmac::resume` | FBRST STC once per VIF1 interrupt (~49 [Hy], see ch2) | apply to shadow, post record (gets a job id and a journal placeholder) |
| VIF1 MARK command | sets MARK | 0 (command `0x07` absent) [C] | shadow |
| FLUSHE/FLUSH/FLUSHA | no-op | FLUSH 355-380, FLUSHA ~1, FLUSHE ~0 [C] | stay no-ops; ordering is kept by the journal |
| MSKPATH3 (`0x06`) | no-op | ~1.1 [C] | no-op (baseline order = program order) |
| MSCAL/MSCALF/MSCNT | runs VU1 | MSCAL 724-815, MSCALF ~0.6, MSCNT 0 [C]; compiled VU1 calls 2,435-2,727 incl. chained [C] | worker |
| DIRECT/DIRECTHL (PATH2 in VIF1) | GS now | 197+220 commands [C] | EE scans signals; worker emits the packet into its job output |
| XGKICK (PATH1) | GS now | 1,497-1,525 packed + 321 reglist tags [C] | worker emits into job output; EE drains in order |
| DMA ch1 completion/IRQ (`dmac.cpp:73`, `finish`) | after deliver unless stalled | ch1 starts 1.85-2.4 [C] | decided by shadow stall state: exact, no wait |
| DMA ch1 reverse (GS→host, `dmac.cpp:118-127` → `gs::read_image`) | GPU download | [U] (night race shows 4.1 ms GS wait) | **hard sync**: drain worker + journal, then download (exact path only) |
| DMA ch2 PATH3 (`dmac.cpp:62`) | GS now | 49.5-56 starts [C], 100 image tags [C]; started from the VIF1 interrupt handler (`0x4AB614-0x4AB658`: D2_CHCR \|= 0x100) [C] | copy payload into the journal behind pending placeholders; EE scans signals |
| GS privileged writes (`gs.cpp:765-805`: CSR reset, IMR, SIGLBLID, display regs) | direct | ELF scan: 7 CSR, 3 IMR write sites [H] | EE-side registers stay on the EE; CSR RESET's vertex reset becomes a journal op |
| GS CSR/SIGLBLID reads (`gs.cpp:755-762`, kernel `on_vblank_start` `kernel.cpp:433-438`) | EE state | 4 CSR read sites [H]; once per VBlank in the kernel | no wait: PATH2/3 signals are prescanned; PATH1 invariant (below) |
| SIGNAL/FINISH/LABEL in PATH1 | applied inline, may raise INTC 0 | **[U]** | invariant: none. Checked on every drained PATH1 packet (counter `mtvu_path1_signal`) |
| GIF reset (`gif.cpp:149-156`, GIF_CTRL writes at `0x49FF88`, `0x4A21C8`) | resets paths | rare [H] | journal op + scanner reset |
| VU1 code/data memory from the EE (`memory.cpp:28-43`, slow path `:74-78` incl. `code_changed`) | direct | ELF constant-address scan: 0 sites [H] (pointer-based access [U]) | **hard sync**, then direct access on the EE |
| VU1 registers (CFC2/CTC2, VPU_STAT, CMSAR1, VU0 0x4000 mirror) | not modeled | 0 [C] | nothing to do; keep "not implemented" logs |
| DMA reading VU memory | impossible: `dmac.cpp:36-39` sources RAM/SPR only [C] | 0 | none |
| VU reset (`vu_micro.cpp:619-627`) | inline | rare | post record + placeholder |
| VBlank: `gs::vblank` flush (`gs.cpp:809-817`), present (`native/window.cpp:234`), frame dump | inline | 1 | **hard sync** before present / dump; flush becomes a journal op |
| Save/load state (`system.cpp:255-257`) | inline | 0 in play | hard sync |
| Interrupt delivery (`kernel.cpp:408-431`) | `must_unwind` checks INTC/DMAC pending | very frequent | unchanged; all IRQ sources stay on the EE thread |

GT4 frame structure [H from code, Hy for timing]: the VIF1 i-bit handler at
`0x4AB548` checks VIF1 STAT bit 11, decodes CODE, starts a PATH3 GIF chain and writes
FBRST STC. So PATH3 texture uploads are sequenced by VIF1 stalls (~49 per frame).
With the shadow parser the EE runs through all stalls quickly while the worker
executes VU1; the journal keeps PATH3 between the right VU1 outputs.

## 3. PCSX2 MTVU (design reference) [C]

- Transport: 16 MB ring of u32 words from EE to the MTVU thread (`MTVU.cpp:128-237`):
  `VU_EXECUTE` (addr, TOP, ITOP, FBRST), `VIF_UNPACK` (VIF state + data), micro/data
  writes, VI/VF writes, ROW/COL writes. The EE thread parses VIF; only unpack and
  execute cross. A `WaitOnSize` comment documents a pointer bug and a 4 KB safety gap
  (`MTVU.cpp` ~212-236).
- Reads: VU memory reads/writes from the EE wait for the thread (`Memory.cpp:780-955`);
  VIF ROW/COL reads wait (`Vif.cpp:297-338`) because PCSX2 keeps ROW/COL on the thread;
  DMA touching VU1 memory waits (`Dmac.cpp:104-107`); save state waits.
- Path order: `ExecuteVU` (`MTVU.cpp:441-465`) inserts a placeholder packet into the
  MTGS ring (`TransferGSPacketData(GIF_TRANS_MTVU)`); MTGS blocks on `semaXGkick`
  until the PATH1 packet exists (`Gif_Unit.h:443-520`). PATH1 goes to the GS thread,
  never back through the EE.
- Signals: PATH1 SIGNAL/FINISH/LABEL become atomic flags on the thread
  (`Gif_Unit.cpp:89-128`) applied later on the EE (`MTVU.cpp:351-423`) — late by design,
  "double SIGNAL not handled" errors. VU cycles are estimated from past programs;
  VPU_STAT busy is faked (`VU1micro.cpp:51-63`).
- Known issues: `GameIndex.yaml` disables MTVU for some titles (corrupted textures,
  SPS, halved frame rate; e.g. lines 870, 1731, 3031, 3906). GT4 (`SCUS-97328`,
  lines 11870-11882) has only clamp and GS fixes, no MTVU/instant-VU1 override.
- Lesson for us: keep PCSX2's placeholder ordering, avoid its two inexact parts
  (late PATH1 signals, cycle estimates) — we do not need cycle estimates because
  VU1 is instant in our baseline.

## 4. Interaction with our pieces

- **GS submit ring / timeline** (`native/gs_submit_ring.hpp`, `native_vulkan.cpp:335-343,
  576-583, 1031-1141`): unchanged. Vulkan stays single-threaded on the EE thread
  because the journal is drained there [C by design].
- **Pacing** (`native/pacing.*`, `native/window.cpp:185-235`): present stays in
  `on_vblank`; the VBlank hard sync happens just before it and is accounted as its
  own category so the pacing histogram can tell VU1 lateness from host waits.
- **Perf probe**: the worker gets its own `Accounting` (one host thread per instance,
  as today). F8 adds: worker section (`busy_ms`, `idle_ms`, `vu1_aot`,
  `vu1_interpret`, PATH1/PATH2 output bytes, worker thread CPU ms); EE categories
  `mtvu_wait` split by reason (`vblank`, `download`, `vu1_memory`, `ring_full`,
  `state`) and `gs_drain` (EE executing journal ops, i.e. the GS front end); counters
  `jobs`, `journal_ops`, `opportunistic_drains`, `mtvu_path1_signal`. The existing
  `vu1_aot` EE-side value becomes ~0 with MTVU on; F8 must print both sides so
  owner reports stay comparable with `GT4_MTVU=0`.
- **TASK-012 paraLLEl-GS**: its front end (`gif_transfer(path, data)`,
  `write_register`, readbacks, `vsync`) is fed by the same drain loop, so GIF order
  is identical for either renderer. Journal entries are already (path, raw qwords)
  plus privileged-register and VBlank ops, a direct match. MTVU does not touch
  `native_vulkan.cpp` or the renderer, so the two branches only meet in the drain
  function (low merge risk). Downloads use one exact sync entry point.

## 5. Design

### 5.1 Threads and data

- **EE thread (producer + GS consumer):** guest EE, IOP, VIF0/VU0, shadow VIF1, GS
  privileged registers and signal scanners, interrupts, the journal, the GS front end
  and Vulkan, present, pacing.
- **VU1 worker (consumer of VIF1, producer of GIF packets):** `g_vu_side` VIF1 state,
  VU1 registers/code/data, the VU1 AOT registry, chaining, verify mode, XGKICK.
- **Input ring** (EE→worker, SPSC, 32 MiB default): records `VifPacket{job, qwc, bytes}`,
  `VifWrite{job, reg, value}`, `VuReset{job}`, `Fence{job}`, `Stop`. Every record
  that can produce GS output gets a monotonically increasing job id.
- **Output ring** (worker→EE, SPSC, 8 MiB default): records `Gif{path 1|2, qwc, bytes}`
  in emission order and `JobDone{job}` after the last output of each job.
- **Journal** (EE-local FIFO, single-threaded): `EeGif{path 3, copied bytes}`,
  `GifReset`, `VertexReset`, `VblankFlush`, and `Placeholder{job}`. A placeholder is
  appended whenever a job is posted.

### 5.2 Drain

`drain(blocking)`: pop journal entries in order; an `Ee*` entry executes the GS front
end; a `Placeholder{N}` consumes output-ring records until `JobDone{N}` (blocking:
wait; non-blocking: stop at the first incomplete job). Fast path: when the journal
is empty and no job is outstanding, EE GS operations execute immediately with no
copy (menus and the GT4 interrupt-driven PATH3 when the worker is idle).

Drain points: non-blocking at every journal append, every DMA start, and in
`run_due_events` (one atomic load when nothing is ready); blocking at the hard sync
points of section 2. **Every EE wait loop also drains the output ring**, so a full
output ring can never deadlock with a full input ring.

### 5.3 Exact sync rules

| Observation | Rule |
|---|---|
| VIF1 register reads, ch1 completion, VIF1 IRQ, STAT.INT/CODE | shadow, never wait |
| VIF1 register writes / FBRST | shadow + `VifWrite` job + placeholder |
| PATH2/PATH3 SIGNAL/FINISH/LABEL, TRX registers | scanned on the EE at enqueue (donor scanner); `gs::write` skips `signal()` for ops drained from the journal |
| PATH1 SIGNAL/FINISH/LABEL | invariant "none"; on detection: count, log the job and entry, fall back to synchronous mode for the session (the one late IRQ is a reported, known divergence); strict test mode aborts |
| GS→host download (`read_image`) | blocking drain (all jobs, all journal ops), then the exact single-thread download; donor caches and async downloads stay disabled |
| VU1 code/data memory access by EE | blocking drain, then direct access (writes keep `code_changed`) |
| VBlank: present, frame dump, `gs::vblank` flush | blocking drain before `on_vblank` presents; flush is a journal op in order |
| VU reset, VIF1 reset (FBRST RST) | record + placeholder |
| State save/load, shutdown, GT4_MTVU switch at runtime (not supported) | blocking drain; `Stop` record; join |

### 5.4 Why the result is exact

1. The worker receives the same VIF1 command sequence and EE-side VU1 memory effects
   in the same order as the single-thread path, and VU1 is deterministic and
   instant → identical VU1 state and identical PATH1/PATH2 bytes [H, by construction;
   checked by verify mode].
2. The journal concatenation equals the single-thread GS operation order: each
   VIF1 packet's outputs sit at the packet's EE program position, including
   post-stall output resumed by a later FBRST STC (its own job) [H].
3. GS front-end work has no guest-visible effect other than signals (prescanned,
   PATH1 invariant) and downloads (hard sync) [C: `gs.cpp:614-633`, `gs.cpp:682`].
4. Shadow VIF1 registers equal the worker's for every EE-readable register: the
   shadow runs the same `execute` code, `run_vu` updates TOPS/TOP/ITOP/DBF before
   returning (`vif.cpp:59-64`), mode-2 unpack keeps ROW [C]. Verify mode compares them.
5. Interrupt timing is unchanged because every IRQ source (VIF1 i-bit, DMA, GS CSR,
   VBlank) is still raised on the EE thread at the same guest point [C].

### 5.5 Overlay changes (new `scripts/mtvu_overlay.inc`, `--mtvu` in `prepare_perf_runtime.py`)

- `vif.cpp`: keep shadow routing; post with job ids; worker DIRECT → output ring.
- `vu_micro.cpp`: XGKICK hook → output ring on the worker (the kick sink keeps
  priority for `--vu1-verify`); per-unit `top/itop`; `reset` posting; the worker owns
  `g_chaining`, `g_kick_sink`, `vu1aot::g_counters`.
- `gif.cpp`/`gs.cpp`: route EE GIF/privileged ops through the journal (not through the
  donor `queue::active()` branches, which stay off); scanner gating; exact download
  entry point.
- `memory.cpp`: hard sync in the VU-region slow path.
- `system.cpp`: worker start/stop, `run_due_events` non-blocking drain, state sync.
- Waiting: `std::atomic::wait/notify` (C++20) with a short bounded spin
  (≤ ~2 µs) first; no `sleep_for`.
- Switch: `GT4_MTVU` unset or `1` = on; `0` = never start the worker, no shadow, the
  current code path. The launcher records and forwards it like `GT4_PACING`
  (`scripts/external_runtime_probe.py:87-88` scrubs only `PS2_`, `GT4_PERF_`,
  `GT4_VU1_`, `GT4_VU0_`); add `--mtvu on|off` to `play_performance.py`.
- Diagnostics: `GT4_MTVU_VERIFY=1` (shadow vs worker VIF1 registers at every hard
  sync; per-frame hash of the drained GS operation stream printed for comparison
  with `GT4_MTVU=0`); `GT4_MTVU_STRICT=1` aborts on any invariant violation.

### 5.6 Failure modes

| Failure | Guard |
|---|---|
| Deadlock (ring full both ways) | every EE wait drains output; worker never waits on the EE except output-ring space; pure fixture with tiny rings |
| Lost wakeup / ARM64 ordering bug | acquire/release on head/tail, seq_cst handshake on waiting flags; stress fixture; TSan build of pure fixtures on a Linux/Android clang host |
| Hidden EE observation point (native code reading GS or VU1 state) | audit list in step 2 (`race_display`, shape capture, perf counters such as `g_primitives`, screenshots, resize); debug assert that GS state is touched only from the drain or synced code |
| PATH1 signal | counter + session fallback + strict abort |
| Worker exception/halt | captured, rethrown on the EE at the next drain |
| Worker slower than the frame | EE waits at VBlank (visible as `mtvu_wait.vblank`); the frame is no slower than today minus EE overlap |
| Output copy cost | measured bytes/frame in F8 |

## 6. Steps, acceptance and effort

Each step is one candidate build in a fresh `private/sliceNNN-*` directory with new
pins; reference = slice166 (`--mode main`). Headless = 4,200 frames, the 12-press
schedule, frame dumps every 100 (42 dumps), SHA-256 per dump.

| # | Step | Acceptance | Effort |
|---|---|---|---|
| 0 | **Measure, no behavior change.** Counters per F7/F8 interval: jobs that would be posted, MSCAL, PATH1/2/3 packets and bytes, SIGNAL/FINISH/LABEL per path, downloads (`read_image` calls/bytes), EE VU1-memory accesses, VIF1 reads by register, ch1/ch2 starts, and a nested scope that splits `vu1_aot` into "VU1 program" vs "PATH1 GS front end". Also the overlap window: host µs between each ch1 kick and the next VBlank. | CTest/Python green; headless 42/42 identical to slice166; owner F8 in showroom, night race, rally. **Go/no-go:** PATH1 signals = 0 in all scenes; overlap window large enough to hide most VU1 time (else add the present-lag option in step 6). | 1-1.5 d |
| 1 | **Pure primitives:** `native/mtvu_ring.hpp` (SPSC byte ring, wrap, wait/notify), `native/mtvu_journal.hpp` (placeholders, blocking/non-blocking drain, wait-drains-output rule). | New CTest `native_mtvu_ring`, `native_mtvu_journal`: 10^5 random scripts with random worker delays produce the serial operation order; tiny-ring deadlock test; shutdown drain; no leaks. | 1.5-2 d |
| 2 | **Runtime overlay `--mtvu`** (section 5.5), off-path identical; audit of native observers. | CTest + Python with the new pins; headless MTVU on 42/42 identical to slice166 and to MTVU off; `GT4_MTVU_VERIFY=1` 0 register mismatches and identical per-frame GS-stream hashes on/off; `--vu1-verify 50` 0 mismatches on the worker; `--vu1-interpret-only` identical; `mtvu_path1_signal` = 0. | 3-5 d |
| 3 | **Per-thread accounting + F8** (section 4). | `perf_accounting`/`perf_probe` fixtures extended (two instances, wrong-thread still detected per instance); report lines parsed by Python. | 1-1.5 d |
| 4 | **Launcher, pins, docs:** `--mode mtvu`, `--mtvu on/off`, manifest, README/STATUS/journal, decision record. | Python pin tests; launcher records `GT4_MTVU`. | 0.5-1 d |
| 5 | **Owner A/B:** showroom, night race, rally, Nordschleife Supra TT 480p; `GT4_MTVU=0` vs on. | Median and p99 frametime equal or better in every scene; `vu1_aot` on the EE thread ~0; `mtvu_wait` small outside the night race; no crash, audio/input/saves unchanged. | owner time |
| 6 | **Weak-CPU check and options:** run the owner scenes with process affinity limited to 2 and 4 logical CPUs (SMT sibling worst case); if step 0 showed a short overlap window, evaluate an opt-in one-frame present lag (exact content, +1 frame latency). | No regression at 2 CPUs; documented gain at 4. | 1-2 d |
| 7 | **Later, separate task:** move the journal consumer (GS front end) to a render thread (GS-ASYNC step 4), only if F8 shows `gs_drain` on the critical path; coordinate with TASK-012. | — | not in TASK-011 |

Total: about 9-13 agent-days plus two owner sessions.

## 7. Risks

- **[U] PATH1 signals.** If GT4 sends FINISH/SIGNAL through XGKICK, exact MTVU
  needs a different structure (e.g. per-entry synchronous execution for the programs
  that emit them). Step 0 decides before any threading code.
- **[U] Overlap window.** If GT4 kicks most VIF1 work late in the frame, the
  VBlank sync leaves little to overlap; gain then needs the present-lag option.
- **[Hy] PATH1 GS front-end share.** Part of today's `vu1_aot` is GS front end
  that stays on the EE thread in this design; if it is large, step 7 matters more.
- **Night race downloads** force full syncs; its gain will be smaller (GS wait of
  4.1 ms is already on the EE).
- **x86 hides ordering bugs** that ARM64 exposes; fixtures must run under TSan on
  an ARM64 or Linux host before phase 2.
- **Merge conflicts** with TASK-012 in the GS entry points (`gs.cpp`, `gif.cpp`
  overlays) and with TASK-015 overlay anchors; keep anchors small and documented.
- **Memory:** 40 MiB of rings; fine on PC, size-tunable for mobile.
- **Debuggability:** worker-side crashes lose the EE stack; the job id in every log
  line links them.

## 8. Mobile (phase 2/3)

- ARM64 weak memory model: correct acquire/release is mandatory; no reliance on
  x86 TSO; test the pure rings with TSan and a stress run on device.
- Heterogeneous cores (e.g. Snapdragon 8 Gen 2: 1 prime, 4 performance, 3
  efficiency): put the heavier of EE and VU1 on the prime core (decided from the
  step 0/3 per-thread numbers); Android ADPF performance hints for both threads;
  iOS QoS user-interactive. Never let the worker land on an efficiency core.
- Prefer blocking waits over spinning (thermal and battery); keep the bounded spin
  short and configurable.
- Benefit is larger on mobile than on the 5700X: weaker cores make both the EE and
  VU1 sides longer, and their sum is what MTVU removes from the critical path [Hy].
- No JIT is involved: the worker runs the same AOT C++ VU1 programs (clang vector
  extensions compile to NEON), so iOS rules are unaffected.
