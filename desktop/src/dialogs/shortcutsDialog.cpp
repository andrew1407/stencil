#include "shortcutsDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace stencil::gui {

  ShortcutsDialog::ShortcutsDialog(const QVector<Entry>& entries, QWidget* parent)
      : QDialog(parent) {
    setWindowTitle("Customize Shortcuts");
    // Narrower than before: the reset is now a compact icon button, not a wide
    // "Reset" label, so the editor column no longer needs the extra room.
    setMinimumWidth(380);
    const QColor ico = palette().color(QPalette::WindowText);

    // Live search box (mirrors the browser shortcuts modal), magnifier-prefixed.
    search_ = new QLineEdit(this);
    search_->setPlaceholderText("Search shortcuts…");
    search_->setToolTip("Filter the shortcut list by name");
    search_->setClearButtonEnabled(true);
    search_->addAction(themedIcon("search", palette().color(QPalette::PlaceholderText), 16),
                       QLineEdit::LeadingPosition);

    // Scrollable grid (label | editor | reset) so a long list stays usable.
    auto* inner = new QWidget(this);
    auto* grid = new QGridLayout(inner);
    grid->setColumnStretch(1, 1);
    int r = 0;
    for (const auto& e : entries) {
      const QString labelText = e.label.isEmpty() ? e.id : e.label;
      auto* lbl = new QLabel(labelText, inner);
      auto* edit = new QKeySequenceEdit(QKeySequence(e.currentSeq), inner);
      edit->setToolTip("Click and press a new key combination to rebind");
      // Reset = a small refresh icon button (restores the config default for this
      // row, dropping the override), replacing the wide "Reset" text button.
      auto* reset = new QToolButton(inner);
      reset->setIcon(themedIcon("refresh", ico, 15));
      reset->setToolTip("Reset to default");
      reset->setAutoRaise(true);
      const QString def = e.defaultSeq;
      connect(reset, &QToolButton::clicked, edit,
              [edit, def] { edit->setKeySequence(QKeySequence(def)); });
      grid->addWidget(lbl, r, 0);
      grid->addWidget(edit, r, 1);
      grid->addWidget(reset, r, 2);
      rows_.push_back({e.id, e.defaultSeq, labelText, edit, lbl, reset});
      ++r;
    }

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(inner);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* buttons =
        makeButtonBox(this, QDialogButtonBox::Save | QDialogButtonBox::Cancel);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(search_);
    layout->addWidget(scroll, 1);
    layout->addWidget(buttons);

    connect(search_, &QLineEdit::textChanged, this,
            [this](const QString& q) { applyFilter(q); });
  }

  void ShortcutsDialog::applyFilter(const QString& query) {
    const QString q = query.trimmed().toLower();
    for (const Row& row : rows_) {
      const bool match = q.isEmpty() || row.label.toLower().contains(q) ||
                         row.id.toLower().contains(q);
      // Hide all three cells so the grid row collapses when filtered out.
      if (row.labelWidget) row.labelWidget->setVisible(match);
      if (row.edit) row.edit->setVisible(match);
      if (row.reset) row.reset->setVisible(match);
    }
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
