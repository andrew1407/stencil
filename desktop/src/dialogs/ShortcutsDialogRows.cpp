#include "ShortcutsDialog.hpp"
#include "shortcutsDialogParts.hpp"
#include "../support/DisintegrateOverlay.hpp"   // the dust a new combination forms from
#include "../support/filterFade.hpp"  // rows fade in/out with the search, never blink
#include "../support/KeycapChip.hpp"  // the combo as keycaps: shake on hover, capture on click
#include "../support/modalChrome.hpp"
#include "../support/ShimmerOverlay.hpp"   // row hover sweep
#include "iconSet.hpp"
#include "tipContent.hpp"   // comboKeycapsHtml, currentPalette
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace stencil::gui {

  void ShortcutsDialog::setRowSeq(Row& row, const QString& seq, bool formed) {
    row.lastSeq = portable(QKeySequence(seq));
    row.cell->setResting(capsHtml(row.lastSeq, CAP_SCALE));
    row.reset->setVisible(row.lastSeq != row.defaultSeq);
    if (!formed || support::motionReduced() || !isVisible()) return;
    // The new caps form out of dust gathered over the cell (browser markIn), the cell
    // veiled until the motes have very nearly landed.
    QWidget* host = window();
    ComboCell* cell = row.cell;
    QTimer::singleShot(0, cell, [cell, host] {
      if (!cell->isVisible()) return;
      auto* fx = DisintegrateOverlay::overRect(cell, cell->rect(), host,
                                               DisintegrateOverlay::Sweep::GATHER,
                                               /*dust=*/true, FORM_CELLS, FORM_MS);
      if (!fx) return;
      auto* veil = new QGraphicsOpacityEffect(cell);
      veil->setOpacity(0.0);
      cell->setGraphicsEffect(veil);
      QTimer::singleShot(int(FORM_MS * FORM_VEIL), cell, [cell] { cell->setGraphicsEffect(nullptr); });
    });
  }

  void ShortcutsDialog::captured(int rowIndex, const QString& seq) {
    Row& row = rows[rowIndex];
    if (seq == row.lastSeq) return;
    if (!seq.isEmpty()) {
      // A combo another action owns is refused — the other is never silently unbound.
      for (int i = 0; i < rows.size(); ++i) {
        if (i == rowIndex || rows[i].lastSeq != seq) continue;
        emit conflict(tr("\"%1\" is already used by \"%2\" — the old shortcut was kept")
                          .arg(native(seq), rows[i].label));
        return;
      }
    }
    setRowSeq(row, seq, /*formed=*/true);
    emit overridesChanged();
  }

  void ShortcutsDialog::resetAll() {
    ConfirmSpec spec;
    spec.title = tr("Reset shortcuts");
    spec.message = tr("Reset ALL keyboard shortcuts to their defaults?");
    spec.confirmIcon = QStringLiteral("refresh");
    spec.danger = true;
    if (!confirmModal(this, spec)) return;
    // Only rows that actually change form again, and past the mark budget the rest
    // simply appear: one gesture, not thirty clouds.
    int formed = 0;
    for (Row& r : rows) {
      const bool changes = r.lastSeq != r.defaultSeq;
      setRowSeq(r, r.defaultSeq, /*formed=*/changes && formed < 8);
      if (changes) ++formed;
    }
    emit overridesChanged();
    emit allReset();
  }

  void ShortcutsDialog::applyFilter(const QString& query) {
    const QString q = query.trimmed().toLower();
    bool any = false;
    for (const Row& row : rows) {
      const bool match = q.isEmpty() || row.label.toLower().contains(q) ||
                         native(row.lastSeq).toLower().contains(q) ||
                         native(row.defaultSeq).toLower().contains(q);
      fadeFiltered(row.widget, match);
      any = any || match;
    }
    empty->setVisible(!any);
  }

  QHash<QString, QString> ShortcutsDialog::overrides() const {
    QHash<QString, QString> out;
    for (const auto& row : rows) {
      // Only persist an override when it differs from the config default.
      if (row.lastSeq != row.defaultSeq) out.insert(row.id, row.lastSeq);
    }
    return out;
  }
}

