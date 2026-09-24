// MainWindow GUI e2e — An item cloud goes with the surface it belongs to (DisintegrateOverlay bindToSurface).
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

namespace {
  using Clouds = QList<QPointer<QWidget>>;

  Clouds cloudsUnder(QWidget* root) {
    Clouds out;
    for (QWidget* w : root->findChildren<QWidget*>())
      if (w->objectName() == QLatin1String(stencil::gui::DisintegrateOverlay::OBJECT_NAME)
          || w->objectName() == QLatin1String("stencilFilterDust"))
        out.append(w);
    return out;
  }

  bool noneShowing(const Clouds& clouds) {
    for (const QPointer<QWidget>& c : clouds)
      if (c && !c->isHidden()) return false;
    return true;
  }

  bool allGone(const Clouds& clouds) {
    for (const QPointer<QWidget>& c : clouds)
      if (c) return false;
    return true;
  }
}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Removing rows then closing the Projects dialog at once: the rows' scatter leaves with the dialog.
  void projectsRowCloudsLeaveWithTheDialog() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkYellow);
    QVERIFY(!win.addImageProjectEntry(img, "cloud-owner-row").isEmpty());

    Clouds clouds;
    bool hiddenAtClose = false;
    QTimer::singleShot(0, [&] {
      const auto bailOut = [] {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) d->reject();
      };
      QDialog* dlg = nullptr;
      QListWidget* list = nullptr;
      for (int i = 0; i < 200 && !list; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dlg) list = dlg->findChild<QListWidget*>("projectsList");
        if (!list) QTest::qWait(10);
      }
      if (!list) { bailOut(); return; }
      settle([&] { return !surfaceFlight(&win) && !modalGhost(&win); }, 2500);
      QPushButton* clearBtn = nullptr;
      QPushButton* closeBtn = nullptr;
      for (QPushButton* b : dlg->findChildren<QPushButton*>()) {
        if (b->text().startsWith("Clear All")) clearBtn = b;
        if (b->text() == "Close") closeBtn = b;
      }
      if (!clearBtn || !closeBtn) { bailOut(); return; }
      dismissModal("OK");
      clearBtn->click();
      clouds = cloudsUnder(dlg);
      closeBtn->click();
      hiddenAtClose = noneShowing(clouds);
      bailOut();
    });
    win.openProjects();
    QVERIFY2(!clouds.isEmpty(), "Clear All raised no row cloud to close over");
    QVERIFY2(hiddenAtClose, "a row cloud was still showing as the dialog closed");
    QTest::qWait(20);
    QVERIFY2(allGone(clouds), "a row cloud outlived the dialog it was removed from");
    beat();
  }

  // Clearing the docked chat, then closing it: the cards' scatter must not play on over the canvas.
  void chatCardCloudsLeaveWithTheDock() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = qobject_cast<stencil::gui::ChatDock*>(win.findChild<QDockWidget*>("llmChatDock"));
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY(dock->width() > 200);
    settle([&] { return !surfaceFlight(&win); }, 2000);
    dock->appendUser("make it sepia");
    dock->appendAssistant("done");
    QTest::qWait(400);   // past the cards' arrival

    const Clouds before = cloudsUnder(&win);
    dock->clearConversation();
    Clouds clouds;
    for (const QPointer<QWidget>& c : cloudsUnder(&win))
      if (!before.contains(c)) clouds.append(c);
    QVERIFY2(!clouds.isEmpty(), "clearing the chat raised no card cloud");
    for (const QPointer<QWidget>& c : clouds) {
      auto* fx = dynamic_cast<stencil::gui::DisintegrateOverlay*>(c.data());
      QVERIFY(fx && fx->boundSurface() == dock);
    }

    chat->setChecked(false);
    QTRY_VERIFY_WITH_TIMEOUT(!dock->isVisible(), 800);
    QVERIFY2(noneShowing(clouds), "a card cloud was still showing once the chat had closed");
    QTest::qWait(20);
    QVERIFY2(allGone(clouds), "a card cloud outlived the chat dock it was cleared from");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.motionCloudOwner.gui.moc"
