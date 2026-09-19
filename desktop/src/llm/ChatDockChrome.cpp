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
#include "MediaLoader.hpp"  // isImageFileName / isVideoFileName (attach routing)
#include "theme.hpp"        // Palette (restyleIcons)
#include "scrollReveal.hpp"  // transcript cards fade at the viewport edges
#include "../support/DisintegrateOverlay.hpp"  // cards scatter on Clear, gather on append
#include "../support/FlowLayout.hpp"           // the suggestion chips wrap like browser chips
#include "../support/modalReveal.hpp"          // support::motionReduced()
#include "../support/menuReveal.hpp"           // card menu grows from the click
#include "../support/iconMotion.hpp"           // the per-icon hover motion
#include "../support/ShimmerOverlay.hpp"       // the shared hover sweep

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

  const char* const CHAT_STATUS_OK_COLOR = "#2e9e4f";
  const char* const CHAT_STATUS_BAD_COLOR = "#d6293e";

  QToolButton* makeChatAccentButton(QWidget* parent, const QString& tooltip) {
    QToolButton* b = makeGhostButton(parent, tooltip);
    b->setProperty("chatAccent", true);
    b->setFixedSize(ACCENT_EDGE, ACCENT_EDGE);
    b->setIconSize(QSize(ACCENT_ICON, ACCENT_ICON));  // fill the button like the browser's
    return b;
  }

  void styleProviderStatusDot(QLabel* dot, QToolButton* gear,
                              const QString& richTooltip,
                              ChatDock::ProviderStatus status,
                              const QPalette& pal) {
    const QString color =
        status == ChatDock::ProviderStatus::OK            ? QString(CHAT_STATUS_OK_COLOR)
        : status == ChatDock::ProviderStatus::UNREACHABLE ? QString(CHAT_STATUS_BAD_COLOR)
                                                          : pal.color(QPalette::Mid).name();
    // Badge on the gear's corner: filled dot + a subtle ring for legibility.
    dot->setStyleSheet(
        QStringLiteral("background:%1;border-radius:3px;border:1px solid rgba(255,255,255,160);")
            .arg(color));
    gear->setToolTip(richTooltip);
    dot->setToolTip(richTooltip);
  }

  // Branded title bar (browser header-row parity): sparkle + accent "Assistant" + right-aligned
  // float/close. QDockWidget keeps its native title drag through a custom title-bar widget; an event
  // filter OBSERVES press/move/release to drive the dock zones without disturbing Qt's drag.
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
    static const Place PLACES[] = {
        {Qt::LeftDockWidgetArea, "Dock left — or drag the header to an edge"},
        {Qt::TopDockWidgetArea, "Dock top — or drag the header to an edge"},
        {Qt::BottomDockWidgetArea, "Dock bottom — or drag the header to an edge"},
        {Qt::RightDockWidgetArea, "Dock right — or drag the header to an edge"},
    };
    auto* dockGroup = new QWidget(titleBar_);
    auto* dockRow = new QHBoxLayout(dockGroup);
    dockRow->setContentsMargins(0, 0, 0, 0);
    dockRow->setSpacing(1);   // .chat-dock-btns gap: 1px
    for (const Place& p : PLACES) {
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
    // NOT QWidget::close(): that hides the dock on the spot, and a side-docked chat blinked out instead
    // of sliding into its edge. The owner runs the same animated path the toolbar toggle uses
    // (closeEvent below routes every OTHER close the same way).
    connect(closeBtn_, &QToolButton::clicked, this, [this] { emit closeRequested(); });
    row->addWidget(closeBtn_);
    setTitleBarWidget(titleBar_);
    titleBar_->installEventFilter(this);  // drag observation (drag dock zones)
  }

  // Empty-state suggestions (browser parity): clickable pills laid out INLINE with wrapping. Clicking
  // PREFILLS the caller's composer, never sends. SHARED with the context menu's assistant panel — one
  // chip list, one flow layout, one style — so the two empty states cannot drift apart.
  QWidget* makeSuggestionChips(QWidget* parent, int gap, std::function<void(QString)> onPick) {
    auto* box = new QWidget(parent);
    box->setObjectName(QStringLiteral("chatSuggest"));
    auto* flow = new FlowLayout(box, 2, gap, gap);
    // One string per chip (browser CHAT_SUGGESTIONS parity): what's written on
    // the button is exactly what lands in the input.
    static const char* const CHIPS[] = {
        "Make it sepia",
        "3 variants: rotated \xc2\xb7 tinted \xc2\xb7 cropped",
        "Extract the lines from this image",
        "Crop 10% off every edge, rotate right",
    };
    for (const char* c : CHIPS) {
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
  // faint accent fill on hover. The boxes are app.qss's; only the palette is live.
  void styleSuggestionChips(QWidget* chips, const Palette& pal) {
    if (!chips) return;
    const QString qss =
        QStringLiteral("QPushButton{border:1px solid %1;background:%2;color:%3;}"
                       "QPushButton:hover{border-color:%4;background:rgba(%5,%6,%7,26);}")
            .arg(pal.borderMain.name(), pal.bgContainer.name(), pal.textMain.name(),
                 pal.accent.name())
            .arg(pal.accent.red())
            .arg(pal.accent.green())
            .arg(pal.accent.blue());
    for (QPushButton* chip : chips->findChildren<QPushButton*>(QStringLiteral("chatSuggestChip")))
      chip->setStyleSheet(qss);
  }

  void ChatDock::buildSuggestions() {
    // The empty state is the prompt chips, nothing more: the composer's own cue
    // (showDropCue) is what says a drop attaches, right where it lands.
    suggest_ = makeSuggestionChips(transcript_, 6, [this](QString prompt) {
      input_->setPlainText(prompt);  // prefill only — never send
      input_->moveCursor(QTextCursor::End);
      input_->setFocus();
    });
    transcriptLayout_->addWidget(suggest_);
  }
}  // namespace stencil::gui

