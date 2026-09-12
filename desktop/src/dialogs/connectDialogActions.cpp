#include "connectDialog.hpp"
#include "connectDialogParts.hpp"
#include "../support/scrollReveal.hpp"             // revealDissolve (scroll edge fade)
#include "../support/disintegrateOverlay.hpp"  // disconnected rows come apart
#include "../support/dissolveEffect.hpp"       // scroll-edge grain dissolve
#include "../support/filterFade.hpp"           // filtered-out rows fade + collapse
#include "../support/flowLayout.hpp"           // the batch bar wraps, never clips
#include "../support/guiHelpers.hpp"           // confirmYesNo()
#include "../support/modalChrome.hpp"          // the browser modal shell
#include "../support/modalReveal.hpp"          // motionReduced()
#include "../support/controlReveal.hpp"     // the batch bar comes and goes as sand
#include "../support/shimmerOverlay.hpp"       // the row's glass hover sweep

#include "connectionStore.hpp"
#include "iconSet.hpp"
#include "theme.hpp"   // infoBackground: the browser's --bg-info, for the row hover
#include "reorderableListWidget.hpp"
#include "searchCombo.hpp"
#include "serverClient.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QClipboard>
#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QColor>
#include <QGuiApplication>
#include <QFont>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace stencil::gui {

  void ConnectDialog::doConnect() {
    if (!manager_) return;
    const QString url = urlEdit_->text().trimmed();
    if (url.isEmpty()) {
      emit toast(tr("Enter a server URL"), true);
      return;
    }
    QPointer<ConnectDialog> self(this);
    manager_->connectToAsync(url, tokenEdit_->text().trimmed(), [this, self, url](bool ok, QString err) {
      if (!self) return;
      // A refused CREDENTIAL still leaves a row behind (the client is kept at
      // Status::EXPIRED, with a Reconnect on it), so the fields that put it there are done
      // — leaving them typed in invites adding the same server twice. An attempt that left
      // NOTHING keeps its text, so a typo can be corrected where it was made.
      if (!ok) emit toast(tr("Could not connect — %1").arg(err), true);
      if (ok || manager_->find(url)) {
        urlEdit_->clear();
        tokenEdit_->clear();
      }
      rebuildList();
    });
    rebuildList();
  }

  void ConnectDialog::setSyncToServer(bool on) {
    if (!syncToServer_ || syncToServer_->isChecked() == on) return;
    QSignalBlocker block(syncToServer_);   // seeding, not a toggle
    syncToServer_->setChecked(on);
  }

  void ConnectDialog::scatterRows(const QStringList& urls) {
    if (!manager_ || !list_ || urls.isEmpty()) return;
    if (support::motionReduced()) return;        // no dust → removal finalizes at once
    const QStringList all = manager_->urls();   // rows are built in this order
    // Shared mote budget across the rows leaving together (projectsDialog says why).
    const int budget = std::max<int>(1, DisintegrateOverlay::DUST_MAX_CELLS / int(urls.size()));
    QStringList doomedNow;
    for (const QString& u : urls) {
      const int row = all.indexOf(u);
      QListWidgetItem* it = row >= 0 ? list_->item(row) : nullptr;
      if (!it) continue;
      if (!DisintegrateOverlay::overRect(list_->viewport(), list_->visualItemRect(it), this,
                                         DisintegrateOverlay::Sweep::ROWS, /*dust=*/true, budget,
                                         DisintegrateOverlay::CONN_MS,
                                         list_->palette().color(QPalette::Text)))   // lifted to the row's ink
        continue;   // nothing to animate (hidden/tiny) → this row just removes instantly
      // Retire the row at once (projectsDialog::retireRow parity): the snapshot is what
      // flies, so the real row blanks and its empty slot is held until the dust settles.
      list_->removeItemWidget(it);
      it->setFlags(Qt::NoItemFlags);
      selected_.remove(u);   // …and it stops counting towards the bar with its own dust
      doomed_.insert(u);
      doomedNow.append(u);
    }
    if (doomedNow.isEmpty()) return;
    // Finalize when the dust settles: slots collapse and (only now) the empty state may
    // appear. Dies with the dialog — done() covers an early close.
    updateBatchBar();   // the count and the buttons come apart WITH the rows, not after
    QTimer::singleShot(DisintegrateOverlay::CONN_MS, this, [this, doomedNow] {
      for (const QString& u : doomedNow) doomed_.remove(u);
      if (doomed_.isEmpty()) rebuildList();
    });
  }

  void ConnectDialog::done(int r) {
    if (!doomed_.isEmpty()) {   // close beat the dust — finalize pending removals now
      doomed_.clear();
      rebuildList();
    }
    // …and the same for a filter fade: nothing half-faded survives into the close flight.
    if (filterFade_) filterFade_->finishNow();
    stopDustClouds(this);   // nothing may still be flying when the close flight photographs
    QDialog::done(r);
  }

  // The viewport minus the list's spacing, which QListView adds on BOTH sides of an
  // item — a viewport-wide slot overhung the right edge and cut the outline off.
  int ConnectDialog::rowWidth() const {
    if (!list_) return 0;
    return std::max(0, list_->viewport()->width() - 2 * list_->spacing());
  }

  // Fade rows at the list's top/bottom edges instead of cutting them mid-outline — the
  // widget twin of ProjectRowDelegate's dissolve (app/scrollReveal.hpp).
  void ConnectDialog::applyRowReveal() {
    if (!list_) return;
    QWidget* vp = list_->viewport();
    const int viewH = vp->height();
    QScrollBar* sb = list_->verticalScrollBar();
    const bool scrollable = sb && sb->maximum() > 0;   // nothing to scroll → no edges
    for (int i = 0; i < list_->count(); ++i) {
      QWidget* w = list_->itemWidget(list_->item(i));
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

  // Return connects, from wherever the focus is (browser twin: connectModal.js wires
  // keydown on both fields). Handled HERE rather than on the fields: removing a row can
  // leave the dialog with no focus widget at all (the trash button that had it died with
  // its row), and QDialog's own default-button path needs a button still carrying the
  // default flag, which autoDefault juggling takes away. Anything that genuinely wants
  // Return accepts it first, so reaching here means nothing else claimed it.
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
    if (list_ && watched == list_->viewport() && event->type() == QEvent::Resize) {
      // Re-cap every row to the new viewport width (the URL label re-elides itself) —
      // same intent as the projects dialog's delegate sizeHint cap.
      const int w = rowWidth();
      for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->sizeHint().isValid())
          list_->item(i)->setSizeHint(QSize(w, list_->item(i)->sizeHint().height()));
      applyRowReveal();
    }
    return QDialog::eventFilter(watched, event);
  }
}  // namespace stencil::gui

