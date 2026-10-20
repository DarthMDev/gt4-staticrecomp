# Manual checklist (launcher UI)

Run the dev page (`dev/index.html`) in a browser, then the Tauri build with the real Rust side.
Do not launch the game for these checks. Tick each item in the Tauri build before merging.

## Layout (1280 x 720, 1920 x 1080, 960 x 600 and 844 x 390 landscape)

- [ ] Setup, Home and every Settings tab fit without horizontal scroll at 960 x 600 and 844 x 390.
- [ ] At 844 x 390 nothing is clipped: the Settings nav scrolls (with a finger or the right stick) and the footer hints stay visible.
- [ ] The Play button is glass when idle, solid white with a glow when focused or hovered, and static under reduced motion.
- [ ] Settings rows: label on the left, control in one right-aligned column; sliders line up with dropdowns.
- [ ] Hover or focus on an "i" shows its help text; the card stays inside the window.
- [ ] Home: the cover fills the window, the logo sits bottom-left, Play sits under it, the gear is top-right.
- [ ] Footer hints show the right buttons per page. Home shows hints only after a pad or key is used.
- [ ] The home slideshow is unchanged: six slots, 72 s cycle, bottom-up fade.

## Keyboard

- [ ] Arrows move focus to the visually nearest control in that direction; no control is skipped.
- [ ] Left and right change a focused slider by one step and cycle a focused dropdown.
- [ ] Enter opens a dropdown list; Esc closes it; Esc on a page goes Home.
- [ ] Q and E switch Settings tabs in the order Graphics, Advanced, Display, Controls, Saves, Replays, About (no Mods in release 1.0.0).
- [ ] Tab and Shift+Tab move through controls in document order.
- [ ] The focus ring shows after a key press and disappears after a mouse click.

## Mouse

- [ ] Every control responds to a click. Sliders drag and commit on release.
- [ ] Clicking a footer hint runs its action.
- [ ] Dialogs close on Cancel and on Esc; Reset to defaults asks first.

## Touch (touchscreen or touch emulation)

- [ ] Every button, the "i" marks, toggles, dropdowns, tabs and footer hints respond to a tap, and each is easy to hit (48 px).
- [ ] Tap an "i": its help opens. Tap it again, or tap anywhere else, and it closes. Nothing needs a hover.
- [ ] Drag a slider with a finger: the value follows the finger and commits on release. A vertical drag on a slider scrolls the page.
- [ ] Tap a dropdown: its list opens; tap an entry to choose it; tap Cancel or outside to close.
- [ ] Long lists (Settings, Saves, Replays) scroll with a finger and stop at their ends (no scroll trap).
- [ ] Swipe left on Settings (over 60 px, mostly sideways) goes to the next tab; swipe right goes to the previous one. A short or diagonal drag does nothing.
- [ ] A swipe that starts on a slider or an open list does not change the tab.
- [ ] Tap a footer hint: A Select acts on the focused item, B Back goes back, LB RB Tabs switches tabs.

## Controller (XInput, WebView2)

- [ ] Settings has two levels. Opening it focuses the tab list on the current tab (Graphics by default); the content shows the highlighted tab as you move up and down.
- [ ] A, or D-pad right, on a tab enters its content on the first row, or on the row you last used in that tab (remembered for the session).
- [ ] In the content: up and down move between rows; left and right change a slider or dropdown; A opens a dropdown or flips a toggle.
- [ ] B in the content goes back to the tab list on the same tab, and nothing is reset. B on the tab list goes Home.
- [ ] LB and RB change tabs from either level; from the content the focus stays in the content on the new tab.
- [ ] After a dropdown or dialog closes, focus is back on the control that opened it.
- [ ] The tab list highlight is a soft pill that glides between tabs; the tab shown while you are in the content keeps a quiet accent edge.
- [ ] A focused row has a lighter band with a thin accent edge; the focused control has a soft glow. There is no hard box.
- [ ] With Windows "Show animations" off (or reduced motion), the pill and the glow change instantly.
- [ ] Footer: the tab list shows A Enter, B Back, LB RB Tabs; the content shows A Select, B Tabs, then the tab's own keys (Advanced: Y Reset; Saves and Replays: X Import; Controls: X Rebind, Y Reset). No tab shows Save.
- [ ] Mouse and touch: a click or tap focuses what was clicked; a click on a tab shows it without entering its rows.
- [ ] The D-pad and the left stick move focus; holding repeats after about 0.4 s, then about every 0.1 s.
- [ ] Stick deadzone: a slight drift does not move focus.
- [ ] A activates; B goes back (closes a dialog first); Start on Home plays.
- [ ] LB and RB switch Settings tabs; X and Y do the page action shown in the footer.
- [ ] While a rebind is waiting, only B cancels and the other buttons do nothing.
- [ ] Before the first button press the page still works with the keyboard and mouse.
- [ ] Every interactive element can be reached with the D-pad or the left stick (no control is skipped); the focus ring shows on each.
- [ ] Right stick up and down scrolls the focused scroll area (Settings content, Saves, Replays, dialogs) at a steady speed; a slight push does nothing.
- [ ] There is no Save: X does nothing on Graphics, Display and Advanced. X on Saves and Replays imports; X on Controls starts a rebind.
- [ ] Pad: the highlight moves over Controls, Saves and Replays and focus stays in the tab list; only A or D-pad right enters a tab. Entering Saves or Replays focuses the first row (Import only when the list is empty); entering Controls focuses L1.
- [ ] A changed value shows the new value at once (resolution dropdown, toggle, slider), and it still shows after leaving and re-entering the tab.
- [ ] Y on Advanced asks "Reset to defaults?"; Y on Saves makes the selected save active; Y on Replays deletes after a confirm; Y on Controls resets the selected binding.
- [ ] B closes an open "i" tip first, then an open dialog or list, then goes back.
- [ ] Left and right on a focused slider change it one step; on a dropdown they cycle the values.
- [ ] Start on Home plays; A on Home plays; Y on Home opens Settings.

## Settings behaviour

- [ ] Graphics: internal resolution goes up to the `max_upscale` the table reports, with the "Limited to ... on this GPU" note when lower.
- [ ] Changing the scaling filter to FSR shows FSR sharpness; changing CAS mode to CAS shows CAS sharpness.
- [ ] CoreGS 2.0 is greyed and not selectable.
- [ ] Advanced: "Reset to defaults" asks, then restores the values and says so in a toast.
- [ ] Mods is not in the nav column; LB/RB and Q/E skip it; `#/settings/mods` opens Graphics; no row has page "mods".
- [ ] An error from `set_setting` shows a red toast and the row shows the value the launcher reads back.

## Controls

- [ ] Every button in the drawing is clickable and focusable; the selected one has a white outline.
- [ ] The L1, L2, R1 and R2 tabs select the matching drawing; the panel shows the PS2 name and binding.
- [ ] Rebind: the overlay names the button; Cancel and Esc call `cancel_capture`; a press saves the new binding.
- [ ] Reset restores the default binding for the selected button only.
- [ ] L2 (from LT) and R2 (from RT) show deadzone and curve sliders.

## Saves and Replays

- [ ] Empty states show the text from the mockup ("No saves yet", "No replays yet").
- [ ] X imports; a cancelled picker changes nothing.
- [ ] Y or A on a save makes it the next-play save; the "Next play" tag moves.
- [ ] Delete asks first; the list refreshes from the returned items.

## Setup

- [ ] Checking shows a progress bar that moves with `progress`.
- [ ] Wrong disc shows the red message and "Choose another file", which opens the picker.
- [ ] Done shows Ready and Play; Play starts the game and opens Home.

## Known gaps (not yet built, from LAUNCHER-PLAN section 14)

- Export of saves and replays, the `.max`/`.cbs`/`.xps` importers, and the Replays "On card" choice.
- Repair game files and the open-source licences list on About.
- Background cross-fade: Home uses the single owner cover, with no rotation.
