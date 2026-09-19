// The shared window-height ease (src/support/easeWindowHeight.hpp) — what the Assistant
// settings form rides when a provider swap adds or drops rows, and the twin of the
// browser's js/ui/motion/easeBoxHeight.js. Two Qt facts make this harder than a resize():
// a window cannot go under its layout's minimum, and Qt has ALREADY grown the window to
// that new minimum by the time the caller asks — so the flight has to be told where it
// started, and the constraint has to stand down for its duration.
#include "easeWindowHeight.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdio>
#include <string>

#include "support/check.hpp"

using stencil::support::WINDOW_RESIZE_MS;
using stencil::support::easeWindowHeight;

namespace {

  // A window of stacked fixed-height rows: adding one raises the layout's minimum exactly
  // as a provider's extra field does.
  QWidget* makeRows(int n) {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->setSizeConstraint(QLayout::SetDefaultConstraint);
    for (int i = 0; i < n; i++) {
      auto* row = new QLabel(QStringLiteral("row"), w);
      row->setFixedHeight(40);
      v->addWidget(row);
    }
    return w;
  }

  void addRow(QWidget* w) {
    auto* row = new QLabel(QStringLiteral("row"), w);
    row->setFixedHeight(40);
    static_cast<QVBoxLayout*>(w->layout())->addWidget(row);
    row->show();
    w->layout()->activate();
  }

  // Pump the event loop for `ms` — QVariantAnimation ticks on the loop, not on sleep.
  void spin(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  {
    // GROWING: the flight starts at the height the window had BEFORE the rows landed, not
    // at the minimum Qt already jumped it to — without `from` it would play nothing.
    QWidget* w = makeRows(3);
    w->show();
    w->layout()->activate();
    const int before = w->height();
    addRow(w);
    const int to = w->sizeHint().height();
    check(to > before, "a new row must ask for more height");
    easeWindowHeight(w, to, before);
    check(w->height() == before, "the flight is pinned back at the height it started from");
    spin(WINDOW_RESIZE_MS / 2);
    const int mid = w->height();
    check(mid > before && mid < to, ("mid-flight height " + std::to_string(mid) +
                                     " is between " + std::to_string(before) + " and " +
                                     std::to_string(to)).c_str());
    spin(WINDOW_RESIZE_MS);
    check(w->height() == to, "it lands exactly on the wanted height");
    // …and the layout gets its constraint back, or the window never resists a shrink again.
    check(w->layout()->sizeConstraint() == QLayout::SetDefaultConstraint,
          "the size constraint is restored once it lands");
    delete w;
  }

  {
    // SHRINKING: the rows that left already lowered the minimum, so the window has to be
    // walked DOWN — Qt grows a window whose layout no longer fits but never shrinks one.
    QWidget* w = makeRows(5);
    w->show();
    w->layout()->activate();
    const int before = w->height();
    delete w->layout()->itemAt(4)->widget();
    w->layout()->activate();
    const int to = w->sizeHint().height();
    check(to < before, "dropping a row must ask for less height");
    easeWindowHeight(w, to, before);
    spin(WINDOW_RESIZE_MS * 2);
    check(w->height() == to, "a shrink lands too, not only a grow");
    delete w;
  }

  {
    // A RUN of changes chases the latest height rather than queueing: one animation per
    // window, restarted.
    QWidget* w = makeRows(3);
    w->show();
    w->layout()->activate();
    const int before = w->height();
    addRow(w);
    easeWindowHeight(w, w->sizeHint().height(), before);
    spin(WINDOW_RESIZE_MS / 3);
    addRow(w);
    const int to = w->sizeHint().height();
    easeWindowHeight(w, to, w->height());
    spin(WINDOW_RESIZE_MS * 2);
    check(w->height() == to, "the second ask wins; the first does not land after it");
    delete w;
  }

  {
    // Never under the window's own minimum hint, whatever the caller asks for.
    QWidget* w = makeRows(4);
    w->show();
    w->layout()->activate();
    easeWindowHeight(w, 10, w->height());
    spin(WINDOW_RESIZE_MS * 2);
    check(w->height() >= w->minimumSizeHint().height(),
          "a height under the minimum hint is raised to it");
    delete w;
  }

  std::printf("%s\n", failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES");
  return failures == 0 ? 0 : 1;
}
