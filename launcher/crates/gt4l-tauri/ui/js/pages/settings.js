/*
 * Settings shell (nav column + content) and the rows generated from
 * get_settings_table. The page never hard-codes an option: each row comes
 * from the table, with its own control, help text and page.
 *
 * Table shape (returned by get_settings_table as {max_upscale, rows}):
 *   row = {id, label, help, page: "basic"|"advanced"|"mods"|"hidden",
 *          group?: "graphics"|"display" (the Display or Graphics section of the Graphics tab), control: "slider"|"toggle"|"dropdown",
 *          values, default, current, disabled?, note? (a hint shown under the label), visible_when?: {id, equals}}
 *   slider values   = {min, max, step, unit?, limit?: "max_upscale", scale?: shown value = stored / scale,
 *                      labels?: {value: readout} (TASK-080 Render scale: "100% · 4x · 2560x1792")}
 *   dropdown values = [{value, label, disabled?, note?}]
 *   toggle values   = null (current is true/false)
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});
  var h = GT4.ui.h;

  // The tab names are keys (tab.<id>), read when the list is drawn, so a language change applies.
  var TABS = [
    { id: "general", key: "tab.general" },
    { id: "graphics", key: "tab.graphics" },
    { id: "controls", key: "tab.controls" },
    { id: "games", key: "tab.games" },
    { id: "saves", key: "tab.saves" },
    { id: "replays", key: "tab.replays" },
    { id: "mods", key: "tab.mods" },
    { id: "about", key: "tab.about" }
  ];
  GT4.TABS = TABS;
  GT4.tabPages = GT4.tabPages || {};

  // Tabs that are on in this build (config.js). Used by the nav column,
  // LB/RB and Q/E cycling, and route checks, so a hidden tab is unreachable.
  function visibleTabs() {
    return TABS.filter(function (tab) {
      return tab.id !== "mods" || GT4.features.mods;
    });
  }
  GT4.visibleTabs = visibleTabs;

  function call(command, args) {
    return GT4.backend.call(command, args);
  }

  function errorText(err) {
    if (typeof err === "string") {
      return err;
    }
    return (err && err.message) || String(err);
  }
  GT4.errorText = errorText;

  var model = GT4.settingsModel;

  // Mod and texture rows never show in this build (rowVisible in js/settings-model.js).
  function rowVisible(table, row) {
    return model.rowVisible(table, row, !!GT4.features.mods);
  }

  // ctx = {table, content, refresh(focusId)}
  // A successful write updates the row model, then the table is read again: other rows may depend on this one
  // (visible_when, disabled), so the whole tab is drawn from the new table, with focus kept on the same row.
  function commit(ctx, id, value) {
    return call("set_setting", { id: id, value: value }).then(
      function () {
        if (id === "language") {
          // TASK-102: the whole launcher changes language at once (app.js re-draws the page).
          GT4.i18n.setSetting(value);
          return;
        }
        model.applyChange(ctx.table, id, value);
        ctx.refresh(id);
      },
      function (err) {
        GT4.ui.toast(errorText(err), true);
        ctx.refresh(id);
      }
    );
  }

  // Row texts (TASK-102): setting.<id>.label / .help and choice.<id>.<value> from the locale files; the table's text
  // (Rust) is the fallback. Trigger and stick rows share one help text each.
  function helpKeyOf(row) {
    if (/^[lr]2_deadzone$/.test(row.id)) {
      return "setting.trigger_deadzone.help";
    }
    if (/^[lr]2_curve$/.test(row.id)) {
      return "setting.trigger_curve.help";
    }
    if (/stick_deadzone$/.test(row.id)) {
      return "setting.stick_deadzone.help";
    }
    return "setting." + row.id + ".help";
  }

  function languageParams() {
    return { language: GT4.i18n.name(GT4.i18n.detected()) };
  }

  function rowLabel(row) {
    var key = "setting." + row.id + ".label";
    return GT4.i18n.has(key) ? GT4.t(key) : row.label;
  }

  function rowHelp(row) {
    var key = helpKeyOf(row);
    return GT4.i18n.has(key) ? GT4.t(key) : row.help;
  }

  function choiceLabel(row, option) {
    var keys = ["choice." + row.id + "." + option.value];
    if (/_curve$/.test(row.id)) {
      keys.push("choice.curve." + option.value);
    }
    for (var i = 0; i < keys.length; i++) {
      if (GT4.i18n.has(keys[i])) {
        return GT4.t(keys[i], languageParams());
      }
    }
    return option.label;
  }

  function sliderSpec(row, table) {
    var spec = row.values;
    var max = spec.max;
    if (spec.limit && typeof table[spec.limit] === "number") {
      max = Math.min(max, table[spec.limit]);
    }
    return { min: spec.min, max: max, step: spec.step || 1, unit: spec.unit || "", fullMax: spec.max,
             scale: spec.scale || 1, labels: spec.labels || null };
  }

  function sliderControl(ctx, row) {
    var spec = sliderSpec(row, ctx.table);
    // scale (TASK-080 custom aspect ratio): the stored whole number is shown divided by it, with two decimals.
    var format = function (value) {
      // labels (TASK-080 Render scale): the table's readout for this step, e.g. "100% · 4x · 2560x1792".
      if (spec.labels && Object.prototype.hasOwnProperty.call(spec.labels, String(Number(value)))) {
        return spec.labels[String(Number(value))];
      }
      if (spec.scale !== 1) {
        return (Number(value) / spec.scale).toFixed(2) + spec.unit;
      }
      return String(value) + spec.unit;
    };
    var input = h("input", {
      type: "range",
      class: "slider",
      min: spec.min,
      max: spec.max,
      step: spec.step,
      value: row.current,
      "aria-label": rowLabel(row),
      "aria-valuetext": format(row.current),
      disabled: !!row.disabled,
      data: { setting: row.id, adjust: "1" }
    });
    var readout = h("span", { class: "val", text: format(row.current) });
    var paint = function () {
      var span = spec.max - spec.min || 1;
      var pct = ((Number(input.value) - spec.min) / span) * 100;
      input.style.setProperty("--fill", pct + "%");
      readout.textContent = format(input.value);
      input.setAttribute("aria-valuetext", format(input.value));
    };
    input.addEventListener("input", paint);
    input.addEventListener("change", function () {
      commit(ctx, row.id, Number(input.value));
    });
    // Touch and pen: drag with pointer capture. The thumb is 14 px, so the track is inset by 7 px on each side.
    var touchDrag = false;
    function valueAt(clientX) {
      var box = input.getBoundingClientRect();
      return GT4.input.valueFromPointer(clientX, box.left + 7, box.width - 14, spec.min, spec.max, spec.step);
    }
    input.addEventListener("pointerdown", function (event) {
      if (event.pointerType === "mouse" || input.disabled) {
        return;
      }
      touchDrag = true;
      input.setPointerCapture(event.pointerId);
      input.value = String(valueAt(event.clientX));
      paint();
      event.preventDefault();
    });
    input.addEventListener("pointermove", function (event) {
      if (!touchDrag) {
        return;
      }
      input.value = String(valueAt(event.clientX));
      paint();
    });
    function endTouchDrag() {
      if (!touchDrag) {
        return;
      }
      touchDrag = false;
      commit(ctx, row.id, Number(input.value));
    }
    input.addEventListener("pointerup", endTouchDrag);
    input.addEventListener("pointercancel", endTouchDrag);
    input.addEventListener("gt4-adjust", function (event) {
      var next = Math.min(spec.max, Math.max(spec.min, Number(input.value) + event.detail.delta * spec.step));
      if (next === Number(input.value)) {
        return;
      }
      input.value = String(next);
      paint();
      commit(ctx, row.id, next);
    });
    paint();
    var nodes = [input, readout];
    return { node: nodes, limited: spec.max < spec.fullMax ? spec.max + spec.unit : null, wide: !!spec.labels };
  }

  function toggleControl(ctx, row) {
    var button = h("button", {
      class: "toggle" + (row.current ? " on" : ""),
      role: "switch",
      "aria-checked": String(!!row.current),
      "aria-label": rowLabel(row),
      disabled: !!row.disabled,
      data: { setting: row.id },
      on: {
        click: function () {
          var next = !button.classList.contains("on");
          button.classList.toggle("on", next);
          button.setAttribute("aria-checked", String(next));
          commit(ctx, row.id, next);
        }
      }
    });
    return { node: [button] };
  }

  function enabledOptions(options) {
    return options.filter(function (option) {
      return !option.disabled;
    });
  }

  function dropdownControl(ctx, row) {
    var options = row.values || [];
    var currentText = String(row.current);
    options.forEach(function (option) {
      if (String(option.value) === String(row.current)) {
        currentText = choiceLabel(row, option);
      }
    });
    var button = h("button", {
      class: "select",
      type: "button",
      "aria-haspopup": "listbox",
      "aria-label": rowLabel(row),
      disabled: !!row.disabled,
      data: { setting: row.id, adjust: "1" }
    }, [h("span", { text: currentText }), h("span", { class: "chev" })]);
    var label = button.querySelector("span");

    // A chosen value shows its own label at once; the table is read again after the write.
    function choose(value, text) {
      row.current = value;
      currentText = text;
      label.textContent = text;
      commit(ctx, row.id, value);
    }

    // Left/right cycle through enabled values without opening the list.
    button.addEventListener("gt4-adjust", function (event) {
      var enabled = enabledOptions(options);
      if (!enabled.length) {
        return;
      }
      var index = enabled.findIndex(function (option) {
        return String(option.value) === String(row.current);
      });
      var next = enabled[(index + event.detail.delta + enabled.length) % enabled.length];
      row.current = next.value;
      currentText = choiceLabel(row, next);
      label.textContent = currentText;
      commit(ctx, row.id, next.value);
    });

    // A (click) opens the full list, so greyed options and their notes are visible.
    button.addEventListener("click", function () {
      var dialog = null;
      var list = h("div", { class: "option-list", role: "listbox" }, options.map(function (option) {
        var selected = String(option.value) === String(row.current);
        return h("button", {
          class: "option",
          role: "option",
          "aria-selected": String(selected),
          disabled: !!option.disabled,
          data: selected ? { default: "1" } : {},
          on: {
            click: function () {
              dialog.close();
              choose(option.value, option.label);
            }
          }
        }, [
          h("span", { text: choiceLabel(row, option) }),
          option.disabled
            ? h("span", { class: "tag", text: option.note || GT4.t("settings.coming_soon") })
            : (selected ? h("span", { class: "tick", text: GT4.t("settings.current") }) : null)
        ]);
      }));
      dialog = GT4.ui.openDialog({
        title: rowLabel(row),
        body: list,
        buttons: [{ label: GT4.t("common.cancel") }]
      });
    });
    return { node: [button] };
  }

  function infoButton(row) {
    var tipId = "tip-" + row.id;
    var button = h("button", {
      class: "info",
      type: "button",
      "aria-label": GT4.t("settings.about_row", { label: rowLabel(row) }),
      "aria-expanded": "false",
      "aria-describedby": tipId,
      on: {
        click: function () {
          GT4.ui.toggleTip(button);
        }
      }
    }, [
      "i",
      h("span", { class: "tip", id: tipId, role: "tooltip" }, [
        h("b", { text: rowLabel(row) }),
        rowHelp(row) || ""
      ])
    ]);
    return button;
  }

  function rowNode(ctx, row) {
    var control;
    if (row.control === "slider") {
      control = sliderControl(ctx, row);
    } else if (row.control === "toggle") {
      control = toggleControl(ctx, row);
    } else if (row.control === "dropdown") {
      control = dropdownControl(ctx, row);
    } else {
      return null;
    }
    var keyNodes = [h("span", { text: rowLabel(row) }), infoButton(row)];
    if (control.limited) {
      keyNodes.push(h("span", { class: "hint-text", text: GT4.t("settings.limited", { value: control.limited }) }));
    }
    if (row.note) {
      // A row that is shown but not available now (the launcher sends `note` only for such a row).
      keyNodes.push(h("span", { class: "hint-text", text: row.note }));
    }
    return h("div", { class: "row" + (row.disabled ? " is-disabled" : ""), data: { row: row.id } }, [
      h("div", { class: "k" }, keyNodes),
      h("div", { class: "ctl" + (control.wide ? " wide" : "") }, control.node)
    ]);
  }

  // Renders rows into parent (the tab's content unless given).
  function renderRows(ctx, rows, parent) {
    var into = parent || ctx.content;
    rows.forEach(function (row) {
      if (!rowVisible(ctx.table, row)) {
        return;
      }
      var node = rowNode(ctx, row);
      if (node) {
        into.appendChild(node);
      }
    });
  }

  // Focus back on the control of focusId after the tab was drawn again (anywhere in the tab, sections included).
  function restoreFocus(ctx, focusId) {
    if (!focusId) {
      return;
    }
    var target = ctx.content.querySelector("[data-setting=\"" + focusId + "\"]");
    if (target) {
      target.focus({ preventScroll: true });
    }
  }

  // Generic settings tab: loads the table, filters rows, renders them.
  // spec = {title, note, filter(row, table), extra(ctx), hooks(ctx)}
  function settingsTab(content, host, spec) {
    var ctx = { content: content, table: null, refresh: null };

    function paint(focusId) {
      GT4.ui.clear(content);
      content.appendChild(h("h1", { text: spec.title }));
      content.appendChild(h("p", { class: "note", text: spec.note || GT4.t("settings.note_default") }));
      var rows = ctx.table.rows.filter(function (row) {
        return spec.filter(row, ctx.table);
      });
      renderRows(ctx, rows);
      if (spec.extra) {
        spec.extra(ctx, host);
      }
      restoreFocus(ctx, focusId);
    }

    var reads = model.readGate();
    ctx.refresh = function (focusId) {
      var ticket = reads.ticket();
      return call("get_settings_table").then(function (table) {
        if (!reads.isLatest(ticket)) {
          return;
        }
        ctx.table = table;
        // Focus moves back to the row only while it is still inside this tab (the user may have moved to the tab list).
        paint(content.contains(document.activeElement) ? focusId : undefined);
      }, function (err) {
        GT4.ui.toast(errorText(err), true);
      });
    };

    content.appendChild(h("p", { class: "loading", text: GT4.t("settings.loading") }));
    return ctx.refresh().then(function () {
      if (spec.hooks) {
        spec.hooks(ctx, host);
      }
    });
  }
  GT4.settingsTab = settingsTab;
  GT4.settingsRows = { rowNode: rowNode };

  // General (TASK-102): the launcher's language. The rows come from the table (group "general").
  GT4.tabPages.general = function (content, host) {
    return settingsTab(content, host, {
      title: GT4.t("settings.general_title"),
      filter: function (row) {
        return row.group === "general";
      }
    });
  };

  // Graphics: three sections in one tab (owner, 2026-10-21; there is no Display tab any more):
  //   Display   page=basic rows in the display group (Monitor, Resolution, Fullscreen, VSync);
  //   Graphics  page=basic rows in the graphics group (Renderer, Render scale, FXAA, CAS);
  //   Advanced  page=advanced rows and the reset-to-defaults action (Y on the pad).
  // The D-pad moves down through all three sections, and B returns to the tab list from anywhere in the tab.
  GT4.tabPages.graphics = function (content, host) {
    function resetDefaults(ctx) {
      GT4.ui.openDialog({
        title: GT4.t("settings.reset_title"),
        text: GT4.t("settings.reset_text"),
        buttons: [
          { label: GT4.t("common.cancel") },
          {
            label: GT4.t("common.reset"),
            primary: true,
            danger: true,
            run: function () {
              call("reset_gs_settings_to_defaults").then(function (table) {
                ctx.table = table;
                ctx.refresh();
                GT4.ui.toast(GT4.t("settings.reset_done"));
              }, function (err) {
                GT4.ui.toast(errorText(err), true);
              });
            }
          }
        ]
      });
    }

    // One titled section; a section whose rows are all hidden is left out (except Advanced, which holds the reset).
    function section(ctx, id, title, rows, always) {
      var visible = rows.filter(function (row) {
        return rowVisible(ctx.table, row);
      });
      if (!visible.length && !always) {
        return null;
      }
      var node = h("section", { class: "settings-section", "aria-labelledby": "section-" + id, data: { section: id } }, [
        h("h2", { class: "section-head", id: "section-" + id, text: title })
      ]);
      renderRows(ctx, visible, node);
      content.appendChild(node);
      return node;
    }

    return settingsTab(content, host, {
      title: GT4.t("tab.graphics"),
      // All rows are drawn by the sections below.
      filter: function () {
        return false;
      },
      extra: function (ctx) {
        var basic = ctx.table.rows.filter(function (row) {
          return row.page === "basic";
        });
        section(ctx, "display", GT4.t("settings.section_display"), basic.filter(function (row) {
          return row.group === "display";
        }));
        var graphics = section(ctx, "graphics", GT4.t("settings.section_graphics"), basic.filter(function (row) {
          return (row.group || "graphics") === "graphics";
        }));
        // TASK-088: opens <install>\data\Textures (TASK-091; made first, TASK-093: no replacements subfolder). Next to the texture options.
        if (graphics) {
          // Inline progress of a switch to GSdx (renderer-progress events; app.js sets the text).
          var rendererStatus = h("p", { class: "note renderer-status", role: "status", data: { action: "renderer-status" }, text: "" });
          GT4.setRendererStatus = function (text) { rendererStatus.textContent = text || ""; };
          graphics.appendChild(rendererStatus);
          var textures = h("button", {
            class: "btn",
            type: "button",
            data: { action: "open-textures-folder" },
            on: { click: function () { call("open_textures_folder").catch(function (err) { GT4.ui.toast(errorText(err), true); }); } }
          }, GT4.t("settings.open_textures"));
          graphics.appendChild(h("div", { class: "advanced-foot" }, [textures]));
        }
        var advanced = section(ctx, "advanced", GT4.t("settings.section_advanced"), ctx.table.rows.filter(function (row) {
          return row.page === "advanced";
        }), true);
        var button = h("button", {
          class: "btn danger",
          type: "button",
          data: { action: "reset-defaults" },
          on: { click: function () { resetDefaults(ctx); } }
        }, GT4.t("settings.reset_defaults"));
        advanced.appendChild(h("div", { class: "advanced-foot" }, [button]));
      },
      hooks: function (ctx, tabHost) {
        tabHost.setHooks({ y: function () { resetDefaults(ctx); } });
        tabHost.hints([
          { key: "Y", label: GT4.t("common.reset"), run: function () { resetDefaults(ctx); } }
        ]);
      }
    });
  };

  // Mods: placeholder until TASK-048.
  // Registered only when the feature flag is on (release 1.1.0).
  if (GT4.features.mods) {
    GT4.tabPages.mods = function (content) {
      content.appendChild(h("h1", { text: GT4.t("tab.mods") }));
      content.appendChild(h("div", { class: "placeholder" }, [
        h("h2", { text: GT4.t("settings.mods_title") }),
        h("p", { text: GT4.t("settings.mods_planned") })
      ]));
      return Promise.resolve();
    };
  }

  // About: version, notice, disc path, log folder.
  GT4.tabPages.about = function (content) {
    return call("about_info").then(function (info) {
      content.appendChild(h("h1", { text: GT4.t("tab.about") }));
      content.appendChild(h("p", { class: "note", text: "GT4Recomp " + (info.gt4recomp || info.version || "") }));
      content.appendChild(h("p", { class: "note", text: GT4.t("about.gt4gs", { version: info.gt4gs || GT4.t("common.not_installed") }) }));
      content.appendChild(h("p", { class: "note", text: GT4.t("about.build", { build: info.build || "Installer" }) }));
      var checkButton = h("button", {
        class: "btn small",
        type: "button",
        on: {
          click: function () {
            GT4.updates.checkNow();
          }
        }
      }, GT4.t("about.check_updates"));
      content.appendChild(h("div", { class: "row" }, [
        h("div", { class: "k", text: GT4.t("about.updates") }),
        h("div", { class: "ctl" }, [checkButton])
      ]));
      content.appendChild(h("div", { class: "about-notice" }, [
        h("p", { text: GT4.t("about.disclaimer") }),
        h("p", { text: GT4.t("about.trademarks") })
      ]));
      // The people behind Gran Turismo 4 Spec II: one localized line and the full credit list (owner 2026-10-23).
      var spec2Credit = GT4.t("about.spec2_credit");
      var creditText = GT4.t("rust.about.credits") || info.credits;
      var creditLines = [];
      if (spec2Credit) creditLines.push(h("p", { text: spec2Credit }));
      if (creditText) creditLines.push(h("p", { class: "note", text: creditText }));
      if (creditLines.length) {
        content.appendChild(h("div", { class: "about-notice about-credits" }, creditLines));
      }
      content.appendChild(h("div", { class: "group", text: GT4.t("about.more") }));
      // The game runs from GT4Recomp's own copy of the disc image (the file chosen at setup is not needed any more,
      // so its path is not shown; it is in prepare.json and the log for support).
      var openGameFiles = h("button", {
        class: "btn small",
        type: "button",
        on: {
          click: function () {
            call("open_game_files_folder").catch(function (err) {
              GT4.ui.toast(errorText(err), true);
            });
          }
        }
      }, GT4.t("common.open_folder"));
      content.appendChild(h("div", { class: "row" }, [
        h("div", { class: "k" }, [
          h("span", { text: info.game_name ? GT4.t("about.game_files_of", { game: info.game_name }) : GT4.t("about.game_files") }),
          h("span", { class: "val", text: info.game_files || "" })
        ]),
        h("div", { class: "ctl" }, [openGameFiles])
      ]));
      // The player's settings, saves and replays live in %APPDATA%\GT4Recomp (kept by the uninstaller).
      var openSaves = h("button", {
        class: "btn small",
        type: "button",
        on: {
          click: function () {
            call("open_player_folder").catch(function (err) {
              GT4.ui.toast(errorText(err), true);
            });
          }
        }
      }, GT4.t("common.open_folder"));
      content.appendChild(h("div", { class: "row" }, [
        h("div", { class: "k" }, [
          h("span", { text: GT4.t("about.saves_replays") }),
          h("span", { class: "val", text: info.saves_folder || "" })
        ]),
        h("div", { class: "ctl" }, [openSaves])
      ]));
      var openLog = h("button", {
        class: "btn small",
        type: "button",
        on: {
          click: function () {
            call("open_log_folder").catch(function (err) {
              GT4.ui.toast(errorText(err), true);
            });
          }
        }
      }, GT4.t("about.open_log"));
      content.appendChild(h("div", { class: "row" }, [
        h("div", { class: "k", text: GT4.t("about.logs") }),
        h("div", { class: "ctl" }, [openLog])
      ]));
    }, function (err) {
      content.appendChild(h("p", { class: "loading", text: errorText(err) }));
    });
  };

  // The settings shell: a tab list and the tab's rows, with the two focus levels of js/focus-levels.js.
  //   side     focus on the tab list; the content shows the highlighted tab (preview).
  //   content  focus inside the rows; B returns to the tab list on the same tab.
  // Switching tabs re-renders only the content, so focus never resets. The hash follows the shown tab.
  var FL = GT4.focusLevels;
  var focusState = null; // kept while the app runs: the row last focused in each tab (session memory)

  GT4.pages = GT4.pages || {};
  GT4.pages.settings = {
    mount: function (root, params) {
      var tabs = visibleTabs();
      var tabIds = tabs.map(function (tab) {
        return tab.id;
      });
      var requested = params.tab || "graphics";
      // Retired tabs live on as sections of Graphics (Display and Advanced, owner 2026-10-21).
      if (requested === "display" || requested === "advanced") {
        requested = "graphics";
      }
      if (tabIds.indexOf(requested) < 0) {
        // Unknown or hidden tab (for example #/settings/mods in this build): fall back.
        requested = tabIds[0];
      }
      var state = focusState ? FL.open(focusState, requested) : FL.start(requested);
      var shown = null;
      var tabHints = [];
      var tabHintsReplace = false; // the tab gives the whole content-level footer (Controls)
      var entryPick = null; // the page's choice of the row A enters (null: the remembered row, else the first)
      var previewing = false; // true while a tab's content is being rendered: that never moves focus
      var ready = Promise.resolve();

      var content = h("div", { class: "content" });
      var pill = h("span", { class: "side-pill", "aria-hidden": "true" });
      var version = h("div", { class: "side-version", text: "" });
      var side = h("nav", { class: "side", "aria-label": GT4.t("common.settings") }, [
        h("button", {
          class: "back",
          type: "button",
          on: { click: function () { GT4.go("#/home"); } }
        }, [h("span", { "aria-hidden": "true", text: "‹" }), GT4.t("common.back")]),
        h("img", { class: "side-logo", src: "assets/logo-gt4-white.png", alt: "Gran Turismo 4" }),
        pill,
        tabs.map(function (tab) {
          return h("button", {
            class: "tab",
            type: "button",
            data: { tab: tab.id },
            on: { click: onTabClick }
          }, GT4.t(tab.key));
        }),
        version
      ]);
      var stage = h("div", { class: "stage settings" }, [side, content]);
      root.appendChild(stage);

      function sideButton(id) {
        return side.querySelector("[data-tab=\"" + id + "\"]");
      }

      function rowKeyOf(el) {
        var row = el && el.closest ? el.closest("[data-row]") : null;
        return row ? row.getAttribute("data-row") : null;
      }

      function setState(next) {
        state = next;
        focusState = next;
        stage.classList.toggle("is-side-level", state.level === FL.SIDE);
        stage.classList.toggle("is-content-level", state.level === FL.CONTENT);
        placePill();
        renderFooter();
      }

      // The highlight pill sits on the focused item of the tab list (CSS animates its movement).
      function placePill() {
        var focused = side.contains(document.activeElement) ? document.activeElement : sideButton(state.tab);
        if (!focused || focused.offsetParent === null) {
          return;
        }
        pill.style.top = focused.offsetTop + "px";
        pill.style.height = focused.offsetHeight + "px";
      }

      function markSide() {
        side.querySelectorAll("[data-tab]").forEach(function (button) {
          if (button.getAttribute("data-tab") === shown) {
            button.setAttribute("aria-current", "page");
          } else {
            button.removeAttribute("aria-current");
          }
        });
      }

      // Footer: the level's keys, then (content level) the tab's own keys such as Y Reset or X Rebind.
      function renderFooter() {
        var runs = {
          Enter: enterContent,
          Select: function () { GT4.nav.activate(); },
          Back: function () { GT4.back(); },
          Tabs: function () { if (state.level === FL.CONTENT) { leaveContent(); } else { stepTab(1); } }
        };
        var list = FL.footer(state.level).map(function (item) {
          var run = item.key === "LB RB" ? function () { stepTab(1); } : runs[item.label];
          return { key: item.key, label: item.label, run: run };
        });
        if (state.level === FL.CONTENT) {
          list = tabHintsReplace ? tabHints.slice() : list.concat(tabHints);
        }
        GT4.ui.setHints(list);
      }

      function showTab(id) {
        shown = id;
        var fresh = h("div", { class: "content" });
        stage.replaceChild(fresh, content);
        content = fresh;
        // Focus inside the rows: the level becomes content (a mouse click lands here too) and the row is remembered.
        fresh.addEventListener("focusin", function (event) {
          var next = FL.onContentFocus(state, previewing);
          if (next !== state) {
            setState(next);
          }
          if (previewing) {
            return;
          }
          var key = rowKeyOf(event.target);
          if (key) {
            setState(FL.remember(state, key));
          }
        });
        // Nothing from the previous tab is kept; the page sets its own keys and hooks while it mounts.
        tabHints = [];
        tabHintsReplace = false;
        entryPick = null;
        delete GT4.hooks.x;
        delete GT4.hooks.y;
        delete GT4.hooks.start;
        markSide();
        // The hash follows the shown tab without a route change (replaceState fires no hashchange).
        try {
          window.history.replaceState(null, "", "#/settings/" + id);
        } catch (err) {
          // Some hosts refuse history changes; the tab still shows.
        }
        var host = {
          setHooks: function (partial) {
            Object.keys(partial).forEach(function (key) {
              GT4.hooks[key] = partial[key];
            });
          },
          // The row A enters on this tab: a function that returns the element to focus (null: default).
          entry: function (pick) {
            entryPick = pick;
          },
          // The tab's own footer keys (shown on the content level); the level keys are added by renderFooter unless
          // options.replace is set, when the tab's list is the whole footer of the content level.
          hints: function (extra, options) {
            tabHints = extra || [];
            tabHintsReplace = !!(options && options.replace);
            renderFooter();
          }
        };
        previewing = true;
        var mounted;
        try {
          mounted = GT4.tabPages[id] ? GT4.tabPages[id](fresh, host) : null;
        } finally {
          previewing = false;
        }
        renderFooter();
        ready = Promise.resolve(mounted).catch(function (err) {
          GT4.ui.toast(errorText(err), true);
        });
        return ready;
      }

      // A, or D-pad right, on the highlighted tab: focus goes into its rows (the one last used, else the first).
      function enterContent() {
        if (state.level === FL.CONTENT) {
          return;
        }
        setState(FL.enter(state));
        ready.then(focusRow);
      }

      // B in the rows: the tab list, on the same tab. Nothing is reset.
      function leaveContent() {
        setState(FL.back(state));
        var button = sideButton(state.tab);
        if (button) {
          button.focus({ preventScroll: true });
        }
      }

      function focusRow() {
        if (state.level !== FL.CONTENT) {
          return;
        }
        var target = entryPick ? entryPick() : null;
        if (!target) {
          var list = GT4.nav.focusables();
          var key = FL.rowFor(state);
          target = key ? list.filter(function (el) { return rowKeyOf(el) === key; })[0] : null;
          target = target || list.filter(function (el) { return !!el.closest(".ctl"); })[0] || list[0];
        }
        if (target) {
          target.focus({ preventScroll: true });
        }
      }

      // LB/RB (or a swipe): the next or previous tab. The level stays; in content the focus lands on that tab's row.
      function stepTab(delta) {
        var next = FL.moveTab(state, delta, tabIds);
        setState(next);
        showTab(next.tab);
        if (state.level === FL.CONTENT) {
          ready.then(focusRow);
        } else {
          var button = sideButton(state.tab);
          if (button) {
            button.focus({ preventScroll: true });
          }
        }
      }

      // Clicks: a pad or keyboard activation enters the tab. A mouse or touch click only focuses it (the preview
      // already shows the tab).
      function onTabClick(event) {
        var mode = GT4.inputMode ? GT4.inputMode() : "mouse";
        if (mode === "mouse") {
          return;
        }
        event.preventDefault();
        enterContent();
      }

      // Focus on the tab list: the highlighted tab is shown (preview). Focus on Back keeps the shown tab.
      side.addEventListener("focusin", function (event) {
        var id = event.target.getAttribute ? event.target.getAttribute("data-tab") : null;
        if (state.level !== FL.SIDE) {
          setState(FL.back(state));
        }
        if (id && id !== shown) {
          setState(FL.select(state, id));
          showTab(id);
        }
        placePill();
      });

      GT4.focusScope = function () {
        return state.level === FL.CONTENT ? content : side;
      };
      GT4.hooks.back = function () {
        if (state.level === FL.CONTENT) {
          leaveContent();
        } else {
          GT4.go("#/home");
        }
      };
      GT4.hooks.tabs = function (delta) {
        stepTab(delta);
      };
      // D-pad right on a tab enters it; everything else (sliders, the rows) is left to nav.js.
      GT4.hooks.horizontal = function (delta) {
        var focused = document.activeElement;
        if (state.level === FL.SIDE && delta > 0 && focused && focused.hasAttribute && focused.hasAttribute("data-tab")) {
          enterContent();
          return true;
        }
        return false;
      };

      // Swipe left or right (finger, not mouse) changes the tab. Sliders and dropdown lists keep their own drags.
      var swipe = null;
      stage.addEventListener("pointerdown", function (event) {
        if (event.pointerType === "mouse" || GT4.capturing || GT4.ui.topDialog()) {
          swipe = null;
          return;
        }
        if (event.target.closest && event.target.closest("input, .select, .option-list, .info, .toggle")) {
          swipe = null;
          return;
        }
        swipe = { id: event.pointerId, x: event.clientX, y: event.clientY };
      });
      stage.addEventListener("pointerup", function (event) {
        if (!swipe || event.pointerId !== swipe.id) {
          return;
        }
        var step = GT4.input.swipeStep(event.clientX - swipe.x, event.clientY - swipe.y);
        swipe = null;
        if (step && GT4.hooks.tabs) {
          GT4.hooks.tabs(step);
        }
      });
      stage.addEventListener("pointercancel", function () {
        swipe = null;
      });

      call("about_info").then(function (info) {
        version.textContent = GT4.t("settings.version", { version: info.version || "" });
      }).catch(function () {});

      // Dev harness only (ui/dev/index.html?focus=...): sets the focus state for screenshots and checks.
      var dev = GT4.devFocus || "";
      if (dev) {
        GT4.nav.setMode("pad");
      }
      setState(state);
      // The pill needs the final layout: the logo and the fonts load after mount.
      side.querySelector(".side-logo").addEventListener("load", placePill);
      if (document.fonts && document.fonts.ready) {
        document.fonts.ready.then(placePill);
      }
      window.requestAnimationFrame(placePill);
      showTab(requested).then(function () {
        placePill();
        if (dev === "content" || dev === "row" || dev === "slider" || dev === "dropdown") {
          enterContent();
          ready.then(function () {
            if (dev === "slider") {
              var slider = content.querySelector("input[type=range]");
              if (slider) { slider.focus({ preventScroll: true }); }
            } else if (dev === "dropdown") {
              var select = content.querySelector(".select");
              if (select) { select.focus({ preventScroll: true }); select.click(); }
            }
          });
        } else {
          var button = sideButton(requested);
          if (button) {
            button.focus({ preventScroll: true });
          }
        }
      });
      return {
        destroy: function () {
          GT4.focusScope = null;
        }
      };
    }
  };
})(window);
