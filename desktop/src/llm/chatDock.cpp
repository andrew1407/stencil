#include "chatDock.hpp"
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

  namespace {
    // Qt still reserves the unused shortcut column when sizing the popup, so the width
    // must be set outright. fitMenuWidth / compactIconMenu live in support/guiHelpers —
    // the projects row's "⋯" wants the same treatment.
    using gui::compactIconMenu;
    using gui::fitMenuWidth;

    constexpr int kThumbEdge = 160;  // variant thumbnail long edge (px)
    constexpr int kButtonEdge = 23;  // compact ghost action buttons (browser .chat-hbtn: 23x23)
    constexpr int kHeaderIcon = 13;  // …with a 13px glyph, as in the browser header
    // The composer's action trio is a step larger than the title-bar ghosts —
    // it is the primary control cluster, and the context menu's assistant panel
    // mirrors these exact numbers so the two composers read identically.
    constexpr int kAccentEdge = 30;
    constexpr int kAccentIcon = 20;
    // Suggestion-chip corner: Qt silently draws a SQUARE box when border-radius exceeds
    // half the height; 14 is half the app-wide floor, so the chip stays a true pill.
    constexpr int kSuggestChipRadius = 14;
    // …and it FADES while the dust flies, instead of blinking out from under it
    // (browser css/animations.css chatCardLeave, motion.js CHAT_LEAVE_MS).
    constexpr int kChatLeaveMs = 260;
    // Widest a chip's filename may render — beyond it the name elides (tooltip has it).
    constexpr int kChipNameMaxPx = 150;
    // How long a removed chip HOLDS its slot before the neighbours slide over — the
    // scatter gets a beat to read before anything else moves (user-tuned).
    constexpr int kChatChipHoldMs = 140;
    // How many images ONE message may carry (browser/extension chatController.js
    // MAX_ATTACHMENTS). A turn's images are re-encoded, replayed and paid for per turn
    // (contract §7); past three the queue is refused rather than silently trimmed.
    constexpr int kMaxAttachments = 3;
    // Jump pills and the row "…" triggers rest translucent so a short bubble under
    // them stays readable; hover restores full opacity. One deliberately shared
    // figure (browser .chat-jump-btn / .chat-row-menu-btn on all three surfaces).
    constexpr double kGhostRestOpacity = 0.7;
    // How close to the bottom (px) still counts as "reading the end" — the
    // transcript follows new content only inside this band (chat stickiness).
    constexpr int kStickyBottomPx = 40;

    // Sink + dissolve `w` in place, then delete it. The snapshot the scatter is made
    // of was taken already, so the two play together exactly as they do in the
    // browser: the bubble melts into its own dust rather than vanishing first.
    void fadeOutAndDelete(QWidget* w) {
      if (!w) return;
      // CLAIM the card's effect for the length of the fade. ScrollReveal installs its
      // own DissolveEffect on any card near a viewport edge, and setGraphicsEffect
      // DELETES the one already there — so a card that scrolled while it faded had the
      // effect this animation writes to freed under it, and the next frame crashed in
      // QGraphicsOpacityEffect::setOpacity. The entrance animation claims it the same
      // way (animateCardIn), which is why ScrollReveal skips those.
      w->setProperty(ScrollReveal::kEnteringProperty, true);
      // A card removed mid-entrance still has its appear animation running, and that
      // animation writes to the effect it installed. setGraphicsEffect() DELETES the
      // old effect, so installing a fresh one left the entrance holding a dangling
      // pointer — a use-after-free that crashed on the very next frame. Stop the
      // card's own animations first and REUSE whatever effect is already there.
      for (QVariantAnimation* a : w->findChildren<QVariantAnimation*>()) a->stop();
      auto* fx = qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
      if (!fx) {
        fx = new QGraphicsOpacityEffect(w);
        w->setGraphicsEffect(fx);   // the widget owns the effect
      }
      const double from = fx->opacity();
      fx->setOpacity(from);
      auto* anim = new QVariantAnimation(w);
      anim->setDuration(kChatLeaveMs);
      anim->setStartValue(from);   // continue from wherever the entrance got to
      anim->setEndValue(0.0);
      anim->setEasingCurve(QEasingCurve::OutCubic);
      // QPointer, not a raw capture: belt and braces for the same hazard — if anything
      // ever replaces the effect mid-flight again, the write is skipped, not fatal.
      QPointer<QGraphicsOpacityEffect> fxp(fx);
      QObject::connect(anim, &QVariantAnimation::valueChanged, w,
                       [fxp](const QVariant& v) { if (fxp) fxp->setOpacity(v.toDouble()); });
      QObject::connect(anim, &QVariantAnimation::finished, w, [w] {
        w->hide();
        w->deleteLater();
      });
      anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
    // Card appear motion (browser parity): subtle fade + short upward slide.
    constexpr int kAppearMs = 140;
    constexpr int kAppearSlidePx = 6;
    // Compact default when torn off (browser floating-panel parity). Without it
    // the floating dock inherits its docked span and stretches across the whole
    // main window, scattering the hint / input / buttons apart.
    // 385, not 380: the extra 5px is breathing room for the per-row "…", which
    // hangs OUTSIDE the bubble and was landing hard against the transcript edge.
    constexpr QSize kFloatingSize{385, 480};

    QToolButton* makeGhostButton(QWidget* parent, const QString& tooltip) {
      auto* b = new QToolButton(parent);
      b->setAutoRaise(true);
      b->setToolButtonStyle(Qt::ToolButtonIconOnly);
      b->setIconSize(QSize(kHeaderIcon, kHeaderIcon));
      b->setFixedSize(kButtonEdge, kButtonEdge);
      b->setToolTip(tooltip);
      b->setCursor(Qt::PointingHandCursor);
      // The browser shimmers every <button>, chat controls included (layout.css
      // ui-shimmer); this is the one factory behind ALL of them — title-bar
      // ghosts, the composer's send/attach/gear, the cards' Resend — so the
      // sweep lands on each exactly once. Mouse-through, so nothing about the
      // click target or the disabled styling changes.
      installHoverShimmer(b);
      return b;
    }


    // Re-run the stylesheet for a widget whose objectName just changed (Qt matches
    // selectors at polish time, not on every paint).
    void repolish(QWidget* w) {
      if (!w || !w->style()) return;
      w->style()->unpolish(w);
      w->style()->polish(w);
      w->update();
    }

    // EXIF-aware file decode shared by the attach dialog and paste/drop routing.
    QImage readImageFile(const QString& path) {
      QImageReader reader(path);
      reader.setAutoTransform(true);
      return reader.read();
    }

  }  // namespace

  const char* const kChatStatusOkColor = "#2e9e4f";
  const char* const kChatStatusBadColor = "#d6293e";

  QToolButton* makeChatAccentButton(QWidget* parent, const QString& tooltip) {
    QToolButton* b = makeGhostButton(parent, tooltip);
    b->setProperty("chatAccent", true);
    b->setFixedSize(kAccentEdge, kAccentEdge);
    b->setIconSize(QSize(kAccentIcon, kAccentIcon));  // fill the button like the browser's
    return b;
  }

  void styleProviderStatusDot(QLabel* dot, QToolButton* gear,
                              const QString& richTooltip,
                              ChatDock::ProviderStatus status,
                              const QPalette& pal) {
    const QString color =
        status == ChatDock::ProviderStatus::Ok            ? QString(kChatStatusOkColor)
        : status == ChatDock::ProviderStatus::Unreachable ? QString(kChatStatusBadColor)
                                                          : pal.color(QPalette::Mid).name();
    // Badge on the gear's corner: filled dot + a subtle ring for legibility.
    dot->setStyleSheet(
        QStringLiteral("background:%1;border-radius:3px;border:1px solid rgba(255,255,255,160);")
            .arg(color));
    const QString tip =
        richTooltip.isEmpty() ? QStringLiteral("AI assistant settings") : richTooltip;
    gear->setToolTip(tip);
    dot->setToolTip(tip);
  }

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

    // ── transcript: borderless, 10px padding / 8px gap (.chat-transcript) ──
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
    // ── Jump pills over the transcript's bottom edge (browser .chat-jumps): ⌄ shows
    // once the view scrolled up from the latest message, ⌃ once it left the very
    // beginning — both mid-log, neither while the log fits. ──
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

    // ── input area (busy bar + attachment note + input row) ──
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

    // ── The drop cue: an icon + label drawn OVER the composer while a drag hovers
    // it (browser .chat-drop-cue). A child of the composer rather than a layout item
    // — it must cover the input, not push it around — so its geometry is synced from
    // the composer's resize in eventFilter. Colours land in restyleIcons. ──
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

  // Branded title bar (browser header-row parity): sparkle + accent
  // "Assistant" + right-aligned float/close. QDockWidget keeps its native
  // title drag through a custom title-bar widget (the buttons consume only
  // their own clicks); an event filter OBSERVES press/move/release to drive
  // the drag dock zones without disturbing Qt's drag.
  void ChatDock::buildTitleBar() {
    titleBar_ = new QWidget(this);
    titleBar_->setObjectName("chatTitleBar");
    titleBar_->setAttribute(Qt::WA_StyledBackground);  // part of the card
    // The header IS the drag handle (browser .chat-header): say so with the
    // cursor, and switch to the closed hand while a drag is actually running.
    titleBar_->setCursor(Qt::OpenHandCursor);
    auto* row = new QHBoxLayout(titleBar_);
    row->setContentsMargins(8, 5, 8, 5);   // .chat-header padding: 5px 8px
    row->setSpacing(3);     // .chat-header gap: 3px (the placement chips tighten to 1 below)
    headerIcon_ = new QLabel(titleBar_);
    // Optical vertical centering: the bubble glyph's tail row is mostly empty, so a
    // box-centered 16px icon reads ~2px high next to the title's cap height.
    headerIcon_->setFixedSize(16, 18);
    headerIcon_->setContentsMargins(0, 2, 0, 0);
    row->addWidget(headerIcon_);
    headerTitle_ = new QLabel("Assistant", titleBar_);
    headerTitle_->setObjectName("chatHeaderTitle");
    {
      QFont f = headerTitle_->font();
      f.setBold(true);
      headerTitle_->setFont(f);
    }
    row->addWidget(headerTitle_);
    row->addStretch(1);
    // Placement buttons, browser header parity: dock left / top / bottom /
    // right, then float. Clicking pins the dock to that side directly (the
    // reliable path — dragging to a zone does the same thing).
    struct Place {
      Qt::DockWidgetArea area;
      const char* tip;
    };
    static const Place kPlaces[] = {
        {Qt::LeftDockWidgetArea, "Dock left — or drag the header to an edge"},
        {Qt::TopDockWidgetArea, "Dock top — or drag the header to an edge"},
        {Qt::BottomDockWidgetArea, "Dock bottom — or drag the header to an edge"},
        {Qt::RightDockWidgetArea, "Dock right — or drag the header to an edge"},
    };
    auto* dockGroup = new QWidget(titleBar_);
    auto* dockRow = new QHBoxLayout(dockGroup);
    dockRow->setContentsMargins(0, 0, 0, 0);
    dockRow->setSpacing(1);   // .chat-dock-btns gap: 1px
    for (const Place& p : kPlaces) {
      QToolButton* b = makeGhostButton(dockGroup, p.tip);
      const Qt::DockWidgetArea area = p.area;
      connect(b, &QToolButton::clicked, this, [this, area] { emit dockRequested(area); });
      dockRow->addWidget(b);
      dockBtns_.append(b);
    }
    row->addWidget(dockGroup);
    floatBtn_ = makeGhostButton(titleBar_, "Float — drag the header to move");
    // NOT a raw setFloating() here: that just teleports the panel with no animation
    // at all. The owner answers with the same dust flight a side switch plays,
    // ending in setFloating (or a re-dock) itself.
    connect(floatBtn_, &QToolButton::clicked, this, [this] { emit floatToggleRequested(); });
    row->addWidget(floatBtn_);
    closeBtn_ = makeGhostButton(titleBar_, "Close assistant");
    // NOT QWidget::close(): that hides the dock on the spot, and a side-docked
    // chat blinked out instead of sliding into its edge. The owner runs the same
    // animated path the toolbar toggle uses (closeEvent below routes every OTHER
    // close the same way).
    connect(closeBtn_, &QToolButton::clicked, this, [this] { emit closeRequested(); });
    row->addWidget(closeBtn_);
    setTitleBarWidget(titleBar_);
    titleBar_->installEventFilter(this);  // drag observation (drag dock zones)
  }

  // Empty-state suggestions (browser parity): clickable pills laid out INLINE
  // with wrapping (flow layout). Clicking PREFILLS the caller's composer (never
  // sends). SHARED with the context menu's assistant panel — one chip list, one
  // flow layout, one style — so the two empty states can't drift apart.
  QWidget* makeSuggestionChips(QWidget* parent, std::function<void(QString)> onPick) {
    auto* box = new QWidget(parent);
    box->setObjectName(QStringLiteral("chatSuggest"));
    auto* flow = new FlowLayout(box, 2, 6, 6);
    // One string per chip (browser CHAT_SUGGESTIONS parity): what's written on
    // the button is exactly what lands in the input.
    static const char* const kChips[] = {
        "Make it sepia",
        "3 variants: rotated \xc2\xb7 tinted \xc2\xb7 cropped",
        "Extract the lines from this image",
        "Crop 10% off every edge, rotate right",
    };
    for (const char* c : kChips) {
      const QString text = QString::fromUtf8(c);
      auto* chip = new QPushButton(text, box);
      chip->setObjectName(QStringLiteral("chatSuggestChip"));
      chip->setCursor(Qt::PointingHandCursor);
      chip->setFocusPolicy(Qt::NoFocus);
      QObject::connect(chip, &QPushButton::clicked, box,
                       [onPick, text] { if (onPick) onPick(text); });
      flow->addWidget(chip);
    }
    return box;
  }

  // Quiet solid chips: normal border/card background/text, accent border + a
  // faint accent fill on hover. Applied to whatever chip block is passed in.
  void styleSuggestionChips(QWidget* chips, const Palette& pal) {
    if (!chips) return;
    const QString qss =
        QStringLiteral("QPushButton{border:1px solid %1;border-radius:%8px;"
                       "background:%2;color:%3;padding:4px 12px;}"
                       "QPushButton:hover{border-color:%4;background:rgba(%5,%6,%7,26);}")
            .arg(pal.borderMain.name(), pal.bgContainer.name(), pal.textMain.name(),
                 pal.accent.name())
            .arg(pal.accent.red())
            .arg(pal.accent.green())
            .arg(pal.accent.blue())
            .arg(kSuggestChipRadius);
    for (QPushButton* chip : chips->findChildren<QPushButton*>(QStringLiteral("chatSuggestChip")))
      chip->setStyleSheet(qss);
  }

  void ChatDock::buildSuggestions() {
    // The empty state is the prompt chips, nothing more: the composer's own cue
    // (showDropCue) is what says a drop attaches, right where it lands.
    suggest_ = makeSuggestionChips(transcript_, [this](QString prompt) {
      input_->setPlainText(prompt);  // prefill only — never send
      input_->moveCursor(QTextCursor::End);
      input_->setFocus();
    });
    transcriptLayout_->addWidget(suggest_);
  }

  bool ChatDock::eventFilter(QObject* obj, QEvent* event) {
    // Transcript viewport resized → re-cap the bubble widths (never consumed).
    if (scroll_ && obj == scroll_->viewport() && event->type() == QEvent::Resize) {
      applyBubbleWidths();
      positionJumpButtons();
    }
    // (The per-card "⋯" hover/placement lives in the shared ChatCardMore watcher
    // now — installChatCardMenu attaches one per card, on both surfaces.)
    // Jump pills: translucent at rest, full opacity under the cursor.
    if ((obj == jumpTop_ || obj == jumpBottom_) &&
        (event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
      auto* pill = static_cast<QToolButton*>(obj);
      auto* fx = qobject_cast<QGraphicsOpacityEffect*>(pill->graphicsEffect());
      if (fx) fx->setOpacity(event->type() == QEvent::Enter ? 1.0 : kGhostRestOpacity);
      // …and the glyph brightens to --text-main under the cursor, dropping back
      // to --text-muted (browser .chat-jump-btn / :hover).
      const QColor glyph = event->type() == QEvent::Enter
          ? (textCache_.isValid() ? textCache_ : palette().color(QPalette::Text))
          : (mutedCache_.isValid() ? mutedCache_ : palette().color(QPalette::PlaceholderText));
      pill->setIcon(themedIcon(pill == jumpTop_ ? "chevron-up" : "chevron-down", glyph, 14));
    }
    // ── The composer IS the drop target (the dock itself declines drops) ──
    if (obj == inputArea_) {
      // The cue and the attach belong to the INPUT BOX only (browser parity): a
      // drag over the composer's buttons or chips neither lights the cue nor
      // attaches — that drop falls through to the dock, which swallows it.
      const auto overInput = [this](const QPointF& p) {
        if (!input_ || !input_->isVisible()) return true;
        return QRect(input_->mapTo(inputArea_, QPoint(0, 0)), input_->size())
            .contains(p.toPoint());
      };
      switch (event->type()) {
        case QEvent::DragEnter: {
          auto* de = static_cast<QDragEnterEvent*>(event);
          if (!canAttachMime(de->mimeData())) return false;   // let it fall through
          de->acceptProposedAction();
          showDropCue(overInput(de->position()));
          return true;
        }
        case QEvent::DragMove: {
          auto* dm = static_cast<QDragMoveEvent*>(event);
          if (!canAttachMime(dm->mimeData())) return false;
          dm->acceptProposedAction();
          showDropCue(overInput(dm->position()));
          return true;
        }
        case QEvent::DragLeave:
          showDropCue(false);
          return true;
        case QEvent::Drop: {
          auto* dr = static_cast<QDropEvent*>(event);
          showDropCue(false);
          if (!overInput(dr->position())) return false;   // dock swallows the miss
          if (!attachFromMimeData(dr->mimeData())) return false;
          dr->acceptProposedAction();
          return true;
        }
        case QEvent::Resize:
          // The cue tracks the input's box as the composer resizes (never consumed).
          if (dropCue_ && dropCue_->isVisible()) showDropCue(true);
          break;
        default:
          break;
      }
    }
    // Title-bar press starts the drag POLL (never consumed — Qt's own dock
    // drag runs on the same press). Tracking is poll-based because the native
    // floating-window drag swallows the subsequent move/release events.
    if (obj == titleBar_) {
      // Test seam installed (offscreen, no real cursor) → the poll path.
      if (dragPosProbe_) {
        if (event->type() == QEvent::MouseButtonPress &&
            static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
          startDragPoll();
        return QDockWidget::eventFilter(obj, event);
      }
      // Real input: drive the drag ourselves and CONSUME the events, so Qt
      // never starts its own move. Qt's floating-dock drag becomes a
      // window-server move on macOS, which swallows the release — the drop
      // then never resolves and nothing docks.
      switch (event->type()) {
        case QEvent::MouseButtonPress: {
          auto* me = static_cast<QMouseEvent*>(event);
          if (me->button() != Qt::LeftButton) break;
          manualDrag_ = true;
          manualDragging_ = false;
          titleBar_->setCursor(Qt::OpenHandCursor);   // drag ended — back to "grab me"
          dragStartCursor_ = me->globalPosition().toPoint();
          // Explicit grab: once the cursor leaves the bar the moves would
          // otherwise be delivered to whatever is underneath, and the drag
          // (and its zones) would never start.
          titleBar_->grabMouse();
          return true;
        }
        case QEvent::MouseMove: {
          if (!manualDrag_) break;
          const QPoint g = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
          if (!manualDragging_) {
            if ((g - dragStartCursor_).manhattanLength() < QApplication::startDragDistance())
              return true;
            manualDragging_ = true;
            titleBar_->setCursor(Qt::ClosedHandCursor);
            setNativeDockingSuppressed(true);
            if (!isFloating()) {
              // Tear off under the cursor, at the compact float default —
              // the browser's undock-at-the-pointer behaviour.
              setFloating(true);
              resize(kFloatingSize);
              manualGrabOffset_ = QPoint(qMin(kFloatingSize.width() / 2, 140), 12);
            } else {
              manualGrabOffset_ = g - frameGeometry().topLeft();
            }
            dragActive_ = true;
            emit titleDragStarted();
          }
          move(g - manualGrabOffset_);
          emit titleDragMoved(g);
          return true;
        }
        case QEvent::MouseButtonRelease: {
          if (!manualDrag_) break;
          const QPoint g = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
          const bool dragged = manualDragging_;
          manualDrag_ = manualDragging_ = false;
          titleBar_->setCursor(Qt::OpenHandCursor);   // drag ended — back to "grab me"
          titleBar_->releaseMouse();
          setNativeDockingSuppressed(false);   // dock AFTER the restore
          if (dragged) {
            dragActive_ = false;
            emit titleDragFinished(g);
          }
          return true;
        }
        default:
          break;
      }
      return QDockWidget::eventFilter(obj, event);
    }
    if (obj == input_ && event->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(event);
      // Paste: an image on the clipboard (or image/video file URLs) becomes an
      // attachment; plain text falls through to the normal paste.
      if (ke->matches(QKeySequence::Paste)) {
        if (attachFromMimeData(QGuiApplication::clipboard()->mimeData())) return true;
        return QDockWidget::eventFilter(obj, event);
      }
      // Word delete, spelled out rather than left to the platform's standard-key table: ⌥⌫ is
      // what people press here, and Qt's mapping for it varies by platform (on this one it
      // arrived as a plain Backspace and ate a single character). ⌥⌦ deletes forward.
      if ((ke->key() == Qt::Key_Backspace || ke->key() == Qt::Key_Delete) &&
          (ke->modifiers() & (Qt::AltModifier | Qt::ControlModifier))) {
        const auto toward = ke->key() == Qt::Key_Backspace ? QTextCursor::PreviousWord
                                                           : QTextCursor::NextWord;
        QTextCursor c = input_->textCursor();
        if (!c.hasSelection()) c.movePosition(toward, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        input_->setTextCursor(c);
        return true;
      }
      // Enter sends; Shift+Enter inserts a newline (browser textarea convention).
      if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
          !(ke->modifiers() & Qt::ShiftModifier)) {
        submit();
        return true;
      }
    }
    return QDockWidget::eventFilter(obj, event);
  }

  // ── drag-poll (drag dock zones; see the header comment) ──

  // The button for the CURRENT placement is accent-filled and inert (browser
  // .chat-dock-btn-active) — you can't dock where you already are.
  void ChatDock::updatePlacementState() {
    if (dockBtns_.size() < 4 || !floatBtn_ || !accentCache_.isValid()) return;
    static const char* kGlyphs[] = {"chevron-left", "chevron-up", "chevron-down",
                                    "chevron-right"};
    static const Qt::DockWidgetArea kAreas[] = {
        Qt::LeftDockWidgetArea, Qt::TopDockWidgetArea, Qt::BottomDockWidgetArea,
        Qt::RightDockWidgetArea};
    auto* mw = qobject_cast<QMainWindow*>(parentWidget());
    const Qt::DockWidgetArea current =
        (!isFloating() && mw) ? mw->dockWidgetArea(this) : Qt::NoDockWidgetArea;
    const QString activeQss = QStringLiteral("background:%1;border:none;border-radius:5px;")
                                  .arg(chipCache_.name());
    const auto paint = [&](QToolButton* b, const char* glyph, bool active) {
      b->setIcon(themedIcon(glyph, active ? accentCache_ : textCache_, kHeaderIcon));
      b->setStyleSheet(active ? activeQss : QString());
      // The float button's `maximize` glyph has a second motion for the ALREADY-floating
      // state: its corners retract instead of extending (iconMotion.json variants.active).
      b->setProperty(kIconStateProperty, active ? "active" : "");
      // Left ENABLED: docking where you already are is a no-op anyway, and disabling it
      // handed the button to QToolButton:disabled — a bordered grey chip with a dimmed
      // glyph, which is what made the row look dark and unclear.
      b->setEnabled(true);
    };
    for (int i = 0; i < 4; ++i)
      paint(dockBtns_[i], kGlyphs[i], !isFloating() && current == kAreas[i]);
    paint(floatBtn_, "maximize", isFloating());
  }

  bool ChatDock::dragPollActive() const { return dragPoll_ && dragPoll_->isActive(); }

  bool ChatDock::dragActive() const { return manualDragging_ || dragPollActive(); }

  void ChatDock::setDragProbesForTest(std::function<QPoint()> cursorPos,
                                      std::function<bool()> leftButtonDown) {
    dragPosProbe_ = std::move(cursorPos);
    dragDownProbe_ = std::move(leftButtonDown);
  }

  void ChatDock::setNativeDockingSuppressed(bool on) {
    // With no allowed areas Qt can never show its drop placeholder or
    // hover-dock natively mid-drag — the zone overlay is the ONLY mechanism.
    // (addDockWidget on release happens AFTER the restore.)
    setAllowedAreas(on ? Qt::NoDockWidgetArea : Qt::AllDockWidgetAreas);
  }

  void ChatDock::startDragPoll() {
    if (!dragPoll_) {
      dragPoll_ = new QTimer(this);
      dragPoll_->setInterval(16);
      connect(dragPoll_, &QTimer::timeout, this, &ChatDock::pollDrag);
    }
    if (dragPoll_->isActive()) return;
    dragStartCursor_ = dragPosProbe_ ? dragPosProbe_() : QCursor::pos();
    dragMoved_ = false;
    dragActive_ = false;
    setNativeDockingSuppressed(true);
    dragPoll_->start();
  }

  void ChatDock::pollDrag() {
    const QPoint pos = dragPosProbe_ ? dragPosProbe_() : QCursor::pos();
    const bool down = dragDownProbe_
                          ? dragDownProbe_()
                          : QGuiApplication::mouseButtons().testFlag(Qt::LeftButton);
    if (!dragMoved_ && (pos - dragStartCursor_).manhattanLength() >= 4)
      dragMoved_ = true;
    if (down) {
      // Past the platform drag threshold the dock is OURS: force (and keep)
      // it floating, so a tear-off from a docked side flows straight into the
      // zone flow and a native mid-drag dock can never stick (item 27).
      if (dragMoved_ && !isFloating() &&
          (pos - dragStartCursor_).manhattanLength() >=
              QApplication::startDragDistance())
        setFloating(true);
      // Zones only for a genuine FLOATING drag — a plain click never shows
      // them (and can therefore never dock on release).
      if (dragMoved_ && isFloating()) {
        if (!dragActive_) {
          dragActive_ = true;
          emit titleDragStarted();
        }
        emit titleDragMoved(pos);
      }
      return;
    }
    // Button released: restore native docking FIRST, then the LAST cursor
    // position decides the drop (zone docking or stay floating).
    dragPoll_->stop();
    setNativeDockingSuppressed(false);
    const bool wasActive = dragActive_;
    dragActive_ = false;
    if (wasActive) emit titleDragFinished(pos);
  }

  void ChatDock::cancelDragPoll() {
    if (dragPoll_) dragPoll_->stop();
    setNativeDockingSuppressed(false);  // never leave the dock undockable
    if (dragActive_) {
      dragActive_ = false;
      emit titleDragCanceled();
    }
  }

  void ChatDock::moveEvent(QMoveEvent* event) {
    QDockWidget::moveEvent(event);
    // A floating dock being dragged moves continuously — start the poll even
    // when the native drag consumed the title-bar press.
    const bool down = dragDownProbe_
                          ? dragDownProbe_()
                          : QGuiApplication::mouseButtons().testFlag(Qt::LeftButton);
    // Not while WE drive the drag (the event path owns it end to end).
    if (isFloating() && down && !manualDrag_) startDragPoll();
  }

  void ChatDock::closeEvent(QCloseEvent* event) {
    // Hand the close to the owner (it animates, then hides us). With nobody
    // listening — or once the window is going away — fall back to Qt's own close,
    // so the dock can never become unclosable.
    if (receivers(SIGNAL(closeRequested())) > 0 && isVisible() && window() &&
        window()->isVisible()) {
      event->ignore();
      emit closeRequested();
      return;
    }
    QDockWidget::closeEvent(event);
  }

  void ChatDock::hideEvent(QHideEvent* event) {
    QDockWidget::hideEvent(event);
    // The float/dock transition re-parents the dock (a transient hide+show) —
    // cancel the drag poll only for a REAL hide (still hidden a tick later),
    // so a tear-off mid-drag keeps its poll; a closed dock never leaks it.
    QTimer::singleShot(0, this, [this] {
      if (!isVisible()) cancelDragPoll();
    });
  }

  // Can this payload become an attachment? Raw image data (a drag out of a browser)
  // or a local image/video file. Shared by the composer's drag-enter and its drop.
  bool ChatDock::canAttachMime(const QMimeData* mime) const {
    if (!mime) return false;
    if (mime->hasImage()) return true;
    if (mime->hasUrls()) {
      for (const QUrl& u : mime->urls()) {
        if (!u.isLocalFile()) continue;
        const QString path = u.toLocalFile();
        if (isImageFileName(path) || isVideoFileName(path)) return true;
      }
    }
    return false;
  }

  // The dock swallows what the composer didn't take: accepted so the drag can land,
  // then deliberately ignored. Without this the drop reached the main window and
  // offered to open the image as a project — from a gesture aimed at the chat.
  void ChatDock::dragEnterEvent(QDragEnterEvent* event) {
    if (canAttachMime(event->mimeData())) event->acceptProposedAction();
  }

  void ChatDock::dropEvent(QDropEvent* event) {
    // Accepted, not acted on: attaching is the composer's job (eventFilter), and the
    // transcript is for reading. Accepting stops it falling through to the window.
    if (canAttachMime(event->mimeData())) event->acceptProposedAction();
  }

  // The cue over the composer while a drag hovers it (browser .chat-drop-cue): an
  // icon that bobs beside the label. Only the composer takes a drop, so only the
  // composer lights up — a drop over the transcript belongs to the window behind it.
  // ── Jump pills (browser chatPanel syncJumps parity): ⌃ while the view is off the
  // beginning, ⌄ while it is off the latest message, both mid-log, neither fits. ──
  // The pills' own global box, whichever are visible right now — a null QRect while
  // neither is. Recomputed from the live geometry (never a cached flag), so a row's
  // "…" always reads where the pills ACTUALLY are, this instant.
  QRect ChatDock::jumpPillsGlobalRect() const {
    if (!jumpBottom_) return QRect();
    const auto globalOf = [](QWidget* w) {
      return QRect(w->parentWidget() ? w->parentWidget()->mapToGlobal(w->pos())
                                     : w->mapToGlobal(QPoint(0, 0)),
                   w->size());
    };
    QRect pills;
    if (jumpBottom_->isVisible()) pills = globalOf(jumpBottom_);
    if (jumpTop_ && jumpTop_->isVisible())
      pills = pills.isNull() ? globalOf(jumpTop_) : pills.united(globalOf(jumpTop_));
    return pills;
  }

  // The pills just moved, appeared or vanished — any row "…" already on screen must
  // reconsider whether it still clears them (shift further, settle back, or hide).
  // placeChatCardMore is idempotent, so re-running it on a row that needed no change
  // is a no-op.
  void ChatDock::revalidateMoreButtons() {
    if (!transcript_ || !scroll_) return;
    const QRect avoid = jumpPillsGlobalRect();
    // Direct children: placeChatCardMore parents every "…" to the transcript itself.
    for (QToolButton* more : transcript_->findChildren<QToolButton*>(
             QStringLiteral("chatCardMore"), Qt::FindDirectChildrenOnly)) {
      if (!more->isVisible()) continue;
      if (auto* card = qobject_cast<QFrame*>(more->property("chatMoreCard").value<QObject*>()))
        placeChatCardMore(card, more, scroll_, avoid);
    }
  }

  void ChatDock::updateJumpButtons() {
    if (!jumpTop_ || !jumpBottom_ || !scroll_) return;
    const auto* bar = scroll_->verticalScrollBar();
    const bool up = bar->value() > 12;
    const bool down = bar->maximum() - bar->value() > 12;
    if (up || down) positionJumpButtons();
    // The pills answer to the scroll position alone now — a row's "…" is what gets
    // out of THEIR way (revalidateMoreButtons), never the reverse (an earlier rule
    // hid the pills instead, inverted per user report: the arrows are the
    // higher-priority control and stay put).
    jumpTop_->setVisible(up);
    jumpBottom_->setVisible(down);
    revalidateMoreButtons();
  }

  // Grace-hide for a card's "⋯": it sits across a small gap from the bubble, so
  // a Leave waits ~220ms for the cursor to land on it (or back on the card).
  void ChatDock::positionJumpButtons() {
    if (!jumpTop_ || !jumpBottom_ || !scroll_) return;
    // Bottom-right of the VIEWPORT (left of any scrollbar), riding the lower edge.
    const QRect vp = scroll_->viewport()->geometry();
    const int y = vp.bottom() - jumpBottom_->height() - 8;
    const int x = vp.right() - jumpBottom_->width() - 10;
    jumpBottom_->move(x, y);
    jumpTop_->move(x - jumpTop_->width() - 6, y);
    jumpBottom_->raise();
    jumpTop_->raise();
  }

  void ChatDock::showDropCue(bool on) {
    if (!dropCue_) return;
    if (on) {
      // Over the INPUT only. Covering the whole composer swallowed the attachment
      // chips, the busy bar and the send/… buttons under one slab, which is not what
      // the browser does — there the cue sits on the box you type in.
      QRect r = inputArea_->rect();
      if (input_ && input_->isVisible()) {
        const QPoint tl = input_->mapTo(inputArea_, QPoint(0, 0));
        r = QRect(tl, input_->size());
      }
      dropCue_->setGeometry(r);
      dropCue_->raise();
      dropCue_->show();
      if (dropCueAnim_ && dropCueAnim_->state() != QAbstractAnimation::Running)
        dropCueAnim_->start();
    } else {
      dropCue_->hide();
      if (dropCueAnim_) dropCueAnim_->stop();
    }
  }

  bool ChatDock::attachFromMimeData(const QMimeData* mime) {
    if (!mime) return false;
    bool any = false;
    bool overCap = false;
    if (mime->hasUrls()) {
      for (const QUrl& u : mime->urls()) {
        if (!u.isLocalFile()) continue;
        const QString path = u.toLocalFile();
        if (isImageFileName(path)) {
          if (images_.size() >= kMaxAttachments) { overCap = true; continue; }   // §7: three per message
          const QImage img = readImageFile(path);
          if (!img.isNull()) {
            images_.append(img);
            imageNames_.append(QFileInfo(path).fileName());
            any = true;
          }
        } else if (isVideoFileName(path)) {
          // Same routing as the attach-video button: MainWindow extracts a
          // preview frame / offers the server upload.
          videoPath_ = path;
          any = true;
          emit videoAttached(path);
        }
      }
    }
    if (!any && mime->hasImage() && images_.size() >= kMaxAttachments) overCap = true;
    if (!any && mime->hasImage() && images_.size() < kMaxAttachments) {
      const QImage img = qvariant_cast<QImage>(mime->imageData());
      if (!img.isNull()) {
        images_.append(img);
        // A web-drag delivers the bitmap plus its source URL: name the chip from
        // the URL when its last segment is a real filename ("cat.jpg") — an
        // endpoint segment ("…/images?q=…") stays unnamed and the chip shows the
        // dimensions instead (browser fileNameForUrl parity).
        QString name;
        for (const QUrl& u : mime->urls()) {
          if (u.isLocalFile()) continue;
          const QString f = u.fileName();
          if (f.contains(QLatin1Char('.')) && f.size() <= 80) { name = f; break; }
        }
        imageNames_.append(name);
        any = true;
      }
    }
    if (any) refreshAttachmentTray();
    if (overCap) warnAttachmentCap();
    return any;
  }

  void ChatDock::submit() {
    if (isBusy()) return;  // single turn at a time; Enter is a no-op while busy
    const QString text = input_->toPlainText().trimmed();
    if (text.isEmpty()) return;
    input_->clear();
    emit sendRequested(text);
  }

  void ChatDock::onSendClicked() {
    if (isBusy()) {
      emit stopRequested();  // STOP mode
      return;
    }
    submit();
  }

  void ChatDock::pickMedia() {
    // One dialog for both media kinds (browser single-attach parity); each
    // pick routes by suffix — the same sniffers the paste/drop paths use.
    bool overCap = false;
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, "Attach images or videos", QString(),
        "Images & videos (*.png *.jpg *.jpeg *.webp *.gif *.bmp "
        "*.mp4 *.m4v *.mov *.webm *.mkv *.avi *.mpg *.mpeg *.ogv);;"
        "Images (*.png *.jpg *.jpeg *.webp *.gif *.bmp);;"
        "Videos (*.mp4 *.m4v *.mov *.webm *.mkv *.avi *.mpg *.mpeg *.ogv)");
    for (const QString& p : paths) {
      if (isVideoFileName(p)) {
        videoPath_ = p;
        emit videoAttached(p);
      } else {
        if (images_.size() >= kMaxAttachments) { overCap = true; continue; }   // §7: three per message
        const QImage img = readImageFile(p);
        if (!img.isNull()) {
          images_.append(img);
          imageNames_.append(QFileInfo(p).fileName());
        }
      }
    }
    refreshAttachmentTray();
    if (overCap) warnAttachmentCap();
  }

  // Browser parity: a queue past the §7 cap is SAID, not silently swallowed — as an
  // accent toast on the owner's stack, not a transcript card. Once per batch: a drop
  // of five pictures arrives one image at a time and used to post the line four times.
  void ChatDock::warnAttachmentCap() {
    if (capToastAt_.isValid() && capToastAt_.elapsed() < 1500) return;
    capToastAt_.start();
    emit toastRequested(
        QStringLiteral("Up to %1 images per message — the extra ones were not attached.")
            .arg(kMaxAttachments));
  }

  void ChatDock::addAttachmentImage(const QImage& img, const QString& name) {
    if (img.isNull()) return;
    if (images_.size() >= kMaxAttachments) { warnAttachmentCap(); return; }
    images_.append(img);
    // Kept in lockstep with images_ so a chip can say WHICH picture it holds. Empty
    // where there is nothing to say (a clipboard bitmap has no filename) — the chip
    // falls back to the dimensions there, as it always did.
    imageNames_.append(name);
    refreshAttachmentTray();
  }

  void ChatDock::clearAttachments() {
    images_.clear();
    imageNames_.clear();
    videoPath_.clear();
    refreshAttachmentTray();
  }

  void ChatDock::refreshAttachmentTray() {
    auto* row = qobject_cast<QHBoxLayout*>(attachTray_->layout());
    if (!row) return;
    // Drop the previous chips (the trailing stretch is re-added last).
    while (QLayoutItem* item = row->takeAt(0)) {
      if (QWidget* w = item->widget()) w->deleteLater();
      delete item;
    }
    // One chip per queued attachment: thumbnail + label + remove ×. `index` is
    // captured by value, and every removal rebuilds the whole row, so the
    // handlers can never act on a stale position.
    const auto addChip = [this, row](const QPixmap& thumb, const QString& label,
                                     const QString& tip, std::function<void()> remove,
                                     const QImage& full = QImage()) {
      auto* chip = new QFrame(attachTray_);
      chip->setObjectName("chatAttachChip");
      auto* lay = new QHBoxLayout(chip);
      lay->setContentsMargins(4, 2, 4, 2);
      lay->setSpacing(6);
      if (!thumb.isNull()) {
        auto* pic = new QLabel(chip);
        pic->setPixmap(thumb);
        pic->setFixedSize(thumb.size());
        // 28px can't tell two screenshots apart — hovering shows the full picture.
        if (!full.isNull()) new HoverPreview(pic, full, tip);
        lay->addWidget(pic);
      }
      auto* text = makePlainLabel(label, chip);   // a dropped filename is untrusted
      // A long filename must not widen the DOCK (the tray fed minimumSizeHint, which
      // both auto-grew the panel and blocked shrinking it) — elide, tooltip has it all.
      const QFontMetrics chipFm(text->font());
      text->setText(chipFm.elidedText(label, Qt::ElideMiddle, kChipNameMaxPx));
      // The full name/size live here, on the LABEL. The thumbnail deliberately carries
      // no tooltip: it opens the hover preview, and a Qt tooltip on top of that put two
      // popups on screen at once, the tooltip covering the picture it described.
      text->setToolTip(tip);
      lay->addWidget(text);
      auto* rm = new QToolButton(chip);
      rm->setObjectName("chatAttachRemove");
      rm->setText(QStringLiteral("×"));
      rm->setAccessibleName(QStringLiteral("Remove attachment"));  // no tooltip — the × says it
      rm->setCursor(Qt::PointingHandCursor);
      // The chip scatters AND fades, and the tray only rebuilds once it has gone —
      // rebuilding immediately snapped the composer to its new height while the
      // particles were still in the air, so the input jumped under the cursor.
      // The neighbours' slide is HELD back briefly and eased in: fadeOutAndDelete's
      // hide() released the layout slot the instant the fade ended, so the next chip
      // snapped over while the dust was still flying (user feedback: too fast).
      connect(rm, &QToolButton::clicked, this, [this, remove, chip] {
        if (chip->property("chatChipLeaving").toBool()) return;   // one click is enough
        chip->setProperty("chatChipLeaving", true);
        DisintegrateOverlay::over(chip, window(), DisintegrateOverlay::Sweep::Fall,
                                  kChatScatterCols, kChatScatterRows);
        // Fade the chip itself out (the scatter replaces it visually) WITHOUT hiding
        // or deleting it — an invisible chip still holds its slot for the hold+squeeze.
        for (QVariantAnimation* a : chip->findChildren<QVariantAnimation*>()) a->stop();
        auto* fx = qobject_cast<QGraphicsOpacityEffect*>(chip->graphicsEffect());
        if (!fx) { fx = new QGraphicsOpacityEffect(chip); chip->setGraphicsEffect(fx); }
        auto* fade = new QVariantAnimation(chip);
        fade->setDuration(kChatLeaveMs);
        fade->setStartValue(fx->opacity());
        fade->setEndValue(0.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);
        QPointer<QGraphicsOpacityEffect> fxp(fx);
        connect(fade, &QVariantAnimation::valueChanged, chip,
                [fxp](const QVariant& v) { if (fxp) fxp->setOpacity(v.toDouble()); });
        fade->start(QAbstractAnimation::DeleteWhenStopped);
        // Hold the slot while the scatter reads, then collapse the width gently so the
        // surviving chips glide over (browser .chat-attach-chip.leaving parity).
        QTimer::singleShot(kChatChipHoldMs, chip, [this, chip, remove] {
          auto* squeeze = new QVariantAnimation(chip);
          squeeze->setDuration(kChatLeaveMs);
          squeeze->setStartValue(chip->width());
          squeeze->setEndValue(0);
          squeeze->setEasingCurve(QEasingCurve::InOutCubic);
          connect(squeeze, &QVariantAnimation::valueChanged, chip,
                  [chip](const QVariant& v) { chip->setMaximumWidth(v.toInt()); });
          squeeze->start(QAbstractAnimation::DeleteWhenStopped);
          QTimer::singleShot(kChatLeaveMs, this, [chip, remove] {
            chip->deleteLater();
            remove();
          });
        });
      });
      lay->addWidget(rm);
      row->addWidget(chip);
    };

    for (int i = 0; i < images_.size(); ++i) {
      const QImage& img = images_.at(i);
      const QPixmap thumb = QPixmap::fromImage(
          img.scaled(QSize(28, 28), Qt::KeepAspectRatio, Qt::SmoothTransformation));
      const QString name = i < imageNames_.size() ? imageNames_.at(i) : QString();
      const QString dims = QStringLiteral("%1×%2").arg(img.width()).arg(img.height());
      addChip(thumb, name.isEmpty() ? dims : name,
              name.isEmpty() ? QStringLiteral("Queued image (%1)").arg(dims)
                             : QStringLiteral("%1 (%2)").arg(name, dims),
              [this, i] {
                if (i < images_.size()) {
                  images_.removeAt(i);
                  if (i < imageNames_.size()) imageNames_.removeAt(i);
                }
                refreshAttachmentTray();
              },
              img);
    }
    if (!videoPath_.isEmpty()) {
      addChip(QPixmap(), QFileInfo(videoPath_).fileName(),
              QStringLiteral("Queued video — frames are sent, never the video (%1)").arg(videoPath_),
              [this] {
                videoPath_.clear();
                refreshAttachmentTray();
                emit videoDetached();
              });
    }
    row->addStretch(1);
    attachTray_->setVisible(!images_.isEmpty() || !videoPath_.isEmpty());
  }

  // Create a framed transcript card above the bottom stretch and hand back its
  // layout; the deferred scrollToBottom measures it after the caller populates
  // it (the scroll runs on the next event-loop turn).
  QVBoxLayout* ChatDock::appendTranscriptCard(int spacing) {
    suggest_->hide();  // suggestions are an empty-state affordance only
    auto* card = new QFrame(transcript_);
    card->setFrameShape(QFrame::StyledPanel);
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(spacing);
    transcriptLayout_->insertWidget(transcriptLayout_->count() - 1, card);
    animateCardIn(card, lay);
    // Follow only while pinned to the end — a reply landing while the user
    // reads history must not yank them back down. Sends re-pin (appendUser /
    // showPending call scrollToBottom directly).
    if (stickToBottom_) scrollToBottom();
    return lay;
  }

  // The slide is done in the card's OWN contents margins (top +off, bottom
  // -off) rather than pos(): the transcript's layout owns the card's geometry
  // and would fight a moved widget, while the margin pair keeps the card's
  // total height constant for the deferred scrollToBottom. The effect and the
  // animation are children of the card and the valueChanged connection uses
  // the card as its context, so a card deleted mid-flight severs everything;
  // DeleteWhenStopped reaps a normal finish.
  void ChatDock::animateCardIn(QWidget* card, QVBoxLayout* lay) {
    // Claim the card's opacity while the entrance plays: ScrollReveal drives the same
    // effect, and two writers on one effect flicker. Claimed and zeroed NOW even when
    // the entrance itself starts a turn later, so the card never flashes at full
    // strength in between.
    card->setProperty(ScrollReveal::kEnteringProperty, true);
    auto* fx = new QGraphicsOpacityEffect(card);
    fx->setOpacity(0.0);
    card->setGraphicsEffect(fx);  // the widget takes ownership of the effect
    // Reduced motion keeps the old immediate path: nothing to photograph, nothing to
    // wait for. Everything else defers — appendTranscriptCard hands the caller an EMPTY
    // card, and the dust has to be a picture of the FINISHED bubble, laid out at its real
    // width and already scrolled to. The caller's own scrollToBottom() is a singleShot(0)
    // queued AFTER this one, so a 0ms hop here would still measure the pre-scroll box;
    // one frame lets that scroll land first.
    if (support::motionReduced()) { startCardEntrance(card, lay); return; }
    QPointer<QWidget> cp(card);
    QTimer::singleShot(kChatGatherSettleMs, card, [this, cp, lay] {
      if (cp) startCardEntrance(cp, lay);
    });
  }

  void ChatDock::startCardEntrance(QWidget* card, QVBoxLayout* lay) {
    const QMargins rest = lay->contentsMargins();
    // The resting state — every bail-out in the shared machinery takes it, so a card
    // can never be stranded invisible behind a flight that did not happen.
    const auto settle = [this, card, lay, rest] {
      if (auto* e = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect())) e->setOpacity(1.0);
      lay->setContentsMargins(rest);
      card->setProperty(ScrollReveal::kEnteringProperty, false);
      // Laid out FIRST: setContentsMargins only QUEUES the move, and ScrollReveal::apply()
      // measures mapTo(viewport)/height() to decide a card's edge dissolve — run against
      // the old geometry it left the fresh bubble faint until the next scroll (user report).
      lay->activate();
      if (reveal_) reveal_->apply();   // hand the card over to the scroll curve
      repositionChatBubbleTails(transcript_);
    };
    // The slide runs on its own short clock only once the dust actually flies — the
    // gap the card opens in the transcript is layout, not flourish, and the finished
    // bubble must never be drawn under the animation (the card waits fully hidden).
    const auto slide = [this, card, lay, rest] {
      auto* anim = new QVariantAnimation(card);
      anim->setDuration(kAppearMs);
      anim->setStartValue(0.0);
      anim->setEndValue(1.0);
      anim->setEasingCurve(QEasingCurve::OutCubic);
      connect(anim, &QVariantAnimation::valueChanged, card, [lay, rest](const QVariant& v) {
        const int off = qRound(kAppearSlidePx * (1.0 - v.toDouble()));
        lay->setContentsMargins(rest.left(), rest.top() + off, rest.right(),
                                qMax(0, rest.bottom() - off));
      });
      anim->start(QAbstractAnimation::DeleteWhenStopped);
    };
    gatherChatCardIn(card, transcriptLayout_, scroll_, window(), kChatScatterCols,
                     kChatScatterRows, settle, slide);
  }

  // The shared row menu (browser chatView.js chatRowMenuItems): Copy message and
  // Insert into prompt on EVERY settled row — error and stopped cards included,
  // which is the whole point of doing it here — plus Resend on the rows whose
  // surface offers it. ("Select all" was dropped from both surfaces: dragging
  // selects what you actually want, and Copy message already takes the lot.)
  // Popping a menu is a NESTED EVENT LOOP: the transcript can repaint, a turn can
  // land, the chat can finish closing — any of which may delete this card or take
  // its window away. Every widget is therefore held by QPointer and re-checked
  // after exec(), and the menu refuses to pop at all without a live window to
  // pop into (a QMenu shown against a destroyed/absent window crashes inside
  // QCocoaWindow::setVisible — the reported SIGSEGV).
  static void showChatCardMenu(QPointer<QFrame> card, const QPoint& globalPos,
                               const ChatCardMenuHooks& hooks) {
    if (!card) return;
    // NB: the card's own isVisible() is deliberately NOT a gate — a row appended
    // in this very event cycle is not "visible" yet, and the crash never came
    // from the card anyway. What matters is the WINDOW the menu would pop into.
    QPointer<QWidget> owner(hooks.owner);
    // The window the menu will live in must EXIST, be visible and be mapped
    // (windowHandle() is null before that, and for a surface being torn down).
    QWidget* top = card->window();
    if (!top || !top->isVisible() || !top->windowHandle()) return;
    if (hooks.leaving && hooks.leaving()) return;   // the surface is on its way out
    // The card's message: the bubble body plus any notes that ride in it,
    // role-stripped (the properties carry the plain text, never the markup).
    QStringList parts;
    QPointer<QLabel> body;
    for (QLabel* l : card->findChildren<QLabel*>()) {
      const QString b = l->property("chatBody").toString();
      const QString n = l->property("chatNote").toString();
      if (!b.isEmpty() && !body) body = l;
      if (!b.isEmpty()) parts << b;
      else if (!n.isEmpty()) parts << n;
    }
    const QString text = parts.join(QLatin1Char('\n'));
    if (text.isEmpty()) return;

    // Parented to the card's OWN top level, never to a hooks owner that may be a
    // widget inside a popup that is already going away.
    QMenu menu(top);
    // Same glyphs as the browser's chat row menu (copy / pen / send), one flat
    // list — no separators, so nothing dangles now that "Select all" is gone.
    QAction* copy =
        menu.addAction(themedIcon("copy", hooks.text, 16), QStringLiteral("Copy message"));
    QAction* insert =
        menu.addAction(themedIcon("pencil", hooks.text, 16), QStringLiteral("Insert into prompt"));
    QAction* resend = nullptr;
    if (hooks.resend && card->objectName() == QLatin1String("chatCardUser")) {
      // Resend = the same turn again, original attachments included.
      resend = menu.addAction(themedIcon("send", hooks.text, 16), QStringLiteral("Resend"));
      resend->setEnabled(!(hooks.busy && hooks.busy()));
    }
    support::MenuShimmer shimmer(&menu);   // …the same row sweep the composer's menu plays
    compactIconMenu(menu);   // …and it hugs its longest label, like the composer's "…"
    card->setProperty("chatMenuOpen", true);   // holds its "⋯" visible meanwhile
    support::revealMenu(menu, globalPos);  // grow-from-the-cursor pop
    QAction* picked = menu.exec(globalPos);
    // ── everything below runs AFTER the nested loop: re-check every pointer ──
    if (card) {
      card->setProperty("chatMenuOpen", false);
      // Menu closed: drop the hover button unless the cursor is still on the card.
      if (auto* more = qobject_cast<QToolButton*>(
              card->property("chatMoreBtn").value<QObject*>()))
        more->setVisible(card->underMouse() || more->underMouse());
    }
    if (!picked) return;
    if (picked == copy) {
      QGuiApplication::clipboard()->setText(text);   // the text was copied up front
    } else if (picked == insert) {
      // The composer belongs to the owner — gone means nothing to insert into.
      if (owner && hooks.insertIntoPrompt) hooks.insertIntoPrompt(text);
    } else if (resend && picked == resend && owner && card) {
      hooks.resend(card, body ? body->property("chatBody").toString() : text);
    }
  }

  void installChatCardMenu(QFrame* card, const ChatCardMenuHooks& hooks) {
    if (!card) return;
    // QPointer throughout: these lambdas outlive the card they were built for
    // (a turn settling mid-conversation deletes rows), and a right-click on a
    // stale one must do nothing rather than resurrect freed memory.
    const auto show = [hooks](QPointer<QFrame> c, const QPoint& at) {
      showChatCardMenu(c, at, hooks);
    };
    const QPointer<QFrame> cardRef(card);
    const auto wire = [cardRef, show](QWidget* w) {
      if (w->contextMenuPolicy() == Qt::CustomContextMenu) return;  // already wired
      w->setContextMenuPolicy(Qt::CustomContextMenu);
      QPointer<QWidget> wRef(w);
      QObject::connect(w, &QWidget::customContextMenuRequested, w,
                       [cardRef, wRef, show](const QPoint& pos) {
                         if (!cardRef || !wRef) return;
                         show(cardRef, wRef->mapToGlobal(pos));
                       });
    };
    wire(card);
    for (QLabel* l : card->findChildren<QLabel*>()) {
      wire(l);
      // A mouse selection must also FOCUS the label, or Ctrl+C lands in the
      // composer and copying a selection is impossible.
      if (l->textInteractionFlags() & Qt::TextSelectableByMouse)
        l->setFocusPolicy(Qt::ClickFocus);
    }
    // Hover affordance: a ghost "⋯" (hidden at rest, revealed by the card's
    // Enter) opening the SAME menu as a right-click. Created once — a card that
    // gains labels later (appendLateNote, a settling error card) re-wires them
    // above and keeps this one.
    if (card->property("chatMoreBtn").value<QObject*>()) return;
    auto* more = new QToolButton(card);
    more->setObjectName(QStringLiteral("chatCardMore"));
    // It lives on the scrolled content widget but is CHROME, not a row: the
    // transcript's edge-reveal must not dissolve it (and must not take its glow).
    more->setProperty(ScrollReveal::kExemptProperty, true);
    // The button lives OUTSIDE the card (placeChatCardMore reparents it), so the
    // link is a property pair, not parentage; the card's death takes it along.
    card->setProperty("chatMoreBtn", QVariant::fromValue<QObject*>(more));
    more->setProperty("chatMoreCard", QVariant::fromValue<QObject*>(card));
    // Sever the pair on either death FIRST — a stray event on the survivor
    // must never qobject_cast the dangling half (deleteLater lags a beat).
    QObject::connect(card, &QObject::destroyed, more, [more] {
      more->setProperty("chatMoreCard", QVariant());
      more->deleteLater();
    });
    QObject::connect(more, &QObject::destroyed, card,
                     [card] { card->setProperty("chatMoreBtn", QVariant()); });
    more->setAutoRaise(true);
    more->setFixedSize(21, 21);
    more->setIconSize(QSize(15, 15));
    // Browser .chat-row-menu-btn parity: an outlined circle on the container
    // tone, and NO hover fill — hover is the accent glow + 1px lift instead.
    // It rests at 0.7 like the browser's revealed trigger and the jump pills, and the
    // cursor brings it back to full. Not a QGraphicsOpacityEffect: a widget carries only
    // ONE graphics effect and the accent glow below already claims it, so the alpha is
    // baked into the chrome instead (and blended into the glyph, which QSS can't reach).
    const auto rgba = [](const QColor& c, double a) {
      return QStringLiteral("rgba(%1,%2,%3,%4)")
          .arg(c.red()).arg(c.green()).arg(c.blue()).arg(a);
    };
    more->setStyleSheet(QStringLiteral("QToolButton{padding:1px;background:%1;"
                                       "border:1px solid %2;border-radius:10px;}"
                                       "QToolButton:hover{background:%3;border-color:%4;}")
                            .arg(rgba(hooks.chip, kGhostRestOpacity),
                                 rgba(hooks.border, kGhostRestOpacity),
                                 hooks.chip.name(), hooks.border.name()));
    // Centred (no offset), so the glow rings the button like the browser's.
    auto* glow = new QGraphicsDropShadowEffect(more);
    glow->setOffset(0, 0);
    glow->setBlurRadius(12);
    glow->setColor(hooks.accent);
    glow->setEnabled(false);
    more->setGraphicsEffect(glow);
    more->setCursor(Qt::PointingHandCursor);
    more->setFocusPolicy(Qt::NoFocus);  // never steal a label's selection focus
    installHoverShimmer(more);          // the same sweep every chat button gets
    // The glyph takes the same 0.7, blended over the chip it sits on rather than made
    // translucent: the icon is rasterised, and themedIcon's cache is keyed on an
    // alpha-less colour name, so an alpha here would collide with the opaque request.
    more->setIcon(themedIcon("more", blendColors(hooks.muted, hooks.chip, kGhostRestOpacity), 16));
    more->hide();
    QObject::connect(more, &QToolButton::clicked, more, [cardRef, more, show] {
      if (!cardRef) return;
      show(cardRef, more->mapToGlobal(QPoint(0, more->height())));
    });
    new ChatCardMore(card, more, hooks.scroll, hooks.moreMoved, hooks.avoidRect);  // hover + placement
  }

  QWidget* makeChatTypingDots(QWidget* parent) {
    auto* dots = new TypingDots(parent);
    dots->setObjectName(QStringLiteral("chatTypingDots"));
    return dots;
  }

  QLabel* addChatCardNote(QVBoxLayout* lay, const QString& text) {
    if (!lay) return nullptr;
    auto* note = makePlainLabel(text, lay->parentWidget());
    note->setObjectName(QStringLiteral("chatNoteLabel"));   // muted via the QSS rule
    note->setWordWrap(true);
    note->setProperty("chatNote", text);
    applyMutedText(note);
    lay->addWidget(note);
    return note;
  }

  QToolButton* addChatRetryButton(QVBoxLayout* lay, const QColor& glyph,
                                  std::function<void()> onClick) {
    if (!lay) return nullptr;
    auto* retry = makeGhostButton(lay->parentWidget(),
                                  QStringLiteral("Send this message again"));
    retry->setObjectName("chatRetry");
    // Sized up from makeGhostButton's header-ghost default (kHeaderIcon/kButtonEdge):
    // a lone icon-only action at the foot of an error card — often the ONLY thing on
    // it (a plain failure has no Configure CTA beside it) — reads as an afterthought
    // at that size.
    static constexpr int kRetryIcon = 18;
    static constexpr int kRetryEdge = 30;
    retry->setIconSize(QSize(kRetryIcon, kRetryIcon));
    retry->setFixedSize(kRetryEdge, kRetryEdge);
    // Neutral glyph on EVERY card, error ones included: the browser's retry is a
    // .chat-hbtn, which sets `color: var(--text-muted)` of its own and never
    // inherits the bubble's --danger. Painted red it sat red-on-red in the error
    // card's danger wash and barely read; the red belongs to the card's ground
    // and border, not to the control offering the way out.
    retry->setIcon(labelIcon("refresh", glyph, kRetryIcon));
    QObject::connect(retry, &QToolButton::clicked, retry,
                     [onClick] { if (onClick) onClick(); });
    lay->addWidget(retry, 0, Qt::AlignLeft);
    return retry;
  }

  // The unreachable-card "Configure provider" CTA (browser chatConfigureButton),
  // shared by the dock and the menu panel: accent-filled, white gear glyph (haloed
  // on a light accent — the glyph inherits the canonical gear hover motion for
  // free). `onClick` gets the button, still on screen, as the settings reveal's
  // anchor. Accent look via the accentCta property (theme.cpp); the objectName
  // stays free for the GUI tests.
  QPushButton* addChatConfigureCta(QVBoxLayout* lay, const QColor& accent,
                                   std::function<void(QPushButton*)> onClick) {
    if (!lay) return nullptr;
    auto* cta = new QPushButton(QObject::tr("Configure provider"), lay->parentWidget());
    cta->setObjectName(QStringLiteral("chatConfigureCta"));
    cta->setProperty("accentCta", true);
    cta->setCursor(Qt::PointingHandCursor);
    cta->setIcon(labelIcon("gear", Qt::white, 14, accentNeedsGlyphShadow(accent)));
    cta->setIconSize(QSize(14, 14));
    QObject::connect(cta, &QPushButton::clicked, cta,
                     [cta, onClick] { if (onClick) onClick(cta); });
    lay->addWidget(cta, 0, Qt::AlignLeft);
    return cta;
  }

  // The dock's own hooks for the shared menu above: its composer, its resend
  // (attachments requeued), its transcript viewport.
  ChatCardMenuHooks ChatDock::cardMenuHooks() {
    ChatCardMenuHooks h;
    h.owner = this;
    h.scroll = scroll_;
    h.busy = [this] { return isBusy(); };
    h.insertIntoPrompt = [this](const QString& text) {
      const QString existing = input_->toPlainText();
      input_->setPlainText(existing.isEmpty() ? text : existing + QLatin1Char('\n') + text);
      input_->moveCursor(QTextCursor::End);
      QTimer::singleShot(0, input_, [this] { focusInput(); });  // after the menu's focus restore
    };
    h.resend = [this](QFrame* card, const QString& text) {
      // Requeue the turn's own images as fresh tray attachments (the browser's
      // requeueLastTurnAttachments), then send the same text through the normal
      // path — one bubble, one wire payload, tray drained by the send.
      for (const QVariant& v : card->property("chatImages").toList())
        addAttachmentImage(v.value<QImage>());
      emit sendRequested(text);
    };
    // No h.moreMoved: the relationship inverted (the pills win, the trigger gets out
    // of THEIR way — jumpPillsGlobalRect/revalidateMoreButtons), so a moved trigger no
    // longer has anything to tell the pills. Wiring it back to updateJumpButtons would
    // also be reentrant: it now moves triggers itself, which would fire this same hook.
    h.avoidRect = [this] { return jumpPillsGlobalRect(); };
    h.leaving = [this] { return closing_; };
    h.text = textCache_.isValid() ? textCache_ : palette().color(QPalette::Text);
    h.chip = chipCache_.isValid() ? chipCache_ : palette().color(QPalette::AlternateBase);
    h.border = borderCache_.isValid() ? borderCache_ : palette().color(QPalette::Mid);
    h.accent = accentCache_.isValid() ? accentCache_ : palette().highlight().color();
    h.muted = mutedCache_.isValid() ? mutedCache_ : palette().color(QPalette::PlaceholderText);
    return h;
  }

  void ChatDock::installCardMenu(QFrame* card) { installChatCardMenu(card, cardMenuHooks()); }


  // The share of the viewport a bubble may take. Browser/extension parity
  // (@container chat-transcript (max-width: 300px)): below that the split has no
  // room to read as a SIDE any more, so bubbles widen toward the full column
  // instead of squeezing their text — the alignment itself is unchanged (unlike
  // the CSS surfaces, dropping it here would mean re-running the layout's
  // alignment on every resize, and the L/R split still reads fine at this width,
  // it is only the TEXT that was cramped).
  double chatBubbleCapFraction(int avail) { return avail > 0 && avail < 300 ? 0.96 : 0.88; }

  // Bubbles stop short of the full width so the side they sit on is legible;
  // re-applied whenever the viewport resizes. Shared with the context menu's
  // assistant panel: a wrapped label CLIPS ITSELF without this pass, so both
  // transcripts run it.
  void applyChatBubbleWidths(QWidget* transcript, QScrollArea* scroll) {
    // The widest a wrapped label inside `card` may be: the bubble cap minus the card's
    // own padding. Used both to cap the label and, when it has not been laid out yet,
    // to measure the height its text will need.
    const auto lwFor = [scroll](QFrame* card) {
      const int avail = scroll && scroll->viewport() ? scroll->viewport()->width() : 0;
      const int cap = qMax(120, static_cast<int>(avail * chatBubbleCapFraction(avail)));
      const QMargins m = card && card->layout() ? card->layout()->contentsMargins() : QMargins();
      return qMax(80, cap - m.left() - m.right());
    };
    const int avail = scroll && scroll->viewport() ? scroll->viewport()->width() : 0;
    // A viewport with no width yet (a surface that has never been shown) cannot
    // measure anything — leave the cards alone and let the owner re-run this
    // once it has a real width, rather than pinning them to a bogus cap.
    if (avail <= 0 || !transcript) return;
    const int cap = qMax(120, static_cast<int>(avail * chatBubbleCapFraction(avail)));
    for (QFrame* card : transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)) {
      card->setMaximumWidth(cap);
      // Word-wrapped labels grow TALLER as the card narrows: without
      // heightForWidth the card keeps the height it computed at full width and
      // clips its own text (the layout must re-ask after every cap change).
      QSizePolicy sp = card->sizePolicy();
      sp.setHeightForWidth(true);
      sp.setVerticalPolicy(QSizePolicy::MinimumExpanding);
      card->setSizePolicy(sp);
      for (QLabel* l : card->findChildren<QLabel*>()) {
        if (!l->wordWrap()) continue;
        // Cap the LABEL too, not just the card: a wrapped label's sizeHint is its longest
        // unbreakable run (a URL), and that would push the card past the cap and widen
        // the whole transcript.
        const int lw = lwFor(card);
        l->setMaximumWidth(lw);
        // Browser shrink-to-fit parity: a wrapped bubble narrows below the cap only
        // when its text fits one line; otherwise it takes the FULL cap (Qt's wrapped
        // sizeHint favours a squarer, needlessly narrow shape instead).
        const int natural = l->fontMetrics().size(0, l->text()).width();
        // Pinned explicitly rather than left at minimumWidth 0: a wrapped QLabel's
        // sizeHint reads its CURRENT geometry, so a fresh label wrapped short lines.
        l->setMinimumWidth(qMin(natural, lw));
        QSizePolicy lp = l->sizePolicy();
        lp.setHeightForWidth(true);
        lp.setVerticalPolicy(QSizePolicy::MinimumExpanding);
        l->setSizePolicy(lp);
        l->setMinimumHeight(0);   // re-measured below, at the width it really gets
      }
      if (card->layout()) card->layout()->activate();
      card->updateGeometry();
      card->adjustSize();
      // …then RESERVE each wrapped label's height at the width it ACTUALLY got. The
      // cap is only an upper bound — a card sizes to its content and is usually
      // narrower, so text wrapped at the cap needs MORE room than reserved, and the
      // last line was cut off by the bubble's own edge. heightForWidth is a hint the
      // layout does not re-ask for once it has sized the card, so it is pinned here.
      bool regrew = false;
      for (QLabel* l : card->findChildren<QLabel*>()) {
        if (!l->wordWrap() || l->text().isEmpty()) continue;
        // Measure at the width the layout WILL give the label, computed from the
        // card's sizeHint (a pure query, valid before any layout pass): the card
        // takes min(hint, cap) and the label spans it minus the card padding.
        // l->width() lied here — a freshly appended card still carries its
        // default 100×30 child geometry, so the height was reserved for a much
        // narrower wrap and the bubble kept fat top/bottom padding around the
        // vertically centered text until the next viewport resize re-measured.
        const QMargins cm =
            card->layout() ? card->layout()->contentsMargins() : QMargins();
        const int w =
            qMax(80, qMin(card->sizeHint().width(), cap) - cm.left() - cm.right());
        const int wrapped = l->heightForWidth(w);
        if (wrapped > 0 && wrapped != l->minimumHeight()) {
          l->setMinimumHeight(wrapped);
          regrew = true;
        }
      }
      if (regrew) {
        if (card->layout()) card->layout()->activate();
        card->updateGeometry();
        card->adjustSize();
      }
    }
    if (transcript->layout()) transcript->layout()->activate();
    // Every card above may have just moved — its tail (if any) has to follow.
    repositionChatBubbleTails(transcript);
  }

  void ChatDock::applyBubbleWidths() { applyChatBubbleWidths(transcript_, scroll_); }

  void ChatDock::showPending() {
    clearPending();  // defensive: never two pending cards
    QVBoxLayout* lay = appendTranscriptCard(2);
    pendingCard_ = lay->parentWidget();
    pendingCard_->setObjectName(QStringLiteral("chatCardAssistant"));
    pendingRole_ = nullptr;  // the bubble's own tone says "assistant" (browser parity)
    // An in-flight turn shows bouncing dots; markPendingStopped swaps in the label.
    pendingBody_ = makePlainLabel(QString(), pendingCard_);
    pendingBody_->setWordWrap(true);
    pendingBody_->setProperty("chatRole", QStringLiteral("Assistant"));
    pendingBody_->setProperty("chatBody", QStringLiteral("…"));
    pendingBody_->hide();
    pendingDots_ = new TypingDots(pendingCard_);
    lay->addWidget(pendingDots_);
    lay->addWidget(pendingBody_);
    transcriptLayout_->setAlignment(pendingCard_, Qt::AlignLeft);
    applyBubbleWidths();
    // The "…" card is the send's tail end — bring it fully into view.
    scrollToBottom();
  }

  void ChatDock::clearPending() {
    if (pendingCard_) pendingCard_->deleteLater();
    pendingDots_ = nullptr;   // owned by the card
    pendingCard_ = nullptr;
    pendingRole_ = nullptr;
    pendingBody_ = nullptr;
  }

  void ChatDock::markPendingStopped(const QString& stoppedText) {
    if (!pendingCard_) return;
    // The "…" card becomes the stop notice in place, error-card styled.
    if (pendingDots_) { pendingDots_->deleteLater(); pendingDots_ = nullptr; }
    pendingBody_->show();
    pendingBody_->setText(QStringLiteral("Stopped."));
    pendingBody_->setProperty("chatBody", QStringLiteral("Stopped."));
    pendingCard_->setObjectName(QStringLiteral("chatCardError"));
    // A stylesheet is matched when the widget is POLISHED, so renaming it afterwards
    // changes nothing until the style is re-run — which is why the stopped card kept
    // the assistant bubble's frame and text while only its (programmatically tinted)
    // retry glyph turned red.
    repolish(pendingCard_);
    for (QLabel* l : pendingCard_->findChildren<QLabel*>()) repolish(l);
    // The browser renders a stopped turn with .chat-msg-error and the extension with
    // .msg.error — both in --danger. Muted text here was the odd one out: on this
    // surface alone a stop looked like an ordinary note.
    applyDangerText(pendingBody_, dangerCache_.isValid() ? dangerCache_ : QColor("#d6293e"));
    // Stopping is a change of mind, not a dead end: the card keeps the prompt.
    addRetryButton(qobject_cast<QVBoxLayout*>(pendingCard_->layout()), stoppedText);
    // …and now that it is a SETTLED row it gets the row menu, like every other
    // one (browser chatRowMenuItems excludes only pending rows). Built here
    // rather than in showPending so an in-flight "…" never offers one.
    installCardMenu(qobject_cast<QFrame*>(pendingCard_));
    pendingCard_ = nullptr;
    pendingRole_ = nullptr;
    pendingBody_ = nullptr;
  }

  void ChatDock::appendUser(const QString& text, const QList<QImage>& images) {
    QVBoxLayout* lay = appendCard("You", text, CardKind::Bubble);
    // Sending always lands the view at the very bottom, wherever it was.
    scrollToBottom();
    if (images.isEmpty()) return;
    // The attached images ARE part of what the user said, so they sit in the user's
    // own bubble as thumbnails — above the text, right-aligned with the bubble.
    // Previously only the count was appended as "[N image(s) attached]".
    QWidget* card = lay->parentWidget();
    auto* row = new QHBoxLayout;
    row->setSpacing(6);
    row->addStretch(1);
    for (const QImage& img : images) {
      if (img.isNull()) continue;
      auto* thumb = new QLabel(card);
      thumb->setPixmap(QPixmap::fromImage(
          img.scaled(kThumbEdge, kThumbEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
      // No tooltip: the bubble thumbnail is already big, and "Attached image (500×750)"
      // told you nothing the picture doesn't. The hover preview declines to open for a
      // thumbnail this size, so hovering here does nothing at all — which is right.
      new HoverPreview(thumb, img, QString());
      row->addWidget(thumb);
    }
    // The text label is already in the layout — put the strip above it.
    lay->insertLayout(0, row);
    // The turn's images ride the card so the context menu's Resend can requeue
    // them; the thumbs joined after appendCard, so wire the menu onto them too.
    QVariantList stored;
    for (const QImage& img : images) stored << QVariant::fromValue(img);
    card->setProperty("chatImages", stored);
    installCardMenu(qobject_cast<QFrame*>(card));
    applyBubbleWidths();
  }

  void ChatDock::appendAssistant(const QString& text, const QStringList& warnings,
                                 const QStringList& notes) {
    QString t = text;
    for (const QString& w : warnings) t += QStringLiteral("\n⚠ ") + w;
    QVBoxLayout* lay = appendCard("Assistant", t, CardKind::Bubble);
    lastAssistantCard_ = lay->parentWidget();   // late notes merge into THIS bubble
    // Executor notes ride WITH the reply — muted lines in the SAME bubble, never
    // a separate card (browser parity: they merge into the reply's warnings).
    for (const QString& n : notes) {
      auto* note = makePlainLabel(n, lay->parentWidget());
      note->setObjectName(QStringLiteral("chatNoteLabel"));  // muted via the QSS rule
      note->setWordWrap(true);
      note->setProperty("chatNote", n);
      applyMutedText(note);  // fallback tone when no stylesheet is active
      lay->addWidget(note);
    }
    if (!notes.isEmpty()) {
      installCardMenu(qobject_cast<QFrame*>(lay->parentWidget()));  // wire the fresh notes
      applyBubbleWidths();  // re-measure with the extra labels
    }
  }

  void ChatDock::appendAsk(const stencil::llm::AskCard& ask, const QVector<QImage>& previews) {
    if (ask.options.isEmpty()) return;
    QVBoxLayout* lay = appendTranscriptCard(6);
    QWidget* card = lay->parentWidget();
    lay->addWidget(makeRoleLabel(QStringLiteral("Assistant asks"), card));

    auto* question = makePlainLabel(ask.question, card);
    question->setWordWrap(true);
    lay->addWidget(question);

    // One group per card so a single-pick card's radios are exclusive to it — several cards
    // can sit in the transcript at once, and Qt would otherwise link every radio in the dock.
    auto* group = new QButtonGroup(card);
    group->setExclusive(!ask.multi);
    QVector<QAbstractButton*> buttons;
    for (int i = 0; i < ask.options.size(); ++i) {
      auto* row = new QWidget(card);
      auto* rowLay = new QHBoxLayout(row);
      rowLay->setContentsMargins(0, 0, 0, 0);
      rowLay->setSpacing(6);
      QAbstractButton* pick = ask.multi ? static_cast<QAbstractButton*>(new QCheckBox(row))
                                        : static_cast<QAbstractButton*>(new QRadioButton(row));
      group->addButton(pick, i);
      buttons.push_back(pick);
      rowLay->addWidget(pick, 0);
      if (i < previews.size() && !previews[i].isNull()) {
        auto* thumb = new QLabel(row);
        thumb->setPixmap(QPixmap::fromImage(
            previews[i].scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        rowLay->addWidget(thumb, 0);
      }
      auto* label = makePlainLabel(ask.options[i].label, row);
      label->setWordWrap(true);
      rowLay->addWidget(label, 1);
      lay->addWidget(row);
    }

    // The custom row: ticking it or typing in it means the same thing, so they stay in step.
    QAbstractButton* customPick = nullptr;
    QLineEdit* customText = nullptr;
    if (ask.allowCustom) {
      auto* row = new QWidget(card);
      auto* rowLay = new QHBoxLayout(row);
      rowLay->setContentsMargins(0, 0, 0, 0);
      rowLay->setSpacing(6);
      customPick = ask.multi ? static_cast<QAbstractButton*>(new QCheckBox(row))
                             : static_cast<QAbstractButton*>(new QRadioButton(row));
      group->addButton(customPick, ask.options.size());
      rowLay->addWidget(customPick, 0);
      customText = new QLineEdit(row);
      customText->setPlaceholderText(ask.customLabel);
      rowLay->addWidget(customText, 1);
      lay->addWidget(row);
    }

    auto* submit = new QPushButton(QStringLiteral("Submit"), card);
    // The affirmative action of the card, so it wears the app's accent CTA face
    // (theme.cpp QPushButton[accentCta="true"]) and the same hover sweep every other
    // button in the app carries — the browser's twin is a plain <button>, which gets both
    // for free from its shared rules (css/layout.css, .chat-ask-submit).
    submit->setProperty("accentCta", true);
    installHoverShimmer(submit);
    submit->setEnabled(false);
    lay->addWidget(submit, 0, Qt::AlignLeft);

    // Enabled only once something is chosen — a Submit that sends nothing is a dead control.
    auto sync = [submit, buttons, customPick, customText]() {
      bool any = false;
      for (QAbstractButton* b : buttons) any = any || b->isChecked();
      if (customPick && customPick->isChecked() && customText && !customText->text().trimmed().isEmpty()) any = true;
      submit->setEnabled(any);
    };
    for (QAbstractButton* b : buttons) connect(b, &QAbstractButton::toggled, card, sync);
    if (customPick) connect(customPick, &QAbstractButton::toggled, card, sync);
    if (customText) {
      connect(customText, &QLineEdit::textChanged, card, [customPick, sync]() {
        if (customPick) customPick->setChecked(true);
        sync();
      });
    }

    connect(submit, &QPushButton::clicked, card,
            [this, card, submit, buttons, customPick, customText, opts = ask.options]() {
              QStringList picked;
              for (int i = 0; i < buttons.size(); ++i) {
                if (buttons[i]->isChecked()) picked << opts[i].label;
              }
              const QString custom =
                  (customPick && customPick->isChecked() && customText) ? customText->text() : QString();
              const QString answer = stencil::llm::askAnswerText(picked, custom);
              if (answer.isEmpty()) return;
              // Lock it: the card becomes the record of what was sent, not a control.
              for (QAbstractButton* b : buttons) b->setEnabled(false);
              if (customPick) customPick->setEnabled(false);
              if (customText) customText->setEnabled(false);
              submit->setVisible(false);
              auto* sent = makePlainLabel(answer, card);   // may be the user's free text
              sent->setWordWrap(true);
              applyMutedText(sent);
              if (auto* lay = qobject_cast<QVBoxLayout*>(card->layout())) lay->addWidget(sent);
              emit sendRequested(answer);
            });
  }

  // A one-click Retry inside a card: re-sends exactly `retryText` through the owner's
  // normal send path (never auto-retried; the owner ignores it mid-turn). Icon-only —
  // a labelled button inside the bubble reads as part of the message.
  void ChatDock::appendError(const QString& text, const QString& retryText) {
    addRetryButton(appendCard("Error", text, CardKind::Error), retryText);
  }

  void ChatDock::addRetryButton(QVBoxLayout* lay, const QString& retryText) {
    if (!lay || retryText.isEmpty()) return;
    const QColor glyph = mutedCache_.isValid()
        ? mutedCache_
        : (textCache_.isValid() ? textCache_
                                : palette().color(QPalette::PlaceholderText));
    addChatRetryButton(lay, glyph, [this, retryText] { emit retryRequested(retryText); });
    // The card was measured before this button existed, so its wrapped text ends up
    // clipped under it. Re-run the width/height pass now that the row is complete.
    applyBubbleWidths();
  }

  void ChatDock::appendExpiredSession(const QString& text, const QString& host,
                                     const QString& retryText) {
    QVBoxLayout* lay = appendCard("Error", text, CardKind::Error);
    // The way back in, spelled out — an expired session is not something Resend
    // can fix, so the card leads with the reconnect (browser parity).
    auto* cta = new QPushButton(tr("Reconnect to %1").arg(host), lay->parentWidget());
    cta->setObjectName(QStringLiteral("chatReconnectCta"));   // the GUI test finds it
    cta->setProperty("accentCta", true);   // accent fill via theme.cpp's property rule
    cta->setCursor(Qt::PointingHandCursor);
    connect(cta, &QPushButton::clicked, this, [this, host] { emit reconnectRequested(host); });
    lay->addWidget(cta, 0, Qt::AlignLeft);
    addRetryButton(lay, retryText);   // …and the turn is still resendable after
    applyBubbleWidths();
  }

  void ChatDock::appendUnreachable(const QString& text, const QString& retryText) {
    QVBoxLayout* lay = appendCard("Error", text, CardKind::Error);
    // The reveal flies from THIS button — it lives in the transcript and stays on
    // screen through the click, unlike the gear behind "…".
    QPointer<ChatDock> self(this);
    addChatConfigureCta(lay, accentCache_, [self](QPushButton* cta) {
      if (self) emit self->configureProviderRequested(cta);
    });
    addRetryButton(lay, retryText);
    applyBubbleWidths();
  }

  void ChatDock::appendNotice(const QString& text) {
    appendCard("Assistant off", text, CardKind::Muted);
  }

  void ChatDock::appendLateNote(const QString& text) {
    // Anything the turn has to say about its own reply reports INTO that reply's
    // bubble (a separate card read as a second assistant message); falls back to
    // a plain note with no bubble.
    if (lastAssistantCard_) {
      if (auto* lay = qobject_cast<QVBoxLayout*>(lastAssistantCard_->layout())) {
        auto* note = makePlainLabel(text, lastAssistantCard_);
        note->setObjectName(QStringLiteral("chatNoteLabel"));
        note->setWordWrap(true);
        note->setProperty("chatNote", text);
        applyMutedText(note);
        lay->addWidget(note);
        installCardMenu(qobject_cast<QFrame*>(lastAssistantCard_.data()));
        applyBubbleWidths();
        emit lateNotePosted(text);
        return;
      }
    }
    appendNote(text);
  }

  void ChatDock::appendNote(const QString& text) {
    // Informational, about work that SUCCEEDED — the danger style is reserved
    // for actual turn errors (appendError / a stopped turn).
    appendCard("Note", text, CardKind::Muted);
    emit notePosted(text);
  }

  void ChatDock::appendVariants(const QVector<VariantCard>& variants) {
    if (variants.isEmpty()) return;
    QVBoxLayout* lay = appendTranscriptCard(6);
    QWidget* card = lay->parentWidget();
    for (const VariantCard& v : variants) {
      auto* row = new QHBoxLayout;
      row->setSpacing(8);
      auto* thumb = new QLabel(card);
      const QPixmap pm = QPixmap::fromImage(
          v.image.scaled(kThumbEdge, kThumbEdge, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation));
      thumb->setPixmap(pm);
      row->addWidget(thumb);
      auto* meta = new QVBoxLayout;
      meta->setSpacing(2);
      auto* name = makePlainLabel(v.label, card);   // model-written variant label
      name->setWordWrap(true);
      meta->addWidget(name);
      auto* btns = new QHBoxLayout;
      btns->setSpacing(4);
      auto* open = new QToolButton(card);
      open->setText("Open");
      open->setAutoRaise(true);
      open->setToolTip("Open this variant's project in a new window");
      open->setEnabled(!v.projectId.isEmpty());
      const QString pid = v.projectId;
      connect(open, &QToolButton::clicked, this,
              [this, pid] { emit openVariantRequested(pid); });
      btns->addWidget(open);
      auto* save = new QToolButton(card);
      save->setText("Save…");
      save->setAutoRaise(true);
      save->setToolTip("Save this variant image to disk");
      const QImage img = v.image;
      const QString label = v.label;
      connect(save, &QToolButton::clicked, this, [this, img, label] {
        const QString path = QFileDialog::getSaveFileName(
            this, "Save variant", label + QStringLiteral(".png"), "PNG image (*.png)");
        if (!path.isEmpty()) img.save(path, "PNG");
      });
      btns->addWidget(save);
      btns->addStretch(1);
      meta->addLayout(btns);
      meta->addStretch(1);
      row->addLayout(meta, 1);
      lay->addLayout(row);
    }
  }

  // Wipe the conversation surface: every transcript card (a pending "…"
  // included) plus the attachment state, then the empty state returns. The
  // provider settings and the working image are deliberately NOT touched; the
  // model-side history is the owner's to clear (clearRequested).
  void ChatDock::clearConversation() {
    clearPending();  // the in-flight card is a transcript card too
    // Walk backwards so the indices stay valid; suggest_ and the bottom stretch
    // ARE the empty state, so they survive.
    bool wiped = false;
    for (int i = transcriptLayout_->count() - 1; i >= 0; --i) {
      QLayoutItem* item = transcriptLayout_->itemAt(i);
      QWidget* w = item ? item->widget() : nullptr;
      if (!w || w == suggest_) continue;
      // Scatter a snapshot of the card over the dock BEFORE it leaves the layout —
      // the particles can't live inside a widget that is about to be destroyed.
      // Hosted on the WINDOW, not the dock: the dock's own content widget paints
      // over its children, so particles parented to the dock never show.
      // Fall, not Rows: a cleared message comes apart from its top edge and drops, the
      // way the cleared IMAGE does — the two removals now read as the same gesture.
      DisintegrateOverlay::over(w, window(), DisintegrateOverlay::Sweep::Fall,
                                kChatScatterCols, kChatScatterRows,
                                DisintegrateOverlay::kItemMs);   // a message is read, not glanced at
      // Anything REMOVED means the empty state waits, whether or not the scatter
      // could play (over() declines what it cannot grab — an off-screen dock, a
      // zero-sized card). Keying the wait off the animation instead made the wait
      // silently vanish in exactly the cases hardest to reason about, and the chips
      // came back over a transcript that was still emptying.
      wiped = true;
      delete transcriptLayout_->takeAt(i);
      // Out of the layout (so the transcript closes up) but still painted while it
      // fades under its own dust; the fade owns the delete.
      fadeOutAndDelete(w);
    }
    clearAttachments();
    // The empty state comes back only once the particles have landed. Showing it in
    // the same tick put the hint + chips on screen underneath a scatter that was
    // still playing, so the clear read as happening twice and the panel flickered.
    // Rows out first, THEN the placeholder — the browser (chatView.js
    // restoreEmptyState) and the cleared canvas sequence it exactly this way.
    if (wiped) {
      // A hair past the scatter's own duration, so the last particle is gone before
      // the empty state lands (the overlay deletes itself on its animation's finish).
      QTimer::singleShot(DisintegrateOverlay::kItemMs + 60, this, [this] {
        // A turn may have started while the wipe played — then the chips are wrong.
        if (transcriptHasCards()) return;
        suggest_->show();
        scrollToBottom();
      });
    } else {
      suggest_->show();
    }
    scrollToBottom();
  }

  // Any real transcript card present (the empty state and the bottom stretch don't
  // count) — what tells a deferred empty state whether it is still wanted.
  bool ChatDock::transcriptHasCards() const {
    for (int i = 0; i < transcriptLayout_->count(); ++i) {
      QLayoutItem* item = transcriptLayout_->itemAt(i);
      QWidget* w = item ? item->widget() : nullptr;
      if (w && w != suggest_ && !w->isHidden()) return true;
    }
    return false;
  }

  // Show a … item only while it can actually act (user decision; the browser hides
  // its .chat-more-item the same way): attach until the §7 cap with no video queued,
  // clear only over a non-empty transcript, neither while a turn is in flight.
  void ChatDock::syncMoreMenuItems() {
    if (actAttach_)
      actAttach_->setVisible(!busyFlag_ && images_.size() < kMaxAttachments
                             && videoPath_.isEmpty());
    if (actClear_) actClear_->setVisible(!busyFlag_ && transcriptHasCards());
  }

  void ChatDock::setBusy(bool on) {
    // Tracked as state, NOT as the progress bar's visibility: a turn can be
    // driven from the context-menu chat with this dock closed, and a hidden
    // widget is never isVisible().
    busyFlag_ = on;
    busy_->setVisible(on);
    // Attaching is frozen while a request is in flight; typing stays open.
    attach_->setEnabled(!on);
    // …and so is clearing: the transcript can't be wiped out from under an
    // answer that is still landing in it.
    if (clearBtn_) clearBtn_->setEnabled(!on);
    // The … menu mirrors those two (its items are the visible affordance now):
    // mid-turn they leave the menu entirely instead of greying out.
    syncMoreMenuItems();
    // While in flight, the send button IS the stop button (white glyph on the
    // accent fill, like the rest of the action group).
    send_->setIcon(themedIcon(on ? "stop" : "send", QColor(Qt::white), kAccentIcon));
    send_->setToolTip(on ? QStringLiteral("Stop the response")
                         : QString());
    updateSendEnabled();
  }

  bool ChatDock::isBusy() const { return busyFlag_; }

  QSize ChatDock::floatingDefaultSize() const { return kFloatingSize.expandedTo(minimumSize()); }

  void ChatDock::focusInput() { input_->setFocus(); }

  bool ChatDock::hasComposerText() const { return !input_->toPlainText().trimmed().isEmpty(); }

  void ChatDock::updateSendEnabled() {
    // Busy = STOP mode (always clickable); idle = gated on non-empty input.
    send_->setEnabled(isBusy() || !input_->toPlainText().trimmed().isEmpty());
  }

  void ChatDock::setProviderStatus(const QString& richTooltip, ProviderStatus status) {
    // The rich provider tooltip belongs on the … TRIGGER: the gear now lives
    // inside the menu (hidden), so hanging it there would never be seen. The
    // gear keeps it too, for when the menu is open.
    styleProviderStatusDot(statusDot_, more_, richTooltip, status, palette());
    if (gear_ && !richTooltip.isEmpty()) gear_->setToolTip(richTooltip);
  }

  void ChatDock::restyleIcons(const Palette& pal) {
    // Browser .chat-panel parity: one card on the controls-panel tone with a
    // themed hairline border; transcript + input are recessed rounded surfaces
    // (page tone, 8px radius, accent focus ring).
    paletteCache_ = pal;   // so a later swap toggle can re-issue this stylesheet
    accentCache_ = pal.accent;
    chipCache_ = pal.bgContainer;
    borderCache_ = pal.borderMain;
    textCache_ = pal.textMain;
    dangerCache_ = pal.danger;
    mutedCache_ = pal.textMuted;
    // Values ported 1:1 from browser/css/components.css: header on --bg-info
    // with only a bottom divider, a BORDERLESS transcript on the panel tone,
    // and the composer on --input-bg (8px radius, accent focus ring).
    setStyleSheet(
        QStringLiteral(
            "#chatTitleBar{background:%1;border:1px solid %2;border-bottom:1px solid %2;}"
            // The app-wide QToolButton rule pads 5x7 and reserves a border; inside a fixed
            // 23px header chip that leaves ~7px for the glyph, i.e. half the browser's mark.
            // .chat-hbtn has neither (padding: 0, border: none), so the 13px glyph fills it.
            "#chatTitleBar QToolButton{padding:0;border:none;background:transparent;"
            "border-radius:5px;}"
            "#chatTitleBar QToolButton:hover{background:%6;}"
            "#chatBody{background:%1;border:1px solid %2;border-top:none;}"
            "#chatBody QScrollArea{background:%1;border:none;}"
            "#chatInputArea{background:%1;border-top:1px solid %2;}"
            "#chatBody QScrollArea > QWidget > QWidget{background:transparent;}"
            "#chatInput{background:%3;color:%4;border:1px solid %2;border-radius:8px;"
            "padding:6px 8px;font-size:14px;}"
            "#chatInput:focus{border:1px solid %5;}"
            // The in-bubble executor note line: --text-muted through the
            // STYLESHEET — under QSS a palette colour loses.
            "QLabel#chatNoteLabel{color:%7;background:transparent;}"
            // Attachment chips (browser .chat-attach-chip): quiet pill on the
            // input tone with the themed hairline; the × turns accent on hover.
            "#chatAttachChip{background:%3;border:1px solid %2;border-radius:6px;}"
            "#chatAttachChip QLabel{color:%4;font-size:11px;background:transparent;}"
            "#chatAttachRemove{border:none;background:transparent;color:%4;"
            "font-size:13px;padding:0 2px;}"
            "#chatAttachRemove:hover{color:%5;}")
            .arg(pal.bgControls.name(), pal.borderMain.name(), pal.inputBg.name(),
                 pal.inputText.name(), pal.accent.name(), pal.bgContainer.name(),
                 // %7 — muted text (--text-muted), alpha-preserving.
                 QStringLiteral("rgba(%1,%2,%3,%4)")
                     .arg(pal.textMuted.red())
                     .arg(pal.textMuted.green())
                     .arg(pal.textMuted.blue())
                     .arg(pal.textMuted.alphaF()))
        // The transcript bubbles themselves come from the SHARED sheet the
        // context menu's panel applies too, so one message looks the same
        // wherever it is rendered.
        + chatCardStyleSheet(pal, chatSwapSides_));
    // The accent-filled composer buttons carry white line-art (like checked
    // toolbar toggles); the title-bar float/close ghosts use the theme text.
    const QColor onAccent = Qt::white;
    // On a LIGHT accent that white would wash out, so the glyphs get a dark halo.
    const bool halo = accentNeedsGlyphShadow(pal.accent);
    send_->setIcon(themedIcon(isBusy() ? "stop" : "send", onAccent, kAccentIcon, halo));
    attach_->setIcon(themedIcon("image", onAccent, kAccentIcon, halo));
    gear_->setIcon(themedIcon("gear", onAccent, kAccentIcon, halo));
    clearBtn_->setIcon(themedIcon("trash", onAccent, kAccentIcon, halo));
    if (more_) more_->setIcon(themedIcon("dots", onAccent, kAccentIcon, halo));
    if (actAttach_) actAttach_->setIcon(themedIcon("image", pal.textMain, 14));
    if (actClear_) actClear_->setIcon(themedIcon("trash", pal.textMain, 14));
    if (actSwapSides_) actSwapSides_->setIcon(themedIcon("swap", pal.textMain, 14));
    if (actSettings_) actSettings_->setIcon(themedIcon("gear", pal.textMain, 14));
    closeBtn_->setIcon(themedIcon("x", pal.textMain, 14));
    updatePlacementState();
    // Theme-tracking chrome: accent header sparkle/title, and the suggestion
    // pills as quiet solid chips — normal border/card background/text, with an
    // accent border + faint accent fill on hover.
    headerIcon_->setPixmap(themedIcon("sparkle", pal.textMain, 16).pixmap(16, 16));
    // The composer's drop cue: dashed accent border over a mostly-opaque accent tint
    // mixed into the card colour, so it stays legible in both themes (browser
    // .chat-drop-cue). The glyph is the same "image" mark the attach button uses.
    if (splitter_) splitter_->setPillColors(pal.borderMain, pal.accent);
    if (dropCue_) {
      // SOLID, blended — the browser's color-mix(accent 16%, card) is opaque, and a
      // translucent slab here let the placeholder text show straight through the label.
      dropCue_->setStyleSheet(
          QStringLiteral("#chatDropCue{border:2px dashed %1;border-radius:10px;background:%2;}")
              .arg(pal.accent.name(), blendColors(pal.accent, pal.inputBg, 0.16).name()));
      if (dropCueIcon_) dropCueIcon_->setPixmap(themedIcon("image", pal.accent, 16).pixmap(16, 16));
      if (dropCueText_)
        dropCueText_->setStyleSheet(
            QStringLiteral("color:%1;background:transparent;font-weight:600;").arg(pal.accent.name()));
    }
    headerTitle_->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;").arg(pal.textMain.name()));
    // Jump pills: circle ghosts over the transcript (browser .chat-jump-btn). The
    // browser's hover comes from TWO rules that compose: its own (border → --accent,
    // glyph → --text-main, done below via the eventFilter) plus the app-wide generic
    // `button:hover { background: var(--accent-2) }`, which .chat-jump-btn:hover never
    // overrides — so the pill fills solid on hover there. QSS has no such generic rule
    // to fall back on, so it has to be stated here explicitly, or the fill is missing
    // (an earlier gap: only the border recoloured, and the pill stayed unfilled).
    // pal.textKey doubles as --accent-2 (see theme.cpp themePalette).
    if (jumpTop_ && jumpBottom_) {
      const QString jumpQss =
          QStringLiteral(
              "QToolButton{border:1px solid %1;border-radius:14px;background:%2;}"
              "QToolButton:hover{border-color:%3;background:%4;}")
              .arg(pal.borderMain.name(), pal.bgControls.name(), pal.accent.name(),
                   pal.textKey.name());
      jumpTop_->setIcon(themedIcon("chevron-up", pal.textMuted, 14));
      jumpBottom_->setIcon(themedIcon("chevron-down", pal.textMuted, 14));
      jumpTop_->setStyleSheet(jumpQss);
      jumpBottom_->setStyleSheet(jumpQss);
    }
    styleSuggestionChips(suggest_, pal);
  }

  void ChatDock::scrollToBottom() {
    stickToBottom_ = true;   // an explicit jump to the end re-arms the follow pin
    // Defer until the layout has run so the new card's height is included; the
    // rangeChanged pin then keeps following any later growth.
    QTimer::singleShot(0, scroll_, [this] {
      scroll_->verticalScrollBar()->setValue(scroll_->verticalScrollBar()->maximum());
    });
  }

}  // namespace stencil::gui
