#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "../../support/guiHelpers.hpp"
#include "chatWidgets.hpp"
#include "PillSplitter.hpp"
#include <QLineEdit>
#include <QRadioButton>
#include <QCheckBox>
#include <QButtonGroup>

#include "iconSet.hpp"
#include "MediaLoader.hpp"
#include "theme.hpp"
#include "scrollReveal.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/control/FlowLayout.hpp"
#include "../../support/modal/modalReveal.hpp"
#include "../../support/menu/menuReveal.hpp"
#include "../../support/icon/iconMotion.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QCloseEvent>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include "../../support/motion/MenuShimmer.hpp"
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmap>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTextCursor>
#include <QPainter>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QPointer>
#include <QScreen>
#include <QStyle>
#include <QTimer>
#include <cmath>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <functional>

namespace stencil::gui {

  using namespace chatdock;

  ChatDock::ChatDock(QWidget* parent) : QDockWidget("AI Assistant", parent) {
    setObjectName("llmChatDock");
    // NOT SelectionPanel's NoDockWidgetFeatures: this panel docks on all four sides and floats.
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                QDockWidget::DockWidgetClosable);
    // The dock SWALLOWS a drop that misses the composer, so none reaches the window's drop zone.
    setAcceptDrops(true);
    buildTitleBar();

    auto* body = new QWidget(this);
    body->setObjectName("chatBody");
    body->setAttribute(Qt::WA_StyledBackground);
    auto* col = new QVBoxLayout(body);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(6);

    // The SHARED pill grip (PillSplitter.hpp): a stylesheet handle stretched with the panel.
    log.splitter = new PillSplitter(Qt::Vertical, body);
    log.splitter->setObjectName("chatSplitter");
    log.splitter->setChildrenCollapsible(false);

    scroll = new QScrollArea(log.splitter);
    scroll->setWidgetResizable(true);
    // Never scrolls sideways: a long unbreakable token would slide the bubbles out of view.
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumHeight(100);
    log.transcript = new QWidget(scroll);
    log.transcriptLayout = new QVBoxLayout(log.transcript);
    log.transcriptLayout->setContentsMargins(10, 10, 10, 10);
    log.transcriptLayout->setSpacing(8);
    buildSuggestions();
    log.transcriptLayout->addStretch(1);
    scroll->setWidget(log.transcript);
    // browser .reveal-item (css/animations.css); parented to scroll.
    reveal = new ScrollReveal(scroll);
    scroll->viewport()->installEventFilter(this);
    // Jump pills (browser .chat-jumps): both mid-log, neither while the log fits.
    const auto mkJump = [this](const QString& tip) {
      auto* b = new QToolButton(scroll);
      b->setObjectName(QStringLiteral("chatJumpBtn"));
      b->setToolTip(tip);
      b->setCursor(Qt::PointingHandCursor);
      b->setFixedSize(28, 28);
      b->setIconSize(QSize(14, 14));
      auto* fx = new QGraphicsOpacityEffect(b);
      fx->setOpacity(GHOST_REST_OPACITY);
      b->setGraphicsEffect(fx);
      b->installEventFilter(this);
      b->hide();
      return b;
    };
    log.jumpTop = mkJump(QStringLiteral("Jump to the beginning"));
    log.jumpBottom = mkJump(QStringLiteral("Jump to the latest message"));
    connect(scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
      // Chat stickiness: scrolling away releases the follow pin; a jump to the end re-arms it.
      const auto* bar = scroll->verticalScrollBar();
      stickToBottom = bar->maximum() - bar->value() <= STICKY_BOTTOM_PX;
      updateJumpButtons();
    });
    connect(scroll->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] {
      auto* bar = scroll->verticalScrollBar();
      if (stickToBottom && bar->value() < bar->maximum()) bar->setValue(bar->maximum());
      updateJumpButtons();
    });
    const auto jumpTo = [this](bool top) {
      auto* sb = scroll->verticalScrollBar();
      auto* anim = new QVariantAnimation(sb);
      anim->setDuration(220);
      anim->setStartValue(sb->value());
      anim->setEndValue(top ? 0 : sb->maximum());
      anim->setEasingCurve(QEasingCurve::OutCubic);
      connect(anim, &QVariantAnimation::valueChanged, sb,
              [sb](const QVariant& v) { sb->setValue(v.toInt()); });
      anim->start(QAbstractAnimation::DeleteWhenStopped);
    };
    connect(log.jumpTop, &QToolButton::clicked, this, [jumpTo] { jumpTo(true); });
    connect(log.jumpBottom, &QToolButton::clicked, this, [jumpTo] { jumpTo(false); });
    log.splitter->addWidget(scroll);
    buildComposer();

    log.splitter->addWidget(log.inputArea);
    // The pill centres on the handle (= the dock); the context-menu panel differs on purpose.
    log.splitter->setStretchFactor(0, 1);
    log.splitter->setStretchFactor(1, 0);
    // A tall transcript over a compact composer.
    log.splitter->setSizes({360, 96});
    col->addWidget(log.splitter, 1);

    setWidget(body);
    setMinimumWidth(260);
    // Deferred one tick so it lands AFTER Qt's own tear-off geometry.
    connect(this, &QDockWidget::topLevelChanged, this, [this](bool floating) {
      updatePlacementState();
      if (!floating) return;
      QTimer::singleShot(0, this, [this] {
        if (isFloating()) resize(FLOATING_SIZE.expandedTo(minimumSize()));
      });
    });
    connect(this, &QDockWidget::dockLocationChanged, this,
            [this](Qt::DockWidgetArea) { updatePlacementState(); });
    setProviderStatus(QString(), ProviderStatus::UNKNOWN);
  }
}  // namespace stencil::gui
