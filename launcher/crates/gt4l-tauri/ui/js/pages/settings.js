/*
 * Settings shell (nav column + content) and the rows generated from
 * get_settings_table. The page never hard-codes an option: each row comes
 * from the table, with its own control, help text and page.
 *
 * Table shape (returned by get_settings_table as {max_upscale, rows}):
 *   row = {id, label, help, page: "basic"|"advanced"|"mods"|"hidden",
 *          group?: "graphics"|"display", control: "slider"|"toggle"|"dropdown",
 *          values, default, current, disabled?, visible_when?: {id, equals}}
 *   slider values   = {min, max, step, unit?, limit?: "max_upscale"}
 *   dropdown values = [{value, label, disabled?, note?}]
 *   toggle values   = null (current is true/false)
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});
  var h = GT4.ui.h;

  var TABS = [
    { id: "graphics", label: "Graphics" },
    { id: "advanced", label: "Advanced" },
    { id: "display", label: "Display" },
    { id: "controls", label: "Controls" },
    { id: "saves", label: "Saves" },
    { id: "replays", label: "Replays" },
    { id: "mods", label: "Mods" },
    { id: "about", label: "About" }
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

  function findRow(table, id) {
    for (var i = 0; i < table.rows.length; i++) {
      if (table.rows[i].id === id) {
        return table.rows[i];
      }
    }
    return null;
  }

  // A change to a row that other rows depend on re-renders the tab.
  function affectsVisibility(table, id) {
    return table.rows.some(function (row) {
      return row.visible_when && row.visible_when.id === id;
    });
  }

  function rowVisible(table, row) {
    // Mod and texture rows never show in this build.
    if (row.page === "mods" && !GT4.features.mods) {
      return false;
    }
    if (!row.visible_when) {
      return true;
    }
    var other = findRow(table, row.visible_when.id);
    return !!other && String(other.current) === String(row.visible_when.equals);
  }

  // ctx = {table, content, refresh(focusId)}
  function commit(ctx, id, value) {
    return call("set_setting", { id: id, value: value }).then(
      function () {
        if (affectsVisibility(ctx.table, id)) {
          ctx.refresh(id);
        }
      },
      function (err) {
        GT4.ui.toast(errorText(err), true);
        ctx.refresh(id);
      }
    );
  }

  function sliderSpec(row, table) {
    var spec = row.values;
    var max = spec.max;
    if (spec.limit && typeof table[spec.limit] === "number") {
      max = Math.min(max, table[spec.limit]);
    }
    return { min: spec.min, max: max, step: spec.step || 1, unit: spec.unit || "", fullMax: spec.max };
  }

  function sliderControl(ctx, row) {
    var spec = sliderSpec(row, ctx.table);
    var format = function (value) {
      return String(value) + spec.unit;
    };
    var input = h("input", {
      type: "range",
      class: "slider",
      min: spec.min,
      max: spec.max,
      step: spec.step,
      value: row.current,
      "aria-label": row.label,
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
    return { node: nodes, limited: spec.max < spec.fullMax ? spec.max + spec.unit : null };
  }

  function toggleControl(ctx, row) {
    var button = h("button", {
      class: "toggle" + (row.current ? " on" : ""),
      role: "switch",
      "aria-checked": String(!!row.current),
      "aria-label": row.label,
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
        currentText = option.label;
      }
    });
    var button = h("button", {
      class: "select",
      type: "button",
      "aria-haspopup": "listbox",
      "aria-label": row.label,
      disabled: !!row.disabled,
      data: { setting: row.id, adjust: "1" }
    }, [h("span", { text: currentText }), h("span", { class: "chev" })]);
    var label = button.querySelector("span");

    function choose(value) {
      label.textContent = currentText;
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
      currentText = next.label;
      label.textContent = next.label;
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
              row.current = option.value;
              choose(option.value);
            }
          }
        }, [
          h("span", { text: option.label }),
          option.disabled
            ? h("span", { class: "tag", text: option.note || "Coming soon" })
            : (selected ? h("span", { class: "tick", text: "Current" }) : null)
        ]);
      }));
      dialog = GT4.ui.openDialog({
        title: row.label,
        body: list,
        buttons: [{ label: "Cancel" }]
      });
    });
    return { node: [button] };
  }

  function infoButton(row) {
    return h("button", { class: "info", type: "button", "aria-label": "About " + row.label }, [
      "i",
      h("span", { class: "tip", role: "tooltip" }, [
        h("b", { text: row.label }),
        row.help || ""
      ])
    ]);
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
    var keyNodes = [h("span", { text: row.label }), infoButton(row)];
    if (control.limited) {
      keyNodes.push(h("span", { class: "hint-text", text: "Limited to " + control.limited + " on this GPU" }));
    }
    return h("div", { class: "row" + (row.disabled ? " is-disabled" : ""), data: { row: row.id } }, [
      h("div", { class: "k" }, keyNodes),
      h("div", { class: "ctl" }, control.node)
    ]);
  }

  // Renders the rows of one tab, restoring focus to focusId when given.
  function renderRows(ctx, rows, focusId) {
    rows.forEach(function (row) {
      if (!rowVisible(ctx.table, row)) {
        return;
      }
      var node = rowNode(ctx, row);
      if (node) {
        ctx.content.appendChild(node);
      }
    });
    if (focusId) {
      var target = ctx.content.querySelector("[data-setting=\"" + focusId + "\"]");
      if (target) {
        target.focus({ preventScroll: true });
      }
    }
  }

  // Generic settings tab: loads the table, filters rows, renders them.
  // spec = {title, note, filter(row, table), extra(ctx), hooks(ctx)}
  function settingsTab(content, host, spec) {
    var ctx = { content: content, table: null, refresh: null };

    function paint(focusId) {
      GT4.ui.clear(content);
      content.appendChild(h("h1", { text: spec.title }));
      content.appendChild(h("p", { class: "note", text: spec.note || "Changes apply the next time you play." }));
      var rows = ctx.table.rows.filter(function (row) {
        return spec.filter(row, ctx.table);
      });
      renderRows(ctx, rows, focusId);
      if (spec.extra) {
        spec.extra(ctx, host);
      }
    }

    ctx.refresh = function (focusId) {
      return call("get_settings_table").then(function (table) {
        ctx.table = table;
        paint(focusId);
      }, function (err) {
        GT4.ui.toast(errorText(err), true);
      });
    };

    content.appendChild(h("p", { class: "loading", text: "Loading settings..." }));
    return ctx.refresh().then(function () {
      if (spec.hooks) {
        spec.hooks(ctx, host);
      }
    });
  }
  GT4.settingsTab = settingsTab;
  GT4.settingsRows = { rowNode: rowNode };

  // Graphics: page=basic rows in the graphics group.
  GT4.tabPages.graphics = function (content, host) {
    return settingsTab(content, host, {
      title: "Graphics",
      filter: function (row) {
        return row.page === "basic" && (row.group || "graphics") === "graphics";
      }
    });
  };

  // Display: page=basic rows in the display group.
  GT4.tabPages.display = function (content, host) {
    return settingsTab(content, host, {
      title: "Display",
      filter: function (row) {
        return row.page === "basic" && row.group === "display";
      }
    });
  };

  // Advanced: page=advanced rows, plus the reset-to-defaults action (X).
  GT4.tabPages.advanced = function (content, host) {
    function resetDefaults(ctx) {
      GT4.ui.openDialog({
        title: "Reset to defaults?",
        text: "Graphics settings go back to their defaults. Changes apply the next time you play.",
        buttons: [
          { label: "Cancel" },
          {
            label: "Reset",
            primary: true,
            danger: true,
            run: function () {
              call("reset_gs_settings_to_defaults").then(function (table) {
                ctx.table = table;
                ctx.refresh();
                GT4.ui.toast("Graphics settings reset");
              }, function (err) {
                GT4.ui.toast(errorText(err), true);
              });
            }
          }
        ]
      });
    }
    return settingsTab(content, host, {
      title: "Advanced",
      filter: function (row) {
        return row.page === "advanced";
      },
      extra: function (ctx) {
        var button = h("button", {
          class: "btn danger",
          type: "button",
          on: { click: function () { resetDefaults(ctx); } }
        }, "Reset to defaults");
        content.appendChild(h("div", { class: "advanced-foot" }, [button]));
      },
      hooks: function (ctx, tabHost) {
        tabHost.setHooks({ x: function () { resetDefaults(ctx); } });
        tabHost.hints([
          { key: "X", label: "Reset to defaults", run: function () { resetDefaults(ctx); } }
        ]);
      }
    });
  };

  // Mods: placeholder until TASK-048.
  // Registered only when the feature flag is on (release 1.1.0).
  if (GT4.features.mods) {
    GT4.tabPages.mods = function (content) {
      content.appendChild(h("h1", { text: "Mods" }));
      content.appendChild(h("div", { class: "placeholder" }, [
        h("h2", { text: "Texture packs and mods — coming soon" }),
        h("p", { text: "Planned in TASK-048." })
      ]));
      return Promise.resolve();
    };
  }

  // About: version, notice, disc path, log folder.
  GT4.tabPages.about = function (content) {
    return call("about_info").then(function (info) {
      content.appendChild(h("h1", { text: "About" }));
      content.appendChild(h("p", { class: "note", text: "GT4Recomp " + (info.version || "") }));
      content.appendChild(h("div", { class: "about-notice" }, [
        h("p", { text: "GT4Recomp is an unofficial fan project. It is not affiliated with, endorsed by or sponsored by Sony Interactive Entertainment or Polyphony Digital." }),
        h("p", { text: "Gran Turismo and the Gran Turismo 4 logo are trademarks of Sony Interactive Entertainment Inc. and Polyphony Digital Inc. They are used here only to identify the game. You need your own copy of the game to play." })
      ]));
      content.appendChild(h("div", { class: "group", text: "More" }));
      content.appendChild(h("div", { class: "row" }, [
        h("div", { class: "k", text: "Disc image" }),
        h("div", { class: "ctl" }, [h("span", { class: "val", text: info.disc_path || "Not set" })])
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
      }, "Open log folder");
      content.appendChild(h("div", { class: "row" }, [
        h("div", { class: "k", text: "Logs" }),
        h("div", { class: "ctl" }, [openLog])
      ]));
    }, function (err) {
      content.appendChild(h("p", { class: "loading", text: errorText(err) }));
    });
  };

  // The settings shell: nav column, then the tab content.
  GT4.pages = GT4.pages || {};
  GT4.pages.settings = {
    mount: function (root, params) {
      var tabs = visibleTabs();
      var tabId = params.tab || "graphics";
      var index = tabs.findIndex(function (tab) {
        return tab.id === tabId;
      });
      if (index < 0) {
        // Unknown or hidden tab (for example #/settings/mods in this build): fall back.
        index = 0;
        tabId = tabs[0].id;
      }

      var content = h("div", { class: "content" });
      var version = h("div", { class: "side-version", text: "" });
      var backIcon = h("span", { "aria-hidden": "true", text: "‹" });
      var side = h("nav", { class: "side", "aria-label": "Settings" }, [
        h("button", {
          class: "back",
          type: "button",
          on: { click: function () { GT4.go("#/home"); } }
        }, [backIcon, "Back"]),
        h("img", { class: "side-logo", src: "assets/logo-gt4.png", alt: "Gran Turismo 4" }),
        tabs.map(function (tab) {
          return h("button", {
            class: "tab",
            type: "button",
            "aria-current": tab.id === tabId ? "page" : null,
            on: { click: function () { GT4.go("#/settings/" + tab.id); } }
          }, tab.label);
        }),
        version
      ]);
      root.appendChild(h("div", { class: "stage settings" }, [side, content]));

      GT4.hooks.back = function () { GT4.go("#/home"); };
      GT4.hooks.tabs = function (delta) {
        var next = tabs[(index + delta + tabs.length) % tabs.length];
        GT4.go("#/settings/" + next.id);
      };

      var host = {
        setHooks: function (partial) {
          Object.keys(partial).forEach(function (key) {
            GT4.hooks[key] = partial[key];
          });
        },
        hints: function (extra) {
          GT4.ui.setHints((extra || []).concat([
            { key: "A", label: "Select" },
            { key: "B", label: "Back" },
            { key: "LB RB", label: "Tabs" }
          ]));
        }
      };
      host.hints([]);

      call("about_info").then(function (info) {
        version.textContent = "Version " + (info.version || "");
      }).catch(function () {});

      var mounted = GT4.tabPages[tabId] ? GT4.tabPages[tabId](content, host) : null;
      if (mounted && mounted.catch) {
        mounted.catch(function (err) {
          GT4.ui.toast(errorText(err), true);
        });
      }
      return { destroy: function () {} };
    }
  };
})(window);
