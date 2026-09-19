#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "../support/guiHelpers.hpp"
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
#include "../support/DisintegrateOverlay.hpp"
#include "../support/FlowLayout.hpp"
#include "../support/modalReveal.hpp"
#include "../support/menuReveal.hpp"
#include "../support/iconMotion.hpp"
#include "../support/ShimmerOverlay.hpp"

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
#include "../support/MenuShimmer.hpp"
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
    log_.splitter = new PillSplitter(Qt::Vertical, body);
    log_.splitter->setObjectName("chatSplitter");
    log_.splitter->setChildrenCollapsible(false);

    scroll_ = new QScrollArea(log_.splitter);
    scroll_->setWidgetResizable(true);
    // Never scrolls sideways: a long unbreakable token would slide the bubbles out of view.
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setMinimumHeight(100);
    log_.transcript = new QWidget(scroll_);
    log_.transcriptLayout = new QVBoxLayout(log_.transcript);
    log_.transcriptLayout->setContentsMargins(10, 10, 10, 10);
    log_.transcriptLayout->setSpacing(8);
    buildSuggestions();
    log_.transcriptLayout->addStretch(1);
    scroll_->setWidget(log_.transcript);
    // browser .reveal-item (css/animations.css); parented to scroll_.
    reveal_ = new ScrollReveal(scroll_);
    scroll_->viewport()->installEventFilter(this);
    // Jump pills (browser .chat-jumps): both mid-log, neither while the log fits.
    const auto mkJump = [this](const QString& tip) {
      auto* b = new QToolButton(scroll_);
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
    log_.jumpTop = mkJump(QStringLiteral("Jump to the beginning"));
    log_.jumpBottom = mkJump(QStringLiteral("Jump to the latest message"));
    connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
      // Chat stickiness: scrolling away releases the follow pin; a jump to the end re-arms it.
      const auto* bar = scroll_->verticalScrollBar();
      stickToBottom_ = bar->maximum() - bar->value() <= STICKY_BOTTOM_PX;
      updateJumpButtons();
    });
    connect(scroll_->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] {
      auto* bar = scroll_->verticalScrollBar();
      if (stickToBottom_ && bar->value() < bar->maximum()) bar->setValue(bar->maximum());
      updateJumpButtons();
    });
    const auto jumpTo = [this](bool top) {
      auto* sb = scroll_->verticalScrollBar();
      auto* anim = new QVariantAnimation(sb);
      anim->setDuration(220);
      anim->setStartValue(sb->value());
      anim->setEndValue(top ? 0 : sb->maximum());
      anim->setEasingCurve(QEasingCurve::OutCubic);
      connect(anim, &QVariantAnimation::valueChanged, sb,
              [sb](const QVariant& v) { sb->setValue(v.toInt()); });
      anim->start(QAbstractAnimation::DeleteWhenStopped);
    };
    connect(log_.jumpTop, &QToolButton::clicked, this, [jumpTo] { jumpTo(true); });
    connect(log_.jumpBottom, &QToolButton::clicked, this, [jumpTo] { jumpTo(false); });
    log_.splitter->addWidget(scroll_);
    buildComposer();

    log_.splitter->addWidget(log_.inputArea);
    // The pill centres on the handle (= the dock); the context-menu panel differs on purpose.
    log_.splitter->setStretchFactor(0, 1);
    log_.splitter->setStretchFactor(1, 0);
    // A tall transcript over a compact composer.
    log_.splitter->setSizes({360, 96});
    col->addWidget(log_.splitter, 1);

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
