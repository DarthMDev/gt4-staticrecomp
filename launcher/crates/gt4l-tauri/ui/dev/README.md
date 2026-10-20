# Launcher UI (Tauri 2 frontend)

Plain HTML, CSS and JavaScript. No framework, no Node build, no remote content.
Strict CSP (see `tauri.conf.json`): no inline scripts, no inline styles, no `eval`.
Scripts are classic (not modules) so the pages also open from `file://`.

## Files

| Path | Purpose |
|---|---|
| `index.html` | App shell (Tauri `frontendDist` is `ui/`) |
| `css/app.css` | Tokens from `docs/launcher/LAUNCHER-PLAN.md` section 12.2 |
| `js/spatial.js` | Pure spatial focus picker (`GT4.spatial.pickNext`) |
| `js/backend.js` | Command bridge: `window.__TAURI__.core.invoke`, or the dev mock |
| `js/ui.js` | Element builder, hint bar, toast, dialogs, date format |
| `js/nav.js` | Keyboard, Web Gamepad polling, Rust `gamepad` event, focus ring |
| `js/controller-svg.js` | Original DualShock 2 drawing (no Sony artwork) |
| `js/config.js` | Feature flags (`mods` off for release 1.0.0) |
| `js/app.js` | Router (`#/setup`, `#/home`, `#/settings/<tab>`) and boot |
| `js/pages/*.js` | Setup, Home, Settings shell and rows, Saves and Replays, Controls |
| `assets/` | Fonts (Roboto, Roboto Condensed), `cover-front.png`, `logo-gt4.png` |
| `dev/index.html` | Dev entry: same app with `dev/mock-data.js` (plain browser) |
| `dev/mock-data.js` | Mock command layer; sample values only, not game data |
| `dev/nav-test.html` | Unit test page for `spatial.js` (open in any browser) |
| `dev/MANUAL-CHECKLIST.md` | Manual checks for a controller and the Tauri build |

`ui/dev/` is for development. Exclude it from release bundles if the build does not need it.

## Commands the UI calls

Arguments are the payload object shown. Errors are strings or Error objects; the UI shows them as toasts.

| Command | Payload | Returns |
|---|---|---|
| `app_state` | none | `{setup: {state: "needed"\|"checking"\|"wrong_disc"\|"done", step: "checking"\|"preparing"\|"card", progress: 0..1, disc_name, problem}, game_running: bool}` |
| `setup_start` | none | none (called once when `state` is `needed`) |
| `setup_choose_disc` | none | none (file picker runs in Rust) |
| `setup_cancel` | none | none |
| `play` | none | none |
| `stop_game` | none | none |
| `get_settings_table` | none | `{max_upscale: int, rows: [row]}` |
| `set_setting` | `{id, value}` | any; the UI re-reads the table only when another row's visibility depends on `id` |
| `reset_gs_settings_to_defaults` | none | `{max_upscale, rows}` |
| `get_controls` | none | `{controls: [{id, name, binding}], triggers: [row]}` |
| `capture_binding` | `{control}` | `{control, binding}`; resolves on the next press |
| `cancel_capture` | none | none |
| `reset_binding` | `{control}` | none |
| `saves_list` | none | `{items: [{id, name, modified, active}]}` |
| `saves_import` | none | `{items}` or `null` when the picker is cancelled |
| `saves_activate` | `{id}` | `{items}` |
| `saves_delete` | `{id}` | `{items}` (the UI confirms first) |
| `replays_list` | none | `{items: [{id, name, modified}]}` |
| `replays_import` | none | `{items}` or `null` |
| `replays_delete` | `{id}` | `{items}` (the UI confirms first) |
| `about_info` | none | `{version, disc_path}` |
| `open_log_folder` | none | none |

`modified` is an ISO 8601 string; the UI formats it.

Settings row (`rows[]` in the table):

```
{ id, label, help, page: "basic"|"advanced"|"mods"|"hidden",
  group?: "graphics"|"display",          // splits page=basic into the Graphics and Display tabs
  control: "slider"|"toggle"|"dropdown",
  values, default, current, disabled?,
  visible_when?: {id, equals} }          // e.g. FSR sharpness only when scaling_filter == "fsr"
```

- slider values: `{min, max, step, unit?, limit?: "max_upscale"}`; the effective max is `min(max, table[limit])`.
- dropdown values: `[{value, label, disabled?, note?}]`; disabled entries are greyed and not selectable.
- toggle values: `null`; `current` is a bool.
- Controls triggers use the same row shape. Their ids are `l2_deadzone`, `l2_curve`, `r2_deadzone`, `r2_curve`, and they are written with `set_setting`.

Tauri event (optional fallback): `gamepad` with `{button, pressed: true}`, where `button` is one of `a b x y lb rb start up down left right`.

Commands not in the brief that the UI needs: `cancel_capture`, `reset_binding`, `saves_*` and `replays_*` (names as listed), `open_log_folder`. The Rust side in `src/commands.rs` currently uses other names (`settings_rows`, `settings_set`, `controls_list`, `controls_capture_start`, `controls_bind_key`, `controls_reset`, `setup_*` and so on). The names above are the ones this UI calls.

## Navigation map

Focus moves spatially (nearest element in the pressed direction; sideways offset costs twice the distance).

| Page | A | B / Esc | X | Y | Start | LB/RB or Q/E | Left/right |
|---|---|---|---|---|---|---|---|
| Setup | primary button (Choose another file / Play) | none | none | none | none | none | none |
| Home | Play | none | none | Settings | Play | none | none |
| Settings (any tab) | activate (dropdown opens its list) | Back to Home (closes a dialog first) | tab action (Advanced: reset) | none | none | previous / next tab | adjust slider or dropdown |
| Saves | select / make active | Back | Import | Make active | none | tabs | none |
| Replays | none | Back | Import | Delete (confirm) | none | tabs | none |
| Controls | select the pad button | Back | Rebind | Reset binding | none | tabs | none |
| Capture overlay | none | cancel the rebind | none | none | none | none | none (navigation is suspended) |
| Dialog (list, confirm) | activate | close | none | none | none | none | none |

Tab order for LB/RB and Q/E: Graphics, Advanced, Display, Controls, Saves, Replays, About. The Mods tab and its rows are behind `GT4.features.mods` in `js/config.js` (off in release 1.1.0 builds; the mod manager ships in 1.1.0).

Keyboard: arrows move, Enter/Space activate (native), Esc = B, Q/E = LB/RB, Tab/Shift+Tab move in DOM order.
Mouse use hides the focus ring. A pad or key press brings it back.

## Mock data and browser preview

Serve `ui/` with any static server and open `dev/index.html`, for example
`python -m http.server 8765` in `launcher/crates/gt4l-tauri/ui`, then `http://127.0.0.1:8765/dev/index.html#/home`.

Query options: `?setup=done|checking|preparing|card|wrong|needed` (default `done`), `&running=1`.

`dev/nav-test.html` checks the spatial picker in a browser. It should report 20 of 20 passed.
