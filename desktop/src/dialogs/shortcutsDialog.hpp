#pragma once
#include <QDialog>
#include <QHash>
#include <QString>
#include <QVector>

class QKeySequenceEdit;
class QLineEdit;
class QWidget;
class QAbstractButton;

// Shortcut rebinding dialog (S13). Lists every hotkey id with a QKeySequenceEdit
// prefilled with its current binding (config default + any saved override), plus
// a per-row "Reset" to drop the override. Replaces the display-only info list.
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

   private:
    struct Row {
      QString id;
      QString defaultSeq;
      QString label;             // for the search filter
      QKeySequenceEdit* edit = nullptr;
      QWidget* labelWidget = nullptr;
      QAbstractButton* reset = nullptr;
    };
    QVector<Row> rows_;
    QLineEdit* search_ = nullptr;

    // Show only rows whose label/id contains `query` (case-insensitive).
    void applyFilter(const QString& query);
  };

}
