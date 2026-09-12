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
    // list's last option (the 2-row All/Local filter popup).
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
    // missing entirely: the glass sweep, and the 2px ease right. The slide
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
}

