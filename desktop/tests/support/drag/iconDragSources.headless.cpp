// The iconDrag suite's sources: a ghost wearing its owner's face, a centred ghost, and a source
// that is no button dragging only from what its grab hook takes hold of.
#include "iconDragParts.hpp"
#include "dragOverlays.hpp"

namespace {
  // A ghost by its face: the widget stands off it by the room its rim takes.
  QSize faceOf(QWidget* child) {
    auto* ghost = dynamic_cast<DragGhost*>(child);
    return ghost ? ghost->faceSize() : child->size();
  }
}  // namespace

void iconDragTest::sourceCases(QApplication& app, QWidget& window, const QPoint& in, int slop,
                               const IconDragHooks& hooks, QStringList& log) {
  // A control painted from elsewhere (the header mark) hands the ghost its picture.
  auto* marked = new QPushButton(QStringLiteral("Mark"), &window);
  marked->setGeometry(100, 20, 60, 28);
  marked->show();
  QPixmap face(40, 40);
  face.fill(Qt::red);
  IconDragHooks faced;
  faced.face = [&face] { return face; };
  installIconDrag(marked, faced);
  const auto ghostOf = [&window](const QSize& size) {
    for (QWidget* child : window.findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
      if (child->testAttribute(Qt::WA_TransparentForMouseEvents) && child->isVisible() && faceOf(child) == size)
        return true;
    return false;
  };
  const QPoint away = marked->mapFrom(&window, QPoint(260, 200));
  press(marked, in);
  drag(marked, away);
  check(ghostOf(QSize(40, 40)), "a ghost wears the face its owner hands it");
  release(marked, away);
  app.processEvents();
  check(!ghostOf(QSize(40, 40)), "…and leaves with the drag");

  // The theme lens rides its switch in the circle's middle, wherever the press took hold of it.
  auto* centred = new QPushButton(QStringLiteral("Lens"), &window);
  centred->setGeometry(180, 20, 60, 28);
  centred->show();
  IconDragHooks mid;
  mid.ghostCentred = true;
  installIconDrag(centred, mid);
  const QPoint lensAt = window.mapToGlobal(QPoint(300, 220));
  press(centred, QPoint(4, 4));
  drag(centred, centred->mapFromGlobal(lensAt));
  QWidget* riding = nullptr;
  for (QWidget* child : window.findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
    if (child->testAttribute(Qt::WA_TransparentForMouseEvents) && child->isVisible() && faceOf(child) == centred->size())
      riding = child;
  check(riding && riding->geometry().center() == window.mapFromGlobal(lensAt),
        "a centred ghost sits centred on the pointer, not held at the grab point");
  release(centred, centred->mapFromGlobal(lensAt));
  app.processEvents();

  // A source that is no button drags from what its grab hook takes hold of, that spot the origin,
  // and never sees the live drag's release.
  auto* strip = new QWidget(&window);
  strip->setGeometry(20, 260, 200, 30);
  strip->show();
  int stripReleases = 0;
  struct Releases : QObject {
    int& seen;
    explicit Releases(int& count) : seen(count) {}
    bool eventFilter(QObject*, QEvent* e) override { seen += e->type() == QEvent::MouseButtonRelease; return false; }
  } counter(stripReleases);
  strip->installEventFilter(&counter);
  IconDragHooks spots = hooks;
  spots.grab = [strip](const QPoint& g) {   // only the left half of the strip is a handle
    const QRect half(strip->mapToGlobal(QPoint(0, 0)), QSize(strip->width() / 2, strip->height()));
    return half.contains(g) ? half : QRect();
  };
  installIconDrag(strip, spots);
  log.clear();
  press(strip, QPoint(150, 15));
  drag(strip, QPoint(150, 15 - slop - 4));
  release(strip, QPoint(150, 15 - slop - 4));
  app.processEvents();
  check(log.isEmpty() && stripReleases == 1, "a press its grab hook declines drags nothing");
  press(strip, QPoint(20, 15));
  drag(strip, QPoint(150, 15));
  release(strip, QPoint(150, 15));
  app.processEvents();
  check(log.contains(QStringLiteral("drop")) && stripReleases == 1,
        "released on the same source off the grabbed spot it drops, the source never seeing the release");
  log.clear();
  press(strip, QPoint(20, 15));
  drag(strip, QPoint(20, 15 + slop + 4));
  release(strip, QPoint(30, 12));
  app.processEvents();
  check(log.last() == QStringLiteral("cancel"), "…and back on the grabbed spot it cancels");
}
