/*
 * Settings row model (pure logic, no DOM; tested in dev/nav-test.html). settings.js and the pages that use its rows
 * (Controls triggers) work on a table as get_settings_table returns it: {max_upscale, rows: [row]}.
 *
 * A row's visibility depends on other rows (visible_when) and on the mods feature flag; a successful set_setting
 * changes the row model, and the table is then read again (disabled rows depend on other settings too).
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});

  function findRow(table, id) {
    var rows = (table && table.rows) || [];
    for (var i = 0; i < rows.length; i++) {
      if (rows[i].id === id) {
        return rows[i];
      }
    }
    return null;
  }

  // modsOn: GT4.features.mods. Mod rows never show while the feature is off.
  function rowVisible(table, row, modsOn) {
    if (row.page === "mods" && !modsOn) {
      return false;
    }
    if (!row.visible_when) {
      return true;
    }
    var other = findRow(table, row.visible_when.id);
    return !!other && String(other.current) === String(row.visible_when.equals);
  }

  // The model after set_setting succeeded: the changed row holds the value that was written.
  // Returns the row, or null for an unknown id.
  function applyChange(table, id, value) {
    var row = findRow(table, id);
    if (row) {
      row.current = value;
    }
    return row;
  }

  // The text a dropdown shows for a value: its option's label, else the raw value.
  function optionLabel(options, value) {
    for (var i = 0; i < (options || []).length; i++) {
      if (String(options[i].value) === String(value)) {
        return options[i].label;
      }
    }
    return String(value);
  }

  // Where A (entering Saves or Replays) puts focus: the first row, or Import when the list is empty.
  function libraryEntry(rowCount) {
    return rowCount > 0 ? "row" : "import";
  }

  // Reads of the table can overlap (a fast pad repeat). Each read takes a ticket; only the newest one is applied.
  function readGate() {
    var latest = 0;
    return {
      ticket: function () {
        latest += 1;
        return latest;
      },
      isLatest: function (ticket) {
        return ticket === latest;
      }
    };
  }

  GT4.settingsModel = {
    findRow: findRow,
    rowVisible: rowVisible,
    applyChange: applyChange,
    optionLabel: optionLabel,
    libraryEntry: libraryEntry,
    readGate: readGate
  };
})(window);
