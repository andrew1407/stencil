#pragma once
// A QListWidget whose rows can be drag-REORDERED and dragged OUT (dropped outside the
// list) to trigger a caller-supplied action — the desktop analogue of the browser
// connectModal/projectsModal HTML5-draggable rows. Rows may host itemWidgets, so we never
// let Qt move items itself: on an in-list drop we report (from,to) row indices and expect
// the caller to permute its model + rebuild the rows; on a drop outside the list we invoke
// onDragOut(row) so the caller can confirm-and-remove.
//
// Header-only and deliberately Q_OBJECT-free (it uses std::function callbacks, not signals),
// so it needs no MOC and can be included from multiple dialogs. Drags are initiated from a
// DragGrip handle placed in each row (itemWidgets otherwise swallow the press that would
// start a view-driven drag).
#include <QAbstractItemView>
#include <QByteArray>
#include <QCursor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QWidget>

#include <functional>

namespace stencil::gui {

  inline QString reorderRowMime() { return QStringLiteral("application/x-stencil-reorder-row"); }

  // A grab handle that starts a row drag once the pointer moves with the left button held.
  class DragGrip : public QLabel {
   public:
    explicit DragGrip(QWidget* parent = nullptr) : QLabel(parent) {
      setText(QStringLiteral("⋮⋮"));  // ⋮⋮
      setCursor(Qt::OpenHandCursor);
      setToolTip(QObject::tr("Drag to reorder · drag out of the window to remove"));
    }
    std::function<void()> onDrag;

   protected:
    void mousePressEvent(QMouseEvent* e) override {
      if (e->button() == Qt::LeftButton) armed_ = true;
      QLabel::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
      if (armed_ && (e->buttons() & Qt::LeftButton)) {
        armed_ = false;
        if (onDrag) onDrag();
      }
      QLabel::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
      armed_ = false;
      QLabel::mouseReleaseEvent(e);
    }

   private:
    bool armed_ = false;
  };

  class ReorderableListWidget : public QListWidget {
   public:
    explicit ReorderableListWidget(QWidget* parent = nullptr) : QListWidget(parent) {
      setAcceptDrops(true);
      viewport()->setAcceptDrops(true);
      setDropIndicatorShown(true);
      setDefaultDropAction(Qt::MoveAction);
      setSelectionMode(QAbstractItemView::SingleSelection);
      // A bright insertion line showing WHERE a dragged row will land (Qt's built-in indicator
      // doesn't show for our manually-handled drag). A thin click-through child of the viewport.
      dropMarker_ = new QWidget(viewport());
      dropMarker_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
      dropMarker_->hide();
    }
    // In-list reorder: move the row at `from` to `to` (QList::move semantics on the model).
    std::function<void(int from, int to)> onReorder;
    // Row dropped OUTSIDE the list viewport (e.g. out of the dialog). The handler decides
    // whether the release point warrants a remove (e.g. only when outside the dialog frame).
    std::function<void(int row)> onDragOut;
    // Fired when a row drag STARTS (before the blocking drag loop) and ENDS (after it), so a
    // dialog can show/hide an out-of-dialog drop-zone overlay for the duration of the drag.
    std::function<void()> onDragStart;
    std::function<void()> onDragEnd;

    // Start a drag for the row at `from` (called by a row's DragGrip). Blocks in the drag
    // loop; on return, dispatches to onReorder (in-list drop) or onDragOut (dropped away).
    void beginRowDrag(int from) {
      if (from < 0 || from >= count()) return;
      auto* mime = new QMimeData;
      mime->setData(reorderRowMime(), QByteArray::number(from));
      auto* drag = new QDrag(this);
      drag->setMimeData(mime);
      // Use the row's rendered appearance as the drag image (a translucent "ghost"), like the
      // browser's dragged row — instead of macOS's default tiny placeholder rectangle.
      const QRect vr = visualItemRect(item(from));
      if (vr.isValid() && !vr.isEmpty()) {
        const QPixmap row = viewport()->grab(vr);
        QPixmap ghost(row.size());
        ghost.setDevicePixelRatio(row.devicePixelRatio());
        ghost.fill(Qt::transparent);
        QPainter gp(&ghost);
        gp.setOpacity(0.55);  // translucent drag ghost (a little more see-through)
        gp.drawPixmap(0, 0, row);
        gp.end();
        drag->setPixmap(ghost);
        // Hot spot = where the pointer grabbed the row, so the ghost tracks the cursor naturally.
        const QPoint inVp = viewport()->mapFromGlobal(QCursor::pos());
        drag->setHotSpot(QPoint(qBound(0, inVp.x() - vr.left(), vr.width()),
                                qBound(0, inVp.y() - vr.top(), vr.height())));
      }
      droppedInList_ = false;
      pendingFrom_ = from;
      pendingTo_ = from;
      if (onDragStart) onDragStart();
      drag->exec(Qt::MoveAction);
      hideDropMarker();
      if (onDragEnd) onDragEnd();
      if (droppedInList_) {
        if (pendingFrom_ != pendingTo_ && onReorder) onReorder(pendingFrom_, pendingTo_);
      } else if (onDragOut) {
        onDragOut(from);
      }
    }

   protected:
    // Position the insertion line at viewport-y `y` (the top or bottom edge of the target row).
    void positionDropMarker(int y) {
      if (!dropMarker_) return;
      dropMarker_->setStyleSheet(
          QStringLiteral("background:%1; border-radius:2px;").arg(palette().color(QPalette::Highlight).name()));
      dropMarker_->setGeometry(3, y - 2, viewport()->width() - 6, 4);
      dropMarker_->raise();
      dropMarker_->show();
    }
    void hideDropMarker() { if (dropMarker_) dropMarker_->hide(); }
    // The viewport-y where the insertion line should sit for a drop at `pos`.
    int dropMarkerY(const QPoint& pos) const {
      QListWidgetItem* tgt = const_cast<ReorderableListWidget*>(this)->itemAt(pos);
      if (tgt) {
        const QRect r = visualItemRect(tgt);
        return (pos.y() > r.center().y()) ? r.bottom() + 1 : r.top();
      }
      if (count() > 0) return visualItemRect(item(count() - 1)).bottom() + 1;
      return 0;
    }

    // View-initiated drag (used when setDragEnabled(true) and rows are delegate-painted, i.e.
    // no DragGrip): route through the same beginRowDrag path as the grip. Lists that drive
    // drags from a grip leave dragEnabled false, so this never fires for them.
    void startDrag(Qt::DropActions) override { beginRowDrag(currentRow()); }
    void dragEnterEvent(QDragEnterEvent* e) override {
      if (e->mimeData()->hasFormat(reorderRowMime())) { e->setDropAction(Qt::MoveAction); e->accept(); }
      else QListWidget::dragEnterEvent(e);
    }
    void dragMoveEvent(QDragMoveEvent* e) override {
      if (e->mimeData()->hasFormat(reorderRowMime())) {
        e->setDropAction(Qt::MoveAction);
        e->accept();
        positionDropMarker(dropMarkerY(e->position().toPoint()));  // show the insertion line
      } else {
        QListWidget::dragMoveEvent(e);
      }
    }
    void dragLeaveEvent(QDragLeaveEvent* e) override {
      hideDropMarker();
      QListWidget::dragLeaveEvent(e);
    }
    void dropEvent(QDropEvent* e) override {
      hideDropMarker();
      if (!e->mimeData()->hasFormat(reorderRowMime())) { QListWidget::dropEvent(e); return; }
      // Defer the actual model mutation to beginRowDrag (after the drag loop unwinds) so we
      // don't rebuild the list from inside its own drop event.
      droppedInList_ = true;
      const int from = e->mimeData()->data(reorderRowMime()).toInt();
      const QPoint pos = e->position().toPoint();
      QListWidgetItem* tgt = itemAt(pos);
      int to;
      if (!tgt) {
        to = count() - 1;  // released in empty space below the rows → move to the end
      } else {
        to = row(tgt);
        const QRect r = visualItemRect(tgt);
        int insertAt = (pos.y() > r.center().y()) ? to + 1 : to;  // below the midpoint → after
        if (from < insertAt) insertAt -= 1;  // removing `from` first shifts higher indices down
        to = insertAt;
      }
      if (to < 0) to = 0;
      if (to >= count()) to = count() - 1;
      pendingFrom_ = from;
      pendingTo_ = to;
      e->setDropAction(Qt::MoveAction);
      e->accept();
    }

   private:
    bool droppedInList_ = false;
    int pendingFrom_ = -1;
    int pendingTo_ = -1;
    QWidget* dropMarker_ = nullptr;
  };

}  // namespace stencil::gui
