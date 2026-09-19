// The dock's composer: the prompt field that takes a drop, the attachment chips, the send control and
// the "…" menu behind it, and the drop cue that covers the input rather than pushing it around.
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
  void ChatDock::buildComposer() {
    auto* inputArea = new QWidget(log_.splitter);
    log_.inputArea = inputArea;
    inputArea->setMinimumHeight(88);
    // The QPlainTextEdit would swallow the drop and paste the path as text.
    inputArea->setAcceptDrops(true);
    inputArea->installEventFilter(this);
    auto* inputCol = new QVBoxLayout(inputArea);
    inputArea->setObjectName("chatInputArea");
    inputArea->setAttribute(Qt::WA_StyledBackground);
    inputCol->setContentsMargins(10, 8, 10, 8);
    inputCol->setSpacing(4);

    cmp_.busy = new QProgressBar(inputArea);
    cmp_.busy->setRange(0, 0);
    cmp_.busy->setTextVisible(false);
    cmp_.busy->setFixedHeight(4);
    cmp_.busy->hide();
    inputCol->addWidget(cmp_.busy);

    // browser .chat-attachments parity
    cmp_.attachTray = new QWidget(inputArea);
    cmp_.attachTray->setObjectName("chatAttachTray");
    // Wide chips clip rather than raising the dock's minimum width.
    cmp_.attachTray->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* trayRow = new QHBoxLayout(cmp_.attachTray);
    trayRow->setContentsMargins(0, 0, 0, 0);
    trayRow->setSpacing(6);
    trayRow->addStretch(1);
    cmp_.attachTray->setVisible(false);
    inputCol->addWidget(cmp_.attachTray);

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
    cmp_.send = makeChatAccentButton(btnWrap, "");
    cmp_.send->setObjectName("chatSend");
    cmp_.send->setEnabled(false);
    connect(cmp_.send, &QToolButton::clicked, this, &ChatDock::onSendClicked);
    btnRow->addWidget(cmp_.send);
    cmp_.attach = makeChatAccentButton(btnWrap, QString());
    cmp_.attach->setAccessibleName(QStringLiteral("Add image"));
    cmp_.attach->setObjectName("chatAttach");
    cmp_.attach->hide();
    connect(cmp_.attach, &QToolButton::clicked, this, &ChatDock::pickMedia);
    cmp_.gear = makeChatAccentButton(btnWrap, QString());
    cmp_.gear->setAccessibleName(QStringLiteral("Assistant settings"));
    cmp_.gear->setObjectName("chatGear");
    cmp_.gear->hide();
    connect(cmp_.gear, &QToolButton::clicked, this, &ChatDock::settingsRequested);
    cmp_.more = makeChatAccentButton(btnWrap, QString());
    cmp_.more->setAccessibleName(QStringLiteral("More actions"));
    cmp_.more->setObjectName("chatMore");
    cmp_.more->setPopupMode(QToolButton::InstantPopup);
    {
      auto* menu = new QMenu(cmp_.more);
      menu->setObjectName("chatMoreMenu");
      moreRows_ = buildChatMoreMenu(*menu, /*withClear=*/true);
      connect(moreRows_.attach, &QAction::triggered, this, &ChatDock::pickMedia);
      connect(moreRows_.clear, &QAction::triggered, this, [this] {
        clearConversation();
        emit clearRequested();
      });
      // Re-skins this dock immediately; the owner persists it and propagates to the mirror panel.
      connect(moreRows_.swapSides, &QAction::triggered, this, [this] {
        setChatSwapSides(!chatSwapSides_);
        emit chatSwapSidesChanged(chatSwapSides_);
      });
      connect(moreRows_.settings, &QAction::triggered, this, &ChatDock::settingsRequested);
      // An item that cannot act right now HIDES rather than greys out (browser parity).
      connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        syncMoreMenuItems();
        fitMenuWidth(*menu);
      });
      compactIconMenu(*menu);
      // Built once and parented to the menu: it is created once, popped many times (menuRowPolish.hpp).
      new support::MenuShimmer(menu, menu);
      support::revealMenuFrom(*menu, cmp_.more);
      cmp_.more->setMenu(menu);
    }
    btnRow->addWidget(cmp_.more);
    // The dot is a child pinned to the "…" trigger's top-right corner: no layout space.
    cmp_.statusDot = new QLabel(cmp_.more);
    cmp_.statusDot->setObjectName("chatStatusDot");
    cmp_.statusDot->setFixedSize(7, 7);
    cmp_.statusDot->setAttribute(Qt::WA_TransparentForMouseEvents);
    cmp_.statusDot->move(ACCENT_EDGE - cmp_.statusDot->width() - 1, 1);
    cmp_.statusDot->raise();
    // Disabled while a turn is in flight — the transcript can't be wiped mid-answer.
    chrome_.clearBtn = makeChatAccentButton(btnWrap, QString());
    chrome_.clearBtn->setAccessibleName(QStringLiteral("Clear history"));
    chrome_.clearBtn->setObjectName("chatClear");
    chrome_.clearBtn->hide();
    connect(chrome_.clearBtn, &QToolButton::clicked, this, [this] {
      clearConversation();
      emit clearRequested();
    });
    btnCol->addLayout(btnRow);
    inputRow->addWidget(btnWrap, 0, Qt::AlignBottom);
    inputCol->addLayout(inputRow, 1);

    // A child of the composer, not a layout item — it must cover the input, not push it around.
    cmp_.dropCue = new QWidget(inputArea);
    cmp_.dropCue->setObjectName(QStringLiteral("chatDropCue"));
    cmp_.dropCue->setAttribute(Qt::WA_StyledBackground);
    cmp_.dropCue->setAttribute(Qt::WA_TransparentForMouseEvents);
    cmp_.dropCue->hide();
    auto* cueRow = new QHBoxLayout(cmp_.dropCue);
    cueRow->setContentsMargins(8, 8, 8, 8);
    cueRow->setSpacing(8);
    cueRow->addStretch(1);
    cmp_.dropCueIcon = new QLabel(cmp_.dropCue);
    cueRow->addWidget(cmp_.dropCueIcon, 0, Qt::AlignVCenter);
    cmp_.dropCueText = makePlainLabel(QStringLiteral("Drop to attach"), cmp_.dropCue);
    cueRow->addWidget(cmp_.dropCueText, 0, Qt::AlignVCenter);
    cueRow->addStretch(1);
    // The bob rides on the label's contents margins — it sits in a layout, so moving it would fight it.
    cmp_.dropCueAnim = new QVariantAnimation(this);
    cmp_.dropCueAnim->setDuration(1000);
    cmp_.dropCueAnim->setLoopCount(-1);
    cmp_.dropCueAnim->setKeyValueAt(0.0, -2.0);
    cmp_.dropCueAnim->setKeyValueAt(0.5, 2.0);
    cmp_.dropCueAnim->setKeyValueAt(1.0, -2.0);
    connect(cmp_.dropCueAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
      if (!cmp_.dropCueIcon) return;
      const int dy = qRound(v.toDouble());
      cmp_.dropCueIcon->setContentsMargins(0, 4 + dy, 0, 4 - dy);
    });
  }

}  // namespace stencil::gui
