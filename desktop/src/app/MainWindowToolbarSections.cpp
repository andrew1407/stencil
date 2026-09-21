#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "iconSet.hpp"
#include "LogoHoverFx.hpp"
#include "modalReveal.hpp"
#include "numericInput.hpp"
#include "SearchCombo.hpp"
#include "ControlsPill.hpp"
#include "OpenImageButton.hpp"
#include "theme.hpp"
#include "../support/controlReveal.hpp"   // section buttons come and go as sand
#include "../support/iconMotion.hpp"
#include "../support/ShimmerOverlay.hpp"
#include "../support/WrapRow.hpp"     // rows wrap like the browser's, never overflow into "»"

#include <QAbstractSpinBox>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

// MainWindow's toolbar assembly: the named tool sections.

namespace stencil::gui {

  QWidget* MainWindow::makeToolSection(const QString& title, const QList<QAction*>& actions,
                                       const QList<QWidget*>& extras,
                                       const QList<QWidget*>& leading) {
    auto* section = new QWidget(this);
    auto* col = new QVBoxLayout(section);
    // 4px sides, not 6: fourteen clusters is a lot of width; top/bottom are the browser's .ctrl-
    // section padding.
    col->setContentsMargins(4, 4, 4, 4);
    // The browser's .ctrl-section-label: 4px padding + 4px margin under the text (css/layout.css).
    col->setSpacing(8);
    auto* label = new QLabel(title.toUpper(), section);
    label->setObjectName("sectionLabel");
    // Colour comes from the theme sheet (QLabel#sectionLabel) so it re-themes on a swap.
    label->setStyleSheet("font-size:9px;font-weight:700;letter-spacing:0.6px;");
    label->setAlignment(Qt::AlignLeft);   // left-aligned header, matching the browser sections
    // Fixed, or the QVBoxLayout hands spare height to the caption and pushes the controls down.
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    col->addWidget(label);
    auto* rowWidget = new QWidget(section);
    // One row height across every section so inputs and icons share a baseline.
    rowWidget->setMinimumHeight(TOOL_ROW_H);
    // A disabled widget receives no mouse events, so the row carries the browser's `cursor: not-
    // allowed` for its children (eventFilter swaps it).
    rowWidget->setMouseTracking(true);
    rowWidget->setProperty("toolRow", true);
    auto* row = new QHBoxLayout(rowWidget);
    row->setContentsMargins(0, 0, 0, 0);
    // The browser's .ctrl-section-row gap is 8px; 5 matches its rhythm after Qt's button padding
    // and keeps the third row out of QToolBar's "»" at 1200px.
    row->setSpacing(5);
    // One popover wiring (Alt-peek / dblclick / right-click), shared by the action loop and
    // leading widgets like Open Image.
    const auto wirePopover = [this](QToolButton* btn, QAction* a) {
      if (!a || !pop.dialogActions.contains(a)) return;
      pop.buttons.insert(btn, a);
      btn->installEventFilter(this);
      btn->setContextMenuPolicy(Qt::CustomContextMenu);
      connect(btn, &QToolButton::customContextMenuRequested, this, [this, a, btn] {
        if (!a->isEnabled()) return;   // a disabled icon opens nothing — mini window included
        if (pop.clickTimer) pop.clickTimer->stop();
        pop.peekAction.clear();   // a deliberate open is sticky — Alt release keeps it
        stopLingerPoll();         // a lingering window's poll must not close THIS open
        pop.anchor = btn;
        a->trigger();
      });
    };
    for (QWidget* w : leading) {
      if (!w) { qWarning("makeToolSection(%s): null leading widget skipped", qPrintable(title)); continue; }
      if (auto* tb = qobject_cast<QToolButton*>(w); tb && tb->defaultAction()) {
        tb->setProperty("toolSection", title);
        wirePopover(tb, tb->defaultAction());
      }
      w->setParent(rowWidget);
      row->addWidget(w, 0, Qt::AlignVCenter);
    }
    for (QAction* a : actions) {
      // A section naming an action its row has not created yet must not take the app down.
      if (!a) { qWarning("makeToolSection(%s): null action skipped", qPrintable(title)); continue; }
      auto* btn = new QToolButton(rowWidget);
      btn->setProperty("toolSection", title);   // which cluster it belongs to (see styleDangerToolButtons)
      btn->setDefaultAction(a);   // reflects the action's icon / tooltip / enabled / checked state
      btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
      btn->setAutoRaise(true);
      btn->setIconSize(QSize(TOOL_ICON, TOOL_ICON));
      // A standalone QToolButton does not auto-hide with its action, so mirror visibility for the
      // gated ones.
      btn->setVisible(a->isVisible());
      connect(a, &QAction::changed, btn, [this, a, btn] {
        // Arrivals ride the sand, a leaving icon goes at once (refreshActions' swapShown twin); a
        // no-change costs nothing.
        const bool show = sectionButtonVisible(a, btn);
        revealControls(btn, show, /*dust=*/show);
        btn->setCursor(a->isEnabled() ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
      });
      btn->setCursor(a->isEnabled() ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
      if (a == actStartDraw) {
        startDrawBtn = btn;   // styled accent while a draw session is active
        // The browser's #draw-toggle: label beside the icon (iconText() is the short
        // "Start"/"Stop"). Width pinned later by refreshActions, once icons and padding exist.
        btn->setObjectName("drawFaceBtn");   // theme.cpp: the pair's larger word
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setIconSize(QSize(TOOL_ICON + FACE_ICON_GAP, TOOL_ICON));   // see FACE_ICON_GAP
      }
      // The event filter owns click/dblclick; right-click arrives here.
      wirePopover(btn, a);
      row->addWidget(btn, 0, Qt::AlignVCenter);
    }
    for (QWidget* ex : extras) {
      if (!ex) { qWarning("makeToolSection(%s): null widget skipped", qPrintable(title)); continue; }
      // A trailing action button takes the same fill treatment (browser's filled-red .danger).
      if (auto* tb = qobject_cast<QToolButton*>(ex); tb && tb->defaultAction())
        tb->setProperty("toolSection", title);
      ex->setParent(rowWidget);
      row->addWidget(ex, 0, Qt::AlignVCenter);
    }
    // Left-packed under its caption (browser .ctrl-section-row): aligned inside the row, not
    // stretched, so rows with a growing field still hand it the leftover.
    bool grows = false;
    for (int i = 0; i < row->count() && !grows; ++i)
      if (QWidget* w = row->itemAt(i)->widget())
        grows = (w->sizePolicy().horizontalPolicy() & QSizePolicy::ExpandFlag) != 0;
    if (!grows) row->setAlignment(Qt::AlignLeft);
    col->addWidget(rowWidget);
    return section;
  }
}  // namespace stencil::gui

