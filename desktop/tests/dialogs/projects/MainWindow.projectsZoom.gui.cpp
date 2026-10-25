// MainWindow GUI e2e — the Projects dialog's hover magnifier on a row's thumbnail: shown and
// Alt-doubled near every screen corner, it stays on the screen with no further mouse move.
// Shared ground is in projectsHeld.gui.hpp, over MainWindow.gui.hpp.
#include "projectsHeld.gui.hpp"
#include "../../../src/dialogs/projects/row/ProjectRowDelegate.hpp"

#include <QScreen>

namespace {

  QLabel* shownPreview(QWidget* dlg) {
    for (QLabel* l : dlg->findChildren<QLabel*>())
      if ((l->windowFlags() & Qt::ToolTip) == Qt::ToolTip && l->isVisible()) return l;
    return nullptr;
  }

  QString offScreen(QLabel* p, const QRect& scr) {
    if (!p) return QStringLiteral("no preview shown");
    const QRect g = p->frameGeometry();
    if (scr.contains(g)) return {};
    return QStringLiteral("preview %1,%2 %3x%4 outside screen %5x%6")
        .arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height()).arg(scr.width()).arg(scr.height());
  }

  void moveOver(QWidget* vp, const QPoint& local) {
    const QPoint global = vp->mapToGlobal(local);
    QCursor::setPos(global);
    QMouseEvent mv(QEvent::MouseMove, QPointF(local), QPointF(global), Qt::NoButton, Qt::NoButton,
                   Qt::NoModifier);
    QApplication::sendEvent(vp, &mv);
    QTest::qWait(30);
  }

}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void altMagnifierStaysOnScreenAtEveryCorner() {
    MainWindow win(nullptr, false);
    QVERIFY(showForProjects(win));
    QImage img(2400, 1600, QImage::Format_RGB32);
    img.fill(QColor("#3a7bd5"));
    QString last;
    for (int i = 0; i < 22; ++i)
      last = win.parts.chatAppliers.addImageProjectEntry(img, QStringLiteral("zoom-%1").arg(i));
    QVERIFY(!last.isEmpty());

    QStringList failures;
    QTimer::singleShot(0, [&] {
      const ProjectsWindow w = projectsWindow();
      if (!w.list) { failures << "no Projects window"; return; }
      QListWidgetItem* it = rowFor(w.list, last);
      for (int i = 0; i < 500 && it && it->data(Qt::UserRole + 2).value<QPixmap>().isNull(); ++i)
        QTest::qWait(10);
      const auto* del = static_cast<stencil::gui::ProjectRowDelegate*>(w.list->itemDelegate());
      QWidget* vp = w.list->viewport();
      const QRect scr = w.dlg->screen()->availableGeometry();
      // The thumbnail is carried to each corner by moving the whole window under it.
      const QPoint corners[] = {scr.bottomRight() - QPoint(12, 12), scr.bottomLeft() + QPoint(12, -12),
                                scr.topRight() + QPoint(-12, 12), scr.topLeft() + QPoint(12, 12)};
      for (const QPoint& corner : corners) {
        w.list->scrollToItem(it);
        QTest::qWait(40);
        const QRect icon = del->iconRectFor(w.list->row(it));
        if (!icon.isValid()) { failures << "the last row's thumbnail never painted"; return; }
        const QPoint at = icon.center();
        w.dlg->move(w.dlg->pos() + corner - vp->mapToGlobal(at));
        QTest::qWait(40);
        moveOver(vp, at);
        QLabel* p = shownPreview(w.dlg);
        if (const QString e = offScreen(p, scr); !e.isEmpty()) failures << "shown: " + e;
        if (!p) continue;
        const QSize small = p->size();
        QTest::keyPress(w.dlg, Qt::Key_Alt);
        QTest::qWait(30);
        if (p->size().width() <= small.width()) failures << "Alt did not grow the preview";
        if (const QString e = offScreen(p, scr); !e.isEmpty()) failures << "Alt held: " + e;
        QTest::keyRelease(w.dlg, Qt::Key_Alt);
        QTest::qWait(30);
        if (const QString e = offScreen(p, scr); !e.isEmpty()) failures << "Alt released: " + e;
        moveOver(vp, QPoint(vp->width() - 4, at.y()));
      }
      rejectModal();
    });
    win.projectHost.flows.openProjects();
    QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral("; "))));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsZoom.gui.moc"
