#pragma once
#include <QComboBox>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>

// Desktop port of the browser's enhanceSelect({ search: true }) dropdown
// (browser/js/ui/customSelect.js + the .accent-dd-* rules in components.css):
// the trigger stays a normal, non-editable combo, and opening it shows a themed
// popup with a "Search…" input pinned above the scrolling option rows. Typing
// filters case-insensitively on label + item DATA (the browser's rowMatches
// contract), an empty result shows the muted "No matching format." placeholder,
// Escape closes, Enter picks the highlighted row. Styled via the objectNames
// searchComboPopup / searchComboSearchRow / searchComboSearch / searchComboList
// / searchComboNoMatch in theme.cpp — a QCompleter popup can't be used here
// because it is a bare top-level QListView the app stylesheet never reaches.
class QAbstractItemDelegate;
class QLabel;
class QLineEdit;
class QListView;
class QSortFilterProxyModel;

namespace stencil::gui {

  // `searchable` false drops the search row (and the keyboard routing that goes with it)
  // but keeps everything else — the themed popup, the hover rows, the checkmark. That is
  // what every SHORT list in the app uses: the OS popup is drawn by macOS, uncss-able, and
  // looked nothing like the rest of the window. Only the long ISO page lists keep the box.
  class SearchComboBox : public QComboBox {
  public:
    explicit SearchComboBox(QWidget* parent = nullptr, bool searchable = true);

    void showPopup() override;
    void hidePopup() override;
    // A delegate for the popup's option rows (the motion modes' animated glyphs —
    // support/motionIcons.hpp); installed when the popup is built. Owned by the list.
    void setListDelegate(QAbstractItemDelegate* delegate);
    // Hover preview (browser twin: customSelect's `preview`): called with a row's item
    // DATA while the pointer rests on it, and again with the committed currentData() when
    // the pointer leaves the list or the popup closes without a pick. The callback must
    // only repaint — never persist — so a filter or compare mode can be seen before it is
    // chosen. A real pick commits through the usual activated/currentIndexChanged path.
    void setPreview(std::function<void(const QString&)> fn);
    // The popup's list, once built (showPopup builds it) — for a delegate that needs it.
    QListView* popupList();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void ensurePopup();
    void choose(int proxyRow);
    void restorePreview();          // put the committed value back after a hover preview
    void moveHighlight(int delta);
    void applyFilter(const QString& query);
    void positionPopup();

    bool searchable_ = true;        // false → no search row (short lists)
    QWidget* popup_ = nullptr;      // Qt::Popup container (translucent corners)
    QLineEdit* search_ = nullptr;
    QAbstractItemDelegate* delegate_ = nullptr;   // setListDelegate, applied in ensurePopup
    std::function<void(const QString&)> preview_;   // setPreview: hover-preview callback
    bool previewing_ = false;       // a preview is showing → restorePreview() should revert
    QTimer previewTimer_;           // rested-intent delay before a hover previews
    QString pendingPreview_;        // …the row-value it will preview when it fires
    QListView* list_ = nullptr;
    QLabel* noMatch_ = nullptr;
    QSortFilterProxyModel* proxy_ = nullptr;
    // Clicking the trigger while the popup is open must CLOSE it (browser
    // toggle): the outside-press auto-close fires first, so without this
    // timestamp the subsequent combo click would instantly reopen it.
    QElapsedTimer lastHide_;
  };

}
