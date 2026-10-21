# Launcher file migrations

Status: 2026-10-20 (TASK-019 / TASK-043). Code: `launcher/crates/gt4l-core/src/migrate.rs`, used by `config.rs` (settings) and `versions.rs` (version.json).

## The rule

Every launcher-owned file carries a schema number. **Every future change to a persisted format adds a migration and a test.** A change that is not a migration is a bug.

Launcher-owned files:

| File | Where | Schema key | Schema today | Chain |
|---|---|---|---|---|
| `settings.toml` | `%LOCALAPPDATA%\GT4Recomp\config\` | `schema_version` | 3 | `migrate::SETTINGS` (first schema 0; schema 3 adds `widescreen`, TASK-079, retired again on 2026-10-21 by a load-time rule, see below) |
| `controls.toml` | `%LOCALAPPDATA%\GT4Recomp\` | `schema` | 1 (the file format is owned by `gt4controls`) | `migrate::CONTROLS` (first schema 1) |
| `version.json` | install folder, or data folder when read-only | `schema` | 1 | `migrate::VERSION_FILE` (first schema 1) |
| save library | `%LOCALAPPDATA%\GT4Recomp\library\` | none | a folder, not a single index file | not applicable yet; see below |
| `prepare.json` (setup receipt) | `%LOCALAPPDATA%\GT4Recomp\prepared\` | `schema_version` | 2 | no chain: setup rewrites it; an older receipt sends the player back to setup (see below) |

### The setup receipt (`prepare.json`)

The receipt is the commit point of setup (`gt4l-disc/src/setup.rs`). It is never migrated in place, because what it
describes must exist on disk:

- **Schema 2** (2026-10-20): adds `image` (`file`, `size_bytes`, `modified_unix_nanos`, `sha256` of the copy of the disc
  image in `%LOCALAPPDATA%\GT4Recomp\disc\`) and `qualified_dump` (the full image equals the qualified v2.00 dump).
  Every launch compares the copy's size and modified time with `image` (no hash); a difference means "Game files
  missing".
- **Schema 1** (earlier dev builds): no copy of the image; the game read the picked file. `check_installed` reports
  such a receipt as missing game files, so the setup page asks for the disc once and setup writes schema 2. The old
  prepared folder is replaced only when the new run commits; nothing else in the data folder changes.
- A damaged receipt is the same case (choose the disc again). A newer receipt (schema above 2) is also sent back to
  setup by a launcher that cannot read it; a downgrade therefore costs one copy of the disc, never user data.

Tests: `gt4l-disc/tests/setup_fake_iso.rs::later_launches_see_a_missing_or_changed_image_and_old_receipts`.

Memory card images (`*.ps2`, `*.psu`, `*.mcd`) are never migrated or rewritten by the launcher. The data-path guard (`guard.rs`) refuses writes to them.

The save library is a folder of save slots and replays, with no single index file. When an index file is added, it gets a schema key and a chain in the same change.

## How it runs

On start, before anything reads a file, the launcher runs the chain for that file (`config::load` for settings; `versions::parse` for version.json):

1. The schema number is read. A missing number is **schema 0** (settings from the earlier dev builds). A number above the chain's current schema is **newer**. A value that is not a whole number, or a number below the chain's first schema, is **corrupt**.
2. **Current**: nothing happens.
3. **Migrate**: the file is copied to `<name>.bak-schema<N>-<unix seconds>` in the same folder. Each step runs in order (`v0 -> v1 -> ... -> current`), the schema key is set to the current number, and the result is written back atomically (temporary file, then rename). The log says which copy was kept.
4. **Newer** (a downgrade, a file from a later launcher): the file is **not touched**. The launcher uses defaults in memory and warns in the log. It is never rewritten.
5. **Corrupt**: the damaged file is kept as a copy (`settings.toml.bad`, `version.json.bad`), and the launcher uses defaults and logs it. The player sees a message on Home when the file is settings.

A backup is written before the first write of a migrated file, so a bad migration never loses the original text.

## The example (schema 0 to 1)

`settings.toml` written by the earlier dev builds has no `schema_version` key. Schema 1 added the key and changed nothing else:

```rust
pub const SETTINGS: Chain = Chain {
    file: "settings.toml",
    key: "schema_version",
    first: 0,
    current: 1,
    steps: &[Step { from: 0, apply: settings_0_to_1 }],
};

fn settings_0_to_1(_document: &mut Value) -> Result<(), String> {
    Ok(())
}
```

Loading a schema 0 file gives: a backup `settings.toml.bak-schema0-<time>`, the same values, `schema_version = 1`, and a log line "settings migrated from schema 0 to 1". Tests: `migrate::tests::settings_schema_zero_gains_schema_one_and_keeps_its_values` and `tests/config_migration.rs::an_unversioned_dev_build_file_is_backed_up_and_migrated_to_schema_one`.

## Adding a change to a persisted format

1. Bump the chain's `current` by one and add a `Step { from: <old current>, apply: ... }` at the end of `steps`. The step changes the document (a `serde_json::Value` of the file), nothing else. Renames, new keys with defaults, and changed value meanings are all steps.
2. Keep the steps in order. `migrate::check_chain` refuses a gap or a wrong start; `every_real_chain_has_complete_ordered_steps` runs on every chain.
3. Add a test that loads a file of the previous schema and checks the result (values kept, the new key set, the backup written).
4. Add a test that a newer file is refused and not changed, if the change adds a new field a downgrade would drop.
5. Update the table above and this document's example if the example changes.
6. Mention the migration in the PR and in `docs/journal/`.

Never edit an old step after it has shipped. Add a new step.

## Schema 1 to 2: Fullscreen on by default (2026-10-21)

Schema 2 adds the `[choices]` table: a setting the user set on the Settings page is listed there as `name = true`
(`Settings::set_by_user`). Schema 1 files had the old windowed default (`fullscreen = "off"`) and no record of a choice,
so the step `settings_1_to_2` changes `fullscreen` to `"on"` unless `[choices] fullscreen = true` is present. The backup
`settings.toml.bak-schema1-<unix seconds>` is written first. A file that already says `"on"` only gains the schema number
and the backup. Tests: `migrate::tests::schema_one_fullscreen_off_becomes_on_unless_chosen`,
`config_migration.rs` (`schema_one_fullscreen_off_that_was_never_chosen...`, `a_fullscreen_value_the_user_chose...`).

## The save library moves to Documents (2026-10-21)

Not a schema step: a one-time move of files, in `gt4saves::migration`, run at every start until it has succeeded.

- **When:** the old AppData library (`library\memcard`, `library\saves`, `library\replays` or `replays`) holds at least
  one file, and `Documents\Gran Turismo 4\Saves` and `\Replays` hold none (empty folders made by an earlier start do
  not count).
- **Mapping:** `memcard\BASCUS-97328GAMEDATA` -> `Saves\Active\`, `memcard\.backup\` -> `Saves\.backup\`, a replay in
  `memcard\` -> `Replays\`, anything else in `memcard\` -> `Saves\Active\`, `saves\Save NN\` -> `Saves\Save NN\`, a
  replay folder -> `Replays\`. `library\backups` stays in AppData and is still used.
- **How:** every file is copied into staging folders beside the targets (`.Saves.migrating-<t>`), then compared with
  its source by size and SHA-256. Any failure removes the staging copy only and leaves the old library untouched (the
  next start tries again). Then the staging folders are renamed into place and the old folders are renamed into
  `<data>\library.migrated-<UTC timestamp>\` (`memcard`, `saves`, ...). Nothing old is deleted. A folder that cannot
  be renamed keeps its data and is listed in the marker.
- **Both hold files:** nothing is copied, merged or overwritten. The Documents library is used, the conflict is logged
  and shown on the Saves page, and the old folders stay as they are.
- **Marker:** `config\library-migration.json` (`schema` 1, `status` = `migrated`, `conflict` or `failed`, the time,
  folders, file and byte counts). It is not part of `settings.toml`. A `migrated` marker stops later runs.
- The owner's manual backup `parked-2026-10-21-100pct-save` is not a source and is never touched.
- Tests: `launcher/crates/gt4saves/tests/migration.rs`.

## Retired settings: Input resolution and Widescreen 16:9 (2026-10-21)

Not a schema step: a **load-time rule**, so `settings.toml` stays at schema 3 (TASK-080 takes schema 4 for its own
change). The owner fixed the game at 480p and 16:9 (until the ultrawide update), so the rows `input_resolution`
(480i/480p/1080i) and `widescreen` were removed from `settings::SETTINGS`, and the launcher always sends
`GT4_FORCE_480P=1` and `GT4_FORCE_WIDESCREEN=1` (no `GT4_RACE_DISPLAY`).

- **Load:** `config::parse` finds the key in `settings::RETIRED_SETTINGS`, logs "setting 'X' is no longer used (...);
  ignored" and keeps nothing. `[choices]` entries for them are ignored as for any unknown id.
- **Write:** `config::render` writes only the rows of `SETTINGS`, so the next save drops the old keys. No backup is
  needed: the dropped values have no effect on anything.
- **GS table values:** `[gs] aspect`, `osd_fps`, `osd_gpu`, `osd_resolution` are now `forced` rows of
  `docs/launcher/gs-settings.toml` (aspect 16:9, OSD off). `Settings::set_gs` refuses any other value ("X is fixed at D
  in this release"), so an old value is logged, ignored and dropped at the next save; the gt4-gs.ini always holds
  `AspectRatio = 16:9` and the three `OsdShow* = false` lines.
- **Downgrade:** an older 1.0.0 test build reading a file without the keys uses its own defaults (480p, Widescreen on).
- Tests: `tests/settings_config.rs::retired_and_forced_values_of_an_old_file_are_ignored_and_dropped_at_the_next_write`,
  `tests/launch_contract.rs::input_resolution_and_widescreen_are_retired_and_fixed`; a scratch run of the release
  launcher with such a file logged the five "ignored" lines (journal 2026-10-21).

The schema 2 to 3 step (`settings_2_to_3`, adds `widescreen = "on"`) is kept as shipped ("never edit an old step"):
its key is dropped by the rule above.

## What the migrations do not do

- They do not rewrite memory cards or save slots (the library move copies save folders byte for byte).
- They do not run for a file the launcher only reads and never writes (the backup is made before any write, so a read-only file is left as it is).
- They do not change `gt4controls` rules: the controls parser refuses other schemas, and the launcher keeps a file it cannot read (a copy is made before a reset).

## Tests

- `launcher/crates/gt4l-core/src/migrate.rs`: chain order, schema 0 to current, a current file untouched, a newer file untouched, corrupt schema values, failing steps, backup names, the real chains complete.
- `launcher/crates/gt4l-core/tests/config_migration.rs`: the file-level behaviour for settings (backup and migration, no second migration, newer untouched, corrupt file kept, skip choices round trip) and version.json (damaged and newer refused).
