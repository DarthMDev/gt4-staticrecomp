# 0072 — Spec II is a second target on the Public Beta ISO, not a merge into v2.00

Date: 2026-10-22. Status: Accepted (owner, 2026-10-22). **Recorded, not scheduled.**
Identity study: [SPEC2-STUDY](../SPEC2-STUDY.md).

**Context.** The community asks for support of Gran Turismo 4 Spec II v1.10. Its image is a
SCUS-97436 build (GT4 Online Public Beta lineage: `SCUS_974.36`, `VER 1.00`, no `CORE.GT4`),
modified by a plugin injector. Two paths were on the table: bring Spec II content into the
supported v2.00 game, or support the SCUS-97436 image as its own target. The owner chose the
second one on 2026-10-22.

**Measurements of the merge path (read-only, 2026-10-22).** Spec II volume: 26,891 files,
3,435,528,192 stored bytes (4,460 MB decoded). Against v2.00: about 12,200 paths in common,
11,401 of them with the same size; 14,653 files exist only in Spec II (2,252 MB) and 6,105
only in v2.00 (1,422 MB).

- **Car audio is already the v2.00 audio.** 3,570 of the Spec II `carsound/` names match the
  v2.00 `sound/` tree; in the Public Beta every one of them is byte-identical (MD5) to v2.00.
  Spec II adds `exh0..3` (4,378 files, 262 MB) and new-car engine samples. The v2.00 executable
  contains no `exh`/`exhaust` reference, and its sound paths are `/sound/...`; the Spec II
  executable uses `carsound` and `/sound_gt/...`. Copying the files plays nothing.
- **Music moved, it is not new.** 176 of 177 files in `sound_gt/ads` are byte-identical to
  v2.00 `bgm/us`; only the index `ADS.INF` differs. That is most of the 840 MB "new" audio.
- **Menus and GT mode are code, not data.** 612 `.adc` in Spec II (127 new), 1,873 new `.gpb`,
  667 new files in `projects/gtmode` (311 MB) and 557 in `projects/arcade` (65 MB). v2.00 runs
  Adhoc bytecode v5; Spec II runs v7, loads projects by name (`projects/%s/%s`) and registers
  natives that v5 does not have.
- **The spec database has another name and another build.** v2.00 asks for
  `specdb/GT4_US2560`, Spec II for `specdb/GT4_PREMIUM_US2560` (both hard-coded strings).

**Decision.**
- **Spec II support means the SCUS-97436 Public Beta lineage ISO as a second target.** The user
  supplies the image; nothing is distributed. It is not a content merge into the v2.00 game.
- **The target is a separate build.** Its own input path (plain ELF, no `CORE.GT4`/Salsa
  reconstruction), its own pins, its own AOT EE library and its own executable are the default,
  because the generated units use one global symbol set (`gen::unit_XXXX`, `gen::kUnits`).
  Sharing one executable for both titles needs namespacing and is not assumed.
- **Not scheduled.** Phase 2 (CoreGS 2.0, release 2.0.0) stays the priority. When work starts it
  is its own task, branch and owner test, under the normal task lifecycle.

**Constraints carried by the future task (all measured in the study).**
- **The plugin injector patches game code at boot.** About 27 text addresses (stubs, prologue
  rewrites, physics words) and at least 5 `jal` redirections into the payload at `0x68A480`
  were found by a static scan. AOT code never reads those words, so the patch set must be
  applied to a build-time ELF derivative and retranslated (the existing `aspect` profile is the
  precedent). Patch variants selected by arguments must be resolved explicitly.
- **VU1 carries over.** The 0x3FF0-byte VU1 blob is byte-identical to v2.00, encrypted and
  decrypted (SHA-256 prefix `ae6b6cb3b4406ff3`), so the content-addressed AOT table applies as
  is. The blob lives at file offset `0x59C110` in this image.
- **VU0 does not.** The upload is 0x920 bytes against 0xA80 in v2.00, and 1,050 of 2,336 bytes
  differ: it needs its own table or the counted interpreter fallback.
- **No address parity.** 0 of 1,377 executable 4 KiB windows match v2.00 at the same address
  (41.6% of 16-byte blocks appear somewhere, 3.7% of 256-byte blocks). Retail decomp names,
  accepted entry seeds and every address-based hook do not transfer.
- **Retail-specific host hooks must be re-targeted or disabled**: the 16:9 HUD/option pointers,
  the race display 480p write, the license demo guard and the widescreen hold all write or read
  v2.00 addresses at VBlank. Writing them in this target would corrupt guest RAM.
- **Entry coverage has no decompilation.** The generic ledger sources (`fallthrough`, `dataword`,
  `luipair`) apply; the name table and the seeded entry list do not.
- **Product work.** The launcher disc rule (`gt4l-disc` refuses SCUS-97436 as
  `Unsupported::SpecII`), the prepared-folder layout (no CORE), a second executable in the
  installer, a separate save folder and the second 3.4 GB disc copy.
- **IOP.** The ISO is a superset of the v2.00 module set, adding `INET`, `NETCNF`, `DEV9`,
  `SMAP`, `RTINETB`, `INETCTL`, `PDINETCNF`, `PPP`, `PPPOE` and `LGAUD`, plus a `NET/` folder and
  `DNAS310.IMG`. System libraries are HLE; game modules are interpreted; no DEV9/SMAP hardware
  model exists. Offline behaviour is Unknown until tested.
- **Legal and credit.** The base is an unreleased 2006 Public Beta, not a retail disc. The
  distribution model of decision [0069](0069-own-hardware-renderer-clean-room.md) (the user
  supplies the disc, we ship the executable) applies unchanged; the Spec II authors get credit
  and should be asked for permission before any public release.

**Consequences.**
- The launcher keeps refusing SCUS-97436 with "This disc is not supported yet" until a target
  exists. The message stays accurate; no user-facing promise is added.
- `TASKS.md` records the decision under Ideas (not scheduled); `AGENTS.md` no longer calls Spec
  II a mere recorded idea.
- The asset-merge variant is closed by measurement, not by preference.
