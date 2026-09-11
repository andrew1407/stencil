#include "shortcutsDialog.hpp"
#include "../support/disintegrateOverlay.hpp"   // the dust a new combination forms from
#include "../support/filterFade.hpp"  // rows fade in/out with the search, never blink
#include "../support/keycapChip.hpp"  // the combo as keycaps: shake on hover, capture on click
#include "../support/modalChrome.hpp"
#include "../support/shimmerOverlay.hpp"   // row hover sweep
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

  namespace {
    constexpr int kShortcutsWidth = 620;   // browser #settings-modal: four columns
    // Keycap-column FLOORS: both grow to their widest chord once every row is built
    // (the browser's max-content columns). Action takes the rest, down to its own floor.
    constexpr int kComboColW = 150;
    constexpr int kDefaultColW = 110;
    constexpr int kActionMinW = 150;
    constexpr int kResetColW = 30;
    constexpr int kCellPadX = 10;   // th/td padding: 7px 10px
    constexpr int kCellPadY = 7;
    constexpr int kSidePad = 18;    // .settings-body padding
    // A new combination's caps arrive as dust (browser markIn: 320ms, veiled to 62%).
    constexpr int kFormMs = 320;
    constexpr double kFormVeil = 0.62;
    constexpr int kFormCells = 600;
    // Cap size against the tooltip's own — the combo and its default wear the same caps.
    constexpr qreal kCapScale = 0.95;

    // Table keycaps wear no face of their own: the container fill read as a dark box
    // against a hovered row, so here a key is its outline and its glyph.
    Palette tableCaps() {
      Palette pal = currentPalette();
      pal.bgContainer = QColor(0, 0, 0, 0);
      return pal;
    }

    QString portable(const QKeySequence& k) { return k.toString(QKeySequence::PortableText); }
    // The muted mono line a cell shows with no caps to draw (browser .hotkey-unset).
    QString mutedHtml(const QString& text) {
      return QString("<span style=\"color:%1;font-family:Menlo,Consolas,monospace;font-size:12px;\">%2</span>")
          .arg(currentPalette().textMuted.name(), text.toHtmlEscaped());
    }
    QString native(const QString& seq) {
      return QKeySequence(seq).toString(QKeySequence::NativeText);
    }
  }

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
      h->setContentsMargins(kCellPadX, kCellPadY, kCellPadX, kCellPadY);
      h->setSpacing(kCellPadX * 2);
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
      comboCol << fixedCol(th(tr("Current shortcut"), true), kComboColW);
      defaultCol << fixedCol(th(tr("Default"), true), kDefaultColW);
      h->addWidget(comboCol.last());
      h->addWidget(defaultCol.last());
      h->addWidget(fixedCol(new QWidget(head_), kResetColW));
    }
    headWrap_ = new QHBoxLayout;
    headWrap_->setContentsMargins(kSidePad, 0, kSidePad, 0);
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
      cell->setResting(capsHtml(e.currentSeq, kCapScale));
      comboCol << fixedCol(cell, kComboColW);
      h->addWidget(cell);

      // The default: the same caps as the current combo, centred in its column.
      auto* def = new KeycapChip(row);
      def->setObjectName(QStringLiteral("hotkeyDefault"));
      def->setAlignment(Qt::AlignCenter);
      def->setCaps(capsHtml(e.defaultSeq, kCapScale));
      defaultCol << fixedCol(def, kDefaultColW);
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
      h->addWidget(fixedCol(slot, kResetColW));

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
    const int comboW = fit(comboCol, kComboColW);
    const int defaultW = fit(defaultCol, kDefaultColW);
    // Wide enough for the two keycap columns and a readable Action column.
    const int needW = 2 * kSidePad + 2 * kCellPadX + 3 * (2 * kCellPadX) + kResetColW +
                      comboW + defaultW + kActionMinW;
    dialogW_ = qMax(kShortcutsWidth, needW);

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

  // Pad the pinned head by the vertical scrollbar's slot so its columns line up with the
  // scrolling rows below. Done SYNCHRONOUSLY off the bar's own extent: the viewport-vs-
  // scrollarea delta lags mid-layout, and the open flight photographs the dialog before
  // any 0-timer would run — the flown picture would land a scrollbar out of step.
  void ShortcutsDialog::reserveHeadGutter() {
    QScrollBar* vbar = scroll_->verticalScrollBar();
    const bool needed = vbar->maximum() > vbar->minimum();
    const int bar = needed ? vbar->sizeHint().width() : 0;
    headWrap_->setContentsMargins(kSidePad, 0, kSidePad + bar, 0);
  }

  // A combo in the tooltips' keycaps (NativeText, so macOS draws ⌥⇧⌘ as glyphs), or
  // the muted "(unset)".
  QString ShortcutsDialog::capsHtml(const QString& seq, qreal scale) const {
    const QString shown = native(seq);
    if (shown.isEmpty()) return mutedHtml(tr("(unset)"));
    return comboKeycapsHtml(shown, tableCaps(), kOnMac, scale);
  }

  void ShortcutsDialog::setRowSeq(Row& row, const QString& seq, bool formed) {
    row.lastSeq = portable(QKeySequence(seq));
    row.cell->setResting(capsHtml(row.lastSeq, kCapScale));
    row.reset->setVisible(row.lastSeq != row.defaultSeq);
    if (!formed || support::motionReduced() || !isVisible()) return;
    // The new caps form out of dust gathered over the cell (browser markIn), the cell
    // veiled until the motes have very nearly landed.
    QWidget* host = window();
    ComboCell* cell = row.cell;
    QTimer::singleShot(0, cell, [cell, host] {
      if (!cell->isVisible()) return;
      auto* fx = DisintegrateOverlay::overRect(cell, cell->rect(), host,
                                               DisintegrateOverlay::Sweep::Gather,
                                               /*dust=*/true, kFormCells, kFormMs);
      if (!fx) return;
      auto* veil = new QGraphicsOpacityEffect(cell);
      veil->setOpacity(0.0);
      cell->setGraphicsEffect(veil);
      QTimer::singleShot(int(kFormMs * kFormVeil), cell, [cell] { cell->setGraphicsEffect(nullptr); });
    });
  }

  void ShortcutsDialog::captured(int rowIndex, const QString& seq) {
    Row& row = rows_[rowIndex];
    if (seq == row.lastSeq) return;
    if (!seq.isEmpty()) {
      // A combo another action owns is refused — the other is never silently unbound.
      for (int i = 0; i < rows_.size(); ++i) {
        if (i == rowIndex || rows_[i].lastSeq != seq) continue;
        emit conflict(tr("\"%1\" is already used by \"%2\" — the old shortcut was kept")
                          .arg(native(seq), rows_[i].label));
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
    for (Row& r : rows_) {
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
    for (const Row& row : rows_) {
      const bool match = q.isEmpty() || row.label.toLower().contains(q) ||
                         native(row.lastSeq).toLower().contains(q) ||
                         native(row.defaultSeq).toLower().contains(q);
      fadeFiltered(row.widget, match);
      any = any || match;
    }
    empty_->setVisible(!any);
  }

  QHash<QString, QString> ShortcutsDialog::overrides() const {
    QHash<QString, QString> out;
    for (const auto& row : rows_) {
      // Only persist an override when it differs from the config default.
      if (row.lastSeq != row.defaultSeq) out.insert(row.id, row.lastSeq);
    }
    return out;
  }

}
