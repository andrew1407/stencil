// MainWindow GUI e2e — The DESCRIPTION & ATTRIBUTES cluster and the one gate it sits behind.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowPaint.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // DESCRIPTION & ATTRIBUTES (browser parity): the cluster sits between IMAGE and
  // PROJECTS with Description · Keywords · Links in that order, and all three follow ONE
  // rule — a saved, non-incognito project — with the reason on the tooltip otherwise.
  // Links used to live in IMAGE and gate on an image; it moved with the browser's.
  void descriptionSectionFollowsImageAndGatesOnASavedProject() {
    MainWindow win(nullptr, false);
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QWidget* section = nullptr;
    QWidget* image = nullptr;
    QWidget* projects = nullptr;
    for (QLabel* l : win.findChildren<QLabel*>("sectionLabel")) {
      if (l->text() == QLatin1String("DESCRIPTION & ATTRIBUTES")) section = l->parentWidget();
      else if (l->text() == QLatin1String("IMAGE")) image = l->parentWidget();
      else if (l->text() == QLatin1String("PROJECTS")) projects = l->parentWidget();
    }
    QVERIFY2(section, "no DESCRIPTION & ATTRIBUTES section on the toolbar");
    QVERIFY(image && projects);
    QCOMPARE(section->parentWidget(), image->parentWidget());   // the same row
    QVERIFY2(section->x() > image->x() && section->x() < projects->x(),
             "the section is not between IMAGE and PROJECTS");
    QList<QAction*> got;
    for (QToolButton* b : section->findChildren<QToolButton*>())
      if (b->defaultAction()) got << b->defaultAction();
    const QList<QAction*> want{win.actDescription_, win.actKeywords_, win.actLinks_};
    QCOMPARE(got, want);
    QVERIFY2(!win.imageSection_->isAncestorOf(win.buttonForAction(win.actLinks_)),
             "Links is still in the IMAGE section");
    // Menu bar: the trio sits together where Links lives.
    QMenu* projectMenu = nullptr;
    for (QMenu* m : win.menuBar()->findChildren<QMenu*>())
      if (m->actions().contains(win.actLinks_)) projectMenu = m;
    QVERIFY(projectMenu);
    const int di = projectMenu->actions().indexOf(win.actDescription_);
    QVERIFY(di >= 0);
    QCOMPARE(projectMenu->actions().at(di + 1), win.actKeywords_);
    QCOMPARE(projectMenu->actions().at(di + 2), win.actLinks_);
    // The shared registry's chords are on the actions.
    QCOMPARE(win.actDescription_->shortcut(), QKeySequence(win.hotkey("openDescription", "Alt+Shift+D")));
    QCOMPARE(win.actKeywords_->shortcut(), QKeySequence(win.hotkey("openKeywords", "Alt+Shift+K")));
    // …and the popover gestures reach all three.
    for (QAction* a : want) QVERIFY(win.pop_.dialogActions.contains(a));

    // No project: all three dead, each with its reason on the tooltip.
    const auto reasonShown = [](QAction* a) {
      return a->toolTip().contains("\n— " + a->property(stencil::gui::TIP_REASON_PROPERTY).toString());
    };
    for (QAction* a : want) {
      QVERIFY2(!a->isEnabled(), qPrintable(a->text() + " is enabled with no project"));
      QVERIFY2(reasonShown(a), qPrintable(a->text() + ": no reason on the tooltip"));
    }
    QCOMPARE(win.actDescription_->property(stencil::gui::TIP_REASON_PROPERTY).toString(),
             QStringLiteral("Save the project first to add a description"));
    QCOMPARE(win.actKeywords_->property(stencil::gui::TIP_REASON_PROPERTY).toString(),
             QStringLiteral("Save the project first to add keywords"));
    QCOMPARE(win.actLinks_->property(stencil::gui::TIP_REASON_PROPERTY).toString(),
             QStringLiteral("Save the project first to add links"));

    // A saved project: all three live, the reason gone.
    // Idempotent against the persisted test store: a copy left by an earlier run (the
    // dialog writes through fileStore) would be found first and carry the "After".
    win.projectList_.erase(std::remove_if(win.projectList_.begin(), win.projectList_.end(),
                                          [](const stencil::gui::Project& p) { return p.meta.id == "meta-gui"; }),
                           win.projectList_.end());
    stencil::gui::Project pr;
    pr.meta.id = "meta-gui";
    pr.meta.name = "Meta";
    pr.meta.description = "Before";
    win.projectList_.push_back(pr);
    win.activeProjectId_ = "meta-gui";
    win.refreshActions();
    for (QAction* a : want) {
      QVERIFY2(a->isEnabled(), qPrintable(a->text() + " is dead with a saved project"));
      QVERIFY2(!reasonShown(a), qPrintable(a->text() + ": the reason lingers"));
    }
    // Incognito takes them away again.
    win.actIncognito_->setChecked(true);
    for (QAction* a : want) QVERIFY2(!a->isEnabled(), qPrintable(a->text() + " survives incognito"));
    win.actIncognito_->setChecked(false);
    for (QAction* a : want) QVERIFY(a->isEnabled());

    // The dialogs open pre-filled and write back through the store.
    QTimer::singleShot(0, [&] {
      QDialog* dlg = nullptr;
      for (int i = 0; i < 200 && !dlg; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) QTest::qWait(10);
      }
      QVERIFY(dlg);
      QCOMPARE(dlg->objectName(), QStringLiteral("stencilDescriptionDialog"));
      auto* area = dlg->findChild<QPlainTextEdit*>("descriptionText");
      // Dismiss whatever happens: an early return leaves exec() spinning until the
      // QtTest watchdog aborts the binary, which reads as a crash, not as this failure.
      if (area) {
        QCOMPARE(area->toPlainText(), QStringLiteral("Before"));
        area->setPlainText("After");
      }
      QPushButton* save = dlg->findChild<QPushButton*>("descriptionSave");
      if (save) save->click(); else dlg->reject();
      QVERIFY(area);
      QVERIFY(save);
    });
    win.actDescription_->trigger();
    QTRY_COMPARE(QString::fromStdString(win.findProject("meta-gui")->meta.description),
                 QStringLiteral("After"));
    QTimer::singleShot(0, [&] {
      QDialog* dlg = nullptr;
      for (int i = 0; i < 200 && !dlg; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) QTest::qWait(10);
      }
      QVERIFY(dlg);
      QCOMPARE(dlg->objectName(), QStringLiteral("stencilKeywordsDialog"));
      // The field is chips now: type a keyword, Enter ADDS it, Save writes the list.
      // Whatever a branch does, the modal must be dismissed — an early return here hangs
      // exec() until the QtTest watchdog aborts the whole binary.
      auto* input = dlg->findChild<QLineEdit*>("keywordsInput");
      if (input) {
        input->setText("Kitchen Plan");
        QTest::keyClick(input, Qt::Key_Return);
        input->setText("plan");
        QTest::keyClick(input, Qt::Key_Return);
      }
      QPushButton* save = dlg->findChild<QPushButton*>("keywordsSave");
      if (save) save->click(); else dlg->reject();
      QVERIFY(input);
      QVERIFY(save);
    });
    win.actKeywords_->trigger();
    // "Kitchen Plan" is ONE keyword, not two; the later "plan" is its own, listed first.
    QTRY_COMPARE(win.findProject("meta-gui")->meta.keywords,
                 std::vector<std::string>({"plan", "kitchen plan"}));
    // …and leave no trace in the store for the next run.
    win.projectList_.erase(std::remove_if(win.projectList_.begin(), win.projectList_.end(),
                                          [](const stencil::gui::Project& p) { return p.meta.id == "meta-gui"; }),
                           win.projectList_.end());
    stencil::gui::fileStore::saveProjects(win.projectList_);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbarDescription.gui.moc"
