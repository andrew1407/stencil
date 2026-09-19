// The row's layout and metrics: it fits the viewport, elides its URL, and stays a card.
#include "connectRowParts.hpp"

namespace connectrow {

  QWidget* checkRowLayout(const QString& longUrl, QListWidget* list) {
  // ── Row layout: everything fits the viewport, the URL elides, nothing scrolls sideways.
  check(list->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
        "horizontal scrollbar is AlwaysOff");
  check(list->count() == 1, "one connection row");
  QWidget* row = list->itemWidget(list->item(0));
  check(row != nullptr, "row hosts a widget");
  const int vpw = list->viewport()->width();
  check(list->item(0)->sizeHint().width() <= vpw, "item size hint capped to the viewport");
  check(row && row->width() <= vpw, "row widget no wider than the viewport");
  QLabel* urlLabel = nullptr;
  for (QLabel* l : row->findChildren<QLabel*>())
    if (l->toolTip().endsWith(longUrl)) urlLabel = l;   // "<state> — <url>"
  check(urlLabel != nullptr, "URL label carries the full url on its tooltip");
  check(urlLabel && urlLabel->text() != longUrl, "long URL is not shown verbatim");
  check(urlLabel && urlLabel->text().contains(QChar(0x2026)), "long URL is elided (…)");
  // ── Row metrics, measured against the browser's .connect-row: a 43-tall card at
  // padding 8px 10px / gap 12 / radius 8, action buttons 31x25, every child on one centre
  // line. Qt reaches those only with `border: none` (a 1px one adds 2px to both axes),
  // and the labelled expired button is pinned to the same box as the trash beside it.
  check(row->height() == 43, "the row card is the browser's own height");
  {
    int centre = -1;
    bool aligned = true;
    for (QWidget* w : row->findChildren<QWidget*>()) {
      if (w->objectName() == QStringLiteral("shimmerOverlay")) continue;   // rides its target
      const QRect g(w->mapTo(row, QPoint(0, 0)), w->size());
      // ±1: a widget whose own box is an even height inside an odd slot rounds one way
      // (the checkbox). Anything further is a real drift off the line.
      if (centre < 0) centre = g.center().y();
      else if (qAbs(g.center().y() - centre) > 1) aligned = false;
    }
    check(aligned, "every item in the row rides one centre line");
  }
  for (QPushButton* b : row->findChildren<QPushButton*>())
    check(b->size() == QSize(31, 25), "each row action is the browser's 31x25 button");
  // The row shimmers as one card on hover (browser .connect-row:hover::after), and so
  // does every control in it — installed per row, since rows are rebuilt on every change
  // and the dialog-wide pass only ever saw the batch that existed at open.
  {
    bool cardSweep = false;
    for (QWidget* w : row->findChildren<QWidget*>(QStringLiteral("shimmerOverlay")))
      if (w->parentWidget() == row && w->size() == row->size()) cardSweep = true;
    check(cardSweep, "the whole row carries a hover shimmer of its own");
  }
  const auto btns = row->findChildren<QPushButton*>();
  check(btns.size() == 2, "row keeps both trailing action buttons");
  for (QPushButton* b : btns) {
    const int right = b->mapTo(list->viewport(), QPoint(b->width(), 0)).x();
    check(right <= vpw, "action button sits fully inside the viewport");
  }

  // ── The row is a CARD whose outline is never clipped. QListView insets every item
  // by the list's spacing on BOTH sides, so a viewport-wide slot overhung the right
  // edge — which is where the gold admin outline was lost.
  check(list->item(0)->sizeHint().width() + 2 * list->spacing() <= vpw,
        "the row slot leaves the list's spacing on both sides");
  check(row && row->mapTo(list->viewport(), QPoint(row->width(), 0)).x() <= vpw - list->spacing(),
        "…so the row's right edge (and its outline) stays inside the viewport");
  // Height likewise: measured after parenting, so the cascaded card sheet is in it.
  check(list->item(0)->sizeHint().height() >= row->sizeHint().height(),
        "the row slot is at least as tall as the row wants to be");
  check(row->height() == list->item(0)->sizeHint().height(),
        "…and the row widget fills exactly that slot");
  check(row->objectName() == QStringLiteral("connRow"),
        "a plain connection row is a card like the projects rows");
  check(list->styleSheet().contains(QStringLiteral("border-radius:8px")),
        "…styled by the list's own cascading row sheet, at the browser's radius");

  // ── Hover follows the whole card: Qt sends Enter/Leave to the child under the
  // pointer, and hovering a label must not blink the wash off.
  QLabel* hoverTarget = urlLabel ? urlLabel : row->findChild<QLabel*>();
  if (hoverTarget) {
    hoverTarget->setAttribute(Qt::WA_UnderMouse, true);
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(hoverTarget, &enter);
    check(row->property("hovered").toBool(), "hovering a child hovers the whole row card");
    hoverTarget->setAttribute(Qt::WA_UnderMouse, false);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(hoverTarget, &leave);
    check(!row->property("hovered").toBool(), "…and leaving it un-hovers the card");
  }

    return row;
  }

}  // namespace connectrow
