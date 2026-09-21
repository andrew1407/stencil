// MainWindow GUI e2e — the compact "mini window" a dialog icon opens (MainWindow::execMaybePopover):
// the SAME QDialog the menus open as a window, reparented into the popover overlay as a plain
// Qt::Widget child, so the modal shell's header drag must not move it out of that overlay.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

#include <QContextMenuEvent>
#include "OpenImageDialog.hpp"
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QTabWidget>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Right-click on a dialog icon is the popover gesture (MainWindowToolbarSections
  // wirePopover). The open blocks in a nested loop, so the drag runs from a timer inside it.
  void compactPopoverStaysPutWhenItsHeaderIsDragged() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QToolButton* icon = nullptr;
    for (auto it = win.pop.buttons.cbegin(); it != win.pop.buttons.cend(); ++it) {
      auto* b = static_cast<QToolButton*>(it.key());
      if (b->isVisible() && it.value()->isEnabled()) { icon = b; break; }
    }
    QVERIFY2(icon, "the tool rows must carry at least one popover-wired dialog icon");

    bool sawPopover = false, wasWindow = true;
    QRect before, after;
    QTimer::singleShot(250, &win, [&] {
      QDialog* dlg = win.pop.active;
      QWidget* overlay = win.pop.overlay;
      if (!dlg || !overlay) { win.dismissPopover(); return; }
      sawPopover = true;
      wasWindow = dlg->isWindow();

      QWidget* header = dlg->findChild<QWidget*>(QStringLiteral("modalHeader"));
      if (!header) { win.dismissPopover(); return; }
      before = dlg->geometry();   // the DIALOG is what move() shifts, inside its overlay

      // A real drag of the header: press on it, travel, release.
      const QPoint grab = header->mapToGlobal(QPoint(30, 12));
      const QPointF local(30, 12);
      QMouseEvent press(QEvent::MouseButtonPress, local, QPointF(grab), Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(header, &press);
      for (const QPoint step : {QPoint(30, 20), QPoint(90, 70), QPoint(160, 120)}) {
        QMouseEvent move(QEvent::MouseMove, local, QPointF(grab + step), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(header, &move);
      }
      QMouseEvent release(QEvent::MouseButtonRelease, local, QPointF(grab + QPoint(160, 120)),
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(header, &release);

      after = dlg->geometry();
      win.dismissPopover();
    });

    QContextMenuEvent ctx(QContextMenuEvent::Mouse, icon->rect().center(),
                          icon->mapToGlobal(icon->rect().center()));
    QApplication::sendEvent(icon, &ctx);
    settle([&] { return sawPopover && !win.pop.active; }, 4000);

    QVERIFY2(sawPopover, "right-click on a dialog icon must open the compact popover");
    QVERIFY2(!wasWindow, "the popover is a child of the overlay, never its own window");
    QCOMPARE(after, before);
  }

  // The same trap one step further in: a dialog that sizes itself to its content and clamps itself to
  // the screen (OpenImageDialog) must do neither as a popover, or it resizes past the overlay's cap.
  void aSelfSizingDialogStaysInsideItsPopoverOverlay() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QToolButton* icon = nullptr;
    for (auto it = win.pop.buttons.cbegin(); it != win.pop.buttons.cend(); ++it) {
      if (it.value() != win.actOpen) continue;
      auto* b = static_cast<QToolButton*>(it.key());
      if (b->isVisible() && it.value()->isEnabled()) { icon = b; break; }
    }
    QVERIFY2(icon, "Open Image must be a popover-wired dialog icon");

    bool sawPopover = false;
    QRect dlgRect, overlayRect;
    QSize minHint;
    QString clipped;
    QPoint dlgPos;
    QTimer::singleShot(400, &win, [&] {
      QDialog* dlg = win.pop.active;
      QWidget* overlay = win.pop.overlay;
      if (!dlg || !overlay) { win.dismissPopover(); return; }
      sawPopover = true;
      dlgRect = dlg->rect();
      minHint = dlg->minimumSizeHint();
      // Anything laid out past the dialog's own right edge is CLIPPED: the scroll body
      // keeps its horizontal bar off, so a row that will not compress simply loses its tail.
      for (QWidget* w : dlg->findChildren<QWidget*>())
        if (w->isVisible() && w->width() > 8 &&
            w->mapTo(dlg, QPoint(w->width(), 0)).x() > dlg->width() + 1)
          clipped += QStringLiteral("%1(%2) right=%3 > %4; ")
                         .arg(w->metaObject()->className(), w->objectName(),
                              QString::number(w->mapTo(dlg, QPoint(w->width(), 0)).x()),
                              QString::number(dlg->width()));
      dlgPos = dlg->pos();
      overlayRect = overlay->rect();
      win.dismissPopover();
    });

    QContextMenuEvent ctx(QContextMenuEvent::Mouse, icon->rect().center(),
                          icon->mapToGlobal(icon->rect().center()));
    QApplication::sendEvent(icon, &ctx);
    settle([&] { return sawPopover && !win.pop.active; }, 6000);

    QVERIFY2(sawPopover, "right-click on Open Image must open the compact popover");
    QCOMPARE(dlgPos, QPoint(0, 0));
    QVERIFY2(dlgRect.width() <= overlayRect.width(),
             "the popover dialog must not grow wider than the overlay framing it");
    QVERIFY2(dlgRect.height() <= overlayRect.height(),
             "the popover dialog must not grow taller than the overlay framing it");
    // …and its CONTENT has to fit that width: a row that cannot compress is laid out wider than the box
    // and loses its tail, where the browser's popover reflows instead.
    QVERIFY2(clipped.isEmpty(), qPrintable("clipped by the popover: " + clipped));
  }
  // A PREVIEW must grow the popover, within its cap (the browser's .modal-popover grows to its own
  // max-height): the compact shape cannot resize itself as a window. A still stands in for a video.
  void aPreviewGrowsTheCompactPopoverInsteadOfBeingClipped() {
    const auto motion = withMotion();   // the height change is EASED; see steps below
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QToolButton* icon = nullptr;
    for (auto it = win.pop.buttons.cbegin(); it != win.pop.buttons.cend(); ++it) {
      if (it.value() != win.actOpen) continue;
      auto* b = static_cast<QToolButton*>(it.key());
      if (b->isVisible() && it.value()->isEnabled()) { icon = b; break; }
    }
    QVERIFY2(icon, "Open Image must be a popover-wired dialog icon");

    bool sawPopover = false;
    int before = 0, after = 0, cap = 0, overlayAfter = 0, away = 0, back = 0, steps = 0;
    QTimer::singleShot(400, &win, [&] {
      auto* dlg = qobject_cast<stencil::gui::OpenImageDialog*>(win.pop.active.data());
      QWidget* overlay = win.pop.overlay;
      if (!dlg || !overlay) { win.dismissPopover(); return; }
      sawPopover = true;
      before = dlg->height();
      cap = dlg->maximumHeight();
      auto* tabs = dlg->findChild<QTabWidget*>(QStringLiteral("oiTabs"));
      tabs->setCurrentIndex(1);
      QWidget* page = tabs->currentWidget();
      QTest::keyClicks(page->findChild<QLineEdit*>(), guiTestImage());
      auto* pv = page->findChild<QPushButton*>();
      settle([&] { return pv->isEnabled(); }, 1000);
      pv->click();
      settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
      settle([] { return false; }, 400);
      after = dlg->height();
      overlayAfter = overlay->height();
      // …and it must come BACK: away to an empty tab, then back to the picture.
      tabs->setCurrentIndex(2);            // Blank — nothing to show
      // Sampled, not settled: the panel and its frame EASE to the new height (a flat
      // resize on the compact shape read as a jump), so the walk down is the assertion.
      int last = dlg->height();
      for (int i = 0; i < 14; ++i) {
        settle([] { return false; }, 35);
        if (dlg->height() != last) { ++steps; last = dlg->height(); }
      }
      away = dlg->height();
      tabs->setCurrentIndex(1);            // URL again, its picture restored from the cache
      settle([] { return false; }, 600);
      back = dlg->height();
      win.dismissPopover();
    });

    QContextMenuEvent ctx(QContextMenuEvent::Mouse, icon->rect().center(),
                          icon->mapToGlobal(icon->rect().center()));
    QApplication::sendEvent(icon, &ctx);
    settle([&] { return sawPopover && !win.pop.active; }, 8000);

    QVERIFY2(sawPopover, "right-click on Open Image must open the compact popover");
    QVERIFY2(after > before,
             qPrintable(QString("the popover stayed %1px for a picture it had to show").arg(after)));
    QVERIFY2(after <= cap, qPrintable(QString("grew to %1px past its %2px cap").arg(after).arg(cap)));
    QCOMPARE(overlayAfter, after);   // the frame grows WITH it, or the picture is clipped
    QVERIFY2(away < after, qPrintable(QString("an empty tab kept %1px of picture room").arg(away)));
    QVERIFY2(steps >= 3, qPrintable(QString("the popover jumped in %1 step(s), not eased").arg(steps)));
    QVERIFY2(back == after,
             qPrintable(QString("came back %1px for the room it needs (%2)").arg(back).arg(after)));
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.popover.gui.moc"
