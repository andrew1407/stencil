#include "MainWindow.hpp"
#include "ToolbarBuilder.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "SearchCombo.hpp"
#include "../../support/control/WrapRow.hpp"     // rows wrap like the browser's, never overflow into "»"

#include "../../support/control/clickToToggle.hpp"
#include "../../support/control/swap/controlSwap.hpp"
#include "tipContent.hpp"

#include <QDockWidget>

// MainWindow's toolbar assembly: the Draw · View sections and the Image Size bar.

namespace stencil::gui {

  namespace {
    // The box is exactly its style's indicator, re-read on a skin swap (webcore's is 17, not 18).
    class IndicatorWidth : public QObject {
     public:
      explicit IndicatorWidth(QCheckBox* box) : QObject(box) { box->installEventFilter(this); fit(box); }

     protected:
      bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() == QEvent::StyleChange || ev->type() == QEvent::Polish) fit(static_cast<QCheckBox*>(obj));
        return QObject::eventFilter(obj, ev);
      }

     private:
      static void fit(QCheckBox* box) {
        box->setFixedWidth(box->style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, box));
      }
    };
  }  // namespace

  void ToolbarBuilder::buildDrawViewToolbar() {
    QToolBar* row = toolRow();
    addWrappedSeparator(row);
    addWrapped(row, makeToolSection("Draw", {w.acts.startDraw}, {w.tools.drawModeBtn}));
    addWrappedSeparator(row);
    // Kept in sync with the View → Compare submenu radio set.
    w.tools.compareCombo = new SearchComboBox(&w, /*searchable=*/false);
    // Short labels: the closed combo is sized by its widest item; the tooltip spells each out.
    w.tools.compareCombo->addItem("None", "none");
    w.tools.compareCombo->addItem("Original", "original");
    w.tools.compareCombo->addItem(QString::fromUtf8("Split ↔"), "vertical");
    w.tools.compareCombo->addItem(QString::fromUtf8("Split ↕"), "horizontal");
    // The browser's words (toolbar.js #compare-mode); the cycle chord is the trailing "(…)"
    // tipContent draws as the keycap, so not named in the prose.
    setTipBase(w.tools.compareCombo,
               "Compare with original\n"
               "• None — normal editing\n"
               "• Original — the original only (crop + rotation)\n"
               "• Vertical split — original left, edit right\n"
               "• Horizontal split — original top, edit bottom\n"
               "(hold Alt+Shift+O to peek)");
    setTipHotkey(w.tools.compareCombo, w.acts.cycleCompare);
    setTipReason(w.tools.compareCombo, "Load an image to compare");
    // Wired here, not in buildStyleToolbar, which runs before this one: a connect() on a null
    // compareCombo is silently dropped.
    QObject::connect(w.tools.compareCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     &w, [this](int) {
                       w.parts.styleControls.setCompareModeUi(w.tools.compareCombo->currentData().toString());
                     });
    // Hover-preview repaints only; leaving without a pick reverts to the committed mode.
    static_cast<SearchComboBox*>(w.tools.compareCombo)->setPreview(
        [this](const QString& mode) { w.canvas->setCompareMode(mode); });

    // Real checkboxes, the menu actions their source of truth; each a bare box and a caption 4px
    // apart (browser <label><input> Points</label>), as the box's own text left room past the word.
    w.tools.showPointsCheck = new QCheckBox(&w);
    w.tools.showLinesCheck = new QCheckBox(&w);
    const auto captioned = [this](QCheckBox* box, const QString& word, int lead) {
      auto* host = new QWidget(&w);
      auto* h = new QHBoxLayout(host);
      h->setContentsMargins(lead, 0, 0, 0);
      h->setSpacing(4);
      auto* caption = new QLabel(word, host);
      caption->setObjectName(QStringLiteral("toggleCaption"));
      new IndicatorWidth(box);   // just the indicator: an empty QCheckBox still reserves room for a label
      box->setAttribute(Qt::WA_LayoutUsesWidgetRect);   // the style's smaller layout rect let the word overlap
      support::captionToggles(caption, box);
      h->addWidget(box);
      h->addWidget(caption);
      return host;
    };
    const auto bindCheck = [this](QCheckBox* box, QAction* act) {
      box->setChecked(act->isChecked());
      box->setToolTip(act->toolTip().isEmpty() ? act->text() : act->toolTip());
      QObject::connect(box, &QCheckBox::toggled, &w, [act](bool on) {
        if (act->isChecked() != on) act->setChecked(on);   // runs the action's own handler
      });
      QObject::connect(act, &QAction::toggled, &w, [box](bool on) {
        if (box->isChecked() == on) return;   // the box was the one clicked: it already dusted
        {
          QSignalBlocker blocked(box);   // echo back without re-entering the handler
          box->setChecked(on);
        }
        swapCheckIndicator(box, on);   // a mirrored change dusts too (browser setChecked)
      });
    };
    bindCheck(w.tools.showPointsCheck, w.acts.showPoints);
    bindCheck(w.tools.showLinesCheck, w.acts.showLines);
    // Built here so it lands at the end of the cluster (makeToolSection puts actions first).
    auto* clearLinesBtn = new QToolButton(&w);
    clearLinesBtn->setDefaultAction(w.acts.clearAll);
    clearLinesBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    clearLinesBtn->setAutoRaise(true);
    clearLinesBtn->setIconSize(QSize(TOOL_ICON, TOOL_ICON));
    // Compare leads (browser twin: toolbar.js View cluster), captioned with no colon.
    auto* compareLabel = new QLabel("Compare", &w);
    compareLabel->setStyleSheet("padding-right: 2px;");
    addWrapped(row, makeToolSection("View", {}, {
        compareLabel, w.tools.compareCombo, captioned(w.tools.showPointsCheck, "Points", 4),
        captioned(w.tools.showLinesCheck, "Lines", 3), clearLinesBtn }));   // leads: the picker gap, and 5 + 3 = the browser's 8
  }

  // browser #image-info. A real top dock like selectedLineDock, so it spans the full window width
  // above both dock corners.
  void ToolbarBuilder::buildImageInfoBar() {
    auto* bar = new QWidget(&w);
    bar->setObjectName("imageInfoBar");
    bar->setAttribute(Qt::WA_StyledBackground, true);
    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);   // the two labels carry their own gap in contentsMargins
    // One vertical alignment for both, so neither is re-placed by the other's arrival; the bar
    // itself spans the window width and the labels stay left.
    lay->addWidget(w.tools.imageSizeInfo, 0, Qt::AlignVCenter);
    lay->addWidget(w.tools.incognitoTag, 0, Qt::AlignVCenter);
    lay->addStretch(1);
    w.tools.imageInfoBar = bar;

    // The host carries the gaps: no side margin, a top gap of 0 while "Selected Line:" shows and 8
    // otherwise (onSelectionChanged), 3px below.
    w.tools.imageInfoHost = new QWidget(&w);
    w.tools.imageInfoHost->setObjectName("imageInfoHost");
    auto* hostLay = new QVBoxLayout(w.tools.imageInfoHost);
    hostLay->setContentsMargins(0, 8, 0, 3);
    hostLay->setSpacing(0);
    hostLay->addWidget(bar);

    w.tools.imageInfoDock = new QDockWidget(&w);
    w.tools.imageInfoDock->setObjectName("imageInfoDock");
    w.tools.imageInfoDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    w.tools.imageInfoDock->setTitleBarWidget(new QWidget(w.tools.imageInfoDock));   // no title bar of its own
    w.tools.imageInfoDock->setWidget(w.tools.imageInfoHost);
    w.editor->addDockWidget(Qt::TopDockWidgetArea, w.tools.imageInfoDock);
    // splitDockWidget against the still-hidden selectedLineDock does not register;
    // onSelectionChanged re-affirms it once shown.
  }
}  // namespace stencil::gui

