#pragma once
// Bordered keycap chips for a context menu's shortcut hints — the desktop port of the
// browser's .ctx-hotkey .tip-key (components.css): a QMenu can't rich-render an action's
// own text, so every hotkey-bearing row's native "\tCtrl+C" is replaced with a blank
// "\t"-plus-spaces run measured to the SAME width (so QMenu still sizes/aligns the row
// exactly as it would have) and a small click-through gui::TipBody is painted over the
// blank spot instead — the exact cap-diffing/shake widget the app tooltip uses. Caps
// shake once per row on HOVER (browser: .ctx-item:hover .tip-key), not once on open like
// the tooltip's own appearance shake.
//
// Header-only and Q_OBJECT-free (lambdas only, like menuReveal.cpp's MenuReveal/
// MenuFlight — no MOC needed). Construct ONCE, as a STACK-local variable declared right
// AFTER the menu (`StayOpenMenu menu(this); support::MenuHotkeyChips chips(&menu); ...`),
// so C++ destroys it before the menu itself: restoring the shared actions' real text at
// that point is safe, whereas parenting this to the menu is not — a QAction::setText()
// during the menu's OWN QObject::deleteChildren() teardown reenters a findChildren() over
// the same half-destroyed tree (crashed via styleDangerToolButtons' changed() handler).
// Never heap-allocate or parent this to the menu; it must outlive exec() but not the menu.
#include "appTooltip.hpp"   // gui::TipBody
#include "theme.hpp"        // gui::kMenuItemRightPadPx
#include "tipContent.hpp"   // comboKeycapsHtml, currentPalette

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidgetAction>

#include <algorithm>
#include <vector>

namespace stencil::support {

  class MenuHotkeyChips : public QObject {
   public:
    explicit MenuHotkeyChips(QMenu* root, bool compact = false);
    ~MenuHotkeyChips() override;

   protected:
    bool eventFilter(QObject*, QEvent* e) override;

   private:
    struct Row {
      QPointer<QAction> action;
      QPointer<QMenu> menu;   // the level this row lives on — place() filters by it
      QString originalText;
      QKeySequence originalShortcut;
      gui::TipBody* chip = nullptr;
      QVariantAnimation* shake = nullptr;
    };
    bool compact_ = false;

    static QString comboOf(QAction* a);

    void wire(QMenu* menu, const gui::Palette& pal);

    void installPlacer(QMenu* menu);

    void place(QMenu* menu);

    void shakeRow(QAction* a);

    std::vector<Row> rows_;
    // Which row's shake is currently playing (or just played) — see shakeRow()'s own
    // comment. Reset on aboutToHide (installPlacer) so a FRESH open always shakes its
    // first-hovered row again, rather than reading it as "already current" from last time.
    QPointer<QAction> current_;
  };

}  // namespace stencil::support
