#include "chatMenuPanel.hpp"
#include "chatMenuPanelParts.hpp"

#include "chatWidgets.hpp"   // placeChatBubbleTail / ChatBubbleTail
#include "../support/pillSplitter.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/iconSet.hpp"
#include "../support/modalReveal.hpp"   // support::motionReduced()
#include "../support/theme.hpp"

#include <QEasingCurve>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace stencil::gui {

  // This surface's hooks for the shared row menu: its own composer, its own
  // send path, its own transcript viewport.
  ChatCardMenuHooks ChatMenuPanel::menuHooks() {
    ChatCardMenuHooks h;
    h.owner = this;
    h.scroll = scroll_;
    h.busy = [this] { return busy_; };
    h.insertIntoPrompt = [this](const QString& t) {
      const QString existing = input_->toPlainText();
      input_->setPlainText(existing.isEmpty() ? t
                                              : existing + QLatin1Char('\n') + t);
      input_->moveCursor(QTextCursor::End);
      input_->setFocus();
      updateSendEnabled();
    };
    h.resend = [this](QFrame*, const QString& t) { if (onSend_) onSend_(t); };
    h.text = text_;
    h.chip = chip_;
    h.border = border_;
    h.accent = accent_;
    h.muted = muted_;
    return h;
  }

  void ChatMenuPanel::showPending() {
    clearPending();
    appendRow(QStringLiteral("Assistant"), QStringLiteral("…"), true, QString(),
              /*pending=*/true);
    pending_ = rowsAdded_.isEmpty() ? nullptr : rowsAdded_.last();
    // The DOCK's bouncing dots, not a static "…": an in-flight turn has to
    // look in-flight on this surface too.
    if (!pending_) return;
    if (QLabel* body = bodyOf(pending_)) body->hide();
    if (auto* lay = qobject_cast<QVBoxLayout*>(pending_->layout()))
      lay->insertWidget(0, makeChatTypingDots(pending_));
  }

  void ChatMenuPanel::clearPending() {
    if (!pending_) return;
    rowsAdded_.removeAll(pending_);
    delete pending_;
    pending_ = nullptr;
  }

  // The dock's semantics: the in-flight row becomes a muted "Stopped." in
  // place — no error row, no history push.
  void ChatMenuPanel::markStopped(const QString& retryText) {
    if (!pending_) return;
    // The dots stop with the turn; the text takes their place.
    if (QWidget* dots = pending_->findChild<QWidget*>(QStringLiteral("chatTypingDots")))
      delete dots;
    if (QLabel* body = bodyOf(pending_)) {
      body->show();
      body->setText(QStringLiteral("Stopped."));
      body->setProperty("chatBody", QStringLiteral("Stopped."));
    }
    // A settled row: the dock's error tone, its Resend, and the row menu the
    // pending card deliberately went without.
    pending_->setObjectName(QStringLiteral("chatCardError"));
    pending_->style()->unpolish(pending_);
    pending_->style()->polish(pending_);
    if (!retryText.isEmpty()) addRetry(pending_, retryText);
    installChatCardMenu(pending_, menuHooks());
    applyChatBubbleWidths(body_, scroll_);
    pending_ = nullptr;
  }

  // A mirrored row ARRIVES the way a dock card does — the SHARED gatherChatCardIn
  // machinery (chatWidgets.hpp), deferred a frame: scrollToBottom() is itself a
  // singleShot(0), so a 0ms hop would measure the row's box before the scroll landed.
  void ChatMenuPanel::gatherRow(QFrame* card) {
    if (!card || support::motionReduced()) return;
    // Veiled from the first frame — the row keeps its height (so the panel grows and
    // scrolls to it as usual) but is never seen ahead of its own motes.
    if (!card->graphicsEffect()) {
      auto* fx = new QGraphicsOpacityEffect(card);
      fx->setOpacity(0.0);
      card->setGraphicsEffect(fx);
    }
    QPointer<QFrame> cp(card);
    QTimer::singleShot(CHAT_GATHER_SETTLE_MS, card, [this, cp] {
      if (!cp) return;
      const auto settle = [cp] {
        if (auto* e = qobject_cast<QGraphicsOpacityEffect*>(cp->graphicsEffect()))
          e->setOpacity(1.0);
      };
      gatherChatCardIn(cp, rows_, scroll_, window(), CHAT_SCATTER_COLS, CHAT_SCATTER_ROWS,
                       settle);
    });
  }

  // A mirrored row leaves the way a dock card does: it scatters, then goes.
  // Returns whether anything is actually playing — over() declines when the
  // panel is off screen, and then there is nothing for the empty state to wait for.
  bool ChatMenuPanel::dissolveRow(QFrame* l) {
    if (!l) return false;
    // The same finer grid the dock's cards use (chatWidgets.hpp CHAT_SCATTER_*).
    const bool playing =
        DisintegrateOverlay::over(l, window(), DisintegrateOverlay::Sweep::FALL,
                                  CHAT_SCATTER_COLS, CHAT_SCATTER_ROWS, 0,
                                  l->palette().color(QPalette::WindowText))
        != nullptr;
    rows_->removeWidget(l);
    // Out of the layout, but painted while it fades under its own dust. Reuse
    // any effect already on the row and stop its animations first —
    // setGraphicsEffect() deletes the old effect, and anything still driving it
    // would be left dangling (chatDock.cpp fadeOutAndDelete).
    for (QVariantAnimation* a : l->findChildren<QVariantAnimation*>()) a->stop();
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(l->graphicsEffect());
    if (!fx) {
      fx = new QGraphicsOpacityEffect(l);
      l->setGraphicsEffect(fx);
    }
    auto* anim = new QVariantAnimation(l);
    anim->setDuration(CHAT_ROW_LEAVE_MS);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    // QPointer: the effect can be replaced out from under a running animation
    // (chatDock.cpp fadeOutAndDelete has the same guard, and the crash it fixes).
    QPointer<QGraphicsOpacityEffect> fxp(fx);
    QObject::connect(anim, &QVariantAnimation::valueChanged, l,
                     [fxp](const QVariant& v) { if (fxp) fxp->setOpacity(v.toDouble()); });
    QObject::connect(anim, &QVariantAnimation::finished, l, [l] {
      l->hide();
      l->deleteLater();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
    return playing;
  }

  void ChatMenuPanel::clearRows() {
    clearPending();
    bool wiped = false;
    for (QFrame* l : rowsAdded_) wiped = dissolveRow(l) || wiped;
    rowsAdded_.clear();
    // The dock's sequencing (chatDock.cpp clearConversation): the rows leave
    // FIRST and the empty state returns only once the particles have landed.
    if (!wiped) { suggest_->show(); return; }
    QTimer::singleShot(DisintegrateOverlay::ITEM_MS, this, [this] {
      // A turn may have started while the wipe played — then the chips are wrong.
      if (!rowsAdded_.isEmpty() || pending_) return;
      suggest_->show();
    });
  }
}  // namespace stencil::gui

