#pragma once
#include <QComboBox>
#include <QElapsedTimer>

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
class QLabel;
class QLineEdit;
class QListView;
class QSortFilterProxyModel;

namespace stencil::gui {

  class SearchComboBox : public QComboBox {
  public:
    explicit SearchComboBox(QWidget* parent = nullptr);

    void showPopup() override;
    void hidePopup() override;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void ensurePopup();
    void choose(int proxyRow);
    void moveHighlight(int delta);
    void applyFilter(const QString& query);
    void positionPopup();

    QWidget* popup_ = nullptr;      // Qt::Popup container (translucent corners)
    QLineEdit* search_ = nullptr;
    QListView* list_ = nullptr;
    QLabel* noMatch_ = nullptr;
    QSortFilterProxyModel* proxy_ = nullptr;
    // Clicking the trigger while the popup is open must CLOSE it (browser
    // toggle): the outside-press auto-close fires first, so without this
    // timestamp the subsequent combo click would instantly reopen it.
    QElapsedTimer lastHide_;
  };

}
