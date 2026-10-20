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
| `js/input-rules.js` | Pure input rules: swipe step, right-stick scroll, slider value from a touch, face-button routing (`GT4.input`) |
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
| `about_info` | none | `{version, gt4recomp, gt4gs, disc_path}`; `gt4gs` is `not installed` until the renderer is there |
| `open_log_folder` | none | none |
| `check_updates` | none | `{offers: [offer], errors: [string], upToDate: bool, launcher, renderer}`; offer = `{stream, installed, latest, notes, package, update}` |
| `apply_update` | `{stream}` (`launcher` or `renderer`) | `{ok, message}`; the renderer installs in place (not while the game runs); the launcher starts its setup and exits |
| `skip_update` | `{stream, version}` | none; no offer again for exactly that version |
| `confirm_close` | none | none; the player chose Close launcher in the close confirmation |

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

Other Tauri events from Rust (listened to in `js/app.js`, popup code in `js/updates.js`):

| Event | Payload | Meaning |
|---|---|---|
| `update-available` | `{offers: [offer]}` | Sent at launch when a stream has a newer stable version that is not skipped. Shows the update popup. |
| `renderer-progress` | `{step}`: `listing`, `downloading`, `installing`, `downloading-launcher`, `done`, `failed` | Progress of a gt4-gs install (first install or update). |
| `update-finished` | `{stream, ok, message}` | The end of an `apply_update`. |
| `game-state` | `{running, message}` | The game ended (`running: false`); a non-empty message is shown and Home is opened. |
| `close-confirm` | `{message}` | The window close button was pressed while the game runs. The page asks "Close the launcher anyway?". |

The update popup: Update (runs `apply_update`), Not now (closes it; B does the same), Skip this version (runs `skip_update`). The release notes are shown as text (`#` headings, bullets and `**bold**` only).

Commands not in the brief that the UI needs: `cancel_capture`, `reset_binding`, `saves_*` and `replays_*` (names as listed), `open_log_folder`. The Rust side in `src/commands.rs` currently uses other names (`settings_rows`, `settings_set`, `controls_list`, `controls_capture_start`, `controls_bind_key`, `controls_reset`, `setup_*` and so on). The names above are the ones this UI calls.

## Navigation map

Focus moves spatially (nearest element in the pressed direction; sideways offset costs twice the distance).

| Page | A | B / Esc | X | Y | Start | LB/RB or Q/E | Left/right |
|---|---|---|---|---|---|---|---|
| Setup | primary button (Choose another file / Play) | none | none | none | none | none | none |
| Home | Play | none | none | Settings | Play | none | none |
| Settings: Graphics, Display | activate (dropdown opens its list) | Back to Home (closes a tip or a dialog first) | Save: confirms with a "Saved" toast (settings already apply on change) | none | none | previous / next tab | adjust slider or dropdown |
| Settings: Advanced | activate | Back to Home | Save (as Graphics) | Reset to defaults (confirm) | none | tabs | adjust |
| Settings: About | activate (log folder) | Back to Home | none | none | none | tabs | none |
| Saves | select / make active | Back | Import | Make active | none | tabs | none |
| Replays | none | Back | Import | Delete (confirm) | none | tabs | none |
| Controls | select the pad button | Back | Rebind | Reset binding | none | tabs | none |
| Capture overlay | none | cancel the rebind | none | none | none | none | none (navigation is suspended) |
| Dialog (list, confirm) | activate | close | none | none | none | none | none |

Right stick: scrolls the nearest scroll area of the focused element (or the page main area). Deadzone 0.2.
Footer hints (A Select, B Back, X Save where it applies, Y where defined, LB RB Tabs) are tappable by touch and mouse.

Routing of the face and shoulder buttons lives in `GT4.input.padRoute` (`js/input-rules.js`), tested in `dev/nav-test.html`.

## Touch

- Every target is at least 48 x 48 CSS px (the visuals stay small; the hit area is padded). `pointer: coarse` adds a little spacing.
- Nothing depends on hover. A help "i" opens on tap and closes on a second tap or a tap outside. Focus shows it for keys and pad only.
- Sliders drag with touch (pointer events with capture; `touch-action: pan-y`). Dropdowns open as a list.
- Long lists (settings, saves, replays) scroll with a finger and do not chain the scroll to the page.
- A horizontal swipe over 60 px on the Settings page (finger, mostly horizontal) switches tabs: left goes to the next tab, right to the previous one. Swipes that start on a slider or a dropdown list do not switch tabs.

## Play button

Idle: dark translucent glass with a thin white border and a slow light sweep (about every 3.4 s). Focused (pad or keys) and hovered: solid white, dark label, scale 1.03, soft glow. Pressed: a quick press-down. Under `prefers-reduced-motion` the sweep is removed and the idle state stays static. Other primary buttons (`.btn.primary`) use the same idle and focused language, without the sweep.

Tab order for LB/RB and Q/E: Graphics, Advanced, Display, Controls, Saves, Replays, About. The Mods tab and its rows are behind `GT4.features.mods` in `js/config.js` (off in release 1.1.0 builds; the mod manager ships in 1.1.0).

Keyboard: arrows move, Enter/Space activate (native), Esc = B, Q/E = LB/RB, Tab/Shift+Tab move in DOM order.
Mouse use hides the focus ring. A pad or key press brings it back.

## Mock data and browser preview

Serve `ui/` with any static server and open `dev/index.html`, for example
`python -m http.server 8765` in `launcher/crates/gt4l-tauri/ui`, then `http://127.0.0.1:8765/dev/index.html#/home`.

Query options: `?setup=done|checking|preparing|card|wrong|needed` (default `done`), `&running=1`, `&focus=play` (home only: focus the Play button, for screenshots).

`dev/nav-test.html` checks the spatial picker, the swipe, stick and slider rules and the face-button routing in a browser. It should report every case passed (55 of 55 at the time of writing).
