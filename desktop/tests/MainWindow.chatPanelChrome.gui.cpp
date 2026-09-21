// MainWindow GUI e2e — The dock's chrome: its header row, the save disclosure at its toggle, and the panel op.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // §12.2: the save-chats toggle has to say who can READ a saved chat, right where it is offered;
  // a server project's chat file carries the project's own access. LlmSettingsForm is the host.
  void chatSaveDisclosureSitsAtTheToggle() {
    const stencil::gui::Settings defaults;
    stencil::gui::LlmSettingsForm form(defaults,
                                       stencil::gui::LlmSettingsForm::RowMode::HIDE_ROWS);

    auto* cb = form.findChild<QCheckBox*>("llmSaveChats");
    QVERIFY2(cb, "the §12 save-chats opt-in is missing from the assistant settings");
    QVERIFY2(!cb->isChecked(), "chat persistence ships OFF — it is an explicit opt-in");

    // Visible, not hover-only: a tooltip nobody opens does not disclose anything.
    auto* hint = form.findChild<QLabel*>("llmSaveChatsHint");
    QVERIFY2(hint, "the sharing consequence has no visible label at the toggle");
    QVERIFY2(hint->text().contains("shared with"),
             qPrintable("the visible hint does not state who can read it: " + hint->text()));
    QVERIFY2(!hint->text().isEmpty() && hint->isVisibleTo(&form),
             "the hint is present but not shown alongside the checkbox");

    // The tooltip carries it too, with the local-vs-server split spelled out.
    const QString tip = cb->toolTip();
    QVERIFY2(tip.contains("shared with"), qPrintable("tooltip omits the sharing rule: " + tip));
    QVERIFY2(tip.contains("Local projects"), qPrintable("tooltip omits the local case: " + tip));

    // …and the box it sits in reads as the NEXT row, not a hole in the form (user report; the
    // browser has no such gap between .vs-checks and .chat-cors-note).
    form.resize(420, form.sizeHint().height());
    form.show();
    QVERIFY(QTest::qWaitForWindowExposed(&form));
    auto* noteBox = form.findChild<QFrame*>("llmNoteBox");
    QVERIFY2(noteBox, "the disclosure note has no box to sit in");
    const int gap = noteBox->mapTo(&form, QPoint(0, 0)).y() - (cb->mapTo(&form, QPoint(0, 0)).y() + cb->height());
    QVERIFY2(gap >= 0 && gap < 40,
             qPrintable(QStringLiteral("checkbox-to-note gap is %1px").arg(gap)));
    beat();
  }

  // The chat header reads as chrome, not an accent badge: the "Assistant" label and its mark take
  // the theme text colour, and the current placement button is the browser's .chat-dock-btn-active.
  void chatHeaderMatchesTheBrowserChrome() {
    MainWindow win;
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat->setChecked(true);
    awaitAnim(win.chatAnim);
    auto* dock = win.findChild<stencil::gui::ChatDock*>();
    QVERIFY2(dock, "no chat dock");
    QLabel* title = dock->findChild<QLabel*>("chatHeaderTitle");
    QVERIFY2(title, "no header title");
    const stencil::gui::Palette pal = stencil::gui::themePalette(
        stencil::gui::resolveDark(win.settings.themeMode), win.settings.accentColor);
    QVERIFY2(title->styleSheet().contains(pal.textMain.name()),
             qPrintable("the Assistant label is not theme text: " + title->styleSheet()));
    QVERIFY2(!title->styleSheet().contains(pal.accent.name()),
             "the Assistant label still paints in the accent");
    // The active placement chip: container ground, and never the accent fill.
    bool sawActive = false;
    for (QToolButton* b : dock->findChildren<QToolButton*>()) {
      if (b->objectName() == QLatin1String("chatJumpBtn")) continue;  // transcript jump pills, not chips
      const QString qss = b->styleSheet();
      if (!qss.contains("background:")) continue;
      sawActive = true;
      QVERIFY2(qss.contains(pal.bgContainer.name()),
               qPrintable("the active placement button is not a container chip: " + qss));
      QVERIFY2(!qss.contains(pal.accent.name()),
               qPrintable("the active placement button is still accent-filled: " + qss));
    }
    QVERIFY2(sawActive, "no placement button is marked active");
    // The glyph must actually FILL its chip: the app-wide QToolButton rule pads 5x7 and reserves a
    // border, which inside a fixed 23px button squeezed the 13px mark down to ~4px.
    {
      QWidget* bar = dock->findChild<QWidget*>("chatTitleBar");
      QVERIFY(bar);
      const QImage im = bar->grab().toImage();
      const QColor ground = im.pixelColor(im.width() - 2, 1);
      // Measured INSIDE one button's own rect, so the bold "Assistant" label cannot stand
      // in for a glyph (it did, and the negative control passed).
      QToolButton* up = nullptr;
      for (QToolButton* b : bar->findChildren<QToolButton*>())
        if (b->toolTip().startsWith("Dock top")) up = b;
      QVERIFY2(up, "no dock-top button");
      const QRect r(up->mapTo(bar, QPoint(0, 0)), up->size());
      int widest = 0, run = 0;
      for (int x = r.left(); x <= r.right(); ++x) {
        bool ink = false;
        for (int y = r.top(); y <= r.bottom() && !ink; ++y) {
          const QColor c = im.pixelColor(x, y);
          ink = qAbs(c.red() - ground.red()) + qAbs(c.green() - ground.green())
              + qAbs(c.blue() - ground.blue()) > 40;
        }
        run = ink ? run + 1 : 0;
        widest = qMax(widest, run);
      }
      QVERIFY2(widest >= 6, qPrintable(QString("the chevron is squeezed: %1px of ink in a %2px chip")
                                           .arg(widest).arg(r.width())));
    }
    // Geometry parity with .chat-hbtn: a 23px chip holding a 13px glyph, 1px apart.
    for (QToolButton* b : dock->findChildren<QToolButton*>()) {
      if (!b->toolTip().contains("Dock ") && !b->toolTip().startsWith("Float")
          && !b->toolTip().startsWith("Close")) continue;
      QCOMPARE(b->size(), QSize(23, 23));
      QCOMPARE(b->iconSize(), QSize(13, 13));
      // Never disabled: QToolButton:disabled would repaint the active chip as a dead
      // bordered square with a dimmed glyph, which is what made the row look murky.
      QVERIFY2(b->isEnabled(), qPrintable("header button is disabled: " + b->toolTip()));
      QVERIFY2(!b->styleSheet().contains("border:1px"),
               qPrintable("header button has a border: " + b->styleSheet()));
    }
  }

  // §10 chatPanel: the assistant panel's own placement, driven by a plan (browser opPlan.js
  // chatPanel parity), run through the real parser and executor, not the target method alone.
  void chatPanelOpDocksAndOpensThePanel() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());

    const auto run = [&win](const char* json) {
      const stencil::llm::OpPlanResult r = stencil::llm::parseOpPlan(QString::fromUtf8(json));
      QVERIFY2(r.ok, qPrintable(r.error));
      stencil::gui::ChatPlanTarget target(win);
      const stencil::llm::ExecResult res = stencil::llm::executePlan(r.plan, target);
      QVERIFY2(res.ok, qPrintable(res.error));
    };

    // A dock with no "open" moves it AND shows it — placing a panel nobody can see is
    // not what was asked for.
    run(R"({"reply":"ok","actions":[{"op":"chatPanel","dock":"right"}]})");
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(chat->isChecked());
    // The open and the side switch both FLY (chatSurfaceFlight) — the area is what it
    // settles at, not what it holds mid-flight.
    QTRY_VERIFY(!win.chatAnim);
    QVERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);

    // …the other sides go through the same path as the title bar's own buttons.
    run(R"({"reply":"ok","actions":[{"op":"chatPanel","dock":"bottom"}]})");
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::BottomDockWidgetArea);
    // The side switch flies (chatSurfaceFlight) and its finish SHOWS the dock again —
    // let it land before asking for a close, exactly as a user's second sentence would.
    QTRY_VERIFY(!win.chatAnim);

    // "open": false closes it and leaves the placement alone.
    run(R"({"reply":"ok","actions":[{"op":"chatPanel","open":false}]})");
    QTRY_VERIFY(!dock->isVisible());
    QVERIFY(!chat->isChecked());

    // …and "float" lifts it off the edges.
    run(R"({"reply":"ok","actions":[{"op":"chatPanel","open":true,"dock":"float"}]})");
    QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY(dock->isFloating());

    // A field-less chatPanel says nothing and is rejected by the PARSER, so no plan
    // reaches the editor at all.
    const auto bad = stencil::llm::parseOpPlan(
        QStringLiteral(R"({"reply":"ok","actions":[{"op":"chatPanel"}]})"));
    QVERIFY2(!bad.ok, "a chatPanel with neither open nor dock must not parse");
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatPanelChrome.gui.moc"
