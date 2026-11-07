#include "ConnectDialog.hpp"
#include "../../support/control/reveal/modeGhost.hpp"
#include <QVariantAnimation>
#include "connectDialogParts.hpp"
#include "../../support/motion/scrollReveal.hpp"             // revealDissolve (scroll edge fade)
#include "../../support/motion/DisintegrateOverlay.hpp"  // disconnected rows come apart
#include "../../support/motion/DissolveEffect.hpp"       // scroll-edge grain dissolve
#include "../../support/theme/filterFade.hpp"           // filtered-out rows fade + collapse
#include "../../support/modal/modalChrome.hpp"          // the browser modal shell
#include "../../support/modal/modalReveal.hpp"          // motionReduced()
#include "../../support/control/reveal/controlReveal.hpp"     // the batch bar comes and goes as sand

#include "connectionStore.hpp"
#include "iconSet.hpp"
#include "theme.hpp"   // infoBackground: the browser's --bg-info, for the row hover

#include <QCheckBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPalette>
#include <QPointer>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSize>
#include <QTimer>

#include <algorithm>

namespace stencil::gui {

  void ConnectDialog::doConnect() {
    if (!manager) return;
    const QString url = urlEdit->text().trimmed();
    if (url.isEmpty()) {
      emit toast(tr("Enter a server URL"), true);
      return;
    }
    QPointer<ConnectDialog> self(this);
    manager->connectToAsync(url, tokenEdit->text().trimmed(), [this, self, url](bool ok, QString err) {
      if (!self) return;
      // A refused CREDENTIAL still leaves a row (EXPIRED + Reconnect), so its fields are cleared -
      // leaving them typed invites adding the server twice. An attempt that left NOTHING keeps its text.
      if (!ok) emit toast(tr("Could not connect — %1").arg(err), true);
      if (ok || manager->find(url)) {
        urlEdit->clear();
        tokenEdit->clear();
      }
      rebuildList();
    });
    rebuildList();
  }

  void ConnectDialog::setSyncToServer(bool on) {
    if (!syncToServer || syncToServer->isChecked() == on) return;
    QSignalBlocker block(syncToServer);   // seeding, not a toggle
    syncToServer->setChecked(on);
  }

  void ConnectDialog::scatterRows(const QStringList& urls) {
    if (!manager || !list || urls.isEmpty()) return;
    if (support::motionReduced()) return;        // no dust → removal finalizes at once
    const QStringList all = manager->urls();   // rows are built in this order
    // Shared mote budget across the rows leaving together (projectsDialog says why).
    const int budget = std::max<int>(1, DisintegrateOverlay::DUST_MAX_CELLS / int(urls.size()));
    QStringList doomedNow;
    for (const QString& u : urls) {
      const int row = all.indexOf(u);
      QListWidgetItem* it = row >= 0 ? list->item(row) : nullptr;
      if (!it) continue;
      const QRect r = list->visualItemRect(it);
      // Sliding / Transition: the row's picture slides out or shrinks away in place of the dust.
      if (!DisintegrateOverlay::overRect(list->viewport(), r, this,
                                         DisintegrateOverlay::Sweep::ROWS, /*dust=*/true, budget,
                                         DisintegrateOverlay::CONN_MS,
                                         list->palette().color(QPalette::Text))   // lifted to the row's ink
          && !playModeGhost(list->viewport(), list->viewport()->grab(r), r, list->viewport()->rect(),
                            /*in=*/false, Qt::Horizontal, MODE_ROW_MS))
        continue;   // nothing to animate (hidden/tiny) → this row just removes instantly
      // Retire the row at once (projectsDialog::retireRow parity): the snapshot is what
      // flies, so the real row blanks and its empty slot is held until the dust settles.
      list->removeItemWidget(it);
      it->setFlags(Qt::NoItemFlags);
      if (!support::isDustAllowed()) closeSlot(it, support::motionMs(MODE_ROW_MS));
      selected.remove(u);   // …and it stops counting towards the bar with its own dust
      doomed.insert(u);
      doomedNow.append(u);
    }
    if (doomedNow.isEmpty()) return;
    // Finalize when the dust settles: slots collapse and (only now) the empty state may
    // appear. Dies with the dialog — done() covers an early close.
    updateBatchBar();   // the count and the buttons come apart WITH the rows, not after
    const int hold = support::isDustAllowed() ? DisintegrateOverlay::CONN_MS : MODE_ROW_MS;
    QTimer::singleShot(support::motionMs(hold), this, [this, doomedNow] {
      for (const QString& u : doomedNow) doomed.remove(u);
      if (doomed.isEmpty()) rebuildList();
    });
  }

  // Sliding / Transition: the emptied slot shuts over the slide's second half (browser rowSlideLeave's max-height).
  void ConnectDialog::closeSlot(QListWidgetItem* it, int ms) {
    const QSize full = it->sizeHint();
    QPersistentModelIndex idx(list->indexFromItem(it));
    auto* anim = new QVariantAnimation(this);
    anim->setDuration(ms);
    anim->setKeyValueAt(0.0, 1.0);
    anim->setKeyValueAt(0.55, 1.0);
    anim->setKeyValueAt(1.0, 0.0);
    QObject::connect(anim, &QVariantAnimation::valueChanged, this, [this, idx, full](const QVariant& v) {
      if (QListWidgetItem* row = idx.isValid() ? list->item(idx.row()) : nullptr)
        row->setSizeHint(QSize(full.width(), std::max(0, int(full.height() * v.toDouble()))));
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

  void ConnectDialog::done(int r) {
    if (!doomed.isEmpty()) {   // close beat the dust — finalize pending removals now
      doomed.clear();
      rebuildList();
    }
    // …and the same for a filter fade: nothing half-faded survives into the close flight.
    if (filterFade) filterFade->finishNow();
    QDialog::done(r);
  }

  // The viewport minus the list's spacing, which QListView adds on BOTH sides of an
  // item — a viewport-wide slot overhung the right edge and cut the outline off.
  int ConnectDialog::rowWidth() const {
    if (!list) return 0;
    return std::max(0, list->viewport()->width() - 2 * list->spacing());
  }

  // Fade rows at the list's top/bottom edges instead of cutting them mid-outline — the
  // widget twin of ProjectRowDelegate's dissolve (support/motion/scrollReveal.hpp).
  void ConnectDialog::applyRowReveal() {
    if (!list) return;
    QWidget* vp = list->viewport();
    const int viewH = vp->height();
    QScrollBar* sb = list->verticalScrollBar();
    const bool scrollable = sb && sb->maximum() > 0;   // nothing to scroll → no edges
    for (int i = 0; i < list->count(); ++i) {
      QWidget* w = list->itemWidget(list->item(i));
      if (!w || w->isHidden()) continue;
      // A row mid-filter-fade owns its own opacity effect; two writers would flicker.
      if (w->property(FILTER_FADE_PROPERTY).toBool()) continue;
      const int top = w->mapTo(vp, QPoint(0, 0)).y();
      const int h = w->height();
      const double d = scrollable ? revealDissolve(top, top + h, viewH) : 0.0;
      auto* fx = dynamic_cast<DissolveEffect*>(w->graphicsEffect());
      if (!fx) {
        if (d <= 0.0) continue;   // whole — don't allocate an effect to say so
        fx = new DissolveEffect(w);
        w->setGraphicsEffect(fx);
      }
      fx->setVisibleSpan(h > 0 ? std::clamp(double(-top) / h, 0.0, 1.0) : 0.0,
                         h > 0 ? std::clamp(double(viewH - top) / h, 0.0, 1.0) : 1.0);
      fx->setDissolve(d);
    }
  }

  // Return connects from wherever the focus is (browser twin: modal.js keydown on both
  // fields). Handled HERE because removing a row can leave the dialog with no focus widget at all.
  void ConnectDialog::keyPressEvent(QKeyEvent* e) {
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
        !(e->modifiers() & ~Qt::KeypadModifier)) {
      doConnect();
      e->accept();
      return;
    }
    QDialog::keyPressEvent(e);   // Escape still rejects
  }

  bool ConnectDialog::eventFilter(QObject* watched, QEvent* event) {
    if (list && watched == list->viewport() && event->type() == QEvent::Resize) {
      // Re-cap every row to the new viewport width (the URL label re-elides itself) —
      // same intent as the projects dialog's delegate sizeHint cap.
      const int w = rowWidth();
      for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->sizeHint().isValid())
          list->item(i)->setSizeHint(QSize(w, list->item(i)->sizeHint().height()));
      applyRowReveal();
    }
    return QDialog::eventFilter(watched, event);
  }
}  // namespace stencil::gui

