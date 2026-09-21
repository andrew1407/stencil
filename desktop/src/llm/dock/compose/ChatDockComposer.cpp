// The dock's composer: the prompt field that takes a drop, the attachment chips, the send control and
// the "…" menu behind it, and the drop cue that covers the input rather than pushing it around.
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "../../../support/guiHelpers.hpp"
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
#include "../../../support/motion/DisintegrateOverlay.hpp"
#include "../../../support/control/FlowLayout.hpp"
#include "../../../support/modal/modalReveal.hpp"
#include "../../../support/menu/menuReveal.hpp"
#include "../../../support/icon/iconMotion.hpp"
#include "../../../support/motion/ShimmerOverlay.hpp"

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
#include "../../../support/motion/MenuShimmer.hpp"
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
  void ChatDock::buildComposer() {
    auto* inputArea = new QWidget(log.splitter);
    log.inputArea = inputArea;
    inputArea->setMinimumHeight(88);
    // The QPlainTextEdit would swallow the drop and paste the path as text.
    inputArea->setAcceptDrops(true);
    inputArea->installEventFilter(this);
    auto* inputCol = new QVBoxLayout(inputArea);
    inputArea->setObjectName("chatInputArea");
    inputArea->setAttribute(Qt::WA_StyledBackground);
    inputCol->setContentsMargins(10, 8, 10, 8);
    inputCol->setSpacing(4);

    cmp.busy = new QProgressBar(inputArea);
    cmp.busy->setRange(0, 0);
    cmp.busy->setTextVisible(false);
    cmp.busy->setFixedHeight(4);
    cmp.busy->hide();
    inputCol->addWidget(cmp.busy);

    // browser .chat-attachments parity
    cmp.attachTray = new QWidget(inputArea);
    cmp.attachTray->setObjectName("chatAttachTray");
    // Wide chips clip rather than raising the dock's minimum width.
    cmp.attachTray->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* trayRow = new QHBoxLayout(cmp.attachTray);
    trayRow->setContentsMargins(0, 0, 0, 0);
    trayRow->setSpacing(6);
    trayRow->addStretch(1);
    cmp.attachTray->setVisible(false);
    inputCol->addWidget(cmp.attachTray);

    auto* inputRow = new QHBoxLayout;
    inputRow->setSpacing(6);
    input = new QPlainTextEdit(inputArea);
    input->setObjectName("chatInput");
    input->setPlaceholderText("Ask the assistant… (Enter sends, Shift+Enter newline)");
    input->setMinimumHeight(48);
    input->setAcceptDrops(false);
    input->viewport()->setAcceptDrops(false);
    input->installEventFilter(this);
    connect(input, &QPlainTextEdit::textChanged, this, &ChatDock::updateSendEnabled);
    inputRow->addWidget(input, 1);

    auto* btnWrap = new QWidget(inputArea);
    auto* btnCol = new QVBoxLayout(btnWrap);
    btnCol->setContentsMargins(0, 0, 0, 0);
    btnCol->setSpacing(0);
    btnCol->addStretch(1);
    // Only SEND stays inline; the hidden QToolButtons live on as the "…" menu's action targets.
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(2);
    cmp.send = makeChatAccentButton(btnWrap, "");
    cmp.send->setObjectName("chatSend");
    cmp.send->setEnabled(false);
    connect(cmp.send, &QToolButton::clicked, this, &ChatDock::onSendClicked);
    btnRow->addWidget(cmp.send);
    cmp.attach = makeChatAccentButton(btnWrap, QString());
    cmp.attach->setAccessibleName(QStringLiteral("Add image"));
    cmp.attach->setObjectName("chatAttach");
    cmp.attach->hide();
    connect(cmp.attach, &QToolButton::clicked, this, &ChatDock::pickMedia);
    cmp.gear = makeChatAccentButton(btnWrap, QString());
    cmp.gear->setAccessibleName(QStringLiteral("Assistant settings"));
    cmp.gear->setObjectName("chatGear");
    cmp.gear->hide();
    connect(cmp.gear, &QToolButton::clicked, this, &ChatDock::settingsRequested);
    cmp.more = makeChatAccentButton(btnWrap, QString());
    cmp.more->setAccessibleName(QStringLiteral("More actions"));
    cmp.more->setObjectName("chatMore");
    cmp.more->setPopupMode(QToolButton::InstantPopup);
    {
      auto* menu = new QMenu(cmp.more);
      menu->setObjectName("chatMoreMenu");
      moreRows = buildChatMoreMenu(*menu, /*withClear=*/true);
      connect(moreRows.attach, &QAction::triggered, this, &ChatDock::pickMedia);
      connect(moreRows.clear, &QAction::triggered, this, [this] {
        clearConversation();
        emit clearRequested();
      });
      // Re-skins this dock immediately; the owner persists it and propagates to the mirror panel.
      connect(moreRows.swapSides, &QAction::triggered, this, [this] {
        setChatSwapSides(!chatSwapSides);
        emit chatSwapSidesChanged(chatSwapSides);
      });
      connect(moreRows.settings, &QAction::triggered, this, &ChatDock::settingsRequested);
      // An item that cannot act right now HIDES rather than greys out (browser parity).
      connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        syncMoreMenuItems();
        fitMenuWidth(*menu);
      });
      compactIconMenu(*menu);
      // Built once and parented to the menu: it is created once, popped many times (menuRowPolish.hpp).
      new support::MenuShimmer(menu, menu);
      support::revealMenuFrom(*menu, cmp.more);
      cmp.more->setMenu(menu);
    }
    btnRow->addWidget(cmp.more);
    // The dot is a child pinned to the "…" trigger's top-right corner: no layout space.
    cmp.statusDot = new QLabel(cmp.more);
    cmp.statusDot->setObjectName("chatStatusDot");
    cmp.statusDot->setFixedSize(7, 7);
    cmp.statusDot->setAttribute(Qt::WA_TransparentForMouseEvents);
    cmp.statusDot->move(ACCENT_EDGE - cmp.statusDot->width() - 1, 1);
    cmp.statusDot->raise();
    // Disabled while a turn is in flight — the transcript can't be wiped mid-answer.
    chrome.clearBtn = makeChatAccentButton(btnWrap, QString());
    chrome.clearBtn->setAccessibleName(QStringLiteral("Clear history"));
    chrome.clearBtn->setObjectName("chatClear");
    chrome.clearBtn->hide();
    connect(chrome.clearBtn, &QToolButton::clicked, this, [this] {
      clearConversation();
      emit clearRequested();
    });
    btnCol->addLayout(btnRow);
    inputRow->addWidget(btnWrap, 0, Qt::AlignBottom);
    inputCol->addLayout(inputRow, 1);

    // A child of the composer, not a layout item — it must cover the input, not push it around.
    cmp.dropCue = new QWidget(inputArea);
    cmp.dropCue->setObjectName(QStringLiteral("chatDropCue"));
    cmp.dropCue->setAttribute(Qt::WA_StyledBackground);
    cmp.dropCue->setAttribute(Qt::WA_TransparentForMouseEvents);
    cmp.dropCue->hide();
    auto* cueRow = new QHBoxLayout(cmp.dropCue);
    cueRow->setContentsMargins(8, 8, 8, 8);
    cueRow->setSpacing(8);
    cueRow->addStretch(1);
    cmp.dropCueIcon = new QLabel(cmp.dropCue);
    cueRow->addWidget(cmp.dropCueIcon, 0, Qt::AlignVCenter);
    cmp.dropCueText = makePlainLabel(QStringLiteral("Drop to attach"), cmp.dropCue);
    cueRow->addWidget(cmp.dropCueText, 0, Qt::AlignVCenter);
    cueRow->addStretch(1);
    // The bob rides on the label's contents margins — it sits in a layout, so moving it would fight it.
    cmp.dropCueAnim = new QVariantAnimation(this);
    cmp.dropCueAnim->setDuration(1000);
    cmp.dropCueAnim->setLoopCount(-1);
    cmp.dropCueAnim->setKeyValueAt(0.0, -2.0);
    cmp.dropCueAnim->setKeyValueAt(0.5, 2.0);
    cmp.dropCueAnim->setKeyValueAt(1.0, -2.0);
    connect(cmp.dropCueAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
      if (!cmp.dropCueIcon) return;
      const int dy = qRound(v.toDouble());
      cmp.dropCueIcon->setContentsMargins(0, 4 + dy, 0, 4 - dy);
    });
  }

}  // namespace stencil::gui
