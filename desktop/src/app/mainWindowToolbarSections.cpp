#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasWidget.hpp"
#include "iconSet.hpp"
#include "logoHoverFx.hpp"
#include "modalReveal.hpp"
#include "numericInput.hpp"
#include "searchCombo.hpp"
#include "controlsPill.hpp"
#include "openImageButton.hpp"
#include "theme.hpp"
#include "../support/controlReveal.hpp"   // section buttons come and go as sand
#include "../support/iconMotion.hpp"
#include "../support/shimmerOverlay.hpp"
#include "../support/wrapRow.hpp"     // rows wrap like the browser's, never overflow into "»"

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

// MainWindow's toolbar assembly: the header row, the three tool rows and their
// sections, and the style/formula wiring. Split from mainWindow.cpp; same class,
// definitions only.

namespace stencil::gui {

  QWidget* MainWindow::makeToolSection(const QString& title, const QList<QAction*>& actions,
                                       const QList<QWidget*>& extras,
                                       const QList<QWidget*>& leading) {
    auto* section = new QWidget(this);
    auto* col = new QVBoxLayout(section);
    // 4px sides, not 6: fourteen clusters is a lot of width to give away. Top and bottom
    // are the browser's .ctrl-section padding, which is what a wrapped line's caption needs.
    col->setContentsMargins(4, 4, 4, 4);
    // The gap between the caption and its controls: the browser's .ctrl-section-label
    // runs 4px of padding plus a 4px margin under the text (css/layout.css).
    col->setSpacing(8);
    auto* label = new QLabel(title.toUpper(), section);
    label->setObjectName("sectionLabel");
    // Colour comes from the theme sheet (QLabel#sectionLabel) so it re-themes on a swap and
    // can be darker in the light theme; the rest of the treatment stays inline.
    label->setStyleSheet("font-size:9px;font-weight:700;letter-spacing:0.6px;");
    label->setAlignment(Qt::AlignLeft);   // left-aligned header, matching the browser sections
    // Fixed, or the QVBoxLayout hands a short section's spare height to the caption and
    // pushes the controls under it down — combos then sat lower than the icon rows.
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    col->addWidget(label);
    auto* rowWidget = new QWidget(section);
    // One row height across every section, with the controls centred in it, so inputs and
    // icons share a baseline no matter which is taller.
    rowWidget->setMinimumHeight(kToolRowH);
    // The browser paints `cursor: not-allowed` over a disabled control; Qt cannot be asked
    // to, because a DISABLED widget receives no mouse events at all and its own cursor is
    // never applied — the moves fall through to this row. So the row carries the cursor for
    // its children: tracking on, and eventFilter swaps it as the pointer crosses a dead icon.
    rowWidget->setMouseTracking(true);
    rowWidget->setProperty("toolRow", true);
    auto* row = new QHBoxLayout(rowWidget);
    row->setContentsMargins(0, 0, 0, 0);
    // The gap BETWEEN a cluster's controls. The browser's .ctrl-section-row runs an 8px gap;
    // 5 lands the icon buttons at the same rhythm once Qt's own button padding is counted, and
    // is as wide as the rows can go before the third one tips into QToolBar's "»" at 1200px.
    row->setSpacing(5);
    // Dialog-opening buttons answer the popover gestures (Alt-peek / dblclick /
    // right-click → the compact anchored shape). One wiring, shared by the action
    // loop below AND leading widgets like the labelled Open Image button, which opens
    // the same dialog as the icons.
    const auto wirePopover = [this](QToolButton* btn, QAction* a) {
      if (!a || !pop_.dialogActions.contains(a)) return;
      pop_.buttons.insert(btn, a);
      btn->installEventFilter(this);
      btn->setContextMenuPolicy(Qt::CustomContextMenu);
      connect(btn, &QToolButton::customContextMenuRequested, this, [this, a, btn] {
        if (!a->isEnabled()) return;   // a disabled icon opens nothing — mini window included
        if (pop_.clickTimer) pop_.clickTimer->stop();
        pop_.peekAction.clear();   // a deliberate open is sticky — Alt release keeps it
        stopLingerPoll();         // a lingering window's poll must not close THIS open
        pop_.anchor = btn;
        a->trigger();
      });
    };
    // Widgets that come BEFORE the icons (the browser's EDIT starts with the filter combo).
    for (QWidget* w : leading) {
      if (!w) { qWarning("makeToolSection(%s): null leading widget skipped", qPrintable(title)); continue; }
      // The labelled Open Image button is a section button too (browser #load-image-btn).
      if (auto* tb = qobject_cast<QToolButton*>(w); tb && tb->defaultAction()) {
        tb->setProperty("toolSection", title);
        wirePopover(tb, tb->defaultAction());
      }
      w->setParent(rowWidget);
      row->addWidget(w, 0, Qt::AlignVCenter);
    }
    for (QAction* a : actions) {
      // A section naming an action its row has not created yet must not take the app
      // down — skip it. (Rows are built in order, and moving a section between rows is
      // exactly how a null slips in here.)
      if (!a) { qWarning("makeToolSection(%s): null action skipped", qPrintable(title)); continue; }
      auto* btn = new QToolButton(rowWidget);
      btn->setProperty("toolSection", title);   // which cluster it belongs to (see styleDangerToolButtons)
      btn->setDefaultAction(a);   // reflects the action's icon / tooltip / enabled / checked state
      btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
      btn->setAutoRaise(true);
      btn->setIconSize(QSize(kToolIcon, kToolIcon));
      // A standalone QToolButton does NOT auto-hide when its action is hidden (unlike a toolbar
      // action-widget), so mirror visibility explicitly for the gated ones (Open-in, Clear-project).
      btn->setVisible(a->isVisible());
      connect(a, &QAction::changed, btn, [this, a, btn] {
        // ARRIVALS ride the sand — the slot slides open under gathering motes — while
        // a leaving icon goes at once, no dust-out (user decision, refreshActions'
        // swapShown twin); a no-change costs nothing.
        const bool show = sectionButtonVisible(a, btn);
        revealControls(btn, show, /*dust=*/show);
        btn->setCursor(a->isEnabled() ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
      });
      btn->setCursor(a->isEnabled() ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
      if (a == actStartDraw_) {
        startDrawBtn_ = btn;   // styled accent while a draw session is active
        // The one Start/Stop control (the browser's #draw-toggle): label BESIDE the icon.
        // QToolButton renders the action's iconText(), which is why these two actions carry
        // the short "Start"/"Stop" while their menu entries stay "Start Drawing"/"Stop
        // Drawing". Its width is pinned later, by refreshActions — the themed icons and the
        // stylesheet padding both land after this runs, so measuring here comes out short.
        btn->setObjectName("drawFaceBtn");   // theme.cpp: the pair's larger word
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setIconSize(QSize(kToolIcon + kFaceIconGap, kToolIcon));   // see kFaceIconGap
      }
      // Dialog-opening icons: the popover gestures (see the block above the toolbar
      // sections). The event filter owns click/dblclick; right-click arrives here.
      wirePopover(btn, a);
      row->addWidget(btn, 0, Qt::AlignVCenter);
    }
    for (QWidget* ex : extras) {
      if (!ex) { qWarning("makeToolSection(%s): null widget skipped", qPrintable(title)); continue; }
      // A trailing action button is a section button too (View's clear-lines), so it takes the
      // same fill treatment — without the tag it stayed a bordered ghost with a red glyph
      // instead of the browser's filled-red .danger button.
      if (auto* tb = qobject_cast<QToolButton*>(ex); tb && tb->defaultAction())
        tb->setProperty("toolSection", title);
      ex->setParent(rowWidget);
      row->addWidget(ex, 0, Qt::AlignVCenter);
    }
    // Left-packed under its caption, like the browser's .ctrl-section-row: a section
    // whose caption is wider than its two or three icons (CONNECTIONS & CHAT) keeps them
    // at the left edge instead of Qt spreading the caption's extra width around them.
    // The layout is aligned INSIDE the row (not stretched — a stretch item made every
    // section grow to share the toolbar's width, and rows carrying a growing field like
    // the formula inputs must still hand them the leftover, so those are left alone).
    bool grows = false;
    for (int i = 0; i < row->count() && !grows; ++i)
      if (QWidget* w = row->itemAt(i)->widget())
        grows = (w->sizePolicy().horizontalPolicy() & QSizePolicy::ExpandFlag) != 0;
    if (!grows) row->setAlignment(Qt::AlignLeft);
    col->addWidget(rowWidget);
    return section;
  }
}  // namespace stencil::gui

