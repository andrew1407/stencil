// MainWindow GUI e2e — Projects' Blank image, then Create blank over a picture: the Replace question is
// asked over the Open dialog, still up, so its dust draws on top rather than under Projects on the editor;
// cancelling it goes back to that dialog, on top and keyed, as the browser's does (user report).
#include "projectsHeld.gui.hpp"

#include <QPushButton>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  static QPushButton* buttonIn(QWidget* in, const QString& text) {
    if (in) for (QPushButton* b : in->findChildren<QPushButton*>()) if (b->text() == text) return b;
    return nullptr;
  }
  // The modal up now that is not `but`, polled while the flow raises it.
  static QDialog* nextModal(QDialog* but) {
    QDialog* top = nullptr;
    for (int i = 0; i < 300 && (!top || top == but); ++i) {
      top = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      if (!top || top == but) QTest::qWait(10);
    }
    return top == but ? nullptr : top;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void theReplaceQuestionIsAskedOverTheOpenDialog() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    MainWindow win(nullptr, false);
    QVERIFY(held::showForProjects(win));
    QVERIFY(openLoaded(win));   // a picture, so Create blank asks before replacing it
    QWidget* replaceOwner = nullptr;
    QDialog* openDialog = nullptr;
    bool projectsStayed = false, openStayed = false, openOnTop = false;
    QTimer::singleShot(0, &win, [&] {
      QDialog* projects = held::projectsWindow().dlg;
      QPushButton* blank = buttonIn(projects, QStringLiteral("Blank image"));
      if (!blank) { held::rejectModal(); return; }
      QTimer::singleShot(0, &win, [&, projects] {
        QDialog* open = nextModal(projects);
        QPushButton* create = buttonIn(open, QStringLiteral("Create blank"));
        if (!create) { if (open) open->reject(); return; }
        openDialog = open;
        QTimer::singleShot(0, &win, [&, projects, open] {
          QDialog* ask = nextModal(projects);
          for (int i = 0; i < 50 && ask == open; ++i) { QTest::qWait(10); ask = nextModal(projects); }
          if (ask && ask != open) {
            replaceOwner = ask->parentWidget() ? ask->parentWidget()->window() : nullptr;
            projects->activateWindow();   // macOS keys the window beneath as the question closes
            ask->reject();
          }
        });
        create->click();
        openStayed = open->isVisible();
        for (int i = 0; i < 50 && !open->isActiveWindow(); ++i) QTest::qWait(10);
        openOnTop = open->isActiveWindow();
        open->reject();
      });
      blank->click();
      projectsStayed = projects->isVisible();
      projects->reject();
    });
    win.projectHost.flows.openProjects();
    QVERIFY2(replaceOwner, "the Replace question never came up");
    QVERIFY2(replaceOwner == openDialog, "the Replace question is not asked over the still-open Open dialog");
    QVERIFY2(openStayed, "cancelling the Replace question closed the Open dialog");
    QVERIFY2(openOnTop, "the Open dialog fell behind the Projects window once the question closed");
    QVERIFY2(projectsStayed, "the Projects window closed");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsBlankFlight.gui.moc"
