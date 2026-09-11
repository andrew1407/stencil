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

  // ONE wrapping run, in the browser's order (toolbar.js): Image · Description · Projects ·
  // Connections · Edit · Line · Point · Draw · View · Zoom · Page · Formula · Data ·
  // Settings. Like the browser's single flex-wrap container, it re-packs with the window —
  // so the four sub-builders all append to the same row and MUST run in this order.
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
  namespace {
    // The browser's fields are a flat 180px (toolbar.js `style="width:180px"`). Qt has no
    // "preferred width", and the FORMULA section is content-sized — it sits mid-row, so it
    // cannot take slack the way an end-of-row cluster can — which left the fields at the
    // ~158px a QLineEdit asks for. The hint IS the width here, so it says 180; the minimum
    // below still lets the row squeeze them when it has to.
    class FormulaField : public QLineEdit {
     public:
      explicit FormulaField(QWidget* parent) : QLineEdit(parent) {}
      QSize sizeHint() const override {
        return QSize(kFormulaFieldW, QLineEdit::sizeHint().height());
      }
    };
  }  // namespace

  void MainWindow::buildFormulaFields() {
    formulaGroup_ = new QWidget(this);
    // Maximum, not Expanding: the pair is exactly as wide as the two fields want (2 × 180 +
    // the gap) and may only SHRINK from there. Expanding made this cluster swallow the row's
    // leftover width — a long empty stretch after the fields, with DATA and SETTINGS shoved
    // to the far edge — and, once the fields hid, left the lone pill floating in the middle
    // of that empty box instead of sitting under its caption.
    formulaGroup_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    auto* fl = new QHBoxLayout(formulaGroup_);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(6);   // the browser's gap between the two fields
    const auto makeField = [this](const char* placeholder, const char* tip) {
      auto* e = new FormulaField(formulaGroup_);
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
      e->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
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

  QToolBar* MainWindow::toolRow() const { return findChild<QToolBar*>("mainToolbar"); }

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

  void MainWindow::buildMainToolbar() {
    // Header row (always visible): the "Controls" collapse pill + the project-name group.
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
    // Size the BUTTON to the mark: a QToolBar otherwise lays an added widget out at the
    // toolbar's default icon metric, so the badge stayed ~40px however large its icon
    // (the logo would not grow). The margin leaves the hover fx room.
    logoBtn_->setFixedSize(kHeaderLogo + 6, kHeaderLogo + 6);
    logoBtn_->setIcon(QIcon(makeLogoPixmap(kHeaderLogo)));
    // LogoHoverFx paints the resting mark and blanks this icon — QToolButton draws it at
    // half size on Retina. The fx owns BOTH states.
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
      if (pop_.dismissClick) { pop_.dismissClick = false; return; }
      logoClickTimer_->start(250);
    });
    // Accent picker = a first-class popover (execMaybePopover); registering the
    // logo in pop_.buttons gives it the shared Alt machinery. The logo keeps
    // its own click/dblclick gestures (excluded from popover presses in eventFilter).
    actAccent_ = new QAction(tr("Theme Color"), this);
    actAccent_->setObjectName("actAccent");
    connect(actAccent_, &QAction::triggered, this, [this] { openAccentPicker(); });
    pop_.buttons.insert(logoBtn_, actAccent_);
    // Right-click: the same popover, STICKY — the popover icons' right-click route
    // (makeToolSection): a deliberate open, so the Alt release never closes it.
    // QToolButton::clicked never fires for the right button, so opening it can never
    // arm the cycle timer above.
    logoBtn_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(logoBtn_, &QToolButton::customContextMenuRequested, this, [this] {
      if (pop_.clickTimer) pop_.clickTimer->stop();
      if (logoClickTimer_) logoClickTimer_->stop();
      pop_.peekAction.clear();   // a deliberate open is sticky — Alt release keeps it
      stopLingerPoll();         // a lingering window's poll must not close THIS open
      pop_.anchor = logoBtn_;
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
    // ControlsPill paints its own chevron + label: a stock icon+text QToolButton reserves
    // ~36px for the icon slot however small the chevron, leaving a wide gap beside the
    // label.
    controlsPill_ = new ControlsPill(this);
    controlsPill_->setObjectName("controlsPill");   // outlined pill, styled in theme.cpp
    // Its chevron's angle is STATE (toolbars shown/hidden), not hover feedback — the
    // browser's `[id^="toggle-"]` icon-motion opt-out.
    controlsPill_->setProperty(kNoIconMotionProperty, true);
    controlsPill_->setProperty(kShimmerRadiusProperty, 12);   // its QSS radius (shimmerOverlay.hpp)
    static_cast<ControlsPill*>(controlsPill_)->setLabel("Controls");
    // Capped, so the pill is never stretched to the header row the logo now makes tall —
    // a QToolBar filled it to 41px with a big rounded border ("huge border").
    controlsPill_->setMaximumHeight(28);
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
    // 11px top/bottom, so the readout sits in a band of its own rather than pressed
    // between the toolbars and the canvas.
    imageSizeInfo_->setContentsMargins(10, 11, 10, 11);
    imageSizeInfo_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    addToolBarBreak();

    // The one tool row: it wraps, so nothing reaches QToolBar's "»" (see buildToolbar).
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

    // The dialog-opening icons answer dblclick / right-click with the COMPACT anchored
    // shape of their dialog (execMaybePopover). A plain click keeps the full dialog, but
    // deferred one double-click interval (the logo pattern): the dialog's exec() blocks,
    // so an instant open would swallow the second click of every double-click.
    pop_.dialogActions = {actOpen_, actOpenAnother_, actOpenIn_, actProjects_, actConnect_, actLinks_,
                             actDescription_, actKeywords_, actChat_, actAssistantSettings_, actShortcuts_,
                             actSettings_, actInfo_};
    pop_.clickTimer = new QTimer(this);
    pop_.clickTimer->setSingleShot(true);
    pop_.clickTimer->setInterval(250);
    connect(pop_.clickTimer, &QTimer::timeout, this, [this] {
      if (QAction* act = pop_.pendingAction.data()) {
        pop_.pendingAction.clear();
        act->trigger();
      }
    });

    // Image cluster (browser parity) + blank-fill swatch. Empty state: with no
    // image the cluster collapses to ONE labelled "Open Image" button; once an
    // image loads, refreshActions swaps it for the icon row, whose trailing icon
    // (actOpenAnother_) opens the same dialog as this button's actOpen_ — browser
    // parity: #load-image-btn ↔ #open-image-btn, same handler, different button/icon.
    openImageBtn_ = new OpenImageButton(this);
    openImageBtn_->setDefaultAction(actOpen_);
    openImageBtn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    openImageBtn_->setAutoRaise(true);
    openImageBtn_->setIconSize(QSize(kToolIcon, kToolIcon));
    {  // 14px label, matching the browser's #load-image-btn (its default button font).
      QFont f = openImageBtn_->font();
      f.setPixelSize(14);
      openImageBtn_->setFont(f);
    }
    // OpenImageButton paints its own icon+label as one centred group with a wide gap
    // between them, so there is no stock icon-slot reserve to compensate for.
    // Guarded by openImageButtonLabelIsCentred() in the GUI tests.
    imageSection_ = makeToolSection("Image",
                                    {actSaveImage_, actCopyImage_, actShareImage_, actOpenIn_, actOpenAnother_},
                                    {}, {openImageBtn_});
    addWrapped(tb, imageSection_);
    addWrappedSeparator(tb);
    // Description & attributes: the saved project's description, keywords and links —
    // the browser's cluster between IMAGE and PROJECTS. All three gate on a saved,
    // non-incognito project (updateProjectTitle), so the whole group reads as one rule.
    addWrapped(tb, makeToolSection("Description & attributes", {actDescription_, actKeywords_, actLinks_}));
    addWrappedSeparator(tb);
    // Projects = open editor list + save/open .stencil + live-sync, matching the browser's
    // PROJECTS cluster (layers / save / folder / refresh). Clear-project stays in the menu bar.
    addWrapped(tb, makeToolSection("Projects", {actProjects_, actSaveProjectFile_, actOpenProjectFile_, actStencilLiveSync_, actDeleteProjectFile_}));
    addWrappedSeparator(tb);
    // Connections & chat: servers (connect to share/co-edit) + the AI-assistant sparkle
    // toggle (identical grouping to the browser toolbar; the image's source links live in
    // DESCRIPTION & ATTRIBUTES above, as in the browser).
    connectionsSection_ = makeToolSection("Connections & chat", {actConnect_, actChat_});
    addWrapped(tb, connectionsSection_);
    addWrappedSeparator(tb);
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
    // The browser's #image-filter: heading, Cycle Image Filter's chord as its keycap, and
    // the reason it is greyed out (composed and kept current by tipContent).
    setTipBase(imageFilter_, "Image Filter");
    setTipHotkey(imageFilter_, actCycleFilter_);
    setTipReason(imageFilter_, "Load an image to apply a filter");
    filterColorBtn_ = new QToolButton(this);
    filterColorBtn_->setToolTip("Tint color");
    updateColorSwatch(filterColorBtn_, filterColorValue_);
    filterColorBtn_->setVisible(false);   // shown only for the "custom" filter
    addWrapped(tb, makeToolSection("Edit",
                                   {actCrop_, actRotateLeft_, actRotateRight_, actUndo_, actRedo_},
                                   {blankColorBtn_}, {imageFilter_, filterColorBtn_}));

    // Draw = ONE Start/Stop button (refreshActions swaps its default action, like
    // the browser's single #draw-toggle) + the Line/Rect mode toggle. The toggle
    // is built here, wired in buildStyleToolbar (which needs the canvas signals).
    drawModeBtn_ = new QToolButton(this);
    // Icon + label (the glyph is themed in styleActionIcons / the drawModeChanged handler).
    drawModeBtn_->setObjectName("drawFaceBtn");   // theme.cpp: the pair's larger word
    drawModeBtn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    drawModeBtn_->setText("Line");
    drawModeBtn_->setAutoRaise(true);
    drawModeBtn_->setIconSize(QSize(kToolIcon + kFaceIconGap, kToolIcon));
    setTipBase(drawModeBtn_, "Drawing mode: Line (click to switch to Rectangle)");
    setTipReason(drawModeBtn_, "Load an image to switch line / rectangle");   // #draw-mode-toggle
    // Solid accent, permanently — it has no QAction for styleDangerToolButtons' own
    // fill pass to reach (its click is a plain connect(), not a default action), and
    // the browser's `#draw-mode-toggle` is a bare `<button>`, filled at rest too.
    drawModeBtn_->setProperty("toolFill", QStringLiteral("accent"));
    // Zoom = the editable percent combo + a Fit-to-window button (browser parity — the browser's
    // zoom section ends with the fit icon). The combo replaces the browser's +/- steppers (type or
    // pick a preset). Fit button built here so it sits AFTER the combo, like the browser.
    zoomFitBtn_ = new QToolButton(this);
    zoomFitBtn_->setDefaultAction(actFit_);
    // Filled like the other acting buttons (browser #zoom-fit); the property is kept for
    // its DISABLED face alone — a faded outline rather than a filled chip, since it ends
    // the ZOOM row beside a plain field (theme.cpp QToolButton[toolGhost="true"]:disabled).
    zoomFitBtn_->setProperty("toolGhost", true);
    zoomFitBtn_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    zoomFitBtn_->setAutoRaise(true);
    zoomFitBtn_->setIconSize(QSize(kToolIcon, kToolIcon));
    // Draw / Zoom / Settings are NOT on this row: all seven sections together need
    // ~1230px, so on a 1000px window Zoom and Settings (incognito!) were pushed clean
    // off the end with no way to reach them. They now sit on the rows below, which is
    // also exactly how the browser splits them — row 1 is Image · Projects ·
    // Connections & chat · Edit there too.
  }

  // Project name field + inline-rename ✓/✗ (mirrors the browser topbar). The field shows the
  // active project's name and renames it inline, validated live: ✓ is enabled only for a changed,
  // valid (non-empty, ≤80, unique) name, with the reason on its tooltip when disabled. Enter = ✓,
  // Escape / click-away = ✗. Lives in the always-visible header row beside the "Controls" pill.
  void MainWindow::buildProjectNameGroup(QToolBar* tbName) {
    // (no "Project:" caption — the field alone reads as the project name, browser parity)
    // ONE container for the field + its affordances (browser .project-name-field
    // parity): hover is the container's own gap-free rect, so sweeping between the
    // field and the ✎/🎨 buttons can never flicker the reveal (which replayed the
    // dust and re-armed the tooltip). RowCard (connectDialog) pattern.
    nameGroup_ = new QWidget(this);
    auto* nameLay = new QHBoxLayout(nameGroup_);
    nameLay->setContentsMargins(0, 0, 0, 0);
    // Air between the name and its two chips: at 4 they sat right against the field's
    // edge. Browser twin: .project-name-field's `gap`.
    nameLay->setSpacing(8);
    projectName_ = new QLineEdit(nameGroup_);
    projectName_->setObjectName("projectNameField");   // theme.cpp: no hover ring on a title
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
      // The app's two hover treatments, explicitly: this group is built after the sweep that
      // installs the shimmer across the toolbar rows, so these four were the only controls in
      // the bar without it. The icon-motion filter is app-wide and needs no
      // hand — a themedIcon glyph is all it asks for.
      installHoverShimmer(b);
      // The browser's box with a glyph to match — at 26/15 the pair read as small, faint
      // marks beside the name.
      b->setFixedSize(kNameChipBox, kNameChipBox);
      b->setIconSize(QSize(kNameChipGlyph, kNameChipGlyph));
      b->setProperty("nameAffordance", true);
    };
    projectNameEdit_ = new QToolButton(nameGroup_);
    projectNameEdit_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sizeToRow(projectNameEdit_);
    projectNameEdit_->setAutoRaise(true);
    projectNameEdit_->setToolTip("Rename project");
    setTipHotkey(projectNameEdit_, actRenameProject_);   // browser #project-name-edit: its chord as the keycap
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
    // The key each one answers to, as a keycap: the rich tooltip reads a trailing "(…)"
    // (tipContent's key vocabulary) — the pair said only what they did, not how (user
    // report, with a picture). Browser twin: the same two data-titles in toolbar.js.
    projectNameAccept_->setToolTip("Save name (Enter)");
    sizeToRow(projectNameAccept_);   // ✓/✗ replace ✎/🎨 in edit mode — same box, no jump
    // …but their WIDTH must be free to animate: revealControls slides maximumWidth from 0,
    // and a fixed size pins the minimum too, so the pair simply blinked in and out with no
    // sand at all (the browser's markIn/markOut pair).
    const auto letItSlide = [](QToolButton* b) {
      b->setMinimumWidth(0);
      b->setFixedHeight(kNameChipBox);
      b->setMaximumWidth(kNameChipBox);
    };
    letItSlide(projectNameAccept_);
    projectNameAccept_->setVisible(false);
    nameLay->addWidget(projectNameAccept_);
    projectNameCancel_ = new QToolButton(nameGroup_);
    projectNameCancel_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    projectNameCancel_->setToolTip("Cancel (Esc)");
    sizeToRow(projectNameCancel_);
    letItSlide(projectNameCancel_);
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
    // Zoom · Page · Formula · Data · Settings — the tail of the browser's sequence.
    QToolBar* row = toolRow();

    // Units switch on the toolbar (mirrors View ▸ Units, kept in sync). data
    // carries the canonical code; both surfaces route through applyUnits().
    unitCombo_ = new SearchComboBox(this, /*searchable=*/false);
    unitCombo_->addItem("cm", "cm");
    unitCombo_->addItem("in", "in");
    unitCombo_->setToolTip("Display units (cm / inches)");   // its own caption
    connect(unitCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { applyUnits(unitCombo_->currentData().toString()); });
    // ZOOM and PAGE follow VIEW, keeping the browser's sequence.
    addWrappedSeparator(row);
    // The browser's cluster exactly: [−] [+] [value %] [fit] (toolbar.js .zoom-controls).
    // The steppers are the same two actions the View menu and Alt+↑/↓ drive, so the three
    // ways to zoom stay one thing; the editable combo stands in for the browser's number
    // field with its preset menu, and Fit closes the row there too.
    // Fit LEADS the row, ahead of − and + (user decision; browser twin: #zoom-fit first in
    // .zoom-controls): it is the one that puts the whole image back on screen, and the two
    // steppers follow it with the % field.
    addWrapped(row,
        makeToolSection("Zoom", { actZoomOut_, actZoomIn_ }, { zoom_ }, { zoomFitBtn_ }));
    addWrappedSeparator(row);
    // Inline custom W x H inputs, shown only for the "custom" page size. Built BEFORE
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
      // Width-tightening: keep the custom-page spinboxes compact
      // (browser style width:96px, toolbar.js:110/112) — trimmed a little further
      // with the rest of this row so the SETTINGS cluster always fits after DATA.
      customW_->setMaximumWidth(76);
      // Capped at 76 as before, but the row may squeeze them: a spin box's own
      // minimumSizeHint (89) is a floor the toolbar layout cannot go under, and with the
      // zoom steppers added this row asked for more than a 1000px window has. Ignored +
      // an explicit minimum makes 56 the floor instead; the maximum above still stops
      // them growing. Both boxes only show at all for a CUSTOM page size.
      customW_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
      customW_->setMinimumWidth(56);
      customH_ = new ExprDoubleSpinBox(customGroup_);
      customH_->setRange(0.1, 500.0);
      customH_->setSingleStep(0.1);
      customH_->setDecimals(1);
      customH_->setValue(29.7);
      customH_->setToolTip("Custom page height in the selected units");
      customH_->setMaximumWidth(76);
      customH_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
      customH_->setMinimumWidth(56);
      cl->addWidget(customW_, 0, Qt::AlignVCenter);
      cl->addWidget(new QLabel("×", customGroup_), 0, Qt::AlignVCenter);
      cl->addWidget(customH_, 0, Qt::AlignVCenter);
      // No unit suffix after the boxes (user decision; browser twin: the same span is gone
      // from toolbar.js) — the units combo two controls left already says cm / in.
    }
    customGroup_->setVisible(false);   // revealed by the "custom" page size
    // One NAMED section, like every group in the main row and like the browser's PAGE
    // cluster. Neither combo carries an inline caption (user decision, browser twin):
    // each spells its own answer out ("A4 (21 × 29.7 cm)", "cm").
    addWrapped(row, makeToolSection("Page", {}, { pageSize_, unitCombo_, customGroup_ }));

    // Inline formula controls: an enable checkbox + fx/fy inputs + error.
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
    // f(x,y) sits between PAGE and DATA, where the browser puts it (its PAGE cluster
    // carries the pill and the inputs inline; here they are their own named section, as
    // every cluster on this row is), fenced by its own separator like every neighbour —
    // it was the one section running straight on from PAGE (the browser writes a .ctrl-sep).
    // The x/y inputs belong to the SAME section as the pill that reveals them. Added
    // straight to the toolbar instead, they were centred on the toolbar's full height
    // while the pill sat under the section's caption — so the two never shared a
    // baseline. makeToolSection gives every control in the row one height and
    // Qt::AlignVCenter, which is what the browser's flex row does.
    buildFormulaFields();
    addWrappedSeparator(row);
    // Content-sized, with no expanding tail: DATA and SETTINGS follow it on this row now,
    // and a cluster that took the row's leftover width would shove them to the far edge —
    // the browser packs its sections left and leaves the slack at the END of the row.
    QWidget* formulaSection = makeToolSection("Formula", {}, { allowFormulas_, formulaGroup_ });
    formulaSection->setSizePolicy(QSizePolicy::Maximum, formulaSection->sizePolicy().verticalPolicy());
    addWrapped(row, formulaSection);
    addWrappedSeparator(row);
    // Data then Settings close the row, mirroring the browser's last one
    // (Zoom · Page · Formula · Data · Settings). Incognito lives in Settings.
    // Copy leads, then the two FILE moves (down, then up) — the pair reads as one gesture
    // in two directions (user decision; browser twin: toolbar.js's Data row).
    addWrapped(row, makeToolSection("Data",
                                   {actCopyLayout_, actDownloadJson_, actUploadJson_, actClearProject_}));
    addWrappedSeparator(row);
    // Settings mirrors the browser's last cluster in order: theme · fullscreen ·
    // fullscreen · theme · gear (Shortcuts) · palette (Visuals) · info (Help). Every button
    // drives the existing QAction (keeps toolbar and menu bar in step). actAccent_
    // is NOT in this row — it's the logo's own right-click/dblclick popover, with
    // no toolbar icon of its own in the browser either.
    settingsSection_ = makeToolSection(
        "Settings", {actIncognito_, actFullscreen_, actTheme_, actShortcuts_, actSettings_, actInfo_});
    addWrapped(row, settingsSection_);
    formulaGroup_->setVisible(false);   // revealed by the pill (setFormulaFieldsVisible)
    // (Theme / Incognito / Settings / Info are NOT menu-bar-only any more: they
    // are the SETTINGS section that closes this row, mirroring the browser's last
    // cluster. The actions are shared, so both surfaces stay in step.)
  }

  void MainWindow::buildDrawViewToolbar() {
    // Draw · View, continuing the one run — the wrap points are the layout's to choose.
    QToolBar* row = toolRow();
    addWrappedSeparator(row);
    addWrapped(row, makeToolSection("Draw", {actStartDraw_}, {drawModeBtn_}));
    addWrappedSeparator(row);
    // Compare view combo (browser toolbar View section): hold the edit against the
    // untouched original. Kept in sync with the View → Compare submenu radio set.
    compareCombo_ = new SearchComboBox(this, /*searchable=*/false);
    // Short labels: the closed combo is sized by its widest item, and the verbose ones left a
    // wide empty box beside the clear-lines button. The tooltip below spells each out.
    compareCombo_->addItem("None", "none");
    compareCombo_->addItem("Original", "original");
    compareCombo_->addItem(QString::fromUtf8("Split ↔"), "vertical");
    compareCombo_->addItem(QString::fromUtf8("Split ↕"), "horizontal");
    // The browser's words (browser/js/ui/toolbar.js #compare-mode): one bulleted row per
    // mode and the peek gesture as a parenthesised hint. The cycle chord is Cycle Compare
    // View's, appended as the trailing "(…)" tipContent draws as the heading's keycap —
    // naming it in the prose as well would print it twice — and the greyed-out reason
    // joins while there is nothing to compare.
    setTipBase(compareCombo_,
               "Compare with original\n"
               "• None — normal editing\n"
               "• Original — the original only (crop + rotation)\n"
               "• Vertical split — original left, edit right\n"
               "• Horizontal split — original top, edit bottom\n"
               "(hold Alt+Shift+O to peek)");
    setTipHotkey(compareCombo_, actCycleCompare_);
    setTipReason(compareCombo_, "Load an image to compare");
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
    // Hover-preview each compare mode on the canvas (repaint only — no control re-gating);
    // leaving the list or closing without a pick reverts to the committed mode.
    static_cast<SearchComboBox*>(compareCombo_)->setPreview(
        [this](const QString& mode) { canvas_->setCompareMode(mode); });

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
    // Compare LEADS the section (user decision; browser twin: toolbar.js's View cluster),
    // on the row's own gap — the extra air it carried was for two bare words running
    // together. Captioned as the browser captions it, no colon.
    auto* compareLabel = new QLabel("Compare", this);
    compareLabel->setStyleSheet("padding-right: 2px;");
    addWrapped(row, makeToolSection("View", {}, {
        compareLabel, compareCombo_, showPointsCheck_, showLinesCheck_, clearLinesBtn }));
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
    // keeps this in sync) and a 3px bottom gap, so the readout sits close
    // to the row it describes instead of floating in a black band above the canvas and the
    // assistant dock.
    imageInfoHost_ = new QWidget(this);
    imageInfoHost_->setObjectName("imageInfoHost");
    auto* hostLay = new QVBoxLayout(imageInfoHost_);
    hostLay->setContentsMargins(0, 8, 0, 3);
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
    // Line · Point, continuing the one run. The filter combo opens the EDIT group ahead
    // of them, like the browser's.
    QToolBar* row = toolRow();
    styleToolbar_ = row;

    // Default line color swatch (toolbar.js:40 #lineColor).
    lineColorBtn_ = new QToolButton(this);
    lineColorBtn_->setToolTip("Line color");
    updateColorSwatch(lineColorBtn_, lineColorValue_);

    // Default point colour swatch (toolbar.js #point-color). Empty means inherit (core
    // pointColorOr), so the swatch shows the EFFECTIVE colour: the line colour until a
    // distinct one is picked.
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
    addWrappedSeparator(row);
    addWrapped(row, makeToolSection("Line", {}, {
        new QLabel(" Color ", this), lineColorBtn_,
        new QLabel(" Thickness ", this), lineThickness_, lineStyle_ }));
    addWrappedSeparator(row);
    addWrapped(row, makeToolSection("Point", {}, {
        new QLabel(" Color ", this), pointColorBtn_,
        new QLabel(" Size ", this), pointSize_ }));
    // POINT closes this row — no trailing separator, or the row ends on a hairline with
    // nothing after it (the browser hides exactly that one: toolbar.js
    // syncWrappedSeparators). Draw · View take the row after this one, Zoom · Page ·
    // Formula · Data · Settings the one after that, as in the browser's own sequence.

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
    // Hover-preview each filter on the canvas (repaint only — no persist/sync); leaving
    // the list or closing without a pick reverts (searchCombo setPreview, browser twin).
    static_cast<SearchComboBox*>(imageFilter_)->setPreview([this](const QString& mode) {
      canvas_->setImageFilter(mode, filterColorValue_);
    });
    // Tint color (drawingApp.js:240-249): pick the custom duotone tint.
    connect(filterColorBtn_, &QToolButton::clicked, this, [this] {
      const QColor c =
          support::pickColorAnimated(filterColorValue_, this, "Tint color", filterColorBtn_);
      if (c.isValid()) applyTintColor(c);
    });
  }

}  // namespace stencil::gui
