#include "MenuHotkeys.hpp"

namespace stencil::support {

  // `compact` is for a FLAT list of hotkey rows, never one with submenu-opener rows.
  // Never setFixedWidth: QMenuPrivate::calcActionRects() sizes the shortcut column from
  // sizeHint regardless of the frame, so the chip would spill past a clipped edge.
  MenuHotkeyChips::MenuHotkeyChips(QMenu* root, bool compact) : compact_(compact) {
    if (compact_ && root) root->setStyleSheet(gui::compactMenuQss());
    wire(root, gui::currentPalette());
  }

  MenuHotkeyChips::~MenuHotkeyChips() {
    for (auto& row : rows_) {
      // The menu outlives this object (rebuilt per aboutToShow): an orphaned chip would
      // overlap a later wiring's row. The shake closes over the raw pointer — stop it first.
      if (row.shake) row.shake->stop();
      delete row.chip;
      row.chip = nullptr;
      if (!row.action) continue;
      row.action->setText(row.originalText);
      row.action->setShortcut(row.originalShortcut);
    }
  }

  // Leaving a row fires no hovered(); the chip settles and the guard resets, so returning
  // reads as fresh.
  bool MenuHotkeyChips::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::Leave || e->type() == QEvent::Hide) {
      stopShake(current_.value(o));
      current_.remove(o);
    }
    return false;
  }

  // An action shared by several menus may already be chipped by a live MenuHotkeyChips
  // (its text is the padded "\t   " stand-in) — read the cached combo then, else the live
  // shortcut(): syncSplitCopyDownloadSlot moves shortcuts between actions at runtime.
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
    // A submenu may carry its own compact MenuHotkeyChips already; a second wiring would
    // double the chips and fight over the actions' text.
    menu->setProperty("stencilHotkeyChipsWired", true);
    menu->installEventFilter(this);
    for (QAction* a : menu->actions()) {
      if (a->isSeparator() || qobject_cast<QWidgetAction*>(a)) continue;
      if (QMenu* sub = a->menu()) {
        if (!sub->property("stencilHotkeyChipsWired").toBool()) wire(sub, pal);
      }
      const QString combo = comboOf(a);
      if (combo.isEmpty()) continue;
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
      // Finds the keycap regions now; nothing else here calls capCount(), and the shake
      // paints nothing without it.
      row.chip->capCount();
      // A run of spaces as wide as the chip, measured: one space's advance is a rounded
      // int, so the plain quotient over-reserves a dead band past the chip.
      const QFontMetrics fm = menu->fontMetrics();
      const int need = row.chip->width();
      const int spaceW = std::max(1, fm.horizontalAdvance(QLatin1String(" ")));
      QString run(std::max(0, need / spaceW), QLatin1Char(' '));
      while (fm.horizontalAdvance(run) < need) run += QLatin1Char(' ');
      while (!run.isEmpty() && fm.horizontalAdvance(run.left(run.size() - 1)) >= need)
        run.chop(1);
      a->setText(label + QLatin1Char('\t') + run);
      // Under Fusion a "\t" padding text plus a live native shortcut makes QMenuPrivate
      // reserve the shortcut column twice.
      a->setShortcut(QKeySequence());
      rows_.push_back(row);
    }
    installPlacer(menu);
    QObject::connect(menu, &QMenu::hovered, this,
                     [this, menu](QAction* a) { shakeRow(menu, a); });
  }

  // actionGeometry() is meaningless before Show. The live poll (browser contextMenu.js
  // LIVE_SYNC_INTERVAL_MS) catches an action going visible/invisible under an open menu.
  void MenuHotkeyChips::installPlacer(QMenu* menu) {
    auto* liveSync = new QTimer(this);
    liveSync->setInterval(120);
    QObject::connect(liveSync, &QTimer::timeout, this, [this, menu] { place(menu); });
    QObject::connect(menu, &QMenu::aboutToShow, this, [this, menu, liveSync] {
      place(menu);
      liveSync->start();
    });
    QObject::connect(menu, &QMenu::aboutToHide, this, [this, menu, liveSync] {
      liveSync->stop();
      stopShake(current_.value(menu));
      current_.remove(menu);
    });
    place(menu);
  }

  void MenuHotkeyChips::place(QMenu* menu) {
    for (auto& row : rows_) {
      if (row.menu != menu || !row.chip || !row.action) continue;
      const QRect r = menu->actionGeometry(row.action);
      if (!r.isValid() || !row.action->isVisible()) { row.chip->hide(); continue; }
      if (row.chip->isHidden()) row.chip->show();
      // Right-pinned like the browser's .ctx-hotkey; compact menus have no submenu arrow.
      const int MENU_RIGHT_PAD = compact_ ? 10 : gui::MENU_ITEM_RIGHT_PAD_PX;
      const int x = r.right() - MENU_RIGHT_PAD - row.chip->width();
      const QPoint at(qMax(r.left(), x), r.top() + (r.height() - row.chip->height()) / 2);
      if (row.chip->pos() != at) row.chip->move(at);
    }
  }

  void MenuHotkeyChips::shakeRow(QMenu* menu, QAction* a) {
    // hovered() re-fires for the row already shaking on every mouse move, and reaches every
    // menu in the caused stack besides, so a SUBMENU's row arrives here for its parent too.
    // Only a new row OF THIS LEVEL restarts.
    if (!menu->actionGeometry(a).isValid()) return;
    QPointer<QAction>& cur = current_[menu];
    if (a == cur) return;
    stopShake(cur);   // the row being left settles rather than shaking on without a pointer
    cur = a;
    for (auto& row : rows_) {
      if (row.action != a || !row.chip) continue;
      if (!row.shake) {
        row.shake = new QVariantAnimation(this);
        row.shake->setDuration(gui::AppTooltip::SHAKE_MS);
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

  void MenuHotkeyChips::stopShake(QAction* a) {
    if (!a) return;
    for (auto& row : rows_) {
      if (row.action != a || !row.chip) continue;
      if (row.shake) row.shake->stop();
      row.chip->settle();
    }
  }

}  // namespace stencil::support
