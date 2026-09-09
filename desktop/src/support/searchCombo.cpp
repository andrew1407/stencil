#include "searchCombo.hpp"
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

  namespace {

    // Browser parity: .accent-dd-menu caps at max-height: 280px.
    constexpr int kMaxPopupHeight = 280;
    // .accent-dd-menu padding: 4px (also the search-row → list gap).
    constexpr int kPopupPadding = 4;

    // rowMatches(label + value, query): case-insensitive substring over the
    // display label AND the canonical item DATA ("A4"/"custom"), so "a4"
    // matches whatever unit the label is currently rendered in.
    class LabelValueFilterProxy : public QSortFilterProxyModel {
    public:
      using QSortFilterProxyModel::QSortFilterProxyModel;
      void setQuery(const QString& query) {
        query_ = query.trimmed();
        invalidate();  // re-run filterAcceptsRow (portable across Qt 6.x)
      }

    protected:
      bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        if (query_.isEmpty()) return true;
        const QModelIndex idx = sourceModel()->index(row, 0, parent);
        const QString hay = idx.data(Qt::DisplayRole).toString() + ' ' +
                            idx.data(Qt::UserRole).toString();
        return hay.contains(query_, Qt::CaseInsensitive);
      }

    private:
      QString query_;
    };

  }  // namespace

  SearchComboBox::SearchComboBox(QWidget* parent, bool searchable)
      : QComboBox(parent), searchable_(searchable) {
    setCursor(Qt::PointingHandCursor);   // browser parity: every selector is a pointer
  }

  // Built lazily so the model is already filled and themed when first opened.
  void SearchComboBox::ensurePopup() {
    if (popup_) return;

    // A translucent Qt::Popup shell so the styled inner frame's rounded corners
    // don't sit on an opaque window rectangle (same trick as QMenu's theming).
    popup_ = new QWidget(this, Qt::Popup | Qt::FramelessWindowHint |
                                   Qt::NoDropShadowWindowHint);
    popup_->setAttribute(Qt::WA_TranslucentBackground);
    popup_->installEventFilter(this);
    auto* shell = new QVBoxLayout(popup_);
    shell->setContentsMargins(0, 0, 0, 0);

    auto* frame = new QWidget(popup_);
    frame->setObjectName("searchComboPopup");
    frame->setAttribute(Qt::WA_StyledBackground);
    shell->addWidget(frame);

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(kPopupPadding, kPopupPadding, kPopupPadding,
                               kPopupPadding);
    layout->setSpacing(kPopupPadding);

    // Search row pinned on top, with the hairline divider the browser draws
    // under .accent-dd-search-row. A short list has none: three rows need no filter,
    // and a box over them would only be one more thing to dismiss.
    if (searchable_) {
      auto* searchRow = new QWidget(frame);
      searchRow->setObjectName("searchComboSearchRow");
      searchRow->setAttribute(Qt::WA_StyledBackground);
      auto* searchLayout = new QVBoxLayout(searchRow);
      searchLayout->setContentsMargins(kPopupPadding, kPopupPadding,
                                       kPopupPadding, kPopupPadding * 2);
      search_ = new QLineEdit(searchRow);
      search_->setObjectName("searchComboSearch");
      search_->setPlaceholderText(tr("Search…"));
      search_->setClearButtonEnabled(true);
      search_->installEventFilter(this);
      searchLayout->addWidget(search_);
      layout->addWidget(searchRow);
    }

    proxy_ = new LabelValueFilterProxy(this);
    proxy_->setSourceModel(model());

    list_ = new QListView(frame);
    list_->setObjectName("searchComboList");
    // No QFrame chrome, and no scroll-area MINIMUM: QAbstractScrollArea's
    // minimumSizeHint (~66px each way) outranked positionPopup()'s tight
    // geometry through the popup layout, leaving a blank band under a short
    // list's last option (the 2-row All/Local filter popup — user report).
    list_->setFrameShape(QFrame::NoFrame);
    list_->setMinimumSize(1, 1);
    list_->setModel(proxy_);
    list_->setUniformItemSizes(true);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Hover-highlight rows like .accent-dd-opt:hover (QSS ::item:hover needs it).
    list_->setMouseTracking(true);
    if (delegate_) list_->setItemDelegate(delegate_);
    // The two hover treatments every other item in the app has, which these rows were
    // missing entirely (user report): the glass sweep, and the 2px ease right. The slide
    // WRAPS whatever delegate is installed, so a popup with its own painter keeps it.
    installRowShimmer(list_);
    installRowHoverSlide(list_);
    list_->setFocusPolicy(searchable_ ? Qt::NoFocus : Qt::StrongFocus);   // keys go to
    if (!searchable_) list_->installEventFilter(this);                    // whoever has focus
    // Hover preview: resting on a row live-applies its value (repaint only); leaving the
    // list or closing the popup puts the committed one back. It waits for the pointer to
    // settle, so skimming down the rows does not repaint for every option passed.
    previewTimer_.setSingleShot(true);
    previewTimer_.setInterval(280);
    connect(&previewTimer_, &QTimer::timeout, this, [this] {
      if (preview_ && !pendingPreview_.isEmpty()) { previewing_ = true; preview_(pendingPreview_); }
    });
    connect(list_, &QListView::entered, this, [this](const QModelIndex& idx) {
      if (!preview_) return;
      pendingPreview_ = idx.data(Qt::UserRole).toString();
      previewTimer_.start();   // fires once the pointer settles on the row
    });
    list_->viewport()->installEventFilter(this);
    layout->addWidget(list_, 1);

    noMatch_ = new QLabel(tr("No matching format."), frame);
    noMatch_->setObjectName("searchComboNoMatch");
    noMatch_->hide();
    layout->addWidget(noMatch_);

    if (search_)
      connect(search_, &QLineEdit::textChanged, this,
              [this](const QString& text) { applyFilter(text); });
    connect(list_, &QListView::clicked, this,
            [this](const QModelIndex& idx) { choose(idx.row()); });
  }

  // Re-filter, keep a sensible highlight, and swap in the "no match" row when
  // the query filters everything out (browser applySearch()).
  void SearchComboBox::applyFilter(const QString& query) {
    static_cast<LabelValueFilterProxy*>(proxy_)->setQuery(query);
    const int rows = proxy_->rowCount();
    const bool any = rows > 0;
    list_->setVisible(any);
    noMatch_->setVisible(!any);
    if (any && !list_->currentIndex().isValid())
      list_->setCurrentIndex(proxy_->index(0, 0));
    positionPopup();
  }

  void SearchComboBox::moveHighlight(int delta) {
    const int rows = proxy_->rowCount();
    if (rows <= 0) return;
    const int cur = list_->currentIndex().isValid() ? list_->currentIndex().row()
                                                    : (delta > 0 ? -1 : rows);
    const int next = qBound(0, cur + delta, rows - 1);
    list_->setCurrentIndex(proxy_->index(next, 0));
  }

  void SearchComboBox::setPreview(std::function<void(const QString&)> fn) {
    preview_ = std::move(fn);
  }

  void SearchComboBox::restorePreview() {
    previewTimer_.stop();          // cancel a hover that had not yet fired
    pendingPreview_.clear();
    if (!preview_ || !previewing_) return;
    previewing_ = false;
    preview_(currentData().toString());   // the committed value the trigger still holds
  }

  void SearchComboBox::choose(int proxyRow) {
    previewing_ = false;   // the pick commits the real value; no revert on the hide below
    const QModelIndex src = proxy_->mapToSource(proxy_->index(proxyRow, 0));
    if (src.isValid()) {
      setCurrentIndex(src.row());   // → currentIndexChanged / currentTextChanged
      // A user pick, as the native popup reports it — setCurrentIndex alone emits neither,
      // so a live-applying dialog never heard a pick from this popup. Emitted BEFORE the
      // popup leaves, so its exit plays under what was just picked.
      emit activated(src.row());
      emit textActivated(itemText(src.row()));
    }
    hidePopup();
  }

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
