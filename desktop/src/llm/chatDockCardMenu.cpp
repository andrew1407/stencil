// The per-row card menu (shared by both chat surfaces).
#include "chatDock.hpp"
#include "chatDockShared.hpp"
#include "../support/scrollReveal.hpp"
#include "../support/menuReveal.hpp"
#include "../support/menuShimmer.hpp"
#include "../support/guiHelpers.hpp"
#include "iconSet.hpp"
#include "chatWidgets.hpp"

#include <QClipboard>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QToolButton>
#include <QAction>
#include <QStringList>

namespace stencil::gui {

  using namespace chatdock;
  // Browser chatView.js chatRowMenuItems. Popping a menu is a NESTED EVENT LOOP: the card or its
  // window can be gone after exec(), so every widget is a QPointer and the menu refuses to pop
  // without a live, mapped window (a QMenu against a destroyed window crashed in QCocoaWindow).
  static void showChatCardMenu(QPointer<QFrame> card, const QPoint& globalPos,
                               const ChatCardMenuHooks& hooks) {
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
    card->setProperty("chatMenuOpen", true);
    support::revealMenu(menu, globalPos);
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

  void installChatCardMenu(QFrame* card, const ChatCardMenuHooks& hooks) {
    if (!card) return;
    // QPointer throughout: these lambdas outlive the card (a settling turn deletes rows).
    const auto show = [hooks](QPointer<QFrame> c, const QPoint& at) {
      showChatCardMenu(c, at, hooks);
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
    more->setProperty(ScrollReveal::kExemptProperty, true);
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
    auto* glow = new QGraphicsDropShadowEffect(more);
    glow->setOffset(0, 0);
    glow->setBlurRadius(12);
    glow->setColor(hooks.accent);
    glow->setEnabled(false);
    more->setGraphicsEffect(glow);
    more->setCursor(Qt::PointingHandCursor);
    more->setFocusPolicy(Qt::NoFocus);
    installHoverShimmer(more);
    // Blended, not translucent: themedIcon's cache is keyed on an alpha-less colour name.
    more->setIcon(themedIcon("more", blendColors(hooks.muted, hooks.chip, kGhostRestOpacity), 16));
    more->hide();
    QObject::connect(more, &QToolButton::clicked, more, [cardRef, more, show] {
      if (!cardRef) return;
      show(cardRef, more->mapToGlobal(QPoint(0, more->height())));
    });
    new ChatCardMore(card, more, hooks.scroll, hooks.moreMoved, hooks.avoidRect);
  }
}  // namespace stencil::gui
