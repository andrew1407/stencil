#pragma once
#include <QComboBox>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>

// Desktop port of the browser's enhanceSelect({ search: true }) (browser/js/ui/customSelect.js).
// Not a QCompleter popup: that is a bare top-level QListView the app stylesheet never reaches.
class QAbstractItemDelegate;
class QLabel;
class QLineEdit;
class QListView;
class QSortFilterProxyModel;

namespace stencil::gui {

  // `searchable` false drops the search row but keeps the themed popup: the OS popup is
  // drawn by macOS and uncss-able. Only the long ISO page lists keep the box.
  class SearchComboBox : public QComboBox {
  public:
    explicit SearchComboBox(QWidget* parent = nullptr, bool searchable = true);

    void showPopup() override;
    void hidePopup() override;
    // Installed when the popup is built. Owned by the list.
    void setListDelegate(QAbstractItemDelegate* delegate);
    // Browser twin: customSelect's `preview`. Called with a row's DATA on hover and with
    // currentData() when the pointer leaves; it must only repaint, never persist.
    void setPreview(std::function<void(const QString&)> fn);
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
    // Browser toggle: the outside-press auto-close fires first, so without this timestamp
    // the combo click would instantly reopen the popup.
    QElapsedTimer lastHide_;
  };

}
