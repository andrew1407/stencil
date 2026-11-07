// The per-row card menu (shared by both chat surfaces).
#include "ChatDock.hpp"
#include "chatDockShared.hpp"
#include "../../../support/motion/scrollReveal.hpp"
#include "../../../support/menu/menuReveal.hpp"
#include "../../../support/motion/MenuShimmer.hpp"
#include "../../../support/menu/menuDangerRow.hpp"
#include "../../../support/theme/theme.hpp"
#include "../../../support/skinPrefs.hpp"
#include "menuIconColumn.hpp"
#include "iconSet.hpp"
#include "chatWidgets.hpp"
#include "chatSheets.hpp"

#include <QClipboard>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QToolButton>
#include <QAction>
#include <QStringList>

namespace stencil::gui {

  using namespace chatdock;
  namespace {
    // Blended, not translucent: themedIcon's cache is keyed on an alpha-less colour name. A skin's
    // control wears its own ink (browser: the glass and raised buttons' --aq-ink / --wc-ink).
    QColor moreInk(const QColor& muted, const QColor& chip) {
      switch (support::skin()) {
        case support::Skin::AQUA: return support::skinGlass().ink;
        case support::Skin::WEBCORE: return support::skinBevel().ink;
        case support::Skin::DEFAULT: break;
      }
      return blendColors(muted, chip, GHOST_REST_OPACITY);
    }

    // Browser chat/view.js chatRowMenuItems. Popping a menu is a NESTED EVENT LOOP: the card or its
    // window can be gone after exec(), so every widget is a QPointer and a dead window refuses to pop.
    void showChatCardMenu(QPointer<QFrame> card, const QPoint& globalPos,
                                 const ChatCardMenuHooks& hooks, bool byButton) {
      if (!card) return;
      QPointer<QWidget> owner(hooks.owner);
      // windowHandle() is null before the window is mapped, and for a surface being torn down.
      QWidget* top = card->window();
      if (!top || !top->isVisible() || !top->windowHandle()) return;
      if (hooks.leaving && hooks.leaving()) return;
      // The properties carry the plain text, never the markup.
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

      // Parented to the card's OWN top level, never to an owner inside a popup that may be going away.
      QMenu menu(top);
      QAction* copy =
          menu.addAction(themedIcon("copy", hooks.text, 16), QStringLiteral("Copy message"));
      QAction* insert =
          menu.addAction(themedIcon("pencil", hooks.text, 16), QStringLiteral("Insert into prompt"));
      QAction* resend = nullptr;
      if (hooks.resend && card->objectName() == QLatin1String("chatCardUser")) {
        resend = menu.addAction(themedIcon("send", hooks.text, 16), QStringLiteral("Resend"));
        resend->setEnabled(!(hooks.busy && hooks.busy()));
      }
      support::MenuShimmer shimmer(&menu);
      compactIconMenu(menu);
      // Browser .chat-row-menu-item: the row menus hover in the accent.
      support::markAccentRows(menu, onAccentInk(menu.palette().color(QPalette::Highlight)));
      card->setProperty("chatMenuOpen", true);
      support::revealMenu(menu, globalPos);
      // Only a menu the ⋯ opened is shut by it; one a right-click opened lets the ⋯ open its own.
      QPointer<QWidget> more(byButton ? qobject_cast<QWidget*>(card->property("chatMoreBtn").value<QObject*>()) : nullptr);
      support::closeOnTriggerPress(menu, [more] {
        return more && more->isVisible() ? QRect(more->mapToGlobal(QPoint(0, 0)), more->size()) : QRect();
      });
      QAction* picked = menu.exec(globalPos);
      // AFTER the nested loop: re-check every pointer
      if (card) {
        card->setProperty("chatMenuOpen", false);
        if (auto* more = qobject_cast<QToolButton*>(
                card->property("chatMoreBtn").value<QObject*>()))
          more->setVisible(card->underMouse() || more->underMouse());
      }
      if (!picked) return;
      if (picked == copy) {
        QGuiApplication::clipboard()->setText(text);
      } else if (picked == insert) {
        if (owner && hooks.insertIntoPrompt) hooks.insertIntoPrompt(text);
      } else if (resend && picked == resend && owner && card) {
        hooks.resend(card, body ? body->property("chatBody").toString() : text);
      }
    }

    void styleChatCardMore(QToolButton* more, const QColor& chip, const QColor& border, const QColor& accent,
                           const QColor& muted) {
      more->setStyleSheet(support::chatRowMenuButtonSheet(chip, border, GHOST_REST_OPACITY));
      if (auto* glow = qobject_cast<QGraphicsDropShadowEffect*>(more->graphicsEffect())) glow->setColor(accent);
      more->setIcon(themedIcon("more", moreInk(muted, chip), 16));
    }
  }  // namespace

  void restyleChatCardMores(QWidget* root, const QColor& chip, const QColor& border, const QColor& accent,
                            const QColor& muted) {
    if (!root) return;
    for (QToolButton* more : root->findChildren<QToolButton*>(QStringLiteral("chatCardMore")))
      styleChatCardMore(more, chip, border, accent, muted);
  }

  void installChatCardMenu(QFrame* card, const ChatCardMenuHooks& hooks) {
    if (!card) return;
    // QPointer throughout: these lambdas outlive the card (a settling turn deletes rows).
    const auto show = [hooks](QPointer<QFrame> c, const QPoint& at, bool byButton = false) {
      showChatCardMenu(c, at, hooks, byButton);
    };
    const QPointer<QFrame> cardRef(card);
    const auto wire = [cardRef, show](QWidget* w) {
      if (w->contextMenuPolicy() == Qt::CustomContextMenu) return;
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
      // A mouse selection must also FOCUS the label, or Ctrl+C lands in the composer.
      if (l->textInteractionFlags() & Qt::TextSelectableByMouse)
        l->setFocusPolicy(Qt::ClickFocus);
    }
    // Created once — a card that gains labels later re-wires them above and keeps this one.
    if (card->property("chatMoreBtn").value<QObject*>()) return;
    auto* more = new QToolButton(card);
    more->setObjectName(QStringLiteral("chatCardMore"));
    // CHROME, not a row: the transcript's edge-reveal must not dissolve it.
    more->setProperty(ScrollReveal::EXEMPT_PROPERTY, true);
    // The button lives OUTSIDE the card (placeChatCardMore reparents it): a property pair, not parentage.
    card->setProperty("chatMoreBtn", QVariant::fromValue<QObject*>(more));
    more->setProperty("chatMoreCard", QVariant::fromValue<QObject*>(card));
    // Sever the pair on either death FIRST — deleteLater lags a beat.
    QObject::connect(card, &QObject::destroyed, more, [more] {
      more->setProperty("chatMoreCard", QVariant());
      more->deleteLater();
    });
    QObject::connect(more, &QObject::destroyed, card,
                     [card] { card->setProperty("chatMoreBtn", QVariant()); });
    more->setAutoRaise(true);
    more->setFixedSize(21, 21);
    more->setIconSize(QSize(15, 15));
    // browser .chat-row-menu-btn. Not a QGraphicsOpacityEffect: a widget carries only ONE
    // graphics effect and the accent glow already claims it, so the 0.7 is baked into the chrome.
    auto* glow = new QGraphicsDropShadowEffect(more);
    glow->setOffset(0, 0);
    glow->setBlurRadius(12);
    glow->setEnabled(false);
    more->setGraphicsEffect(glow);
    more->setCursor(Qt::PointingHandCursor);
    more->setFocusPolicy(Qt::NoFocus);
    installHoverShimmer(more);
    styleChatCardMore(more, hooks.chip, hooks.border, hooks.accent, hooks.muted);
    more->hide();
    QObject::connect(more, &QToolButton::clicked, more, [cardRef, more, show] {
      if (!cardRef) return;
      show(cardRef, more->mapToGlobal(QPoint(0, more->height())), true);
    });
    new ChatCardMore(card, more, hooks.scroll, hooks.moreMoved, hooks.avoidRect);
  }
}  // namespace stencil::gui
