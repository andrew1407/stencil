#include "ProjectsDialog.hpp"
#include <QListWidgetItem>

#include "ProjectRowDelegate.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "DescriptionDialog.hpp"
#include "ExpirationDialog.hpp"
#include "KeywordsDialog.hpp"
#include "../../../support/menu/menuReveal.hpp"
#include "../../../support/theme/theme.hpp"
#include "../../../support/menu/menuDangerRow.hpp"
#include "../copy/copyProjectMenu.hpp"
#include "../../../support/motion/MenuShimmer.hpp"
#include "../../../support/modal/modalReveal.hpp"
#include "ProjectDragMenu.hpp"
#include "../../../support/menu/popupSlide.hpp"
#include "../../../support/motionPrefs.hpp"

#include <QAction>
#include <QMenu>
#include <QPalette>

#include <memory>

// The per-row ⋯ menu, run in place or handed over open to a held drag (list/ProjectDragMenu).

namespace stencil::gui {

  QMenu* ProjectsDialog::showRowMenu(QListWidgetItem* it, const QPoint& globalPos, const QPoint* heldFrom) {
    if (!it || it->data(Qt::UserRole).isNull()) return nullptr;
    // Read NOW, never through `it` later: a server refresh rebuilds the list under an open menu.
    const QString rowId = it->data(Qt::UserRole).toString();
    const QString rowServer = it->data(Qt::UserRole + 1).toString();
    const bool remote = !rowServer.isEmpty();
    const QColor ico = palette().color(QPalette::WindowText);
    const bool haveServers = connections && !connections->urls().isEmpty();
    std::unique_ptr<QMenu> owned(new QMenu(this));
    QMenu& menu = *owned;

    // Where a window raised from this menu flies back to: the menu is gone by close time, so the
    // motes pour into the row's "..." chip. Browser twin: projectsModal.js passes `menuBtn`.
    const QRect kebabGlobal = [this, it]() -> QRect {
      auto* del = static_cast<ProjectRowDelegate*>(list->itemDelegate());
      if (!del) return {};
      const QRect chip = del->kebabChipFor(list->visualItemRect(it));
      return chip.isValid() ? QRect(list->viewport()->mapToGlobal(chip.topLeft()), chip.size())
                            : QRect();
    }();

    // "Add description" — edit the row's free-text description inline (no accept()/close),
    // mirroring the colour edit. An empty value clears.
    auto editDescription = [this, rowId, rowServer, kebabGlobal] {
      const QString& id = rowId;
      const QString& server = rowServer;
      QString current;
      if (server.isEmpty()) {
        for (const auto& p : projects)
          if (QString::fromStdString(p.meta.id) == id) {
            current = QString::fromStdString(p.meta.description);
            break;
          }
      } else {
        for (const auto& sp : this->remote)
          if (sp.id == id && sp.serverUrl == server) { current = sp.description; break; }
      }
      // The toolbar's own Description window, over this one (browser row/actions.js editMeta).
      DescriptionDialog dlg(current, this);
      support::revealDialog(dlg, nullptr, support::gestureAnchorRect(), kebabGlobal);
      if (dlg.exec() != QDialog::Accepted) return;
      const QString text = dlg.text();
      if (text == current) return;        // nothing changed
      commitRowEdit(
          id, server,
          [text](Project& p) { p.meta.description = text.toStdString(); },
          [id, text](stencil::net::ServerClient* c, qint64 version,
                     std::function<void(bool, qint64)> done) {
            c->updateProjectDescriptionAsync(
                id, text, version,
                [done](bool ok2, qint64 v, bool) { done(ok2, v); });
          },
          [text](stencil::net::ServerProject& sp) { sp.description = text; });
    };

    // "Add keywords" — the row's search keywords as chips, normalized by KeywordsDialog the way the
    // browser store's setKeywords does. Empty clears.
    auto editKeywords = [this, rowId, rowServer, kebabGlobal] {
      const QString& id = rowId;
      const QString& server = rowServer;
      QStringList current;
      if (server.isEmpty()) {
        for (const auto& p : projects)
          if (QString::fromStdString(p.meta.id) == id) {
            for (const auto& k : p.meta.keywords) current << QString::fromStdString(k);
            break;
          }
      } else {
        for (const auto& sp : this->remote)
          if (sp.id == id && sp.serverUrl == server) { current = sp.keywords; break; }
      }
      KeywordsDialog dlg(current, this);
      support::revealDialog(dlg, nullptr, support::gestureAnchorRect(), kebabGlobal);
      if (dlg.exec() != QDialog::Accepted) return;
      const QStringList next = dlg.keywords();
      if (next == current) return;          // nothing changed
      commitRowEdit(
          id, server,
          [next](Project& p) {
            p.meta.keywords.clear();
            for (const QString& k : next) p.meta.keywords.push_back(k.toStdString());
          },
          [id, next](stencil::net::ServerClient* c, qint64 version,
                     std::function<void(bool, qint64)> done) {
            c->updateProjectKeywordsAsync(
                id, next, version,
                [done](bool ok2, qint64 v, bool) { done(ok2, v); });
          },
          [next](stencil::net::ServerProject& sp) { sp.keywords = next; });
    };

    // "Set expiration" opens the expiration editor OVER this window (browser parity: ui/base.js
    // `stacked`), so the owner is signalled and calls setProjects() rather than closing the list.
    auto editExpiration = [this, rowId, rowServer, kebabGlobal] {
      if (!rowServer.isEmpty()) return;   // local only
      const QString& id = rowId;
      const core::ProjectMeta* meta = nullptr;
      for (const auto& p : projects)
        if (QString::fromStdString(p.meta.id) == id) { meta = &p.meta; break; }
      if (!meta) return;
      ExpirationDialog exp(QString::fromStdString(meta->name), meta->expiresAt,
                           QString::fromStdString(meta->refreshPeriod), meta->autoRefresh,
                           now, this);
      support::revealDialog(exp, nullptr, support::gestureAnchorRect(), kebabGlobal);
      if (exp.exec() != QDialog::Accepted) return;
      emit expirationRequested(id, exp.expiresAtMs(), exp.refreshPeriod(), exp.autoRefresh());
    };

    // "Clear color" only when the row HAS a custom colour (browser modal parity): with none set
    // there is nothing to clear, and "Set color" already clicks straight into the picker.
    // "Make a copy ›" rides right after "Open in another app" (browser row/actions.js copyMenuItem).
    const auto addCopyMenu = [this, &menu, rowId, rowServer, kebabGlobal, ico] {
      gui::compactIconMenu(*support::addCopyProjectMenu(menu, ico, 16, support::CONTEXT_MENU_DUST_MS,
                                                        [this, rowId, rowServer, kebabGlobal](support::CopyScope s) {
        emit copyRequested(rowId, rowServer, s, kebabGlobal);
      }));
    };
    const bool hasColor = !rowColor(it).isEmpty();
    QAction* removeAct = nullptr;   // marked as the danger row once the sheet is on (below)
    QColor dangerColor;
    // Same actions, order and flat shape as the browser modal's overflow menu, which
    // groups by order alone and draws no rules (slots act on the current row).
    if (remote) {
      menu.addAction(themedIcon("folder", ico, 16), "Open from server", this,
                     &ProjectsDialog::openSelected);
      if (openInServerOk)
        menu.addAction(themedIcon("monitor", ico, 16), "Open in another app", this,
                       [this, rowId, rowServer, kebabGlobal] { emit openInRequested(rowId, rowServer, kebabGlobal); });
      addCopyMenu();
      menu.addAction(themedIcon("copy", ico, 16), "Copy to local", this,
                     &ProjectsDialog::makeLocalCopySelected);
      menu.addAction(themedIcon("download", ico, 16), "Move to local", this,
                     &ProjectsDialog::moveToLocalSelected);
      menu.addAction(themedIcon("palette", ico, 16), "Set color", this,
                     &ProjectsDialog::setColorSelected);
      if (hasColor)
        menu.addAction(themedIcon("x", ico, 16), "Clear color", this,
                       &ProjectsDialog::clearColorSelected);
      menu.addAction(themedIcon("flag", ico, 16), "Add keywords", this, editKeywords);
      menu.addAction(themedIcon("file-text", ico, 16), "Add description", this, editDescription);
    } else {
      menu.addAction(themedIcon("folder", ico, 16), "Open", this,
                     &ProjectsDialog::openSelected);
      menu.addAction(themedIcon("external", ico, 16), "Open in new window", this,
                     &ProjectsDialog::openSelectedInNewWindow);
      if (openInLocalOk)
        menu.addAction(themedIcon("monitor", ico, 16), "Open in another app", this,
                       [this, rowId, kebabGlobal] { emit openInRequested(rowId, QString(), kebabGlobal); });
      addCopyMenu();
      menu.addAction(themedIcon("pencil", ico, 16), "Rename", this,
                     [this] { beginInlineRename(list->currentItem()); });
      menu.addAction(themedIcon("palette", ico, 16), "Set color", this,
                     &ProjectsDialog::setColorSelected);
      if (hasColor)
        menu.addAction(themedIcon("x", ico, 16), "Clear color", this,
                       &ProjectsDialog::clearColorSelected);
      menu.addAction(themedIcon("flag", ico, 16), "Add keywords", this, editKeywords);
      menu.addAction(themedIcon("file-text", ico, 16), "Add description", this, editDescription);
      menu.addAction(themedIcon("calendar", ico, 16), "Set expiration", this, editExpiration);
      if (haveServers) {
        menu.addAction(themedIcon("server", ico, 16), "Move to server", this,
                       &ProjectsDialog::moveToServerSelected);
        menu.addAction(themedIcon("copy", ico, 16), "Copy to server", this,
                       &ProjectsDialog::copyToServerSelected);
      }
      // Destructive: the browser's "Remove" row colours both halves in --danger
      // (.project-menu-item.is-danger), so support/menu/menuDangerRow.hpp inks the label to match.
      dangerColor = themePalette(palette().color(QPalette::Window).lightness() < 128).danger;
      removeAct = menu.addAction(themedIcon("trash", dangerColor, 16), "Remove", this,
                                 &ProjectsDialog::deleteSelected);
    }
    // The same glass shimmer every other ctx row's hover sweeps (browser
    // .project-menu-item parity; mainWindow's canvas context menu already plays it).
    new support::MenuShimmer(&menu, &menu);   // lives as long as the menu
    // The treatment every other menu gets (browser projectsModal.js showMenu): a compact icon+label
    // popup fitted to its own longest label, growing out of the click and pouring back.
    gui::compactIconMenu(menu);
    // After compactIconMenu — it replaces the menu's stylesheet, and this appends to it.
    support::markDangerRow(menu, removeAct, dangerColor);
    support::markAccentRows(menu, onAccentInk(palette().color(QPalette::Highlight)), removeAct);
    support::revealMenu(menu, heldFrom ? *heldFrom : globalPos, support::CONTEXT_MENU_DUST_MS);   // the canvas menu's 1.5x clock
    if (heldFrom) {
      // A drag holds the pointer, so no nested loop: open it, and carry the chip to the item run.
      menu.setProperty(HELD_MENU_KEBAB_PROP, kebabGlobal);
      menu.popup(globalPos);
      // revealMenu is the particle modes' entrance; 'slide' grows the list out of the same point.
      if (support::isSlideMotionOk()) support::slidePopupIn(menu, *heldFrom);
      return owned.release();
    }
    // Visible to the slots this menu fires (deleteSelected, the open confirm) for exactly
    // as long as the popup lives — they capture it and fly their answer back into the chip.
    hover.menuKebabRect = kebabGlobal;
    menu.exec(globalPos);
    hover.menuKebabRect = QRect();
    return nullptr;
  }

}  // namespace stencil::gui
