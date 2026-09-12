#include "chatDock.hpp"
#include "chatDockShared.hpp"
#include "../support/guiHelpers.hpp"
#include "chatWidgets.hpp"
#include "pillSplitter.hpp"
#include <QLineEdit>
#include <QRadioButton>
#include <QCheckBox>
#include <QButtonGroup>

#include "iconSet.hpp"
#include "mediaLoader.hpp"  // isImageFileName / isVideoFileName (attach routing)
#include "theme.hpp"        // Palette (restyleIcons)
#include "scrollReveal.hpp"  // transcript cards fade at the viewport edges
#include "../support/disintegrateOverlay.hpp"  // cards scatter on Clear, gather on append
#include "../support/flowLayout.hpp"           // the suggestion chips wrap like browser chips
#include "../support/modalReveal.hpp"          // support::motionReduced()
#include "../support/menuReveal.hpp"           // card menu grows from the click
#include "../support/iconMotion.hpp"           // the per-icon hover motion
#include "../support/shimmerOverlay.hpp"       // the shared hover sweep

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
#include "../support/menuShimmer.hpp"
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
    // Deliberately NOT SelectionPanel's NoDockWidgetFeatures: this panel docks
    // on all four sides and floats freely (drag-move + resize), per the plan.
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                QDockWidget::DockWidgetClosable);
    // The COMPOSER acts on a drop (see the eventFilter); the dock SWALLOWS one.
    // Letting a drop fall through the dock reached the window's own drop zone and
    // offered to open the image as a project — never what dragging onto a chat
    // means. So the dock accepts everything and does nothing with what misses the
    // composer. Browser chatPanel.js parity.
    setAcceptDrops(true);
    // The branded header IS the title bar (no double header; browser parity).
    buildTitleBar();

    // The content is one cohesive CARD (browser panel parity): its own
    // controls-panel background + themed border, styled in restyleIcons so it
    // tracks the live palette.
    auto* body = new QWidget(this);
    body->setObjectName("chatBody");
    body->setAttribute(Qt::WA_StyledBackground);
    auto* col = new QVBoxLayout(body);
    col->setContentsMargins(0, 0, 0, 0);   // the panel edge IS the card border
    col->setSpacing(6);

    // Transcript over the input area in a vertical splitter, so the user can
    // resize the input (browser parity with the resizable composer).
    // The SHARED pill grip (pillSplitter.hpp), the same one the context-menu chat
    // uses: a short centred bar. The stylesheet handle this replaces could only be
    // narrowed by a symmetric margin, so it stretched with the panel — in a wide dock
    // the "pill" became a fat accent band across the whole composer.
    splitter_ = new PillSplitter(Qt::Vertical, body);
    splitter_->setObjectName("chatSplitter");
    splitter_->setChildrenCollapsible(false);

    // transcript: borderless, 10px padding / 8px gap (.chat-transcript)
    scroll_ = new QScrollArea(splitter_);
    scroll_->setWidgetResizable(true);
    // A transcript never scrolls sideways: a long unbreakable token (a pasted URL) would
    // otherwise widen the content, raise a horizontal bar and slide the bubbles out of
    // view. Off, so the cards wrap inside the viewport instead.
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setMinimumHeight(100);
    transcript_ = new QWidget(scroll_);
    transcriptLayout_ = new QVBoxLayout(transcript_);
    transcriptLayout_->setContentsMargins(10, 10, 10, 10);
    transcriptLayout_->setSpacing(8);
    buildSuggestions();  // empty-state chips (top-left), gone on the first card
    transcriptLayout_->addStretch(1);
    scroll_->setWidget(transcript_);
    // Cards dissolve toward the transcript's edges as it scrolls (browser parity:
    // .reveal-item in css/animations.css). Parented to scroll_ — nothing to free.
    reveal_ = new ScrollReveal(scroll_);
    // Bubbles are capped to a share of the viewport, so they follow its width.
    scroll_->viewport()->installEventFilter(this);
    // Jump pills over the transcript's bottom edge (browser .chat-jumps): ⌄ shows
    // once the view scrolled up from the latest message, ⌃ once it left the very
    // beginning — both mid-log, neither while the log fits.
    const auto mkJump = [this](const QString& tip) {
      auto* b = new QToolButton(scroll_);
      b->setObjectName(QStringLiteral("chatJumpBtn"));
      b->setToolTip(tip);
      b->setCursor(Qt::PointingHandCursor);
      b->setFixedSize(28, 28);
      b->setIconSize(QSize(14, 14));
      // Ghosted at rest so the bubble underneath stays readable; the hover
      // handling in eventFilter lifts it to full opacity.
      auto* fx = new QGraphicsOpacityEffect(b);
      fx->setOpacity(kGhostRestOpacity);
      b->setGraphicsEffect(fx);
      b->installEventFilter(this);
      b->hide();
      return b;
    };
    jumpTop_ = mkJump(QStringLiteral("Jump to the beginning"));
    jumpBottom_ = mkJump(QStringLiteral("Jump to the latest message"));
    connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
      // Chat stickiness: scrolling away from the bottom releases the follow
      // pin; returning (or any jump to the end) re-arms it.
      const auto* bar = scroll_->verticalScrollBar();
      stickToBottom_ = bar->maximum() - bar->value() <= kStickyBottomPx;
      updateJumpButtons();
    });
    connect(scroll_->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] {
      // Content growing under a pinned view (deferred layouts, late notes,
      // variant thumbnails) keeps the newest message on screen.
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
    // The composer is the drop target; its QPlainTextEdit would otherwise swallow the
    // drop and paste the path as text, so it declines drops explicitly.
    inputArea->setAcceptDrops(true);
    inputArea->installEventFilter(this);
    auto* inputCol = new QVBoxLayout(inputArea);
    inputArea->setObjectName("chatInputArea");   // .chat-input-row: top divider
    inputArea->setAttribute(Qt::WA_StyledBackground);
    inputCol->setContentsMargins(10, 8, 10, 8);  // padding: 8px 10px
    inputCol->setSpacing(4);

    busy_ = new QProgressBar(inputArea);
    busy_->setRange(0, 0);  // indeterminate
    busy_->setTextVisible(false);
    busy_->setFixedHeight(4);
    busy_->hide();
    inputCol->addWidget(busy_);

    // Pending attachments as removable chips (browser .chat-attachments parity):
    // a thumbnail so you can SEE what is queued, and a × to drop it.
    attachTray_ = new QWidget(inputArea);
    attachTray_->setObjectName("chatAttachTray");
    // The tray's content must never dictate the dock's width — wide chips clip
    // instead of growing the panel or raising its minimum size.
    attachTray_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* trayRow = new QHBoxLayout(attachTray_);
    trayRow->setContentsMargins(0, 0, 0, 0);
    trayRow->setSpacing(6);
    trayRow->addStretch(1);
    attachTray_->setVisible(false);
    inputCol->addWidget(attachTray_);

    // Input stretches; the compact ghost action cluster sits bottom-RIGHT.
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
    btnCol->addStretch(1);  // pin the button row to the bottom edge of the input
    // The composer buttons (browser parity): only SEND stays inline, with a "…"
    // overflow holding attach / clear / settings. The real QToolButtons live on
    // as the menu's action targets, so every existing connection and
    // disabled-state sync keeps working untouched.
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(2);
    send_ = makeChatAccentButton(btnWrap, "");
    send_->setObjectName("chatSend");
    send_->setEnabled(false);  // empty input
    connect(send_, &QToolButton::clicked, this, &ChatDock::onSendClicked);
    btnRow->addWidget(send_);
    attach_ = makeChatAccentButton(btnWrap, QString());  // no tooltip (user decision)
    attach_->setAccessibleName(QStringLiteral("Add image"));
    attach_->setObjectName("chatAttach");
    attach_->hide();  // reachable through the … menu
    connect(attach_, &QToolButton::clicked, this, &ChatDock::pickMedia);
    gear_ = makeChatAccentButton(btnWrap, QString());  // no tooltip (user decision)
    gear_->setAccessibleName(QStringLiteral("Assistant settings"));
    gear_->setObjectName("chatGear");
    gear_->hide();    // reachable through the … menu
    connect(gear_, &QToolButton::clicked, this, &ChatDock::settingsRequested);
    more_ = makeChatAccentButton(btnWrap, QString());  // no tooltip (user decision)
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
        emit clearRequested();  // the owner drops chatHistory_ + per-turn caches
      });
      // Browser/extension parity: "Swap message sides" between Clear history and
      // Settings. Re-skins this dock immediately; the owner persists it and
      // propagates to the context menu's mirror panel (chatSwapSidesChanged).
      actSwapSides_ = menu->addAction(QStringLiteral("Swap message sides"));
      connect(actSwapSides_, &QAction::triggered, this, [this] {
        setChatSwapSides(!chatSwapSides_);
        emit chatSwapSidesChanged(chatSwapSides_);
      });
      // No separator before Settings — the browser menu lists the four flat.
      actSettings_ = menu->addAction(QStringLiteral("Settings"));
      connect(actSettings_, &QAction::triggered, this, &ChatDock::settingsRequested);
      // Item states refresh as the menu opens: an item that cannot act right now
      // HIDES rather than greys out (user decision; browser parity) — no third
      // image past the §7 cap, nothing to clear on an empty conversation.
      connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        syncMoreMenuItems();
        fitMenuWidth(*menu);   // the labels are fixed, the FONT is not (theme/DPI change)
      });
      // Four short labelled icons, not a menu-bar menu — so it hugs them (the card
      // "⋯" gets the same treatment) instead of wearing the theme's wide gutters.
      compactIconMenu(*menu);
      // The same glass hover sweep every other ctx row plays (browser .chat-row-menu-item
      // parity). Built once and parented to the menu, because this one is created once and
      // popped many times (support/menuRowPolish.hpp says why).
      new support::MenuShimmer(menu, menu);
      // Both edges of the overflow fly, browser/extension parity: the motes stream out
      // of the "…" and pour back into it (the menu is built once, popped many times).
      support::revealMenuFrom(*menu, more_);
      more_->setMenu(menu);
    }
    btnRow->addWidget(more_);
    // The reachability dot rides on the … TRIGGER (browser parity): the gear
    // itself now lives in the menu, so it is hidden most of the time. A child
    // pinned to the button's top-right corner — it moves/paints with the button
    // and occupies no layout space. Both carry the provider tooltip.
    statusDot_ = new QLabel(more_);
    statusDot_->setObjectName("chatStatusDot");
    statusDot_->setFixedSize(7, 7);  // a badge, not a bubble (incl. the ring)
    statusDot_->setAttribute(Qt::WA_TransparentForMouseEvents);
    statusDot_->move(kAccentEdge - statusDot_->width() - 1, 1);
    statusDot_->raise();
    // Clear the conversation rides with the composer icons (browser #chat-clear
    // parity — the input row, not the header). Disabled while a turn is in
    // flight (like attach) — the transcript can't be wiped mid-answer.
    clearBtn_ = makeChatAccentButton(btnWrap, QString());  // no tooltip (user decision)
    clearBtn_->setAccessibleName(QStringLiteral("Clear history"));
    clearBtn_->setObjectName("chatClear");
    clearBtn_->hide();  // reachable through the … menu (only send + … stay inline)
    connect(clearBtn_, &QToolButton::clicked, this, [this] {
      clearConversation();
      emit clearRequested();  // the owner drops chatHistory_ + per-turn caches
    });
    btnCol->addLayout(btnRow);
    inputRow->addWidget(btnWrap, 0, Qt::AlignBottom);
    inputCol->addLayout(inputRow, 1);

    // The drop cue: an icon + label drawn OVER the composer while a drag hovers
    // it (browser .chat-drop-cue). A child of the composer rather than a layout item
    // — it must cover the input, not push it around — so its geometry is synced from
    // the composer's resize in eventFilter. Colours land in restyleIcons.
    dropCue_ = new QWidget(inputArea);
    dropCue_->setObjectName(QStringLiteral("chatDropCue"));
    dropCue_->setAttribute(Qt::WA_StyledBackground);
    dropCue_->setAttribute(Qt::WA_TransparentForMouseEvents);   // the drag is the composer's
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
    // The bob: the glyph rides up and down beside the label, so the target reads as
    // "let go here" rather than a static outline. Driven through the label's own
    // contents margins — it sits in a layout, so moving it outright would fight it.
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
    // No pill reference here: the browser's #chat-input-sizer is a full-width strip
    // with a CENTRED pill, so the dock's pill centres on the handle (= the dock).
    // The context-menu panel differs on purpose — its browser twin nests the sizer
    // inside the input column, so THAT grip keeps its input reference.
    splitter_->setStretchFactor(0, 1);  // extra space goes to the transcript
    splitter_->setStretchFactor(1, 0);
    // Initial proportions: a tall transcript over a compact composer, so the
    // input area doesn't balloon when the dock is tall (the user can still drag
    // the splitter).
    splitter_->setSizes({360, 96});
    col->addWidget(splitter_, 1);

    setWidget(body);
    setMinimumWidth(260);
    // Every tear-off adopts the compact floating default instead of the docked
    // span. Deferred one tick so it lands AFTER Qt's own tear-off geometry
    // (drag tear-offs apply theirs when the drag starts).
    connect(this, &QDockWidget::topLevelChanged, this, [this](bool floating) {
      updatePlacementState();
      if (!floating) return;
      QTimer::singleShot(0, this, [this] {
        if (isFloating()) resize(kFloatingSize.expandedTo(minimumSize()));
      });
    });
    connect(this, &QDockWidget::dockLocationChanged, this,
            [this](Qt::DockWidgetArea) { updatePlacementState(); });
    setProviderStatus(QString(), ProviderStatus::Unknown);
  }
}  // namespace stencil::gui

