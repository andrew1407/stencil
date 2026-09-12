#pragma once
// Keycap chips for a context menu's shortcut hints (browser .ctx-hotkey .tip-key): the
// row's native "\tCtrl+C" is replaced by a blank run of the SAME width and a click-through
// gui::TipBody is painted over it. Q_OBJECT-free. Construct ONCE as a STACK-local declared
// right AFTER the menu, never heap-allocated or parented to it: a QAction::setText()
// during the menu's own deleteChildren() reenters a findChildren() over the half-destroyed
// tree (crashed via styleDangerToolButtons' changed() handler).
#include "AppTooltip.hpp"   // gui::TipBody
#include "theme.hpp"        // gui::MENU_ITEM_RIGHT_PAD_PX
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
    // Reset on aboutToHide so a FRESH open shakes its first-hovered row again.
    QPointer<QAction> current_;
  };

}  // namespace stencil::support
