#include "menuHotkeys.hpp"

namespace stencil::support {

  // `compact` is for a small, FLAT list of hotkey-bearing rows (the copy/download-image
  // variant popups) — never a menu that also mixes in submenu-opener rows needing arrow
  // clearance. theme.cpp's QMenu padding is sized for the menu bar; a tighter local
  // stylesheet shrinks it for these short icon+label+chip rows.
  //
  // Width is never forced (no setFixedWidth): QMenuPrivate's calcActionRects() sizes the
  // shortcut column from the row's natural sizeHint independently of the outer widget
  // frame, so clipping the frame left the chip's unclipped content spilling past the
  // visible edge. It only ever comes from Qt's sizeHint reacting to the padding text below.
  MenuHotkeyChips::MenuHotkeyChips(QMenu* root, bool compact) : compact_(compact) {
    if (compact_ && root) root->setStyleSheet(gui::compactMenuQss());
    wire(root, gui::currentPalette());
  }

  MenuHotkeyChips::~MenuHotkeyChips() {
    for (auto& row : rows_) {
      // The chip must be deleted here, not left as an orphan parented to the
      // still-alive menu: the toolbar popups / Data-menu submenus reconstruct this
      // whole object on every aboutToShow, and a stale chip at its old actionGeometry()
      // would overlap whatever row a later wire() lays out in that spot. Stop any
      // in-flight shake first — it closes over this raw pointer.
      if (row.shake) row.shake->stop();
      delete row.chip;
      row.chip = nullptr;
      if (!row.action) continue;
      row.action->setText(row.originalText);
      row.action->setShortcut(row.originalShortcut);
    }
  }

  // The re-fire guard (shakeRow's own comment) only advances `current_` on a genuinely
  // new hovered(QAction*) — but leaving a row without landing on another one first never
  // fires hovered() again either, so current_ would stay stuck and wrongly eat the next,
  // legitimate hover. Reset it on Leave/Hide so returning to the same row reads as fresh.
  bool MenuHotkeyChips::eventFilter(QObject*, QEvent* e) {
    if (e->type() == QEvent::Leave || e->type() == QEvent::Hide) current_ = nullptr;
    return false;
  }

  // The combo a row shows, in the SAME text Qt would have drawn: whatever already
  // follows a manual "\t" (a submenu-opener's hintTab), else the action's own
  // NativeText shortcut. Empty when the row carries no combo at all.
  //
  // An action shared across several owners (the toolbar popups, the Data menu, and the
  // context menu all add the SAME actCopyImage_/actSaveImage_ etc.) can already be
  // chipped by an earlier, still-alive MenuHotkeyChips — its text is then the padded
  // "\t   " stand-in, so that case reads the cached property the first wiring stashed.
  // Otherwise the live shortcut() is read fresh: the primary-gesture pair genuinely
  // moves its shortcut between actions at runtime (syncSplitCopyDownloadSlot), and a
  // stale cache would keep showing the old row chipped after the shortcut moved.
  QString MenuHotkeyChips::comboOf(QAction* a) {
    const QString t = a->text();
    const int tab = t.indexOf('\t');
    if (tab >= 0) {
      const QVariant cached = a->property("stencilHotkeyCombo");
      if (cached.isValid()) return cached.toString();
      return t.mid(tab + 1);
    }
    const QString combo = a->shortcut().toString(QKeySequence::NativeText);
    a->setProperty("stencilHotkeyCombo", combo.isEmpty() ? QVariant() : combo);
    return combo;
  }

  void MenuHotkeyChips::wire(QMenu* menu, const gui::Palette& pal) {
    // A COMPACT target (the ctor's own `root`, e.g. the canvas context menu's nested
    // Copy/Download Image submenus) may already have its own DEDICATED, earlier-
    // constructed MenuHotkeyChips wired directly onto it (compact=true) before this
    // wider, non-compact instance walks the whole tree and would otherwise reach the
    // SAME submenu via recursion — a second, plain wiring on top would double the
    // chips and fight the first one over the actions' text. Marked here, checked below.
    menu->setProperty("stencilHotkeyChipsWired", true);
    menu->installEventFilter(this);   // resets the shake re-fire guard on Leave/Hide
    for (QAction* a : menu->actions()) {
      if (a->isSeparator() || qobject_cast<QWidgetAction*>(a)) continue;
      if (QMenu* sub = a->menu()) {
        if (!sub->property("stencilHotkeyChipsWired").toBool()) wire(sub, pal);
      }
      const QString combo = comboOf(a);
      if (combo.isEmpty()) continue;   // nothing to chip — leave the row exactly as is
      const int tab = a->text().indexOf('\t');
      const QString label = tab >= 0 ? a->text().left(tab) : a->text();
      Row row;
      row.action = a;
      row.menu = menu;
      row.originalText = a->text();
      row.originalShortcut = a->shortcut();
      row.chip = new gui::TipBody(menu);
      row.chip->setAttribute(Qt::WA_TransparentForMouseEvents);
      row.chip->setTip(gui::comboKeycapsHtml(combo, pal));
      row.chip->show();
      row.chip->adjustSize();
      // Hunts the keycap regions NOW (grab()-diffing the rendered text, appTooltip.hpp's
      // own findCaps()), since nothing else in this class ever calls capCount() to
      // trigger it — without this the shake ran but painted nothing.
      row.chip->capCount();
      // A blank run of spaces as wide as the chip: the chip paints over it and the row's
      // real shortcut() is silenced below. Measured, not divided by one space's advance —
      // that advance is a rounded int, so the quotient over-reserves and leaves a dead
      // band past the chip that this menu's compact mode exists to remove.
      const QFontMetrics fm = menu->fontMetrics();
      const int need = row.chip->width();
      // Seeded from one space's advance, then walked to fit: that advance is a rounded
      // int, so the quotient alone over-reserves and leaves a dead band past the chip
      // that this menu's compact mode exists to remove — but it lands within a space or
      // two, which is a couple of measurements rather than one per space.
      const int spaceW = std::max(1, fm.horizontalAdvance(QLatin1String(" ")));
      QString run(std::max(0, need / spaceW), QLatin1Char(' '));
      while (fm.horizontalAdvance(run) < need) run += QLatin1Char(' ');
      while (!run.isEmpty() && fm.horizontalAdvance(run.left(run.size() - 1)) >= need)
        run.chop(1);
      a->setText(label + QLatin1Char('\t') + run);
      // The action keeps its real shortcut() active (still fires) but under Fusion
      // style, a manual "\t"+padding text plus a still-live native shortcut makes
      // QMenuPrivate double up the reserved shortcut column, blowing out the
      // icon-to-label gap. The chip already shows the combo, so silencing the native
      // one here costs nothing.
      a->setShortcut(QKeySequence());
      rows_.push_back(row);
    }
    installPlacer(menu);
    QObject::connect(menu, &QMenu::hovered, this, [this](QAction* a) { shakeRow(a); });
  }

  // Chips can only be positioned once the menu is actually laid out and placed —
  // actionGeometry() is meaningless before Show, same constraint MenuReveal has. Also
  // re-placed on a short live poll (browser parity: contextMenu.js's LIVE_SYNC_INTERVAL_MS)
  // while the menu stays open: an action can go visible/invisible out from under an
  // already-open menu (e.g. toggling the image filter while its popup is up), and
  // place() is otherwise never re-run to notice.
  void MenuHotkeyChips::installPlacer(QMenu* menu) {
    auto* liveSync = new QTimer(this);
    liveSync->setInterval(120);
    QObject::connect(liveSync, &QTimer::timeout, this, [this, menu] { place(menu); });
    QObject::connect(menu, &QMenu::aboutToShow, this, [this, menu, liveSync] {
      place(menu);
      liveSync->start();
    });
    QObject::connect(menu, &QMenu::aboutToHide, this, [this, liveSync] {
      liveSync->stop();
      current_ = nullptr;
    });
    place(menu);   // already visible (a re-entry) — no-op if geometry isn't ready yet
  }

  void MenuHotkeyChips::place(QMenu* menu) {
    // `rows_` is ONE list shared across the whole recursive wire() tree; each row
    // remembers its own level, so a level's poll tick touches only its own chips.
    for (auto& row : rows_) {
      if (row.menu != menu || !row.chip || !row.action) continue;
      const QRect r = menu->actionGeometry(row.action);
      // An invisible (or not-yet-laid-out) action has no real row to sit over — hide
      // the chip rather than leaving it wherever it last WAS valid.
      if (!r.isValid() || !row.action->isVisible()) { row.chip->hide(); continue; }
      if (row.chip->isHidden()) row.chip->show();
      // Right-pinned like the browser's .ctx-hotkey — one constant for every row.
      // The QSS QMenu::item right padding covers both the native shortcut column and
      // a submenu arrow; compact menus carry no submenu rows and sit tighter. The
      // chip was sized once at construction — only a real position change moves it.
      const int kMenuRightPad = compact_ ? 10 : gui::kMenuItemRightPadPx;
      const int x = r.right() - kMenuRightPad - row.chip->width();
      const QPoint at(qMax(r.left(), x), r.top() + (r.height() - row.chip->height()) / 2);
      if (row.chip->pos() != at) row.chip->move(at);
    }
  }

  void MenuHotkeyChips::shakeRow(QAction* a) {
    // QMenu::hovered(QAction*) re-fires for the action already being shaken — Qt's own
    // hover bookkeeping, or mouse jitter within the same row (menuShimmer.hpp's sweep()
    // hit the same thing). Only a genuinely new row restarts the shake.
    if (a == current_) return;
    current_ = a;
    for (auto& row : rows_) {
      if (row.action != a || !row.chip) continue;
      if (!row.shake) {
        row.shake = new QVariantAnimation(this);
        row.shake->setDuration(gui::AppTooltip::kShakeMs);
        row.shake->setStartValue(0.0);
        row.shake->setEndValue(1.0);
        gui::TipBody* chip = row.chip;
        QObject::connect(row.shake, &QVariantAnimation::valueChanged, this,
                         [chip](const QVariant& v) { chip->setShake(v.toDouble()); });
        QObject::connect(row.shake, &QVariantAnimation::finished, this,
                         [chip] { chip->settle(); });
      }
      row.shake->stop();
      row.chip->settle();
      row.shake->start();
    }
  }

}  // namespace stencil::support
