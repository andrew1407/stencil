#include "ProjectsDialog.hpp"
#include <QListWidget>
#include <QListWidgetItem>

#include "ProjectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "ProjectsDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "ExpirationDialog.hpp"
#include "../support/guiHelpers.hpp"
#include "../support/menuReveal.hpp"
#include "../support/theme.hpp"
#include "../support/menuDangerRow.hpp"
#include "../support/MenuShimmer.hpp"
#include "../support/modalChrome.hpp"
#include "../support/modalReveal.hpp"
#include "AppTooltip.hpp"

#include <QAction>
#include <QMenu>
#include <QPalette>
#include <QRegularExpression>

// The per-row ⋯ menu.

namespace stencil::gui {

  void ProjectsDialog::showRowMenu(QListWidgetItem* it, const QPoint& globalPos) {
    if (!it || it->data(Qt::UserRole).isNull()) return;
    const bool remote = !it->data(Qt::UserRole + 1).toString().isEmpty();
    const QColor ico = palette().color(QPalette::WindowText);
    const bool haveServers = connections_ && !connections_->urls().isEmpty();
    QMenu menu(this);

    // Where a window raised from this menu flies back to: it grows out of the picked row,
    // but the menu is gone by close time, so the motes pour into the row's "⋯" chip.
    // Browser twin: projectsModal.js passes `menuBtn` the same way.
    const QRect kebabGlobal = [this, it]() -> QRect {
      auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
      if (!del) return {};
      const QRect chip = del->kebabChipFor(list_->visualItemRect(it));
      return chip.isValid() ? QRect(list_->viewport()->mapToGlobal(chip.topLeft()), chip.size())
                            : QRect();
    }();

    // "Add description" — edit the row's free-text description inline (no accept()/close),
    // mirroring the colour edit. An empty value clears.
    auto editDescription = [this, it, kebabGlobal] {
      const QString id = it->data(Qt::UserRole).toString();
      const QString server = it->data(Qt::UserRole + 1).toString();
      QString current;
      if (server.isEmpty()) {
        for (const auto& p : projects_)
          if (QString::fromStdString(p.meta.id) == id) {
            current = QString::fromStdString(p.meta.description);
            break;
          }
      } else {
        for (const auto& sp : remote_)
          if (sp.id == id && sp.serverUrl == server) { current = sp.description; break; }
      }
      PromptSpec spec;
      spec.title = tr("Project description");
      spec.titleIcon = QStringLiteral("info");
      spec.message = tr("Description:");
      spec.defaultValue = current;
      spec.multiline = true;              // a description is a sentence, not a word
      spec.maxChars = 2000;               // soft cap (UI only; core does no validation)
      spec.flight.closeRect = kebabGlobal;
      const auto entered = promptModal(this, spec);
      if (!entered) return;
      const QString text = *entered;
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

    // "Add keywords" — the row's search keywords, comma/space separated, normalized to
    // lowercase unique words the way the browser store's setKeywords does. Empty clears.
    auto editKeywords = [this, it, kebabGlobal] {
      const QString id = it->data(Qt::UserRole).toString();
      const QString server = it->data(Qt::UserRole + 1).toString();
      QStringList current;
      if (server.isEmpty()) {
        for (const auto& p : projects_)
          if (QString::fromStdString(p.meta.id) == id) {
            for (const auto& k : p.meta.keywords) current << QString::fromStdString(k);
            break;
          }
      } else {
        for (const auto& sp : remote_)
          if (sp.id == id && sp.serverUrl == server) { current = sp.keywords; break; }
      }
      PromptSpec spec;
      spec.title = tr("Project keywords");
      spec.titleIcon = QStringLiteral("info");
      spec.message = tr("Keywords (comma or space separated):");
      spec.defaultValue = current.join(' ');
      spec.multiline = true;              // keywords are a list, not a word
      spec.flight.closeRect = kebabGlobal;
      const auto entered = promptModal(this, spec);
      if (!entered) return;
      QStringList next;
      for (const QString& raw : entered->split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts)) {
        const QString k = raw.toLower();
        if (!next.contains(k)) next << k;
      }
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

    // "Set expiration" — the browser-styled expiration editor, opened OVER this window
    // (browser parity: ui/base.js `stacked`). Closing the list to edit one of its rows
    // lost the user their place, so the owner is signalled and calls setProjects().
    auto editExpiration = [this, it, kebabGlobal] {
      if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;   // local only
      const QString id = it->data(Qt::UserRole).toString();
      const core::ProjectMeta* meta = nullptr;
      for (const auto& p : projects_)
        if (QString::fromStdString(p.meta.id) == id) { meta = &p.meta; break; }
      if (!meta) return;
      ExpirationDialog exp(QString::fromStdString(meta->name), meta->expiresAt,
                           QString::fromStdString(meta->refreshPeriod), meta->autoRefresh,
                           now_, this);
      support::revealDialog(exp, nullptr, support::gestureAnchorRect(), kebabGlobal);
      if (exp.exec() != QDialog::Accepted) return;
      emit expirationRequested(id, exp.expiresAtMs(), exp.refreshPeriod(), exp.autoRefresh());
    };

    // "Clear color" only when the row HAS a custom colour (browser modal parity): with
    // none set there is nothing to clear, and "Set color" already clicks straight into
    // the picker.
    const bool hasColor = !rowColor(it).isEmpty();
    QAction* removeAct = nullptr;   // marked as the danger row once the sheet is on (below)
    QColor dangerColor;
    // Same actions, order and flat shape as the browser modal's overflow menu, which
    // groups by order alone and draws no rules (slots act on the current row).
    if (remote) {
      menu.addAction(themedIcon("folder", ico, 16), "Open from server", this,
                     &ProjectsDialog::openSelected);
      if (openInServerOk_)
        menu.addAction(themedIcon("monitor", ico, 16), "Open in another app", this,
                       [this, it, kebabGlobal] {
                         emit openInRequested(it->data(Qt::UserRole).toString(),
                                              it->data(Qt::UserRole + 1).toString(), kebabGlobal);
                       });
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
      if (openInLocalOk_)
        menu.addAction(themedIcon("monitor", ico, 16), "Open in another app", this,
                       [this, it, kebabGlobal] {
                         emit openInRequested(it->data(Qt::UserRole).toString(), QString(),
                                              kebabGlobal);
                       });
      menu.addAction(themedIcon("pencil", ico, 16), "Rename", this,
                     [this] { beginInlineRename(list_->currentItem()); });
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
      // (.project-menu-item.is-danger), so the glyph takes the theme's red and
      // support/menuDangerRow.hpp inks the label to match.
      dangerColor = themePalette(palette().color(QPalette::Window).lightness() < 128).danger;
      removeAct = menu.addAction(themedIcon("trash", dangerColor, 16), "Remove", this,
                                 &ProjectsDialog::deleteSelected);
    }
    // The same glass shimmer every other ctx row's hover sweeps (browser
    // .project-menu-item parity; mainWindow's canvas context menu already plays it).
    support::MenuShimmer shimmer(&menu);
    // …and the rest of the treatment every other menu gets (browser projectsModal.js
    // showMenu): a compact icon+label popup fitted to its own longest label rather than
    // carrying the menu bar's wide paddings, growing out of the click and pouring back.
    gui::compactIconMenu(menu);
    // After compactIconMenu — it replaces the menu's stylesheet, and this appends to it.
    support::markDangerRow(menu, removeAct, dangerColor);
    support::revealMenu(menu, globalPos);   // grow-from-the-cursor pop
    // Visible to the slots this menu fires (deleteSelected, the open confirm) for exactly
    // as long as the popup lives — they capture it and fly their answer back into the chip.
    menuKebabRect_ = kebabGlobal;
    menu.exec(globalPos);
    menuKebabRect_ = QRect();
  }

}  // namespace stencil::gui
