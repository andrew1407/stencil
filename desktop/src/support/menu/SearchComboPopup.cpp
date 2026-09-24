#include "SearchCombo.hpp"
#include "searchComboParts.hpp"
#include "menuReveal.hpp"       // support::revealPopup / dismissPopup — the shared surface dust
#include "RowHoverSlide.hpp"    // the hovered row eases 2px right (browser .accent-dd-opt:hover)
#include "ShimmerOverlay.hpp"   // …and takes the app's glass sweep with it

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

  // Below the trigger, up to the browser's 280px cap; flipped above when the screen runs out.
  void SearchComboBox::positionPopup() {
    const int rows = proxy->rowCount();
    const int rowH = rows > 0 ? list->sizeHintForRow(0) : 0;
    const int chromeH = POPUP_PADDING * 2 +
                        (search ? search->parentWidget()->sizeHint().height() + POPUP_PADDING : 0);
    const int bodyH = rows > 0 ? rowH * rows + 2 * list->frameWidth()
                               : noMatch->sizeHint().height();
    const int h = qMin(MAX_POPUP_HEIGHT, chromeH + bodyH);
    // The bar's width is reserved only when the rows actually overflow; on a two-item picker
    // ("cm"/"in") that unconditional reserve made the popup visibly wider than its own trigger.
    const bool scrolls = chromeH + bodyH > MAX_POPUP_HEIGHT;
    const int w = qMax(width(), list->sizeHintForColumn(0) + POPUP_PADDING * 4 +
                                    (scrolls ? list->verticalScrollBar()->sizeHint().width() : 0));
    QPoint pos = mapToGlobal(QPoint(0, height() + 2));
    if (QScreen* scr = screen()) {
      const QRect avail = scr->availableGeometry();
      if (pos.y() + h > avail.bottom())
        pos.setY(mapToGlobal(QPoint(0, 0)).y() - h - 2);  // open upward
      pos.setX(qBound(avail.left(), pos.x(), avail.right() - w));
    }
    popup->setGeometry(QRect(pos, QSize(w, h)));
  }

  // Each open starts like the browser's: empty query, current item highlighted, focus in the search.
  void SearchComboBox::setListDelegate(QAbstractItemDelegate* delegate) {
    this->delegate = delegate;
    if (!list) return;
    list->setItemDelegate(delegate);
    installRowHoverSlide(list);   // …re-wrapped around the new painter, never stacked
  }

  QListView* SearchComboBox::popupList() {
    ensurePopup();
    return list;
  }

  void SearchComboBox::showPopup() {
    if (popup && popup->isVisible()) {  // trigger acts as a toggle
      hidePopup();
      return;
    }
    // The outside-press that closed the popup also lands on the trigger: treat it as "toggle closed".
    if (lastHide.isValid() && lastHide.elapsed() < 150) return;
    ensurePopup();
    if (search) {
      const QSignalBlocker block(search);
      search->clear();
    }
    static_cast<LabelValueFilterProxy*>(proxy)->setQuery(QString());
    list->show();
    noMatch->hide();
    const QModelIndex src = model()->index(currentIndex(), modelColumn());
    const QModelIndex cur = proxy->mapFromSource(src);
    list->setCurrentIndex(cur);
    positionPopup();
    popup->show();
    // 1.5x the menu clock.
    support::revealPopup(*popup, this, support::SELECT_POPUP_DUST_MS);
    if (cur.isValid()) list->scrollTo(cur, QAbstractItemView::PositionAtCenter);
    (search ? static_cast<QWidget*>(search) : static_cast<QWidget*>(list))
        ->setFocus(Qt::PopupFocusReason);
  }

  void SearchComboBox::hidePopup() {
    // The dust hangs off popup's Hide event — where every close path funnels through.
    if (popup) popup->hide();
    QComboBox::hidePopup();
  }

  bool SearchComboBox::eventFilter(QObject* watched, QEvent* event) {
    if (list && watched == list->viewport() && event->type() == QEvent::Leave)
      restorePreview();   // pointer left the rows while the popup is still up
    if (watched == popup && event->type() == QEvent::Hide) {
      restorePreview();   // however it closed without a pick, revert to the committed value
      lastHide.start();
      // An outside click hides popup via Qt's grab-loss handling, which never calls hidePopup().
      support::dismissPopup(*popup, this, support::SELECT_POPUP_DUST_MS);
    }
    if ((watched == search || (!searchable && watched == list)) &&
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
          if (list->currentIndex().isValid())
            choose(list->currentIndex().row());
          else
            hidePopup();
          return true;
        case Qt::Key_Escape:
          hidePopup();
          return true;
        default:
          // An editable trigger keeps typing: the key closes the list and lands in the field.
          if (isEditable() && !searchable && !ke->text().isEmpty()) {
            hidePopup();
            lineEdit()->setFocus(Qt::OtherFocusReason);
            lineEdit()->event(event);
            return true;
          }
          break;
      }
    }
    return QComboBox::eventFilter(watched, event);
  }
}

