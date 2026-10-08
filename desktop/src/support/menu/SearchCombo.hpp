#pragma once
#include <QComboBox>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>

// Desktop port of the browser's enhanceSelect({ search: true }) (browser/js/ui/control/customSelect.js).
// Not a QCompleter popup: that is a bare top-level QListView the app stylesheet never reaches.
class QAbstractItemDelegate;
class QLabel;
class QLineEdit;
class QListView;
class QSortFilterProxyModel;

namespace stencil::gui {

  class ShimmerOverlay;

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
    QWidget* popupWindow() const { return popup; }   // null until the first open

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

  private:
    // Press, drag past the slop, release: on a row it picks it as a click does, off the list it
    // closes it (browser dropdownMenu.js wireDragPick). Qt's own combo list already does this.
    bool dragPick(QObject* watched, QEvent* event);
    void ensurePopup();
    void choose(int proxyRow);
    void restorePreview();          // put the committed value back after a hover preview
    void moveHighlight(int delta);
    void applyFilter(const QString& query);
    void positionPopup();

    bool searchable = true;        // false → no search row (short lists)
    QWidget* popup = nullptr;      // Qt::Popup container (translucent corners)
    QLineEdit* search = nullptr;
    QAbstractItemDelegate* delegate = nullptr;   // setListDelegate, applied in ensurePopup
    std::function<void(const QString&)> preview;   // setPreview: hover-preview callback
    bool previewing = false;       // a preview is showing → restorePreview() should revert
    QTimer previewTimer;           // rested-intent delay before a hover previews
    QString pendingPreview;        // …the row-value it will preview when it fires
    QListView* list = nullptr;
    QLabel* noMatch = nullptr;
    QSortFilterProxyModel* proxy = nullptr;
    // Browser toggle: the outside-press auto-close fires first, so without this timestamp
    // the combo click would instantly reopen the popup.
    QElapsedTimer lastHide;
    QPoint dragFrom;            // the press that opened the list, while its button is held
    bool dragArmed = false;
    bool dragging = false;      // …and has moved past the slop
    ShimmerOverlay* sweep = nullptr;   // its own hover sweep, held while its list grabs the pointer
  };

}
