#include "ProjectsDialog.hpp"

#include "accentDefaults.hpp"
#include "ProjectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "guiHelpers.hpp"
#include "../support/controlReveal.hpp"
#include "../support/DisintegrateOverlay.hpp"
#include "../support/filterFade.hpp"
#include "../support/guiHelpers.hpp"
#include "../support/ShimmerOverlay.hpp"
#include "../support/modalChrome.hpp"
#include "../support/modalReveal.hpp"
#include "ShimmerOverlay.hpp"

#include <QPalette>
#include <QTimer>
#include <algorithm>

// Retiring rows, and the colour a row carries.

namespace stencil::gui {

  void ProjectsDialog::scatterRows(const QSet<QString>& keys) {
    if (!list_) return;
    // A copy: retireRow prunes batch_.checked below and the batch removal hands batch_.checked in as `keys`, so
    // this must not iterate a set the loop is emptying. (Implicit sharing keeps it free.)
    const QSet<QString> want = keys;
    QList<QListWidgetItem*> doomed;
    QList<QRect> rects;   // the on-screen slice of each doomed row (scrolled-out rows: none)
    // Clip each row's rect to the viewport: a checked row scrolled out of view must not drop its
    // overlay onto the dialog chrome, nor spend the shared mote budget on pixels nobody can see.
    const QRect view = list_->viewport()->rect();
    for (int i = 0; i < list_->count(); ++i) {
      const QString key = rowKeyAt(i);
      if (key.isEmpty() || (!want.isEmpty() && !want.contains(key))) continue;
      QListWidgetItem* it = list_->item(i);
      if (!it || it->isHidden()) continue;
      doomed.append(it);
      const QRect r = list_->visualItemRect(it).intersected(view);
      if (r.width() >= 8 && r.height() >= 8) rects.append(r);
    }
    if (doomed.isEmpty()) return;
    // Every row scatters at once and all repaint each frame, so the mote budget is SHARED - a mass
    // removal coarsens each (browser: scatterGridFor). Only rows that actually animate share it.
    const int budget =
        std::max<int>(1, DisintegrateOverlay::DUST_MAX_CELLS / std::max(1, int(rects.size())));
    // Overlays FIRST (they snapshot the still-painted rows), then retire the lot.
    for (const QRect& r : rects)
      DisintegrateOverlay::overRect(list_->viewport(), r, this,
                                    DisintegrateOverlay::Sweep::ROWS, /*dust=*/true, budget,
                                    DisintegrateOverlay::ITEM_MS,   // a card is read, not glanced at
                                    list_->palette().color(QPalette::Text));
    for (QListWidgetItem* it : doomed)
      retireRow(it);   // blank the real row at once; its slot outlives the dust
    // The bar answers NOW, beside the rows' dust: retireRow has already dropped these rows from the
    // checked set, so count, buttons and Select all come apart in the SAME turn. Connections parity.
    updateBatchBar();
  }

  // Closing mid-scatter: the close flight re-photographs the dialog as it hides (modalReveal), so
  // doomed rows must leave NOW or the motes redraw them in the shrinking ghost.
  void ProjectsDialog::done(int result) {
    // A filter fade settles NOW too — nothing half-faded survives into the close flight.
    if (filterFade_) filterFade_->finishNow();
    hideHoverPreview();   // the doomed-row sweep below may delete whatever it points to
    closeInlineRename();
    for (int i = list_->count() - 1; i >= 0; --i)
      if (list_->item(i)->data(DOOMED_ROLE).toBool()) delete list_->takeItem(i);
    stopDustClouds(this);   // …the bar's controls' own clouds with them
    QDialog::done(result);
  }

  // The scatter animates a SNAPSHOT - blank the real row as it starts, hold the empty slot while
  // the dust falls, then let the item go (browser parity: leaveThenRemove + beginRemoval).
  void ProjectsDialog::retireRow(QListWidgetItem* it) {
    if (!it || it->data(Qt::UserRole).isNull()) return;
    const QString key = rowKeyAt(list_->row(it));
    it->setData(DOOMED_ROLE, true);   // the delegate paints nothing for it
    it->setFlags(Qt::NoItemFlags);    // no select/check mid-flight
    batch_.checked.remove(key);             // …and it stops counting towards the selection bar
    QTimer::singleShot(DisintegrateOverlay::ITEM_MS, this, [this, key] {
      // Re-found by key: a re-list may have rebuilt the rows (fresh ones aren't doomed).
      for (int i = 0; i < list_->count(); ++i)
        if (rowKeyAt(i) == key && list_->item(i)->data(DOOMED_ROLE).toBool()) {
          // The hover preview may still be pointing at the very row about to go.
          if (list_->item(i) == hover_.hoverItem) hideHoverPreview();
          delete list_->takeItem(i);
          break;
        }
    });
  }

  QString ProjectsDialog::rowKeyAt(int i) const {
    QListWidgetItem* it = list_->item(i);
    if (!it || it->data(Qt::UserRole).isNull()) return {};  // placeholder / non-data row
    return it->data(Qt::UserRole + 1).toString() + "|" + it->data(Qt::UserRole).toString();
  }

  QString ProjectsDialog::rowColor(const QListWidgetItem* it) const {
    if (!it || it->data(Qt::UserRole).isNull()) return {};
    const QString id = it->data(Qt::UserRole).toString();
    const QString server = it->data(Qt::UserRole + 1).toString();
    if (!server.isEmpty()) {  // server row → read the cached record
      for (const auto& sp : remote_)
        if (sp.id == id && sp.serverUrl == server) return sp.color;
      return {};
    }
    for (const auto& p : projects_)  // local row → read the project meta
      if (QString::fromStdString(p.meta.id) == id) return QString::fromStdString(p.meta.color);
    return {};
  }

  QString ProjectsDialog::currentRowColor() const { return rowColor(list_->currentItem()); }

  // Resolve the selected row's (id, serverUrl) and emit a SetColor action with `color`
  // ("" = clear to the theme default). Shared by the set / clear colour menu entries.
  void ProjectsDialog::emitSetColor(QListWidgetItem* it, const QString& color) {
    selectedId_ = it->data(Qt::UserRole).toString();
    selectedServerUrl_ = it->data(Qt::UserRole + 1).toString();
    selectedColor_ = color;
    action_ = Action::SET_COLOR;
    accept();
  }

  void ProjectsDialog::setColorSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    const QString cur = currentRowColor();
    const QColor seed = (!cur.isEmpty() && QColor(cur).isValid()) ? QColor(cur)
                                                                  : QColor(DEFAULT_ACCENT_HEX);
    // Raised from the row's menu, so it flies like every other window that menu opens: out of the
    // pressed row, back into the chip. Rows are delegate-painted, so both ends are global rects.
    const QRect rowRect(list_->viewport()->mapToGlobal(list_->visualItemRect(it).topLeft()),
                        list_->visualItemRect(it).size());
    const QRect from = hover_.menuKebabRect.isValid() ? support::gestureAnchorRect() : rowRect;
    const QColor picked =
        support::pickColorAnimated(seed, this, "Project name color", nullptr, from,
                                   {}, false, hover_.menuKebabRect);
    if (!picked.isValid()) return;   // cancelled
    emitSetColor(it, picked.name());
  }

  void ProjectsDialog::clearColorSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    emitSetColor(it, QString());   // clear → theme default
  }

  void ProjectsDialog::createBlank() {
    action_ = Action::NEW_BLANK;
    accept();
  }

  void ProjectsDialog::createNew() {
    const QString seed = QString::fromStdString(loadedNameStore(projects_)->defaultName());
    const auto name = promptValidatedName(this, "New Project", seed, QString(), projects_);
    if (!name) return;
    newName_ = *name;
    action_ = Action::NEW;
    accept();
  }

}  // namespace stencil::gui
