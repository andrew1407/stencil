#include "chatWidgets.hpp"

#include <QFrame>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>

namespace stencil::gui {

  // Theme-provided muted text (palette PlaceholderText, not a hardcoded hex).
  void applyMutedText(QLabel* label) {
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::PlaceholderText));
    label->setPalette(pal);
  }

  // Error text in the theme's --danger (browser .chat-msg-error).
  void applyDangerText(QLabel* label, const QColor& danger) {
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, danger);
    label->setPalette(pal);
  }

  // The small bold role caption every transcript card starts with.
  QLabel* makeRoleLabel(const QString& role, QWidget* card) {
    auto* roleLabel = new QLabel(role, card);
    QFont f = roleLabel->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 0.85);
    roleLabel->setFont(f);
    return roleLabel;
  }

  // A QLabel for UNTRUSTED text — model output, or a dropped filename. QLabel
  // defaults to Qt::AutoText, so mightBeRichText() would decide per string whether
  // to RENDER a model's markup (and QTextDocument resolves local file resources).
  // The browser/extension use textContent only; this is the Qt spelling of that.
  QLabel* makePlainLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    return label;
  }

  // Park a card's "⋯" beside the bottom corner of its VISIBLE SLICE (user
  // bubbles get it to the left, the rest to the right): the button anchors to
  // the viewport-intersected rect, so ANY visible sliver of a row keeps its menu.
  static constexpr int kMorePad = 4;

  void placeChatCardMore(QFrame* card, QToolButton* more, QScrollArea* scroll) {
    if (!card || !more) return;
    QWidget* host = card->parentWidget();
    if (!host) return;
    // NOTE: setParent() HIDES a widget — re-show it, or the first placement of a
    // button revealed by hover would silently swallow that reveal.
    if (more->parentWidget() != host) {
      const bool wasShown = more->isVisible();
      more->setParent(host);
      if (wasShown) more->show();
    }
    const bool user = card->objectName() == QLatin1String("chatCardUser");
    const QRect g = card->geometry();   // in the content widget's coordinates
    int x = user ? g.left() - more->width() - 6 : g.right() + 7;
    int y = g.bottom() - more->height() + 1;
    if (scroll && scroll->viewport()) {
      // The viewport, in those same content coordinates (it scrolls under them)
      // — intersected with the content widget's OWN rect, because that widget is
      // the button's parent and Qt clips a child to it. Clamping to the viewport
      // alone let the pill sit in a content-widget margin, half clipped.
      const QRect vp = QRect(host->mapFrom(scroll->viewport(), QPoint(0, 0)),
                             scroll->viewport()->size())
                           .intersected(host->rect());
      const QRect vis = g.intersected(vp);
      if (vis.isEmpty()) { more->hide(); return; }   // scrolled clean out of view
      y = vis.bottom() - more->height() + 1;
      // The WHOLE button rect stays inside, with a hair of padding — not just the
      // anchor point, and on both sides (a user row hangs left, the rest right).
      y = qBound(vp.top() + kMorePad, y, vp.bottom() - more->height() - kMorePad);
      x = qBound(vp.left() + kMorePad, x, vp.right() - more->width() - kMorePad);
      // …and it never straddles the NEIGHBOURING message. With only a sliver of
      // this row on screen the clamp above would push the pill up (or down) over
      // the card next to it, which reads as a bug — so for such a row it simply
      // does not show. A fully visible row always keeps its button: the pill sits
      // inside that row's own y-band, where no neighbour can reach it.
      // Hysteresis (one pad to appear, a smaller one to stay) keeps a row
      // crossing the threshold mid-scroll from flickering.
      const int pad = more->isVisible() ? kMorePad : kMorePad + 2;
      const QRect want(x, y, more->width(), more->height());
      if (vis.height() < more->height() + pad) { more->hide(); return; }
      // The neighbours are tested against their PLAIN rects: rows sit as little
      // as 2px apart (the menu panel's transcript), so padding them out would
      // suppress every pill — and it is unnecessary, because a pill that fits its
      // own row's slice is inside that row's band, where no neighbour reaches.
      // This fires exactly when the clamp above pushed it out of the slice.
      for (QFrame* sib : host->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (sib == card || !sib->isVisible()) continue;
        if (!sib->property("chatMoreBtn").isValid()) continue;   // rows only
        if (sib->geometry().intersects(want)) {
          more->hide();
          return;
        }
      }
    }
    more->move(x, y);
    more->raise();
  }

}  // namespace stencil::gui
