#include "searchCombo.hpp"

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

  SearchComboBox::SearchComboBox(QWidget* parent) : QComboBox(parent) {}

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
    // under .accent-dd-search-row.
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

    proxy_ = new LabelValueFilterProxy(this);
    proxy_->setSourceModel(model());

    list_ = new QListView(frame);
    list_->setObjectName("searchComboList");
    list_->setModel(proxy_);
    list_->setUniformItemSizes(true);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Hover-highlight rows like .accent-dd-opt:hover (QSS ::item:hover needs it).
    list_->setMouseTracking(true);
    list_->setFocusPolicy(Qt::NoFocus);  // keyboard stays on the search field
    layout->addWidget(list_, 1);

    noMatch_ = new QLabel(tr("No matching format."), frame);
    noMatch_->setObjectName("searchComboNoMatch");
    noMatch_->hide();
    layout->addWidget(noMatch_);

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

  void SearchComboBox::choose(int proxyRow) {
    const QModelIndex src = proxy_->mapToSource(proxy_->index(proxyRow, 0));
    if (src.isValid()) setCurrentIndex(src.row());  // → currentIndexChanged
    hidePopup();
  }

  // Below the trigger, at least trigger-wide, tall enough for the visible rows
  // up to the browser's 280px cap; flipped above when the screen runs out.
  void SearchComboBox::positionPopup() {
    const int rows = proxy_->rowCount();
    const int rowH = rows > 0 ? list_->sizeHintForRow(0) : 0;
    const int chromeH = kPopupPadding * 2 + search_->parentWidget()->sizeHint().height() +
                        kPopupPadding;
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
  void SearchComboBox::showPopup() {
    if (popup_ && popup_->isVisible()) {  // trigger acts as a toggle
      hidePopup();
      return;
    }
    // The outside-press that closed the popup also lands on the trigger and
    // would reopen it here — treat that press as "toggle closed" instead.
    if (lastHide_.isValid() && lastHide_.elapsed() < 150) return;
    ensurePopup();
    {
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
    if (cur.isValid()) list_->scrollTo(cur, QAbstractItemView::PositionAtCenter);
    search_->setFocus(Qt::PopupFocusReason);
  }

  void SearchComboBox::hidePopup() {
    if (popup_) popup_->hide();
    QComboBox::hidePopup();
  }

  bool SearchComboBox::eventFilter(QObject* watched, QEvent* event) {
    if (watched == popup_ && event->type() == QEvent::Hide) lastHide_.start();
    if (watched == search_ && event->type() == QEvent::KeyPress) {
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
