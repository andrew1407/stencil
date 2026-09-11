// The per-row card menu (shared by both chat surfaces).
// Split out of chatDock.cpp; see chatDockShared.hpp for the shared constants.
#include "chatDock.hpp"
#include "chatDockShared.hpp"
#include "../app/scrollReveal.hpp"
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
    // everything below runs AFTER the nested loop: re-check every pointer
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
}  // namespace stencil::gui
