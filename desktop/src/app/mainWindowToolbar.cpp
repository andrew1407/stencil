#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasWidget.hpp"
#include "iconSet.hpp"
#include "logoHoverFx.hpp"
#include "modalReveal.hpp"
#include "numericInput.hpp"
#include "searchCombo.hpp"
#include "theme.hpp"
#include "../support/controlReveal.hpp"   // section buttons come and go as sand
#include "../support/iconMotion.hpp"
#include "../support/shimmerOverlay.hpp"

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

  // Three rows, in the browser's order (toolbar.js): Image·Projects·Connections·Edit,
  // then Line·Point·Draw·View, then Zoom·Page·Data·Settings. addToolBar/addToolBarBreak
  // sequencing fixes the visual order, so the sub-builders MUST run in this order —
  // buildDrawViewToolbar appends to the row buildStyleToolbar opened.
  void MainWindow::buildToolbar() {
    buildMainToolbar();
    buildStyleToolbar();
    buildDrawViewToolbar();
    buildPageFormulaToolbar();
    buildImageInfoBar();
    // Shared hover shimmer on every interactive control across the toolbar rows (buttons, combos,
    // spinboxes, the f(x,y) checkbox, text fields) so the whole toolbar has one consistent hover
    // treatment — not just the makeToolSection icons.
    for (QToolBar* tb : findChildren<QToolBar*>()) {
      for (QToolButton* b : tb->findChildren<QToolButton*>())
        if (b != logoBtn_) installHoverShimmer(b);   // skip the logo (its own art/affordance)
      for (QComboBox* c : tb->findChildren<QComboBox*>()) installHoverShimmer(c);
      for (QAbstractSpinBox* s : tb->findChildren<QAbstractSpinBox*>()) installHoverShimmer(s);
      for (QCheckBox* c : tb->findChildren<QCheckBox*>()) installHoverShimmer(c);   // f(x,y) pill
      for (QLineEdit* le : tb->findChildren<QLineEdit*>())
        if (le != projectName_) installHoverShimmer(le);   // skip the rename field
    }
    // Hand cursor on everything clickable, like the browser's `cursor: pointer` buttons.
    // Combos and text fields keep Qt's arrow / I-beam, which is what the browser shows too.
    for (QToolBar* tb : findChildren<QToolBar*>())
      for (QAbstractButton* b : tb->findChildren<QAbstractButton*>())
        if (!b->testAttribute(Qt::WA_SetCursor))
          b->setCursor(b->isEnabled() ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
    // The buttons only exist now, so the destructive ones get their filled-red face here
    // (styleActionIcons already ran, with nothing to find).
    styleDangerToolButtons();
  }

  // The f(x,y) transform fields (browser #formula-inputs, toolbar.js): two bare
  // monospace fields (each placeholder already reads "x(x)=" / "y(y)="), living
  // inside the Formula section so they share the pill's row height and centring.
  void MainWindow::buildFormulaFields() {
    formulaGroup_ = new QWidget(this);
    formulaGroup_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* fl = new QHBoxLayout(formulaGroup_);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(6);   // the browser's gap between the two fields
    const auto makeField = [this](const char* placeholder, const char* tip) {
      auto* e = new QLineEdit(formulaGroup_);
      e->setPlaceholderText(placeholder);
      e->setToolTip(tip);
      // Monospace, like the browser's — a formula is code, and the digits have to line up.
      QFont f = e->font();
      f.setFamily(QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
      e->setFont(f);
      // Elastic between the browser's width and the floor: the row hands its slack to the
      // pair (the FORMULA cluster closes that row), so they read like the browser's on a
      // normal window and give the space back on a narrow one instead of overflowing.
      e->setMinimumWidth(kFormulaFieldMinW);
      e->setMaximumWidth(kFormulaFieldW);
      e->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      return e;
    };
    formulaX_ = makeField("x(x)=", "Transform formula for x — e.g. x*2 + 1 (empty = identity)");
    formulaY_ = makeField("y(y)=", "Transform formula for y — e.g. y/2 (empty = identity)");
    // Icon-only, like the browser's #formula-error (its alert glyph with a title) — the
    // tooltip carries the words. Named so tests and restyling can find it without matching
    // on its text, and kept in the danger colour it has always had.
    formulaError_ = new QLabel("\u26A0", formulaGroup_);
    formulaError_->setObjectName("formulaError");
    formulaError_->setToolTip("Invalid formula");
    formulaError_->setStyleSheet("color:#d9534f;");
    formulaError_->setVisible(false);
    // The fields carry the stretch and the tail carries none, so the group's growth goes
    // into the pair until they reach the browser's width and only the excess lands in the
    // tail. With no tail at all that excess came out as SPACING and the pair drifted apart.
    fl->addWidget(formulaX_, 1);
    fl->addWidget(formulaY_, 1);
    fl->addWidget(formulaError_);
    fl->addStretch(0);
  }

  QWidget* MainWindow::makeToolSection(const QString& title, const QList<QAction*>& actions,
                                       const QList<QWidget*>& extras,
                                       const QList<QWidget*>& leading) {
    auto* section = new QWidget(this);
    auto* col = new QVBoxLayout(section);
    col->setContentsMargins(6, 1, 6, 1);
    col->setSpacing(3);
    auto* label = new QLabel(title.toUpper(), section);
    label->setObjectName("sectionLabel");
    label->setStyleSheet("color:#7a828c;font-size:9px;font-weight:700;letter-spacing:0.6px;");
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
    // loop below AND leading widgets like the labelled Open Image button — which
    // opens the same dialog as the icons and used to miss the gestures entirely.
    const auto wirePopover = [this](QToolButton* btn, QAction* a) {
      if (!a || !popoverDialogActions_.contains(a)) return;
      popoverButtons_.insert(btn, a);
      btn->installEventFilter(this);
      btn->setContextMenuPolicy(Qt::CustomContextMenu);
      connect(btn, &QToolButton::customContextMenuRequested, this, [this, a, btn] {
        if (!a->isEnabled()) return;   // a disabled icon opens nothing — mini window included
        if (popoverClickTimer_) popoverClickTimer_->stop();
        altPeekAction_.clear();   // a deliberate open is sticky — Alt release keeps it
        stopLingerPoll();         // a lingering window's poll must not close THIS open
        popoverAnchor_ = btn;
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
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
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
    col->addWidget(rowWidget);
    return section;
  }

  void MainWindow::buildMainToolbar() {
    // ── Header row (always visible): the "Controls" collapse pill + the project-name group.
    // This row stays put while the tool rows below (Main / Page&Formula / Style) slide open/closed,
    // exactly like the browser's header that keeps the "⌃ Controls" pill + title when the body hides.
    headerToolbar_ = addToolBar("Header");
    headerToolbar_->setObjectName("headerToolbar");  // named for QMainWindow::saveState
    headerToolbar_->setMovable(false);
    // App logo (the mini S mark, mirrors the browser's top-left logo). Clicking it cycles the theme
    // accent to the next preset — the same affordance as the browser's clickable logo.
    logoBtn_ = new QToolButton(this);
    logoBtn_->setCursor(Qt::PointingHandCursor);
    logoBtn_->setIconSize(QSize(kHeaderLogo, kHeaderLogo));
    logoBtn_->setIcon(QIcon(makeLogoPixmap(kHeaderLogo)));
    logoBtn_->setToolTip(QString());   // no tooltip on the logo
    // No hover highlight — flat, transparent, borderless (just the logo art).
    logoBtn_->setStyleSheet("QToolButton{border:none;background:transparent;padding:2px;}");
    // Single click cycles the accent, but DEFER it briefly so a double-click can pre-empt it and open
    // the custom-colour picker instead (mirrors the browser logo's click-vs-dblclick behaviour).
    logoClickTimer_ = new QTimer(this);
    logoClickTimer_->setSingleShot(true);
    connect(logoClickTimer_, &QTimer::timeout, this, [this] {
      const auto& presets = accentPresets();
      if (presets.empty()) return;
      int idx = -1;
      for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].key == settings_.accentColor) { idx = static_cast<int>(i); break; }
      auto next = settings_;
      // Browser parity: a CUSTOM colour (not a preset — idx < 0) resets to the default (violet);
      // otherwise advance to the next preset, wrapping.
      next.accentColor = idx < 0 ? presets.front().key : presets[(idx + 1) % presets.size()].key;
      applySettings(next, true);   // apply + persist (re-themes everything, incl. the logo frame)
    });
    connect(logoBtn_, &QToolButton::clicked, this, [this] {
      // The click that dismissed a popover is spent doing exactly that.
      if (popoverDismissClick_) { popoverDismissClick_ = false; return; }
      logoClickTimer_->start(250);
    });
    // Accent picker = a first-class popover (execMaybePopover); registering the
    // logo in popoverButtons_ gives it the shared Alt machinery. The logo keeps
    // its own click/dblclick gestures (excluded from popover presses in eventFilter).
    actAccent_ = new QAction(tr("Theme Color"), this);
    actAccent_->setObjectName("actAccent");
    connect(actAccent_, &QAction::triggered, this, [this] { openAccentPicker(); });
    popoverButtons_.insert(logoBtn_, actAccent_);
    // Right-click: the same popover, STICKY — the popover icons' right-click route
    // (makeToolSection): a deliberate open, so the Alt release never closes it.
    // QToolButton::clicked never fires for the right button, so opening it can never
    // arm the cycle timer above.
    logoBtn_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(logoBtn_, &QToolButton::customContextMenuRequested, this, [this] {
      if (popoverClickTimer_) popoverClickTimer_->stop();
      if (logoClickTimer_) logoClickTimer_->stop();
      altPeekAction_.clear();   // a deliberate open is sticky — Alt release keeps it
      stopLingerPoll();         // a lingering window's poll must not close THIS open
      popoverAnchor_ = logoBtn_;
      actAccent_->trigger();
    });
    logoBtn_->installEventFilter(this);   // catch double-click → custom colour picker (see eventFilter)
    // Hover fx (LogoHoverFx above): the browser's logo pulse/levitate/glow/ray loop.
    // Purely visual — the click-cycle and dblclick-picker gestures above are untouched.
    logoFx_ = new LogoHoverFx(
        logoBtn_, [this] { return makeLogoPixmap(kHeaderLogo); },
        [this] {
          const QColor a = accentPrimary(settings_.accentColor);
          return a.isValid() ? a : QColor("#7c3aed");
        });
    headerToolbar_->addWidget(logoBtn_);
    // "Controls" chevron pill — collapses/expands the tool rows (routes through actToolbars_ so the
    // View-menu entry + Alt+C hotkey stay in sync). Icon (chevron) themed in styleActionIcons.
    controlsPill_ = new QToolButton(this);
    controlsPill_->setObjectName("controlsPill");   // outlined pill, styled in theme.cpp
    // Its chevron's angle is STATE (toolbars shown/hidden), not hover feedback — the
    // browser's `[id^="toggle-"]` icon-motion opt-out.
    controlsPill_->setProperty(kNoIconMotionProperty, true);
    controlsPill_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    controlsPill_->setIconSize(QSize(kPillChevron, kPillChevron));   // scaled to the label, not the toolbar
    controlsPill_->setText("Controls");
    controlsPill_->setAutoRaise(true);
    controlsPill_->setCursor(Qt::PointingHandCursor);
    controlsPill_->setToolTip(QString("Show / hide the toolbars (%1)").arg(hotkey("toggleControls", "Alt+C")));
    connect(controlsPill_, &QToolButton::clicked, this, [this] { if (actToolbars_) actToolbars_->toggle(); });
    headerToolbar_->addWidget(controlsPill_);
    headerToolbar_->addSeparator();
    buildProjectNameGroup(headerToolbar_);
    // "Image Size: W × H px" readout. Created here but placed in its own full-width bar
    // BELOW the toolbars (see buildImageInfoBar) — browser parity with the #image-info bar,
    // left-aligned above the canvas rather than tucked in the top-right corner.
    imageSizeInfo_ = new QLabel(this);
    imageSizeInfo_->setStyleSheet("color:#9aa0a8;");
    // contentsMargins, not stylesheet `padding` — QLabel's sizeHint()/paint don't reliably
    // pick it up. 10px left/right (browser parity: css/layout.css .info padding: 10px);
    // 6px top/bottom, tighter than parity since the row read taller than it needed to.
    imageSizeInfo_->setContentsMargins(10, 6, 10, 6);
    imageSizeInfo_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    addToolBarBreak();

    // Two rows so nothing is pushed into QToolBar's "»" overflow (which is what
    // hid the formula inputs / custom-page inputs at normal window widths). Row 1:
    // file + drawing + history + zoom. Row 2: page size (+custom) + formulas.
    auto* tb = addToolBar("Main");
    tb->setObjectName("mainToolbar");  // named for QMainWindow::saveState
    tb->setMovable(false);
    // Icon-only with the shared line-art glyphs (styleActionIcons assigns them) +
    // the rich tooltips from mk(): compact, browser-faithful chrome that stays
    // narrow enough to avoid the "»" overflow even with the full action set.
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb->setIconSize(QSize(kToolIcon, kToolIcon));

    // Named, stacked groups (label ABOVE the icon row via makeToolSection) mirror the browser
    // topbar order: Image · Projects · Share · Edit · Draw · Zoom · Settings.
    // Blank-background swatch (browser parity): a colour button shown only for blank projects,
    // recolouring the fill (lines kept). Lives in the IMAGE group; gated in updateProjectTitle.
    blankColorBtn_ = new QToolButton(this);
    // Visuals come entirely from updateColorSwatch (the shared 46×26 chip
    // recipe), so it presents the SAME control height as the line-colour chip.
    // Labelled "Blank" beside the chip, as in the browser: a bare white swatch in the middle
    // of the Edit group says nothing about what it recolours.
    blankColorBtn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    blankColorBtn_->setText("Blank");
    blankColorBtn_->setToolTip("Blank background color — recolor this blank image (keeps your lines)");
    blankColorBtn_->setVisible(false);
    connect(blankColorBtn_, &QToolButton::clicked, this, [this] { setActiveBlankColor(); });

    // ── Modal popovers ──
    // The dialog-opening icons answer dblclick / right-click with the COMPACT anchored
    // shape of their dialog (execMaybePopover). A plain click keeps the full dialog, but
    // deferred one double-click interval (the logo pattern): the dialog's exec() blocks,
    // so an instant open would swallow the second click of every double-click.
    popoverDialogActions_ = {actOpen_, actOpenAnother_, actOpenIn_, actProjects_, actConnect_, actLinks_,
                             actDescription_, actKeywords_, actChat_, actShortcuts_, actSettings_, actInfo_};
    popoverClickTimer_ = new QTimer(this);
    popoverClickTimer_->setSingleShot(true);
    popoverClickTimer_->setInterval(250);
    connect(popoverClickTimer_, &QTimer::timeout, this, [this] {
      if (QAction* act = popoverPendingAction_.data()) {
        popoverPendingAction_.clear();
        act->trigger();
      }
    });

    // Image cluster (browser parity) + blank-fill swatch. Empty state: with no
    // image the cluster collapses to ONE labelled "Open Image" button; once an
    // image loads, refreshActions swaps it for the icon row, whose trailing icon
    // (actOpenAnother_) opens the same dialog as this button's actOpen_ — browser
    // parity: #load-image-btn ↔ #open-image-btn, same handler, different button/icon.
    openImageBtn_ = new QToolButton(this);
    openImageBtn_->setDefaultAction(actOpen_);
    openImageBtn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    openImageBtn_->setAutoRaise(true);
    openImageBtn_->setIconSize(QSize(kToolIcon, kToolIcon));
    {  // 14px label, matching the browser's #load-image-btn (its default button font).
      QFont f = openImageBtn_->font();
      f.setPixelSize(14);
      openImageBtn_->setFont(f);
    }
    // Qt draws a text-beside-icon label LEFT-aligned inside a box whose hint reserves more
    // slack on the right, so the pair sat off-centre (browser #load-image-btn centres it).
    // The padding is redistributed, not increased — same button width. Guarded by
    // openImageButtonLabelIsCentred() in the GUI tests.
    openImageBtn_->setStyleSheet("padding-left: 11px; padding-right: 3px;");
    imageSection_ = makeToolSection("Image",
                                    {actSaveImage_, actCopyImage_, actShareImage_, actOpenIn_, actOpenAnother_},
                                    {}, {openImageBtn_});
    tb->addWidget(imageSection_);
    tb->addSeparator();
    // Description & attributes: the saved project's description, keywords and links —
    // the browser's cluster between IMAGE and PROJECTS. All three gate on a saved,
    // non-incognito project (updateProjectTitle), so the whole group reads as one rule.
    tb->addWidget(makeToolSection("Description & attributes", {actDescription_, actKeywords_, actLinks_}));
    tb->addSeparator();
    // Projects = open editor list + save/open .stencil + live-sync, matching the browser's
    // PROJECTS cluster (layers / save / folder / refresh). Clear-project stays in the menu bar.
    tb->addWidget(makeToolSection("Projects", {actProjects_, actSaveProjectFile_, actOpenProjectFile_, actStencilLiveSync_, actDeleteProjectFile_}));
    tb->addSeparator();
    // Connections & chat: servers (connect to share/co-edit) + the AI-assistant sparkle
    // toggle (identical grouping to the browser toolbar; the image's source links live in
    // DESCRIPTION & ATTRIBUTES above, as in the browser).
    tb->addWidget(makeToolSection("Connections & chat", {actConnect_, actChat_}));
    tb->addSeparator();
    // Edit cluster (browser parity; blank-recolour chip closes it). The filter
    // combo + tint swatch open the group — built here, WIRED in buildStyleToolbar
    // (which runs next). data carries the canonical value.
    imageFilter_ = new SearchComboBox(this, /*searchable=*/false);
    imageFilter_->addItem("No Filter", "none");
    imageFilter_->addItem("B&W", "bw");
    imageFilter_->addItem("Sepia", "sepia");
    imageFilter_->addItem("Invert", "invert");
    imageFilter_->addItem("Contour", "contour");
    imageFilter_->addItem("Tint", "custom");
    imageFilter_->setToolTip("Image Filter");
    filterColorBtn_ = new QToolButton(this);
    filterColorBtn_->setToolTip("Tint color");
    updateColorSwatch(filterColorBtn_, filterColorValue_);
    filterColorBtn_->setVisible(false);   // shown only for the "custom" filter
    tb->addWidget(makeToolSection("Edit",
                                  {actCrop_, actRotateLeft_, actRotateRight_, actUndo_, actRedo_},
                                  {blankColorBtn_}, {imageFilter_, filterColorBtn_}));
    tb->addSeparator();
    // Draw = ONE Start/Stop button (refreshActions swaps its default action, like
    // the browser's single #draw-toggle) + the Line/Rect mode toggle. The toggle
    // is built here, wired in buildStyleToolbar (which needs the canvas signals).
    drawModeBtn_ = new QToolButton(this);
    // Icon + label (the glyph is themed in styleActionIcons / the drawModeChanged handler).
    drawModeBtn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    drawModeBtn_->setText("Line");
    drawModeBtn_->setAutoRaise(true);
    drawModeBtn_->setIconSize(QSize(kToolIcon, kToolIcon));
    drawModeBtn_->setToolTip("Drawing mode: Line (click to switch to Rectangle)");
    // Solid accent, permanently — it has no QAction for styleDangerToolButtons' own
    // fill pass to reach (its click is a plain connect(), not a default action), and
    // the browser's `#draw-mode-toggle` is a bare `<button>`, filled at rest too.
    drawModeBtn_->setProperty("toolFill", QStringLiteral("accent"));
    // Zoom = the editable percent combo + a Fit-to-window button (browser parity — the browser's
    // zoom section ends with the fit icon). The combo replaces the browser's +/- steppers (type or
    // pick a preset). Fit button built here so it sits AFTER the combo, like the browser.
    zoomFitBtn_ = new QToolButton(this);
    zoomFitBtn_->setDefaultAction(actFit_);
    // Ghost box, not the sections' accent fill (browser #zoom-fit): an outlined chip whose
    // glyph shape is what you read, so it doesn't look like a third zoom step after the %
    // field. styleDangerToolButtons honours this property by leaving the fill off.
    zoomFitBtn_->setProperty("toolGhost", true);
    zoomFitBtn_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    zoomFitBtn_->setAutoRaise(true);
    zoomFitBtn_->setIconSize(QSize(kToolIcon, kToolIcon));
    // Draw / Zoom / Settings are NOT on this row: all seven sections together need
    // ~1230px, so on a 1000px window Zoom and Settings (incognito!) were pushed clean
    // off the end with no way to reach them. They now sit on the rows below, which is
    // also exactly how the browser splits them — row 1 is Image · Projects ·
    // Connections & links · Edit there too.
  }

  // ── Project name field + inline-rename ✓/✗ (mirrors the browser topbar). The field shows the
  // active project's name and renames it inline, validated live: ✓ is enabled only for a changed,
  // valid (non-empty, ≤80, unique) name, with the reason on its tooltip when disabled. Enter = ✓,
  // Escape / click-away = ✗. Lives in the always-visible header row beside the "Controls" pill. ──
  void MainWindow::buildProjectNameGroup(QToolBar* tbName) {
    tbName->addWidget(new QLabel("Project: ", this));
    // ONE container for the field + its affordances (browser .project-name-field
    // parity): hover is the container's own gap-free rect, so sweeping between the
    // field and the ✎/🎨 buttons can never flicker the reveal (which replayed the
    // dust and re-armed the tooltip — user report). RowCard (connectDialog) pattern.
    nameGroup_ = new QWidget(this);
    auto* nameLay = new QHBoxLayout(nameGroup_);
    nameLay->setContentsMargins(0, 0, 0, 0);
    nameLay->setSpacing(4);
    projectName_ = new QLineEdit(nameGroup_);
    projectName_->setPlaceholderText("No project");
    projectName_->setToolTip(QString());   // no tooltip on the name field (the ✎ button has its own)
    projectName_->setMinimumWidth(150);
    projectName_->setMaximumWidth(300);
    // A QLineEdit is horizontally Expanding by default — in a toolbar that stretches it across the
    // whole row and shoves the ✎/🎨 far to the right. Make it content-sized so the name + icons
    // pack together on the left (a trailing spacer below absorbs the rest of the row).
    projectName_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    projectName_->setEnabled(false);
    projectName_->setReadOnly(true);  // browser-like: read-only until edit mode (✎ / double-click)
    nameLay->addWidget(projectName_);
    // "?" status hint on the never-collapsing header row — a LABEL (hover readout,
    // browser #hints-btn), round outline, added after ✎/🎨 (browser order).
    statusHint_ = new QLabel(this);
    statusHint_->setObjectName("statusHint");
    statusHint_->setText(QStringLiteral("?"));
    statusHint_->setAlignment(Qt::AlignCenter);
    statusHint_->setFixedSize(18, 18);
    statusHint_->setFocusPolicy(Qt::NoFocus);
    statusHint_->setAttribute(Qt::WA_TransparentForMouseEvents, false);   // hover still shows the tip
    statusHint_->setStyleSheet(
        "QLabel#statusHint{color:rgba(154,160,168,0.75);font-size:11px;font-weight:600;"
        "border:1px solid rgba(154,160,168,0.45);border-radius:9px;background:transparent;}");
    // ✎ rename + 🎨 colour affordances beside the name (hover-revealed, enabled only with
    // an active project). Fixed 26px boxes, or the hover reveal grows the header row.
    // Each is a CHIP, not a bare glyph — the browser paints both on --bg-info inside a
    // --border-main outline at rest. QSS half: QToolButton[nameAffordance] in theme.cpp.
    const auto sizeToRow = [](QToolButton* b) {
      b->setFixedSize(26, 26);
      b->setIconSize(QSize(15, 15));
      b->setProperty("nameAffordance", true);
    };
    projectNameEdit_ = new QToolButton(nameGroup_);
    projectNameEdit_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sizeToRow(projectNameEdit_);
    projectNameEdit_->setAutoRaise(true);
    projectNameEdit_->setToolTip("Rename project");
    projectNameEdit_->setEnabled(false);
    nameLay->addWidget(projectNameEdit_);
    connect(projectNameEdit_, &QToolButton::clicked, this, [this] { enterNameEdit(); });
    projectColorBtn_ = new QToolButton(nameGroup_);
    projectColorBtn_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sizeToRow(projectColorBtn_);
    projectColorBtn_->setAutoRaise(true);
    projectColorBtn_->setToolTip("Project color — paints the project name");
    projectColorBtn_->setEnabled(false);
    nameLay->addWidget(projectColorBtn_);
    // Click opens a small menu (browser parity): "Choose colour…" + "Use theme default colour".
    // The menu runs its own loop and fully closes before we open the picker (deferred), so no stray
    // grab dismisses the dialog. Right-click still resets straight to the theme default.
    connect(projectColorBtn_, &QToolButton::clicked, this, [this] { showProjectColorMenu(); });
    // …and the "?" readout closes the group, after the rename + colour affordances, which
    // is the browser's order (name · ✎ · 🎨 · ?).
    statusHintAction_ = tbName->addWidget(statusHint_);
    statusHintAction_->setVisible(false);
    projectColorBtn_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(projectColorBtn_, &QToolButton::customContextMenuRequested, this,
            [this](const QPoint&) { setActiveProjectColor(QString()); });
    // Inline-rename confirm/cancel: line-art check / x glyphs (themed in
    // styleActionIcons) instead of the bare ✓/✗ text, matching the browser's
    // icon buttons. Icon-only with a tooltip.
    projectNameAccept_ = new QToolButton(nameGroup_);
    projectNameAccept_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // Hover text is shared with the browser's toolbar.js (#project-name-accept /
    // #project-name-cancel) — a control in both apps says the same thing.
    projectNameAccept_->setToolTip("Save name");
    sizeToRow(projectNameAccept_);   // ✓/✗ replace ✎/🎨 in edit mode — same box, no jump
    projectNameAccept_->setVisible(false);
    nameLay->addWidget(projectNameAccept_);
    projectNameCancel_ = new QToolButton(nameGroup_);
    projectNameCancel_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    projectNameCancel_->setToolTip("Cancel");
    sizeToRow(projectNameCancel_);
    projectNameCancel_->setVisible(false);
    nameLay->addWidget(projectNameCancel_);
    // The container goes on the toolbar as one action, AFTER its children exist.
    tbName->addWidget(nameGroup_);
    // Trailing expanding spacer: absorbs the rest of the row so the label + name + ✎/🎨 stay packed
    // together on the LEFT (no huge gap), instead of the name field stretching across the whole row.
    { auto* sp = new QWidget(this); sp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); tbName->addWidget(sp); }
    // The per-project name colour control is NOT a toolbar swatch — it lives in the Project
    // menubar menu (actProjectColor_ / actProjectColorClear_). projectColorBtn_ stays null; the
    // active project's colour is still visible because the name field itself is painted in it.
    // textEdited fires only on USER edits (not programmatic setText), so updating the
    // field from updateProjectTitle() never re-triggers validation.
    connect(projectName_, &QLineEdit::textEdited, this,
            [this](const QString&) { refreshProjectNameButtons(); });
    connect(projectName_, &QLineEdit::returnPressed, this, [this] {
      if (nameEditing_) commitProjectName();  // commit (no-op if unchanged) + leave edit mode
    });
    connect(projectNameAccept_, &QToolButton::clicked, this, [this] { commitProjectName(); });
    connect(projectNameCancel_, &QToolButton::clicked, this, [this] { cancelProjectName(); });
    // Escape cancels the edit; clicking away (focus-out) reverts any uncommitted text — both via
    // the event filter below, so the user can always leave the field (Enter still commits).
    projectName_->installEventFilter(this);
    // Hover-reveal the ✎/🎨 group: the CONTAINER is the hover region; children still get
    // their own Enter/Leave (Qt sends the parent a Leave when the cursor moves onto a
    // child), so all four recompute the shared state (eventFilter → updateNameHover,
    // which tests the container's gap-free rect).
    nameGroup_->installEventFilter(this);
    projectNameEdit_->installEventFilter(this);
    projectColorBtn_->installEventFilter(this);
  }

  void MainWindow::buildPageFormulaToolbar() {
    // ── third row: Zoom · Page · Data · Settings (the browser's last toolbar row).
    // The f(x,y) pill and its inputs are built here but LIVE on the Draw · View
    // row (see below), so this row carries exactly the browser's four clusters. ──
    addToolBarBreak();
    auto* tb2 = addToolBar("Page & Formula");
    tb2->setObjectName("pageFormulaToolbar");  // named for QMainWindow::saveState
    tb2->setMovable(false);
    tb2->setToolButtonStyle(Qt::ToolButtonTextOnly);

    // Units switch on the toolbar (mirrors View ▸ Units, kept in sync). data
    // carries the canonical code; both surfaces route through applyUnits().
    unitCombo_ = new SearchComboBox(this, /*searchable=*/false);
    unitCombo_->addItem("cm", "cm");
    unitCombo_->addItem("in", "in");
    unitCombo_->setToolTip("Display units (cm / inches)");   // its own caption
    connect(unitCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { applyUnits(unitCombo_->currentData().toString()); });
    // Zoom rides the Line · Point row, not this one: with it here the last row
    // overflowed on laptop widths and QToolBar's "»" swallowed the SETTINGS cluster.
    QToolBar* zoomHost = findChild<QToolBar*>("styleToolbar");
    if (!zoomHost) zoomHost = tb2;   // defensive: rows are built in order
    zoomHost->addSeparator();
    zoomHost->addWidget(makeToolSection("Zoom", {}, { zoom_, zoomFitBtn_ }));
    // Inline custom W x H inputs (S10), shown only for the "custom" page size. Built BEFORE
    // the section so they can go INSIDE it: added straight to the toolbar they were centred
    // on its full height while the two combos sat under the section caption, so the row
    // never shared a baseline (the same trap the formula fields fell into, below).
    customGroup_ = new QWidget(this);
    {
      auto* cl = new QHBoxLayout(customGroup_);
      cl->setContentsMargins(2, 0, 0, 0);
      cl->setSpacing(5);   // the section row's own gap
      customW_ = new ExprDoubleSpinBox(customGroup_);
      customW_->setRange(0.1, 500.0);  // browser LIMITS custom page bounds
      customW_->setSingleStep(0.1);
      customW_->setDecimals(1);
      customW_->setValue(21.0);
      customW_->setToolTip("Custom page width in the selected units");
      // Width-tightening (S8 req 7): keep the custom-page spinboxes compact
      // (browser style width:96px, toolbar.js:110/112) — trimmed a little further
      // with the rest of this row so the SETTINGS cluster always fits after DATA.
      customW_->setMaximumWidth(76);
      customW_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
      customH_ = new ExprDoubleSpinBox(customGroup_);
      customH_->setRange(0.1, 500.0);
      customH_->setSingleStep(0.1);
      customH_->setDecimals(1);
      customH_->setValue(29.7);
      customH_->setToolTip("Custom page height in the selected units");
      customH_->setMaximumWidth(76);
      customH_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
      cl->addWidget(customW_, 0, Qt::AlignVCenter);
      cl->addWidget(new QLabel("×", customGroup_), 0, Qt::AlignVCenter);
      cl->addWidget(customH_, 0, Qt::AlignVCenter);
      customUnitLabel_ = new QLabel("cm", customGroup_);
      cl->addWidget(customUnitLabel_, 0, Qt::AlignVCenter);
    }
    customGroup_->setVisible(false);   // revealed by the "custom" page size
    // One NAMED section, like every group in the main row and like the browser's PAGE
    // cluster — the inline "Page:"/"Units:" captions become the section header ("Units"
    // keeps its own inline label, exactly as the browser does inside that group).
    tb2->addWidget(makeToolSection("Page", {}, { pageSize_, unitCombo_, customGroup_ }));
    tb2->addSeparator();

    // Inline formula controls (S11): an enable checkbox + fx/fy inputs + error.
    allowFormulas_ = new QCheckBox("𝑓(x,y)", this);
    // Styled as an accent PILL toggle (theme.cpp QCheckBox#formulaPill): accent outline + text
    // when off, accent-filled with contrasting text when on — matching the browser toolbar.
    allowFormulas_->setObjectName("formulaPill");
    allowFormulas_->setToolTip(
        "Enable x/y coordinate transform formulas applied to the points table");
    // Content-sized: this section takes the row's leftover width (below), and a stretchable
    // pill swallowed it whenever the fields were hidden — a wide chip whose clickable area
    // (QCheckBox's click rect) stayed at the left, so half of it did nothing.
    allowFormulas_->setSizePolicy(QSizePolicy::Fixed, allowFormulas_->sizePolicy().verticalPolicy());
    // f(x,y) rides the Draw · View row instead of this one. It is a desktop-only
    // extra — the browser's last row is Zoom · Page · Data · Settings — and its
    // inline inputs are exactly what tipped this row past the window width, which
    // pushed the SETTINGS cluster into QToolBar's "»" where the user never saw it.
    QToolBar* formulaHost = findChild<QToolBar*>("drawViewToolbar");
    if (!formulaHost) formulaHost = tb2;   // defensive: rows are built in order
    formulaHost->addSeparator();
    // The x/y inputs belong to the SAME section as the pill that reveals them. Added
    // straight to the toolbar instead, they were centred on the toolbar's full height
    // while the pill sat under the section's caption — so the two never shared a
    // baseline. makeToolSection gives every control in the row one height and
    // Qt::AlignVCenter, which is what the browser's flex row does.
    buildFormulaFields();
    // FORMULA closes this row, so it is the cluster that may take its leftover width —
    // that is what lets the two fields reach the browser's size (buildFormulaFields). The
    // tail is where the width the fields cannot use goes: with them hidden, a lone
    // content-sized pill in a wide section came out CENTRED instead of under its caption.
    auto* formulaTail = new QWidget(this);
    formulaTail->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    formulaTail->setFixedHeight(1);
    QWidget* formulaSection =
        makeToolSection("Formula", {}, { allowFormulas_, formulaGroup_, formulaTail });
    formulaSection->setSizePolicy(QSizePolicy::Expanding,
                                  formulaSection->sizePolicy().verticalPolicy());
    // …and that width belongs to the FIELDS first: a stretch factor on the group (the tail
    // keeps none) sends the row's slack there, so the tail only collects what the fields
    // cannot use — nothing while they are hidden, the excess once they are at 180.
    if (auto* row = qobject_cast<QBoxLayout*>(formulaGroup_->parentWidget()->layout()))
      row->setStretchFactor(formulaGroup_, 1);
    formulaHost->addWidget(formulaSection);
    // Data then Settings close the row, mirroring the browser's last one
    // (Zoom · Page · Data · Settings). Incognito lives in Settings.
    tb2->addWidget(makeToolSection("Data",
                                   {actDownloadJson_, actCopyLayout_, actUploadJson_, actClearProject_}));
    tb2->addSeparator();
    // Settings mirrors the browser's last cluster in order: theme · fullscreen ·
    // incognito · gear (Shortcuts) · palette (Visuals) · info (Help). Every button
    // drives the existing QAction (keeps toolbar and menu bar in step). actAccent_
    // is NOT in this row — it's the logo's own right-click/dblclick popover, with
    // no toolbar icon of its own in the browser either.
    settingsSection_ = makeToolSection(
        "Settings", {actTheme_, actFullscreen_, actIncognito_, actShortcuts_, actSettings_, actInfo_});
    tb2->addWidget(settingsSection_);
    formulaGroup_->setVisible(false);   // revealed by the pill (setFormulaFieldsVisible)
    // (Theme / Incognito / Settings / Info are NOT menu-bar-only any more: they
    // are the SETTINGS section that closes this row, mirroring the browser's last
    // cluster. The actions are shared, so both surfaces stay in step.)
  }

  // Draw + View CLOSE the second row (browser order: Line · Point · Draw · View), so
  // they are appended to the toolbar buildStyleToolbar opened rather than breaking a
  // row of their own. Data moved to row three, beside Zoom/Page/Settings.
  void MainWindow::buildDrawViewToolbar() {
    // Their OWN row rather than the tail of the style row: appended there, View (the
    // Points/Lines toggles, Compare and clear-lines) overflowed into QToolBar's "»" on any
    // window under ~1100 px and simply vanished — the browser wraps that cluster instead.
    addToolBarBreak();
    QToolBar* tb4 = addToolBar("Draw & View");
    tb4->setObjectName("drawViewToolbar");  // named for QMainWindow::saveState
    tb4->setMovable(false);
    tb4->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tb4->addWidget(makeToolSection("Draw", {actStartDraw_}, {drawModeBtn_}));
    tb4->addSeparator();
    // Compare view combo (browser toolbar View section): hold the edit against the
    // untouched original. Kept in sync with the View → Compare submenu radio set.
    compareCombo_ = new SearchComboBox(this, /*searchable=*/false);
    // Short labels: the closed combo is sized by its widest item, and the verbose ones left a
    // wide empty box beside the clear-lines button. The tooltip below spells each out.
    compareCombo_->addItem("None", "none");
    compareCombo_->addItem("Original", "original");
    compareCombo_->addItem(QString::fromUtf8("Split ↔"), "vertical");
    compareCombo_->addItem(QString::fromUtf8("Split ↕"), "horizontal");
    compareCombo_->setToolTip(
        // The browser's shape (browser/js/ui/toolbar.js #compare-mode): one bulleted row
        // per mode, the peek gesture as a parenthesised hint, and the cycle shortcut ONLY
        // as the trailing "(…)" — tipContent turns that into the heading's keycap, so
        // naming it in the prose as well would print it twice.
        "Compare with original\n"
        "• None — normal editing\n"
        "• Original — the original only (crop + rotation)\n"
        "• Vertical split — original left, edit right\n"
        "• Horizontal split — original top, edit bottom\n"
        "(hold Alt+Shift+O to peek) (Alt+O)");
    // Compare view combo → route through the shared setter (syncs canvas + View
    // submenu). Wired HERE, not alongside the toolbar's other combo connects
    // (buildStyleToolbar) — that function runs BEFORE this one (buildToolbar's own
    // call order), so a connect() there would target a still-null compareCombo_ and
    // silently do nothing (Qt warns "invalid nullptr parameter" and drops it): every
    // row in the popup looked selectable but never touched the canvas (reported).
    connect(compareCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
              setCompareModeUi(compareCombo_->currentData().toString());
            });

    // View cluster in the browser's order: ☑ Points · ☑ Lines · Compare · clear.
    // Points/Lines are real CHECKBOXES (persistent state, browser parity); the
    // menu actions stay the source of truth — these mirror them both ways.
    showPointsCheck_ = new QCheckBox("Points", this);
    showLinesCheck_ = new QCheckBox("Lines", this);
    const auto bindCheck = [this](QCheckBox* box, QAction* act) {
      box->setChecked(act->isChecked());
      box->setToolTip(act->toolTip().isEmpty() ? act->text() : act->toolTip());
      connect(box, &QCheckBox::toggled, this, [act](bool on) {
        if (act->isChecked() != on) act->setChecked(on);   // runs the action's own handler
      });
      connect(act, &QAction::toggled, this, [box](bool on) {
        QSignalBlocker blocked(box);   // echo back without re-entering the handler
        box->setChecked(on);
      });
    };
    bindCheck(showPointsCheck_, actShowPoints_);
    bindCheck(showLinesCheck_, actShowLines_);
    // Clear-lines is the trash at the END of the browser's View cluster, so it is built
    // here rather than passed as an action (makeToolSection puts actions first).
    auto* clearLinesBtn = new QToolButton(this);
    clearLinesBtn->setDefaultAction(actClearAll_);
    clearLinesBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    clearLinesBtn->setAutoRaise(true);
    clearLinesBtn->setIconSize(QSize(kToolIcon, kToolIcon));
    // The row's own gap left "Lines" and "Compare" reading as one run of text — both are bare
    // words, so they need more air between the toggles and the selector than between siblings.
    auto* compareLabel = new QLabel("Compare:", this);
    compareLabel->setStyleSheet("padding-left: 10px; padding-right: 2px;");
    tb4->addWidget(makeToolSection("View", {}, {
        showPointsCheck_, showLinesCheck_, compareLabel, compareCombo_, clearLinesBtn }));
  }

  // "Image Size: W × H px" bar right above the canvas (browser parity: #image-info, a sibling
  // of .main-content rather than nested in .canvas-section). A real Qt::TopDockWidgetArea
  // dock, like selectedLineDock_ above it, so the row spans the full window width above both
  // dock corners instead of staying narrowed by the panel dock — this also retires
  // selectionPanel's setHeaderTopGap() hack, since both now sit below the same dock stack.
  void MainWindow::buildImageInfoBar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("imageInfoBar");
    bar->setAttribute(Qt::WA_StyledBackground, true);
    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(imageSizeInfo_);   // left-aligned label; the bar spans the window width
    lay->addStretch(1);
    imageInfoBar_ = bar;

    // The host carries the gaps around the styled bar so neither paints as part of its
    // background/border: no left/right margin (full-bleed, unlike the canvas column), an
    // adaptive top gap (0 while "Selected Line:" is shown, 8 otherwise — onSelectionChanged
    // keeps this in sync) and a fixed 10px bottom gap (browser parity: .canvas-viewport
    // margin-top: 10px).
    imageInfoHost_ = new QWidget(this);
    imageInfoHost_->setObjectName("imageInfoHost");
    auto* hostLay = new QVBoxLayout(imageInfoHost_);
    hostLay->setContentsMargins(0, 8, 0, 6);
    hostLay->setSpacing(0);
    hostLay->addWidget(bar);

    imageInfoDock_ = new QDockWidget(this);
    imageInfoDock_->setObjectName("imageInfoDock");
    imageInfoDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    imageInfoDock_->setTitleBarWidget(new QWidget(imageInfoDock_));   // no title bar of its own
    imageInfoDock_->setWidget(imageInfoHost_);
    addDockWidget(Qt::TopDockWidgetArea, imageInfoDock_);
    // Stacking below selectedLineDock_ (rather than Qt's default side-by-side tiling) needs
    // splitDockWidget, but selectedLineDock_ is still hidden here (nothing selected yet), and
    // splitting against a hidden dock doesn't register (same caveat as ensurePanelChatSplit).
    // onSelectionChanged re-affirms the split once selectedLineDock_ actually shows.
  }

  void MainWindow::buildStyleToolbar() {
    // ── second row: Line · Point — the browser's second toolbar row. The filter combo it
    // used to carry now opens the EDIT group in row one, like the browser's; Draw and View
    // take the row after this one (buildDrawViewToolbar, which runs next).
    addToolBarBreak();
    auto* tb3 = addToolBar("Style");
    tb3->setObjectName("styleToolbar");  // named for QMainWindow::saveState
    tb3->setMovable(false);
    tb3->setToolButtonStyle(Qt::ToolButtonTextOnly);
    styleToolbar_ = tb3;

    // Default line color swatch (toolbar.js:40 #lineColor).
    lineColorBtn_ = new QToolButton(this);
    lineColorBtn_->setToolTip("Line color");
    updateColorSwatch(lineColorBtn_, lineColorValue_);

    // Default point colour swatch (toolbar.js #point-color) — previously settable only by
    // editing settings.json. Empty means inherit (core pointColorOr), so the swatch shows the
    // EFFECTIVE colour: the line colour until a distinct one is picked.
    pointColorBtn_ = new QToolButton(this);
    pointColorBtn_->setToolTip("Point color — new lines");
    updateColorSwatch(pointColorBtn_, effectiveDefaultPointColor());

    // Thickness / point spinboxes (toolbar.js:41-42, min/max mirrored). Fixed
    // narrow width so they don't sprawl (req: setMaximumWidth(56) + Fixed policy).
    // Each keeps a visible inline caption so the bare numbers aren't cryptic — the
    // section header names the GROUP, the inline label names the field, exactly as
    // the browser's LINE / POINT clusters do.
    lineThickness_ = new ExprSpinBox(this);
    lineThickness_->setRange(1, 20);
    lineThickness_->setValue(2);
    lineThickness_->setToolTip("Line thickness");
    lineThickness_->setMaximumWidth(56);
    lineThickness_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    pointSize_ = new ExprSpinBox(this);
    pointSize_->setRange(1, 30);
    pointSize_->setValue(4);
    pointSize_->setToolTip("Point size");
    pointSize_->setMaximumWidth(56);
    pointSize_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Line-style combo (toolbar.js:43-47). data carries the canonical value.
    lineStyle_ = new SearchComboBox(this, /*searchable=*/false);
    lineStyle_->addItem("Solid", "solid");
    lineStyle_->addItem("Dashed", "dashed");
    lineStyle_->addItem("Dotted", "dotted");
    lineStyle_->setToolTip("Line style");

    // Two NAMED sections, mirroring the browser's LINE (colour · thickness · style)
    // and POINT (colour · size) clusters.
    tb3->addWidget(makeToolSection("Line", {}, {
        new QLabel(" Color ", this), lineColorBtn_,
        new QLabel(" Thickness ", this), lineThickness_, lineStyle_ }));
    tb3->addSeparator();
    tb3->addWidget(makeToolSection("Point", {}, {
        new QLabel(" Color ", this), pointColorBtn_,
        new QLabel(" Size ", this), pointSize_ }));
    tb3->addSeparator();
    // Draw + View are appended to THIS row by buildDrawViewToolbar, exactly as they sit
    // in the browser's second row.

    // ── wiring ──
    // Draw-mode toggle (#draw-mode-toggle; button built in buildMainToolbar):
    // flip the canvas mode only — the drawModeChanged handler below echoes back
    // the label/tooltip.
    connect(drawModeBtn_, &QToolButton::clicked, this, [this] {
      const auto next = canvas_->drawMode() == CanvasWidget::DrawMode::Rect
                            ? CanvasWidget::DrawMode::Line
                            : CanvasWidget::DrawMode::Rect;
      canvas_->setDrawMode(next);
      persistSettings();
    });
    // Echo the canvas draw mode onto the toggle button (drawingApp.js
    // syncDrawModeUI ~1125): label + tooltip per mode.
    connect(canvas_, &CanvasWidget::drawModeChanged, this,
            [this](CanvasWidget::DrawMode mode) {
              // Glyph + word cross over together (support/faceSwap.hpp), like Start/Stop.
              syncDrawModeFace(mode == CanvasWidget::DrawMode::Rect, true);
            });

    // Default line color (drawingApp.js:155): pick a color, store as the default
    // and push to the canvas.
    connect(lineColorBtn_, &QToolButton::clicked, this, [this] {
      const QColor c =
          support::pickColorAnimated(lineColorValue_, this, "Line color", lineColorBtn_);
      if (!c.isValid()) return;
      lineColorValue_ = c;
      updateColorSwatch(lineColorBtn_, c);
      settings_.defaultColor = c.name(QColor::HexRgb);
      // While the point colour is still inheriting, its swatch tracks the line colour.
      if (pointColorBtn_ && settings_.defaultPointColor.isEmpty())
        updateColorSwatch(pointColorBtn_, c);
      onLineStyleControlChanged();
    });
    // Picking the line colour again stores empty (inherit) rather than a duplicate literal,
    // so later line-colour changes keep carrying the points along.
    connect(pointColorBtn_, &QToolButton::clicked, this, [this] {
      const QColor c = support::pickColorAnimated(effectiveDefaultPointColor(), this,
                                                  "Point color", pointColorBtn_);
      if (!c.isValid()) return;
      settings_.defaultPointColor =
          (c.rgb() == lineColorValue_.rgb()) ? QString() : c.name(QColor::HexRgb);
      updateColorSwatch(pointColorBtn_, effectiveDefaultPointColor());
      onLineStyleControlChanged();
    });
    // Thickness / point / style → defaults (drawingApp.js:156-178).
    connect(lineThickness_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              settings_.defaultThickness = v;
              if (thickSpin_) {  // keep the context-menu spinbox in sync (two-way)
                QSignalBlocker b(thickSpin_);
                thickSpin_->setValue(v);
              }
              onLineStyleControlChanged();
            });
    connect(pointSize_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              settings_.defaultPointSize = v;
              if (pointSpin_) {  // keep the context-menu spinbox in sync (two-way)
                QSignalBlocker b(pointSpin_);
                pointSpin_->setValue(v);
              }
              onLineStyleControlChanged();
            });
    connect(lineStyle_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
              applyLineStyle(lineStyle_->currentData().toString());
            });

    // Image filter combo (drawingApp.js:228-238): set mode, toggle tint swatch
    // visibility, apply to the canvas + persist (shared with the context menu).
    connect(imageFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
              applyImageFilter(imageFilter_->currentData().toString());
            });
    // Tint color (drawingApp.js:240-249): pick the custom duotone tint.
    connect(filterColorBtn_, &QToolButton::clicked, this, [this] {
      const QColor c =
          support::pickColorAnimated(filterColorValue_, this, "Tint color", filterColorBtn_);
      if (c.isValid()) applyTintColor(c);
    });
  }

}  // namespace stencil::gui
