// The hover sweep on every selector (support/motion/ShimmerOverlay.hpp, browser .accent-dd-trigger's
// ui-shimmer): a SearchComboBox of each kind carries its own, a plain and an editable QComboBox take
// it from installHoverShimmerIn, and each starts on the pointer's own hover-in, driven through the
// window system; a stray Enter elsewhere, a repeat or a closed list replays nothing.
#include "SearchCombo.hpp"
#include "ShimmerOverlay.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QStyleFactory>
#include <QTest>
#include <QToolButton>

#include "../check.hpp"

using stencil::gui::SearchComboBox;

namespace {
  QWidget* overlayOf(QWidget* w) { return w->findChild<QWidget*>(QStringLiteral("shimmerOverlay"), Qt::FindDirectChildrenOnly); }
  qreal sweep(QWidget* w) { QWidget* o = overlayOf(w); return o ? o->property("sweepProgress").toReal() : -9; }
  void moveTo(QWidget& host, const QPoint& global) {
    QTest::mouseMove(host.windowHandle(), host.windowHandle()->mapFromGlobal(global));
  }
  QPoint centreOf(QWidget* w) { return w->mapToGlobal(w->rect().center()); }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));   // the app's own style
  QWidget host;
  auto* row = new QHBoxLayout(&host);
  row->setContentsMargins(8, 8, 8, 120);
  auto* plain = new QComboBox(&host);
  plain->addItems({"a", "b"});
  auto* editable = new QComboBox(&host);
  editable->setEditable(true);
  editable->addItems({"100", "200"});
  auto* button = new QToolButton(&host);
  button->setText(QStringLiteral("B"));
  for (QWidget* w : {static_cast<QWidget*>(plain), static_cast<QWidget*>(editable), static_cast<QWidget*>(button)})
    row->addWidget(w);
  stencil::gui::installHoverShimmerIn(&host);
  // Built after the host's one install pass, as a rebuilt dialog row's selectors are.
  auto* short_ = new SearchComboBox(&host, /*searchable=*/false);
  short_->addItems({"Solid", "Dashed"});
  auto* searchable = new SearchComboBox(&host, /*searchable=*/true);
  searchable->addItems({"A4", "A3"});
  row->addWidget(short_);
  row->addWidget(searchable);
  row->addStretch(1);
  host.resize(560, 200);
  host.show();
  (void)QTest::qWaitForWindowExposed(&host);
  const QPoint empty = host.mapToGlobal(QPoint(500, 170));

  const QList<QWidget*> selectors{short_, searchable, plain, editable, button};
  for (QWidget* w : selectors)
    check(overlayOf(w) != nullptr, qPrintable(QStringLiteral("%1 carries the hover sweep").arg(w->metaObject()->className())));
  for (QWidget* w : selectors) {
    moveTo(host, empty);
    QTest::qWait(400);   // past the 325 ms sweep
    moveTo(host, centreOf(w));
    check(sweep(w) >= 0.0, "the pointer's hover-in starts the sweep in that same event");
  }

  moveTo(host, empty);
  QTest::qWait(400);
  const QPointF away(-50, -50);
  QEnterEvent stray(away, away, short_->mapToGlobal(away));
  QApplication::sendEvent(short_, &stray);
  check(sweep(short_) < 0.0, "an Enter that names a point off the selector replays nothing");

  moveTo(host, centreOf(short_));
  QTest::qWait(120);
  const qreal mid = sweep(short_);
  const QPointF inside(6, 6);
  QEnterEvent again(inside, inside, short_->mapToGlobal(inside));
  QApplication::sendEvent(short_, &again);
  check(mid > 0.0 && sweep(short_) >= mid, "a repeat Enter while hovered does not restart it");

  moveTo(host, empty);
  QTest::qWait(400);
  QHoverEvent hover(QEvent::HoverMove, QPointF(6, 6), short_->mapToGlobal(QPointF(6, 6)), QPointF(-1, -1));
  QApplication::sendEvent(short_, &hover);
  check(sweep(short_) >= 0.0, "a hover move stands in for an Enter that never came");
  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(short_, &leave);
  check(sweep(short_) < 0.0, "leaving cancels it at once");

  moveTo(host, centreOf(short_));
  QCursor::setPos(centreOf(short_));
  QTest::qWait(400);
  if (QCursor::pos() == centreOf(short_)) {   // the release check reads the real pointer
    short_->showPopup();
    QTest::qWait(50);
    short_->hidePopup();
    QTest::qWait(50);
    moveTo(host, centreOf(short_) + QPoint(2, 0));
    check(sweep(short_) < 0.0, "closing its own list over a resting pointer replays nothing");
  }

  return failures ? 1 : 0;
}
