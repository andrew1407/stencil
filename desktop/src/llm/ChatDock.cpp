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
    splitter_ = new PillSplitter(Qt::Vertical, body);
    splitter_->setObjectName("chatSplitter");
    splitter_->setChildrenCollapsible(false);

    scroll_ = new QScrollArea(splitter_);
    scroll_->setWidgetResizable(true);
    // Never scrolls sideways: a long unbreakable token would slide the bubbles out of view.
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setMinimumHeight(100);
    transcript_ = new QWidget(scroll_);
    transcriptLayout_ = new QVBoxLayout(transcript_);
    transcriptLayout_->setContentsMargins(10, 10, 10, 10);
    transcriptLayout_->setSpacing(8);
    buildSuggestions();
    transcriptLayout_->addStretch(1);
    scroll_->setWidget(transcript_);
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
    jumpTop_ = mkJump(QStringLiteral("Jump to the beginning"));
    jumpBottom_ = mkJump(QStringLiteral("Jump to the latest message"));
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
    connect(jumpTop_, &QToolButton::clicked, this, [jumpTo] { jumpTo(true); });
    connect(jumpBottom_, &QToolButton::clicked, this, [jumpTo] { jumpTo(false); });
    splitter_->addWidget(scroll_);

    auto* inputArea = new QWidget(splitter_);
    inputArea_ = inputArea;
    inputArea->setMinimumHeight(88);
    // The QPlainTextEdit would swallow the drop and paste the path as text.
    inputArea->setAcceptDrops(true);
    inputArea->installEventFilter(this);
    auto* inputCol = new QVBoxLayout(inputArea);
    inputArea->setObjectName("chatInputArea");
    inputArea->setAttribute(Qt::WA_StyledBackground);
    inputCol->setContentsMargins(10, 8, 10, 8);
    inputCol->setSpacing(4);

    busy_ = new QProgressBar(inputArea);
    busy_->setRange(0, 0);
    busy_->setTextVisible(false);
    busy_->setFixedHeight(4);
    busy_->hide();
    inputCol->addWidget(busy_);

    // browser .chat-attachments parity
    attachTray_ = new QWidget(inputArea);
    attachTray_->setObjectName("chatAttachTray");
    // Wide chips clip rather than raising the dock's minimum width.
    attachTray_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* trayRow = new QHBoxLayout(attachTray_);
    trayRow->setContentsMargins(0, 0, 0, 0);
    trayRow->setSpacing(6);
    trayRow->addStretch(1);
    attachTray_->setVisible(false);
    inputCol->addWidget(attachTray_);

    auto* inputRow = new QHBoxLayout;
    inputRow->setSpacing(6);
    input_ = new QPlainTextEdit(inputArea);
    input_->setObjectName("chatInput");
    input_->setPlaceholderText("Ask the assistant… (Enter sends, Shift+Enter newline)");
    input_->setMinimumHeight(48);
    input_->setAcceptDrops(false);
    input_->viewport()->setAcceptDrops(false);
    input_->installEventFilter(this);
    connect(input_, &QPlainTextEdit::textChanged, this, &ChatDock::updateSendEnabled);
    inputRow->addWidget(input_, 1);

    auto* btnWrap = new QWidget(inputArea);
    auto* btnCol = new QVBoxLayout(btnWrap);
    btnCol->setContentsMargins(0, 0, 0, 0);
    btnCol->setSpacing(0);
    btnCol->addStretch(1);
    // Only SEND stays inline; the hidden QToolButtons live on as the "…" menu's action targets.
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(2);
    send_ = makeChatAccentButton(btnWrap, "");
    send_->setObjectName("chatSend");
    send_->setEnabled(false);
    connect(send_, &QToolButton::clicked, this, &ChatDock::onSendClicked);
    btnRow->addWidget(send_);
    attach_ = makeChatAccentButton(btnWrap, QString());
    attach_->setAccessibleName(QStringLiteral("Add image"));
    attach_->setObjectName("chatAttach");
    attach_->hide();
    connect(attach_, &QToolButton::clicked, this, &ChatDock::pickMedia);
    gear_ = makeChatAccentButton(btnWrap, QString());
    gear_->setAccessibleName(QStringLiteral("Assistant settings"));
    gear_->setObjectName("chatGear");
    gear_->hide();
    connect(gear_, &QToolButton::clicked, this, &ChatDock::settingsRequested);
    more_ = makeChatAccentButton(btnWrap, QString());
    more_->setAccessibleName(QStringLiteral("More actions"));
    more_->setObjectName("chatMore");
    more_->setPopupMode(QToolButton::InstantPopup);
    {
      auto* menu = new QMenu(more_);
      menu->setObjectName("chatMoreMenu");
      actAttach_ = menu->addAction(QStringLiteral("Add image"));
      connect(actAttach_, &QAction::triggered, this, &ChatDock::pickMedia);
      actClear_ = menu->addAction(QStringLiteral("Clear history"));
      connect(actClear_, &QAction::triggered, this, [this] {
        clearConversation();
        emit clearRequested();
      });
      // Re-skins this dock immediately; the owner persists it and propagates to the mirror panel.
      actSwapSides_ = menu->addAction(QStringLiteral("Swap message sides"));
      connect(actSwapSides_, &QAction::triggered, this, [this] {
        setChatSwapSides(!chatSwapSides_);
        emit chatSwapSidesChanged(chatSwapSides_);
      });
      actSettings_ = menu->addAction(QStringLiteral("Settings"));
      connect(actSettings_, &QAction::triggered, this, &ChatDock::settingsRequested);
      // An item that cannot act right now HIDES rather than greys out (browser parity).
      connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        syncMoreMenuItems();
        fitMenuWidth(*menu);
      });
      compactIconMenu(*menu);
      // Built once and parented to the menu: it is created once, popped many times (menuRowPolish.hpp).
      new support::MenuShimmer(menu, menu);
      support::revealMenuFrom(*menu, more_);
      more_->setMenu(menu);
    }
    btnRow->addWidget(more_);
    // The dot is a child pinned to the "…" trigger's top-right corner: no layout space.
    statusDot_ = new QLabel(more_);
    statusDot_->setObjectName("chatStatusDot");
    statusDot_->setFixedSize(7, 7);
    statusDot_->setAttribute(Qt::WA_TransparentForMouseEvents);
    statusDot_->move(ACCENT_EDGE - statusDot_->width() - 1, 1);
    statusDot_->raise();
    // Disabled while a turn is in flight — the transcript can't be wiped mid-answer.
    clearBtn_ = makeChatAccentButton(btnWrap, QString());
    clearBtn_->setAccessibleName(QStringLiteral("Clear history"));
    clearBtn_->setObjectName("chatClear");
    clearBtn_->hide();
    connect(clearBtn_, &QToolButton::clicked, this, [this] {
      clearConversation();
      emit clearRequested();
    });
    btnCol->addLayout(btnRow);
    inputRow->addWidget(btnWrap, 0, Qt::AlignBottom);
    inputCol->addLayout(inputRow, 1);

    // A child of the composer, not a layout item — it must cover the input, not push it around.
    dropCue_ = new QWidget(inputArea);
    dropCue_->setObjectName(QStringLiteral("chatDropCue"));
    dropCue_->setAttribute(Qt::WA_StyledBackground);
    dropCue_->setAttribute(Qt::WA_TransparentForMouseEvents);
    dropCue_->hide();
    auto* cueRow = new QHBoxLayout(dropCue_);
    cueRow->setContentsMargins(8, 8, 8, 8);
    cueRow->setSpacing(8);
    cueRow->addStretch(1);
    dropCueIcon_ = new QLabel(dropCue_);
    cueRow->addWidget(dropCueIcon_, 0, Qt::AlignVCenter);
    dropCueText_ = makePlainLabel(QStringLiteral("Drop to attach"), dropCue_);
    cueRow->addWidget(dropCueText_, 0, Qt::AlignVCenter);
    cueRow->addStretch(1);
    // The bob rides on the label's contents margins — it sits in a layout, so moving it would fight it.
    dropCueAnim_ = new QVariantAnimation(this);
    dropCueAnim_->setDuration(1000);
    dropCueAnim_->setLoopCount(-1);
    dropCueAnim_->setKeyValueAt(0.0, -2.0);
    dropCueAnim_->setKeyValueAt(0.5, 2.0);
    dropCueAnim_->setKeyValueAt(1.0, -2.0);
    connect(dropCueAnim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
      if (!dropCueIcon_) return;
      const int dy = qRound(v.toDouble());
      dropCueIcon_->setContentsMargins(0, 4 + dy, 0, 4 - dy);
    });

    splitter_->addWidget(inputArea);
    // The pill centres on the handle (= the dock); the context-menu panel differs on purpose.
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 0);
    // A tall transcript over a compact composer.
    splitter_->setSizes({360, 96});
    col->addWidget(splitter_, 1);

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

