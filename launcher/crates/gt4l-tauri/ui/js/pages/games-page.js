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
        title: GT4.t("games.remove_title", { game: game.name }),
        text: GT4.t("games.remove_text", { size: size ? " (" + size + ")" : "" }),
        buttons: [
          { label: GT4.t("common.cancel") },
          {
            label: GT4.t("common.remove"),
            primary: true,
            danger: true,
            run: function () {
              GT4.games.remove(game.id).then(function (view) {
                GT4.ui.toast(GT4.t("games.removed", { game: game.name }));
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
        ? GT4.t("games.installed") + (game.disc_name ? " · " + game.disc_name : "") + (game.size_bytes ? " · " + GT4.games.size(game.size_bytes) : "")
        : (game.damaged ? GT4.t("games.missing_reason", { reason: game.reason }) : GT4.t("games.not_installed"));
      var buttons = [];
      if (game.installed) {
        buttons.push(h("button", {
          class: "btn small", type: "button",
          on: { click: function () { GT4.games.installGame(game.id, started); } }
        }, GT4.t("games.change_disc")));
        buttons.push(h("button", {
          class: "btn small danger", type: "button",
          on: { click: function () { confirmRemove(game); } }
        }, GT4.t("common.remove")));
      } else {
        buttons.push(h("button", {
          class: "btn small primary", type: "button",
          on: { click: function () { GT4.games.installGame(game.id, started); } }
        }, game.damaged ? GT4.t("games.choose_disc") : GT4.t("common.install")));
        if (game.damaged) {
          buttons.push(h("button", {
            class: "btn small danger", type: "button",
            on: { click: function () { confirmRemove(game); } }
          }, GT4.t("common.remove")));
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

    content.appendChild(h("h1", { text: GT4.t("games.page_title") }));
    content.appendChild(h("p", {
      class: "note",
      text: GT4.t("games.page_note")
    }));
    content.appendChild(list);

    host.hints([]);
    host.entry(function () { return firstButton || content.querySelector("button"); });

    return GT4.games.list().then(paint, function (err) {
      GT4.ui.toast(GT4.errorText(err), true);
    });
  };
})(window);
