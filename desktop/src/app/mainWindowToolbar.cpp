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
        if (le != nameBar_.field) installHoverShimmer(le);   // skip the rename field
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
    nameBar_.blankColorBtn = new QToolButton(this);
    // Visuals come entirely from updateColorSwatch (the shared 46×26 chip
    // recipe), so it presents the SAME control height as the line-colour chip.
    // Labelled "Blank" beside the chip, as in the browser: a bare white swatch in the middle
    // of the Edit group says nothing about what it recolours.
    nameBar_.blankColorBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    nameBar_.blankColorBtn->setText("Blank");
    nameBar_.blankColorBtn->setToolTip("Blank background color — recolor this blank image (keeps your lines)");
    nameBar_.blankColorBtn->setVisible(false);
    connect(nameBar_.blankColorBtn, &QToolButton::clicked, this, [this] { setActiveBlankColor(); });

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
                                   {nameBar_.blankColorBtn}, {imageFilter_, filterColorBtn_}));

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
}  // namespace stencil::gui

