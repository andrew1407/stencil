#include "ProjectsDialog.hpp"

#include "accentDefaults.hpp"
#include "projectsRowChrome.hpp"
#include "../../../support/motion/DisintegrateOverlay.hpp"
#include "../../../support/motionPrefs.hpp"   // support::isDustAllowed()
#include "../../../support/control/reveal/modeGhost.hpp"
#include "../../../support/control/reveal/edgeFlight.hpp"
#include <QVariantAnimation>
#include "../../../support/theme/filterFade.hpp"
#include "../../../support/modal/modalReveal.hpp"

#include <QPalette>
#include <QTimer>
#include <algorithm>

// Retiring rows, and the colour a row carries.

namespace stencil::gui {

  namespace {
    // A dusted row's slot closes while its ash still falls: browser enterLeave.js LEAVE_MS (motion.json
    // ui.LEAVE_MS) and css rowLeave, so the rows below slide up rather than jump when the item goes.
    constexpr int DUST_SLOT_MS = 220;
  }  // namespace

  void ProjectsDialog::scatterRows(const QSet<QString>& keys) {
    if (!list) return;
    // A copy: retireRow prunes batch.checked below and the batch removal hands batch.checked in as `keys`, so
    // this must not iterate a set the loop is emptying. (Implicit sharing keeps it free.)
    const QSet<QString> want = keys;
    QList<QListWidgetItem*> doomed;
    QList<QRect> rects;   // the on-screen slice of each doomed row (scrolled-out rows: none)
    // Clip each row's rect to the viewport: a checked row scrolled out of view must not drop its
    // overlay onto the dialog chrome, nor spend the shared mote budget on pixels nobody can see.
    const QRect view = list->viewport()->rect();
    for (int i = 0; i < list->count(); ++i) {
      const QString key = rowKeyAt(i);
      if (key.isEmpty() || (!want.isEmpty() && !want.contains(key))) continue;
      QListWidgetItem* it = list->item(i);
      if (!it || it->isHidden()) continue;
      doomed.append(it);
      const QRect r = list->visualItemRect(it).intersected(view);
      if (r.width() >= 8 && r.height() >= 8) rects.append(r);
    }
    if (doomed.isEmpty()) return;
    // Every row scatters at once and all repaint each frame, so the mote budget is SHARED - a mass
    // removal coarsens each (browser: scatterGridFor). Only rows that actually animate share it.
    const int budget =
        std::max<int>(1, DisintegrateOverlay::DUST_MAX_CELLS / std::max(1, int(rects.size())));
    // Overlays FIRST (they snapshot the still-painted rows), then retire the lot.
    for (const QRect& r : rects)
      DisintegrateOverlay::overRect(list->viewport(), r, this,
                                    DisintegrateOverlay::Sweep::ROWS, /*dust=*/true, budget,
                                    DisintegrateOverlay::ITEM_MS,   // a card is read, not glanced at
                                    list->palette().color(QPalette::Text));
    // Sliding / Transition: each row's picture leaves by the list's left edge, poured or slid (browser rowLeave).
    if (!support::isDustAllowed())
      for (const QRect& r : rects) edgeAway(list->viewport(), list->viewport()->grab(r), r);
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
    if (filterFade) filterFade->finishNow();
    hideHoverPreview();   // the doomed-row sweep below may delete whatever it points to
    closeInlineRename();
    for (int i = list->count() - 1; i >= 0; --i)
      if (list->item(i)->data(DOOMED_ROLE).toBool()) delete list->takeItem(i);
    QDialog::done(result);
  }

  // The scatter animates a SNAPSHOT - blank the real row as it starts, hold the empty slot while
  // the dust falls, then let the item go (browser parity: leaveThenRemove + beginRemoval).
  void ProjectsDialog::retireRow(QListWidgetItem* it) {
    if (!it || it->data(Qt::UserRole).isNull()) return;
    const QString key = rowKeyAt(list->row(it));
    it->setData(DOOMED_ROLE, true);   // the delegate paints nothing for it
    it->setFlags(Qt::NoItemFlags);    // no select/check mid-flight
    batch.checked.remove(key);             // …and it stops counting towards the selection bar
    // Nothing flew, so there is no dust to outlive: the slot goes with the row (ConnectDialog
    // parity, browser wipeDurationMs). Held open, an empty gap sat there for a second and a half.
    const bool dust = support::isDustAllowed();
    int hold = 0;
    if (dust) hold = support::motionMs(DisintegrateOverlay::ITEM_MS);
    else if (modeGhostMotion()) hold = support::motionMs(edgeFlightMs());
    // The open project's top row keeps its slot for the pinned temporary row the owner's rebuild brings (refresh's
    // restoreDoomed); the timer is only the fallback for a rebuild that never comes (browser renderList.js inPlace).
    const bool inPlace = !temporary && !activeProjectId.isEmpty() && key == QLatin1Char('|') + activeProjectId
                         && list->row(it) == 0;
    if (inPlace) hold = 2 * std::max(hold, support::motionMs(DisintegrateOverlay::DUST_MS)) + 1000;
    // Sliding / Transition: the slot closes over the slide's second half (browser rowSlideLeave's max-height).
    else if (dust) closeSlot(it, support::motionMs(DUST_SLOT_MS));
    else if (modeGhostMotion()) closeSlot(it, hold);
    QTimer::singleShot(hold, this, [this, key] {
      // Re-found by key: a re-list may have rebuilt the rows (fresh ones aren't doomed).
      for (int i = 0; i < list->count(); ++i)
        if (rowKeyAt(i) == key && list->item(i)->data(DOOMED_ROLE).toBool()) {
          // The hover preview may still be pointing at the very row about to go.
          if (list->item(i) == hover.hoverItem) hideHoverPreview();
          delete list->takeItem(i);
          break;
        }
    });
  }

  // The delegate sizes a row by its filter presence (ProjectRowDelegate::sizeHint), so the slot closes on that.
  // By key, not index: a rebuild carries a doomed row over (refresh), which a persistent index does not survive.
  void ProjectsDialog::closeSlot(QListWidgetItem* it, int ms) {
    const QString key = rowKeyAt(list->row(it));
    auto* anim = new QVariantAnimation(this);
    anim->setDuration(ms);
    anim->setKeyValueAt(0.0, 1.0);
    anim->setKeyValueAt(0.55, 1.0);
    anim->setKeyValueAt(1.0, 0.0);
    QObject::connect(anim, &QVariantAnimation::valueChanged, this, [this, key](const QVariant& v) {
      QListWidgetItem* row = nullptr;
      for (int i = 0; i < list->count() && !row; ++i)
        if (rowKeyAt(i) == key && list->item(i)->data(DOOMED_ROLE).toBool()) row = list->item(i);
      if (!row) return;
      building = true;   // our frame, not a check-state edit
      row->setData(FILTER_PRESENCE_ROLE, v.toDouble());
      // Closed, it gives back its list spacing too, or the rows below jump that gap at the rebuild.
      if (v.toDouble() <= 0.0) row->setHidden(true);
      building = false;
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

  QString ProjectsDialog::rowKeyAt(int i) const {
    QListWidgetItem* it = list->item(i);
    if (!it || it->data(Qt::UserRole).isNull()) return {};  // placeholder / non-data row
    return it->data(Qt::UserRole + 1).toString() + "|" + it->data(Qt::UserRole).toString();
  }

  QString ProjectsDialog::rowColor(const QListWidgetItem* it) const {
    if (!it || it->data(Qt::UserRole).isNull()) return {};
    const QString id = it->data(Qt::UserRole).toString();
    const QString server = it->data(Qt::UserRole + 1).toString();
    if (!server.isEmpty()) {  // server row → read the cached record
      for (const auto& sp : remote)
        if (sp.id == id && sp.serverUrl == server) return sp.color;
      return {};
    }
    for (const auto& p : projects)  // local row → read the project meta
      if (QString::fromStdString(p.meta.id) == id) return QString::fromStdString(p.meta.color);
    return {};
  }

  QString ProjectsDialog::currentRowColor() const { return rowColor(list->currentItem()); }

  // Resolve the selected row's (id, serverUrl) and emit a SetColor action with `color`
  // ("" = clear to the theme default). Shared by the set / clear colour menu entries.
  void ProjectsDialog::emitSetColor(QListWidgetItem* it, const QString& color) {
    selectedId = it->data(Qt::UserRole).toString();
    selectedServerUrl = it->data(Qt::UserRole + 1).toString();
    selectedColor = color;
    action = Action::SET_COLOR;
    accept();
  }

  void ProjectsDialog::setColorSelected() {
    auto* it = list->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    const QString cur = currentRowColor();
    const QColor seed = (!cur.isEmpty() && QColor(cur).isValid()) ? QColor(cur)
                                                                  : QColor(DEFAULT_ACCENT_HEX);
    // Raised from the row's menu, so it flies like every other window that menu opens: out of the
    // pressed row, back into the chip. Rows are delegate-painted, so both ends are global rects.
    const QRect rowRect(list->viewport()->mapToGlobal(list->visualItemRect(it).topLeft()),
                        list->visualItemRect(it).size());
    const QRect from = hover.menuKebabRect.isValid() ? support::gestureAnchorRect() : rowRect;
    const QColor picked =
        support::pickColorAnimated(seed, this, "Project name color", nullptr, from,
                                   {}, false, hover.menuKebabRect);
    if (!picked.isValid()) return;   // cancelled
    emitSetColor(it, picked.name());
  }

  void ProjectsDialog::clearColorSelected() {
    auto* it = list->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    emitSetColor(it, QString());   // clear → theme default
  }

  // Stays open, as the browser's: the blank dialog grows out of the button over it.
  void ProjectsDialog::createBlank() {
    auto* btn = qobject_cast<QWidget*>(sender());
    emit blankRequested(btn ? QRect(btn->mapToGlobal(QPoint(0, 0)), btn->size()) : QRect());
  }

  void ProjectsDialog::createNew() {
    const QString seed = QString::fromStdString(loadedNameStore(projects)->defaultName());
    const auto name = promptValidatedName(this, "New Project", seed, QString(), projects);
    if (!name) return;
    newName = *name;
    action = Action::NEW;
    accept();
  }

}  // namespace stencil::gui
