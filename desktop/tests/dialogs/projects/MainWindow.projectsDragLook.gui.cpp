// MainWindow GUI e2e — How a held project row's header looks, as in the browser: the row menu hovers
// in the accent with its glyph in the accent's ink, the ⋯ is an opaque accent chip (accent-2 while
// its menu is up), the drag-out zones cover the whole window under the dialog, and only the targets
// a held row may use take its drop. Browser twins: css/components/projects/*.css, list/dragMenu.js.
#include "projectsHeld.gui.hpp"
#include "../../../src/dialogs/projects/list/ProjectDragZones.hpp"

#include "../../../src/dialogs/projects/list/ProjectDragMenu.hpp"
#include "../../../src/support/motion/DisintegrateOverlay.hpp"
#include "../../../src/support/motionPrefs.hpp"

#include <QDragEnterEvent>
#include <QGraphicsOpacityEffect>
#include <QMimeData>

namespace {

  bool near(const QColor& a, const QColor& b, int tol = 12) {
    return std::abs(a.red() - b.red()) <= tol && std::abs(a.green() - b.green()) <= tol &&
           std::abs(a.blue() - b.blue()) <= tol;
  }

  stencil::gui::ProjectDragZones* zonesOf(QWidget* win) {
    for (QWidget* c : win->findChildren<QWidget*>())
      if (auto* z = dynamic_cast<stencil::gui::ProjectDragZones*>(c)) return z;
    return nullptr;
  }

  // The chip's face at rest: the offscreen cursor parked on it would paint its :hover instead.
  QColor faceOf(QWidget* chip) {
    chip->setAttribute(Qt::WA_UnderMouse, false);
    chip->style()->unpolish(chip);
    chip->style()->polish(chip);
    return chip->grab().toImage().pixelColor(2, chip->height() / 2);
  }

  int cloudsIn(QWidget* host) {
    int n = 0;
    for (QWidget* c : host->findChildren<QWidget*>())
      if (dynamic_cast<stencil::gui::DisintegrateOverlay*>(c) && c->isVisible()) ++n;
    return n;
  }

  // The chip's veil opacity: 1 when nothing veils it.
  qreal veilOf(QWidget* chip) {
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(chip->graphicsEffect());
    return fx ? fx->opacity() : 1.0;
  }

  // A row's drag entering `target`, carrying the list's reorder mime: true when it was accepted.
  bool takesRowDrop(QWidget* target) {
    QMimeData mime;
    mime.setData(stencil::gui::reorderRowMime(), QByteArray("0"));
    QDragEnterEvent ev(target->rect().center(), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    ev.ignore();
    QApplication::sendEvent(target, &ev);
    return ev.isAccepted();
  }

}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void heldHeaderLooksAsTheBrowsers() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog drags need the offscreen platform");
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    const QString open = win.activeProjectId;
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkGreen);
    const QString other = win.parts.chatAppliers.addImageProjectEntry(img, "look-other");
    QVERIFY(!open.isEmpty() && !other.isEmpty());

    QColor accent, chipFace, chipOpen, rowFace;
    bool rowMenu = false, glyphFlips = false, zonesCover = false, zonesUnder = false;
    bool otherPill = true, openPill = false, moreTakes = false, menuTakes = false;
    QTimer::singleShot(0, [&] {
      ProjectsWindow w = projectsWindow();
      if (!w.list || !w.more || !w.pill) {
        if (w.dlg) w.dlg->reject();
        return;
      }
      accent = w.dlg->palette().color(QPalette::Highlight);
      w.list->setCurrentItem(rowFor(w.list, other));
      w.list->onDragStart();
      otherPill = takesRowDrop(w.pill);
      w.list->onDragEnd();

      w.list->setCurrentItem(rowFor(w.list, open));
      w.list->onDragStart();
      auto* zones = zonesOf(&win);
      zonesCover = zones && zones->isVisible() && zones->parentWidget() == &win && zones->geometry() == win.rect();
      zonesUnder = w.dlg->isWindow();   // a top-level dialog: the zones lie under it on the window behind
      openPill = takesRowDrop(w.pill);
      moreTakes = takesRowDrop(w.more);
      chipFace = faceOf(w.more);
      holdAt(w.more->mapToGlobal(w.more->rect().center()));
      QMenu* menu = shownMenuWith(w.dlg, QStringLiteral("Rename"));
      chipOpen = faceOf(w.more);
      if (menu) {
        rowMenu = menu->property("accentRows").toBool();
        menuTakes = takesRowDrop(menu);
        QAction* rename = nullptr;
        for (QAction* a : menu->actions())
          if (a->text() == QLatin1String("Rename")) rename = a;
        const QRect row = menu->actionGeometry(rename);
        holdAt(menu->mapToGlobal(row.center()));
        rowFace = menu->grab().toImage().pixelColor(row.right() - 6, row.center().y());
        const QImage lit = rename->icon().pixmap(QSize(16, 16), 1.0, QIcon::Active).toImage();
        const QImage rest = rename->icon().pixmap(QSize(16, 16), 1.0, QIcon::Normal).toImage();
        glyphFlips = !lit.isNull() && lit != rest;
      }
      holdAt(w.dlg->mapToGlobal(QPoint(w.dlg->width() / 2, w.dlg->height() - 20)));
      w.list->onDragEnd();
      w.dlg->reject();
    });
    win.parts.projects.openProjects();
    QVERIFY2(rowMenu, "the held menu is a row menu");
    QVERIFY2(near(rowFace, accent), qPrintable(QStringLiteral("the hovered row is the accent: %1 vs %2")
                                                   .arg(rowFace.name(), accent.name())));
    QVERIFY2(glyphFlips, "its glyph takes the accent's ink when lit");
    QVERIFY2(near(chipFace, accent), qPrintable(QStringLiteral("the ⋯ is an opaque accent chip: %1 vs %2")
                                                    .arg(chipFace.name(), accent.name())));
    QVERIFY2(!near(chipOpen, chipFace, 2), "…and turns accent-2 while its menu is up");
    QVERIFY2(zonesCover && zonesUnder, "the drag-out zones cover the window, under the dialog");
    QVERIFY2(!otherPill, "a project not open here: Close does not take its drop");
    QVERIFY2(openPill && moreTakes && menuTakes, "the open project's Close, the ⋯ and its menu take the drop");
  }
  // In a particle mode the ⋯ forms out of its dust, veiled until the cloud lands (the browser's
  // surfaceIn), and leaves into it at once; slide plays the controls' slot slide, none
  // and reduced show and hide it with no veil.
  void moreFormsOutOfItsDust() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog drags need the offscreen platform");
    using stencil::support::MotionMode;
    const MotionMode had = stencil::support::storedMotionMode();
    const auto putBack = qScopeGuard([had] { stencil::support::setMotionMode(had); });
    const int inMs = stencil::gui::ProjectDragMenu::moreInMs();
    struct Look { qreal first = -1, mid = -1, landed = -1; bool shown = false, gone = false, cloud = false, leftAtOnce = false; };
    const auto run = [&](MotionMode mode, bool reduced) {
      const auto motion = motionPinned(!reduced);
      MainWindow win(nullptr, false);
      stencil::support::setMotionMode(mode);   // after the window has read the stored prefs
      win.resize(1100, 800);
      win.show();
      QImage img(40, 30, QImage::Format_RGB32);
      img.fill(Qt::darkCyan);
      const QString id = win.parts.chatAppliers.addImageProjectEntry(img, "dust-more");
      Look look;
      QTimer::singleShot(0, [&] {
        ProjectsWindow w = projectsWindow();
        if (!w.list || !w.more) {
          if (w.dlg) w.dlg->reject();
          return;
        }
        QTest::qWait(700);   // the window's own entrance is over
        w.list->setCurrentItem(rowFor(w.list, id));
        w.list->onDragStart();
        look.first = veilOf(w.more);
        QTest::qWait(inMs / 3);
        look.mid = veilOf(w.more);
        QTest::qWait(inMs + 200);
        look.landed = veilOf(w.more);
        look.shown = w.more->isVisible() && w.more->graphicsEffect() == nullptr;
        const int before = cloudsIn(w.dlg);
        holdAt(w.dlg->mapToGlobal(QPoint(w.dlg->width() / 2, w.dlg->height() - 20)));
        w.list->onDragEnd();
        look.cloud = cloudsIn(w.dlg) > before;
        look.leftAtOnce = !w.more->isVisible();
        QTest::qWait(600);
        look.gone = !w.more->isVisible();
        w.dlg->reject();
      });
      win.parts.projects.openProjects();
      return look;
    };
    const Look dust = run(MotionMode::PARTICLES, false);
    QVERIFY2(dust.first < 0.05 && dust.mid < 0.05, qPrintable(QStringLiteral("veiled while its dust gathers: %1, %2")
                                                                   .arg(dust.first).arg(dust.mid)));
    QVERIFY2(dust.landed == 1.0 && dust.shown, "…and whole once the cloud has landed");
    QVERIFY2(dust.cloud && dust.leftAtOnce, "it leaves into its dust, the cloud a copy of it from the first frame");
    for (const auto& [mode, reduced] : {std::pair{MotionMode::SLIDE, false}, std::pair{MotionMode::NONE, false},
                                        std::pair{MotionMode::PARTICLES, true}}) {
      const Look plain = run(mode, reduced);
      QVERIFY2(plain.shown && plain.landed == 1.0 && plain.gone && (mode == MotionMode::SLIDE || plain.first == 1.0),
               qPrintable(QStringLiteral("mode %1 reduced %2: shown %3 first %4 gone %5").arg(int(mode)).arg(reduced).arg(plain.shown).arg(plain.first).arg(plain.gone)));
    }
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsDragLook.gui.moc"
