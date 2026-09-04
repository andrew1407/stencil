#pragma once
#include <QDialog>
#include <QHash>
#include <QString>
#include <QVector>

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QToolButton;
class QWidget;

// Shortcut rebinding dialog (S13) — the browser's "Keyboard Shortcuts" modal
// (browser/js/ui/settingsModal.js): the shared shell, a search box, and an Action /
// Shortcut / Default table drawn in the tooltips' keycaps (support/keycapChip.hpp).
// Click a combo and press the new one; per-row reset glyph, Reset All in the footer.
// Edits apply LIVE: overridesChanged() fires on each change and the owner persists +
// re-applies overrides(). A combo already used elsewhere is refused (conflict()).
// Mirrors the browser's STORAGE_KEYS.hotkeys layered over hotkeysConfig.json.
namespace stencil::gui {

  class ShortcutsDialog : public QDialog {
    Q_OBJECT
   public:
    struct Entry {
      QString id;
      QString label;
      QString defaultSeq;  // from hotkeysConfig.json
      QString currentSeq;  // effective (override or default)
    };

    ShortcutsDialog(const QVector<Entry>& entries, QWidget* parent = nullptr);

    // Overrides to persist: id -> sequence, only for rows that differ from the
    // config default (a reset / matching-default row produces no override).
    QHash<QString, QString> overrides() const;

   signals:
    void overridesChanged();   // a binding was set, cleared or reset — apply overrides()
    void allReset();           // Reset All was confirmed (the owner toasts it)
    void conflict(const QString& message);   // a taken combo was refused (the owner toasts it)

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    struct Row {
      QString id;
      QString defaultSeq;        // PortableText, so a compare with lastSeq is enough
      QString label;             // for the search filter
      QString lastSeq;           // the committed binding (PortableText)
      QWidget* widget = nullptr; // the whole table row
      class ComboCell* cell = nullptr;   // the combo, as keycaps that capture on click
      QToolButton* reset = nullptr;
    };
    QVector<Row> rows_;
    QLineEdit* search_ = nullptr;
    QLabel* empty_ = nullptr;
    QWidget* head_ = nullptr;
    QHBoxLayout* headWrap_ = nullptr;   // pads the head to the rows' side edges
    int dialogW_ = 0;                   // the shell width the keycap columns need
    QScrollArea* scroll_ = nullptr;

    // Show only rows whose action, combo or default contains `query` (case-insensitive).
    void applyFilter(const QString& query);
    // A row's edit produced `seq`: resolve conflicts, then commit or revert.
    void captured(int rowIndex, const QString& seq);
    // Write `seq` into a row; the reset glyph follows. `formed` plays the new caps in
    // as dust (browser markIn).
    void setRowSeq(Row& row, const QString& seq, bool formed = false);
    QString capsHtml(const QString& seq, qreal scale) const;
    void resetAll();
  };

}
