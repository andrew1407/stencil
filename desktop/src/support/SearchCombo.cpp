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


  SearchComboBox::SearchComboBox(QWidget* parent, bool searchable)
      : QComboBox(parent), searchable(searchable) {
    setCursor(Qt::PointingHandCursor);   // browser parity: every selector is a pointer
  }

  // Lazy, so the model is already filled and themed when first opened.
  void SearchComboBox::ensurePopup() {
    if (popup) return;

    // Translucent Qt::Popup shell so the inner frame's rounded corners clip (QMenu's trick).
    popup = new QWidget(this, Qt::Popup | Qt::FramelessWindowHint |
                                   Qt::NoDropShadowWindowHint);
    popup->setAttribute(Qt::WA_TranslucentBackground);
    popup->installEventFilter(this);
    auto* shell = new QVBoxLayout(popup);
    shell->setContentsMargins(0, 0, 0, 0);

    auto* frame = new QWidget(popup);
    frame->setObjectName("searchComboPopup");
    frame->setAttribute(Qt::WA_StyledBackground);
    shell->addWidget(frame);

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(POPUP_PADDING, POPUP_PADDING, POPUP_PADDING,
                               POPUP_PADDING);
    layout->setSpacing(POPUP_PADDING);

    // Browser .accent-dd-search-row hairline. A short list has no search row.
    if (searchable) {
      auto* searchRow = new QWidget(frame);
      searchRow->setObjectName("searchComboSearchRow");
      searchRow->setAttribute(Qt::WA_StyledBackground);
      auto* searchLayout = new QVBoxLayout(searchRow);
      searchLayout->setContentsMargins(POPUP_PADDING, POPUP_PADDING,
                                       POPUP_PADDING, POPUP_PADDING * 2);
      search = new QLineEdit(searchRow);
      search->setObjectName("searchComboSearch");
      search->setPlaceholderText(tr("Search…"));
      search->setClearButtonEnabled(true);
      search->installEventFilter(this);
      searchLayout->addWidget(search);
      layout->addWidget(searchRow);
    }

    proxy = new LabelValueFilterProxy(this);
    proxy->setSourceModel(model());

    list = new QListView(frame);
    list->setObjectName("searchComboList");
    // No scroll-area MINIMUM: QAbstractScrollArea's ~66px minimumSizeHint outranked
    // positionPopup()'s geometry and left a blank band under a 2-row list.
    list->setFrameShape(QFrame::NoFrame);
    list->setMinimumSize(1, 1);
    list->setModel(proxy);
    list->setUniformItemSizes(true);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // QSS ::item:hover needs it.
    list->setMouseTracking(true);
    if (delegate) list->setItemDelegate(delegate);
    // The slide WRAPS whatever delegate is installed, so a popup with its own painter keeps it.
    installRowShimmer(list);
    installRowHoverSlide(list);
    list->setFocusPolicy(searchable ? Qt::NoFocus : Qt::StrongFocus);   // keys go to
    if (!searchable) list->installEventFilter(this);                    // whoever has focus
    // Hover preview (repaint only); waits for the pointer to settle so skimming does not repaint per row.
    previewTimer.setSingleShot(true);
    previewTimer.setInterval(280);
    connect(&previewTimer, &QTimer::timeout, this, [this] {
      if (preview && !pendingPreview.isEmpty()) { previewing = true; preview(pendingPreview); }
    });
    connect(list, &QListView::entered, this, [this](const QModelIndex& idx) {
      if (!preview) return;
      pendingPreview = idx.data(Qt::UserRole).toString();
      previewTimer.start();   // fires once the pointer settles on the row
    });
    list->viewport()->installEventFilter(this);
    layout->addWidget(list, 1);

    noMatch = new QLabel(tr("No matching format."), frame);
    noMatch->setObjectName("searchComboNoMatch");
    noMatch->hide();
    layout->addWidget(noMatch);

    if (search)
      connect(search, &QLineEdit::textChanged, this,
              [this](const QString& text) { applyFilter(text); });
    connect(list, &QListView::clicked, this,
            [this](const QModelIndex& idx) { choose(idx.row()); });
  }

  // Browser applySearch().
  void SearchComboBox::applyFilter(const QString& query) {
    static_cast<LabelValueFilterProxy*>(proxy)->setQuery(query);
    const int rows = proxy->rowCount();
    const bool any = rows > 0;
    list->setVisible(any);
    noMatch->setVisible(!any);
    if (any && !list->currentIndex().isValid())
      list->setCurrentIndex(proxy->index(0, 0));
    positionPopup();
  }

  void SearchComboBox::moveHighlight(int delta) {
    const int rows = proxy->rowCount();
    if (rows <= 0) return;
    const int cur = list->currentIndex().isValid() ? list->currentIndex().row()
                                                    : (delta > 0 ? -1 : rows);
    const int next = qBound(0, cur + delta, rows - 1);
    list->setCurrentIndex(proxy->index(next, 0));
  }

  void SearchComboBox::setPreview(std::function<void(const QString&)> fn) {
    preview = std::move(fn);
  }

  void SearchComboBox::restorePreview() {
    previewTimer.stop();          // cancel a hover that had not yet fired
    pendingPreview.clear();
    if (!preview || !previewing) return;
    previewing = false;
    preview(currentData().toString());   // the committed value the trigger still holds
  }

  void SearchComboBox::choose(int proxyRow) {
    previewing = false;   // the pick commits the real value; no revert on the hide below
    const QModelIndex src = proxy->mapToSource(proxy->index(proxyRow, 0));
    if (src.isValid()) {
      setCurrentIndex(src.row());   // → currentIndexChanged / currentTextChanged
      // setCurrentIndex alone emits neither. Emitted BEFORE the popup leaves, so its exit
      // plays under what was just picked.
      emit activated(src.row());
      emit textActivated(itemText(src.row()));
    }
    hidePopup();
  }
}

