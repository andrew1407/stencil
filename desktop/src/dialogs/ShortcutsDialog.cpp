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


  ShortcutsDialog::ShortcutsDialog(const QVector<Entry>& entries, QWidget* parent)
      : QDialog(parent) {
    setWindowTitle("Keyboard Shortcuts");
    const QColor muted = palette().color(QPalette::PlaceholderText);

    ModalChrome chrome = installModalChrome(this, "gear", tr("Keyboard Shortcuts"));
    search_ = addModalSearchBar(chrome, tr("Search shortcuts…"));
    search_->setToolTip("Filter the shortcut list by name");
    // The head takes the body's top padding (#settings-modal .settings-body padding-top: 0).
    ModalScrollBody body = makeModalScrollBody(chrome, /*topPad=*/0);
    scroll_ = body.scroll;

    // Column geometry shared by the head and every row.
    const auto tableRow = [](QWidget* parent) {
      auto* h = new QHBoxLayout(parent);
      h->setContentsMargins(CELL_PAD_X, CELL_PAD_Y, CELL_PAD_X, CELL_PAD_Y);
      h->setSpacing(CELL_PAD_X * 2);
      return h;
    };
    const auto fixedCol = [](QWidget* w, int width) {
      w->setFixedWidth(width);
      return w;
    };
    QList<QWidget*> comboCol, defaultCol;   // every widget in each keycap column

    // The table head, pinned ABOVE the scrolling rows (the browser's sticky <thead>).
    head_ = new QWidget(this);
    head_->setObjectName(QStringLiteral("hotkeyHead"));
    head_->setAttribute(Qt::WA_StyledBackground, true);
    {
      auto* h = tableRow(head_);
      const auto th = [this](const QString& text, bool centred = false) {
        auto* l = new QLabel(text, head_);
        l->setObjectName(QStringLiteral("hotkeyTh"));
        if (centred) l->setAlignment(Qt::AlignCenter);
        return l;
      };
      h->addWidget(th(tr("Action")), 1);
      comboCol << fixedCol(th(tr("Current shortcut"), true), COMBO_COL_W);
      defaultCol << fixedCol(th(tr("Default"), true), DEFAULT_COL_W);
      h->addWidget(comboCol.last());
      h->addWidget(defaultCol.last());
      h->addWidget(fixedCol(new QWidget(head_), RESET_COL_W));
    }
    headWrap_ = new QHBoxLayout;
    headWrap_->setContentsMargins(SIDE_PAD, 0, SIDE_PAD, 0);
    headWrap_->addWidget(head_);
    chrome.body->insertLayout(0, headWrap_);
    // The scrollbar narrows the rows but not the head: watch the VIEWPORT (it resizes
    // whenever the bar comes or goes) and pad the head to match.
    scroll_->viewport()->installEventFilter(this);

    int idx = 0;
    for (const auto& e : entries) {
      const QString labelText = e.label.isEmpty() ? e.id : e.label;
      auto* row = new QWidget(body.content);
      row->setProperty("hotkeyRow", true);
      row->setAttribute(Qt::WA_StyledBackground, true);
      auto* h = tableRow(row);

      auto* action = new QLabel(labelText, row);
      action->setObjectName(QStringLiteral("hotkeyAction"));
      // Wraps like a table cell: a long action name must never push the fixed
      // columns past the viewport's edge (the content cannot scroll sideways).
      action->setWordWrap(true);
      action->setMinimumWidth(0);
      h->addWidget(action, 1);

      // The combo as keycaps: click it and press the new combination (the browser
      // double-clicks; a single click is the Qt idiom). Escape cancels.
      auto* cell = new ComboCell(row);
      cell->setObjectName(QStringLiteral("hotkeyCell"));
      cell->setAlignment(Qt::AlignCenter);
      cell->setToolTip("Click and press a new key combination to rebind");
      cell->setPlaceholder(mutedHtml(tr("press combination…")));
      cell->setResting(capsHtml(e.currentSeq, CAP_SCALE));
      comboCol << fixedCol(cell, COMBO_COL_W);
      h->addWidget(cell);

      // The default: the same caps as the current combo, centred in its column.
      auto* def = new KeycapChip(row);
      def->setObjectName(QStringLiteral("hotkeyDefault"));
      def->setAlignment(Qt::AlignCenter);
      def->setCaps(capsHtml(e.defaultSeq, CAP_SCALE));
      defaultCol << fixedCol(def, DEFAULT_COL_W);
      h->addWidget(def);

      // Reset = the browser's rotate-ccw glyph, shown only while the row differs from
      // its default; it hides inside a fixed-width slot, so the column stays.
      auto* slot = new QWidget(row);
      auto* slotLay = new QHBoxLayout(slot);
      slotLay->setContentsMargins(0, 0, 0, 0);
      auto* reset = new QToolButton(slot);
      reset->setObjectName(QStringLiteral("hotkeyReset"));
      reset->setIcon(themedIcon("rotate-ccw", muted, 15));
      reset->setToolTip("Reset to default");
      reset->setCursor(Qt::PointingHandCursor);
      slotLay->addWidget(reset, 0, Qt::AlignCenter);
      h->addWidget(fixedCol(slot, RESET_COL_W));

      body.layout->addWidget(row);
      installHoverShimmer(row);   // the app-wide glass sweep, as on a list row
      rows_.push_back({e.id, portable(QKeySequence(e.defaultSeq)), labelText,
                       portable(QKeySequence(e.currentSeq)), row, cell, reset});
      reset->setVisible(rows_.last().lastSeq != rows_.last().defaultSeq);

      cell->onCaptured = [this, idx](const QString& seq) { captured(idx, seq); };
      connect(reset, &QToolButton::clicked, this, [this, idx] {
        setRowSeq(rows_[idx], rows_[idx].defaultSeq, /*formed=*/true);
        emit overridesChanged();
      });
      ++idx;
    }
    // Widen each keycap column to its widest chord, head included, so a four-cap
    // combo never clips (the browser's max-content columns).
    const auto fit = [](QList<QWidget*>& col, int floor) {
      int w = floor;
      for (QWidget* x : col) w = qMax(w, x->sizeHint().width() + 4);
      for (QWidget* x : col) x->setFixedWidth(w);
      return w;
    };
    const int comboW = fit(comboCol, COMBO_COL_W);
    const int defaultW = fit(defaultCol, DEFAULT_COL_W);
    // Wide enough for the two keycap columns and a readable Action column.
    const int needW = 2 * SIDE_PAD + 2 * CELL_PAD_X + 3 * (2 * CELL_PAD_X) + RESET_COL_W +
                      comboW + defaultW + ACTION_MIN_W;
    dialogW_ = qMax(SHORTCUTS_WIDTH, needW);

    empty_ = modalEmptyLabel(tr("No matching shortcuts."), body.content);
    empty_->hide();
    body.layout->addWidget(empty_);
    body.layout->addStretch(1);

    // Footer (browser .settings-footer): the how-to hint beside Reset All.
    QHBoxLayout* footer = addModalFooter(
        chrome, tr("Double-click a shortcut to set a new combination · Esc cancels · reset icon clears one"));
    auto* resetAll = new QPushButton(tr("Reset All"), this);
    makeModalCta(resetAll, "rotate-ccw");   // browser #reset-all-hotkeys: the accent CTA
    resetAll->setAutoDefault(false);
    connect(resetAll, &QPushButton::clicked, this, &ShortcutsDialog::resetAll);
    footer->addWidget(resetAll);

    connect(search_, &QLineEdit::textChanged, this,
            [this](const QString& q) { applyFilter(q); });
    sizeModalTall(this, dialogW_);
  }

  bool ShortcutsDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == scroll_->viewport() && event->type() == QEvent::Resize) {
      reserveHeadGutter();
      return false;
    }
    return QDialog::eventFilter(watched, event);
  }

  // Pad the pinned head by the vertical scrollbar's slot so its columns line up with the scrolling
  // rows. SYNCHRONOUSLY off the bar's own extent: the viewport-vs-scrollarea delta lags mid-layout.
  void ShortcutsDialog::reserveHeadGutter() {
    QScrollBar* vbar = scroll_->verticalScrollBar();
    const bool needed = vbar->maximum() > vbar->minimum();
    const int bar = needed ? vbar->sizeHint().width() : 0;
    headWrap_->setContentsMargins(SIDE_PAD, 0, SIDE_PAD + bar, 0);
  }

  // A combo in the tooltips' keycaps (NativeText, so macOS draws ⌥⇧⌘ as glyphs), or
  // the muted "(unset)".
  QString ShortcutsDialog::capsHtml(const QString& seq, qreal scale) const {
    const QString shown = native(seq);
    if (shown.isEmpty()) return mutedHtml(tr("(unset)"));
    return comboKeycapsHtml(shown, tableCaps(), ON_MAC, scale);
  }
}

