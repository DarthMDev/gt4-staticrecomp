/*
 * Settings > Games (TASK-101): the two games, installed or not, the disc each was set up from and the size on disk.
 * Commands (js/games.js): games_list, setup_choose_disc + setup_start (Install, Change disc image), games_remove.
 *   - Install: opens the disc picker for that game and runs its setup (the Setup page shows the progress).
 *   - Change disc image: the same with a new disc image; the new copy is checked again and replaces the old one only when
 *     it is complete.
 *   - Remove: deletes the disc copy and the prepared files of that game, after asking. Its saves and replays are kept.
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});
  var h = GT4.ui.h;

  GT4.tabPages = GT4.tabPages || {};
  GT4.tabPages.games = function (content, host) {
    var list = h("div", { class: "games-list" });
    var firstButton = null;

    function started() {
      GT4.go("#/setup");
    }

    function confirmRemove(game) {
      var size = GT4.games.size(game.size_bytes);
      GT4.ui.openDialog({
        title: "Remove " + game.name + "?",
        text: "This deletes the copy of the disc image and the prepared game files" + (size ? " (" + size + ")" : "") +
          ". Your saves and replays are kept, and you can install the game again later.",
        buttons: [
          { label: "Cancel" },
          {
            label: "Remove",
            primary: true,
            danger: true,
            run: function () {
              GT4.games.remove(game.id).then(function (view) {
                GT4.ui.toast(game.name + " removed. Its saves are kept.");
                paint(view);
              }, function (err) {
                GT4.ui.toast(GT4.errorText(err), true);
              });
            }
          }
        ]
      });
    }

    function card(game) {
      var status = game.installed
        ? "Installed" + (game.disc_name ? " · " + game.disc_name : "") + (game.size_bytes ? " · " + GT4.games.size(game.size_bytes) : "")
        : (game.damaged ? "Game files missing (" + game.reason + ")" : "Not installed");
      var buttons = [];
      if (game.installed) {
        buttons.push(h("button", {
          class: "btn small", type: "button",
          on: { click: function () { GT4.games.installGame(game.id, started); } }
        }, "Change disc image"));
        buttons.push(h("button", {
          class: "btn small danger", type: "button",
          on: { click: function () { confirmRemove(game); } }
        }, "Remove"));
      } else {
        buttons.push(h("button", {
          class: "btn small primary", type: "button",
          on: { click: function () { GT4.games.installGame(game.id, started); } }
        }, game.damaged ? "Choose disc image" : "Install"));
        if (game.damaged) {
          buttons.push(h("button", {
            class: "btn small danger", type: "button",
            on: { click: function () { confirmRemove(game); } }
          }, "Remove"));
        }
      }
      return h("div", { class: "row game-row", data: { gameId: game.id } }, [
        h("div", { class: "k" }, [
          h("span", { text: game.name }),
          h("span", { class: "val", text: status })
        ]),
        h("div", { class: "ctl" }, buttons)
      ]);
    }

    function paint(view) {
      GT4.ui.clear(list);
      view.games.forEach(function (game) {
        list.appendChild(card(game));
      });
      firstButton = list.querySelector("button");
    }

    content.appendChild(h("h1", { text: "Games" }));
    content.appendChild(h("p", {
      class: "note",
      text: "Each game keeps its own disc image copy, prepared files, memory card, saves and replays. Graphics, display, controls and textures are shared by both."
    }));
    content.appendChild(list);

    host.hints([]);
    host.entry(function () { return firstButton || content.querySelector("button"); });

    return GT4.games.list().then(paint, function (err) {
      GT4.ui.toast(GT4.errorText(err), true);
    });
  };
})(window);
