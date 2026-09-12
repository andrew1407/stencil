#include "searchCombo.hpp"
#include "searchComboParts.hpp"
#include "menuReveal.hpp"       // support::revealPopup / dismissPopup — the shared surface dust
#include "rowHoverSlide.hpp"    // the hovered row eases 2px right (browser .accent-dd-opt:hover)
#include "shimmerOverlay.hpp"   // …and takes the app's glass sweep with it

#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QScreen>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QVBoxLayout>

namespace stencil::gui {

  // Below the trigger, at least trigger-wide, tall enough for the visible rows
  // up to the browser's 280px cap; flipped above when the screen runs out.
  void SearchComboBox::positionPopup() {
    const int rows = proxy_->rowCount();
    const int rowH = rows > 0 ? list_->sizeHintForRow(0) : 0;
    const int chromeH = kPopupPadding * 2 +
                        (search_ ? search_->parentWidget()->sizeHint().height() + kPopupPadding : 0);
    const int bodyH = rows > 0 ? rowH * rows + 2 * list_->frameWidth()
                               : noMatch_->sizeHint().height();
    const int h = qMin(kMaxPopupHeight, chromeH + bodyH);
    const int w = qMax(width(), list_->sizeHintForColumn(0) + kPopupPadding * 4 +
                                    list_->verticalScrollBar()->sizeHint().width());
    QPoint pos = mapToGlobal(QPoint(0, height() + 2));
    if (QScreen* scr = screen()) {
      const QRect avail = scr->availableGeometry();
      if (pos.y() + h > avail.bottom())
        pos.setY(mapToGlobal(QPoint(0, 0)).y() - h - 2);  // open upward
      pos.setX(qBound(avail.left(), pos.x(), avail.right() - w));
    }
    popup_->setGeometry(QRect(pos, QSize(w, h)));
  }

  // Each open starts like the browser's: empty query, every row visible, the
  // current item highlighted, focus in the search field.
  void SearchComboBox::setListDelegate(QAbstractItemDelegate* delegate) {
    delegate_ = delegate;
    if (!list_) return;
    list_->setItemDelegate(delegate);
    installRowHoverSlide(list_);   // …re-wrapped around the new painter, never stacked
  }

  QListView* SearchComboBox::popupList() {
    ensurePopup();
    return list_;
  }

  void SearchComboBox::showPopup() {
    if (popup_ && popup_->isVisible()) {  // trigger acts as a toggle
      hidePopup();
      return;
    }
    // The outside-press that closed the popup also lands on the trigger and
    // would reopen it here — treat that press as "toggle closed" instead.
    if (lastHide_.isValid() && lastHide_.elapsed() < 150) return;
    ensurePopup();
    if (search_) {
      const QSignalBlocker block(search_);
      search_->clear();
    }
    static_cast<LabelValueFilterProxy*>(proxy_)->setQuery(QString());
    list_->show();
    noMatch_->hide();
    const QModelIndex src = model()->index(currentIndex(), modelColumn());
    const QModelIndex cur = proxy_->mapFromSource(src);
    list_->setCurrentIndex(cur);
    positionPopup();
    popup_->show();
    // 1.5x the menu clock — a select reads slower next to the browser's.
    support::revealPopup(*popup_, this, support::kSelectPopupDustMs);
    if (cur.isValid()) list_->scrollTo(cur, QAbstractItemView::PositionAtCenter);
    (search_ ? static_cast<QWidget*>(search_) : static_cast<QWidget*>(list_))
        ->setFocus(Qt::PopupFocusReason);
  }

  void SearchComboBox::hidePopup() {
    // The dust plays off popup_'s own Hide event below — the one place every close path
    // (a pick, Escape, or Qt's own Qt::Popup grab-loss on an outside click) funnels through.
    if (popup_) popup_->hide();
    QComboBox::hidePopup();
  }

  bool SearchComboBox::eventFilter(QObject* watched, QEvent* event) {
    if (list_ && watched == list_->viewport() && event->type() == QEvent::Leave)
      restorePreview();   // pointer left the rows while the popup is still up
    if (watched == popup_ && event->type() == QEvent::Hide) {
      restorePreview();   // however it closed without a pick, revert to the committed value
      lastHide_.start();
      // grab() still renders a hidden widget: an outside click hides popup_ via Qt's own
      // grab-loss handling, which never calls hidePopup() above, so the flight has to hang
      // off this Hide event instead.
      support::dismissPopup(*popup_, this, support::kSelectPopupDustMs);
    }
    if ((watched == search_ || (!searchable_ && watched == list_)) &&
        event->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(event);
      switch (ke->key()) {
        case Qt::Key_Down:
          moveHighlight(+1);
          return true;
        case Qt::Key_Up:
          moveHighlight(-1);
          return true;
        case Qt::Key_PageDown:
          moveHighlight(+8);
          return true;
        case Qt::Key_PageUp:
          moveHighlight(-8);
          return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
          if (list_->currentIndex().isValid())
            choose(list_->currentIndex().row());
          else
            hidePopup();
          return true;
        case Qt::Key_Escape:
          hidePopup();
          return true;
        default:
          break;
      }
    }
    return QComboBox::eventFilter(watched, event);
  }
}

