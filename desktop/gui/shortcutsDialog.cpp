#include "shortcutsDialog.hpp"
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

namespace stencil::gui {

  ShortcutsDialog::ShortcutsDialog(const QVector<Entry>& entries, QWidget* parent)
      : QDialog(parent) {
    setWindowTitle("Customize Shortcuts");
    setMinimumWidth(440);

    // Scrollable grid (label | editor | reset) so a long list stays usable.
    auto* inner = new QWidget(this);
    auto* grid = new QGridLayout(inner);
    grid->setColumnStretch(1, 1);
    int r = 0;
    for (const auto& e : entries) {
      auto* lbl = new QLabel(e.label.isEmpty() ? e.id : e.label, inner);
      auto* edit = new QKeySequenceEdit(QKeySequence(e.currentSeq), inner);
      auto* reset = new QPushButton("Reset", inner);
      // Reset restores the config default for this row (drops the override).
      const QString def = e.defaultSeq;
      connect(reset, &QPushButton::clicked, edit,
              [edit, def] { edit->setKeySequence(QKeySequence(def)); });
      grid->addWidget(lbl, r, 0);
      grid->addWidget(edit, r, 1);
      grid->addWidget(reset, r, 2);
      rows_.push_back({e.id, e.defaultSeq, edit});
      ++r;
    }

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(inner);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(scroll, 1);
    layout->addWidget(buttons);
  }

  QHash<QString, QString> ShortcutsDialog::overrides() const {
    QHash<QString, QString> out;
    for (const auto& row : rows_) {
      const QString seq =
          row.edit->keySequence().toString(QKeySequence::PortableText);
      // Only persist an override when it differs from the config default.
      if (seq != row.defaultSeq) out.insert(row.id, seq);
    }
    return out;
  }

}
