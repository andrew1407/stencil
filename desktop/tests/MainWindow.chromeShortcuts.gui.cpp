// MainWindow GUI e2e — Window shortcuts, the status hint, routine actions that never toast, and close.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

#include <QDirIterator>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A window opens ready to be typed into: the caret lands in its search box, as the browser's
  // windows do (their shells carry focusOnOpen).
  void aWindowOpensWithTheCaretInItsSearchBox() {
    MainWindow win(nullptr, false);
    openLoaded(win);
    bool focused = false, checked = false;
    QTimer::singleShot(0, &win, [&] {
      QDialog* shown = nullptr;
      for (QDialog* d : win.findChildren<QDialog*>())
        if (d->isVisible()) shown = d;
      if (shown) {
        // The 0-timer the dialog itself queues lands first; give it one turn.
        QTest::qWait(30);
        QWidget* f = shown->focusWidget();
        focused = qobject_cast<QLineEdit*>(f) != nullptr;
        checked = true;
        shown->reject();
      }
    });
    win.actShortcuts->trigger();
    QVERIFY2(checked, "the shortcuts window opened");
    QVERIFY2(focused, "its search box holds the caret");
  }

  // A window shortcut pressed while that window is up CLOSES it, and another window's shortcut SWAPS
  // to it. The dialog carries its own copies: a modal loop never lets the window's actions fire.
  void windowShortcutsToggleAndSwap() {
    MainWindow win(nullptr, false);
    openLoaded(win);
    QVERIFY2(!win.actInfo->shortcut().isEmpty(), "help carries a shortcut");
    QVERIFY2(!win.actShortcuts->shortcut().isEmpty(),
             "the shortcuts window has one of its own now");
    QVERIFY2(!win.actSettings->shortcut().isEmpty(), "…and so does Settings");
    QVERIFY(win.hotkeyActions.contains(QStringLiteral("openHotkeys")));
    QVERIFY(win.hotkeyActions.contains(QStringLiteral("openVisuals")));
    QVERIFY(win.hotkeyActions.contains(QStringLiteral("openAssistantSettings")));
    QVERIFY2(!win.actInfo->shortcut().toString().contains(QStringLiteral("F1")),
             "help left the lone F1 for the Alt+letter family");

    int settingsAsked = 0;
    connect(win.actSettings, &QAction::triggered, &win, [&] { settingsAsked++; });

    bool sawOwn = false, sawOther = false, parked = false;
    QTimer::singleShot(0, &win, [&] {
      QDialog* shown = nullptr;
      for (QDialog* d : win.findChildren<QDialog*>())
        if (d->isVisible()) shown = d;
      QVERIFY(shown);
      // While it is up, the main window's own copies of these chords are parked, so the two
      // cannot fire ambiguously at each other.
      parked = win.actInfo->shortcutContext() == Qt::WidgetShortcut;
      // Its own chord…
      for (QShortcut* sc : shown->findChildren<QShortcut*>()) {
        if (sc->key() == win.actInfo->shortcut()) { sawOwn = true; emit sc->activated(); }
      }
      QVERIFY2(!shown->isVisible(), "its own shortcut closed the window");
      // …and another window's chord is wired too, queued to open after this one unwinds.
      for (QShortcut* sc : shown->findChildren<QShortcut*>())
        if (sc->key() == win.actSettings->shortcut()) sawOther = true;
    });
    win.openInfo();

    QVERIFY2(sawOwn, "the dialog carried its own opener's chord");
    QVERIFY2(sawOther, "…and the other windows' chords, for swapping");
    QVERIFY2(parked, "the main window's duplicate was parked while the dialog owned it");
    QVERIFY2(win.actInfo->shortcutContext() != Qt::WidgetShortcut,
             "…and handed back when the dialog closed");
    QCOMPARE(settingsAsked, 0);   // nothing was swapped to in this pass
    beat();
  }

  // image size plus, while incognito, the "not saved" line. Those two facts and
  // nothing else (the canvas pill that used to say it is gone).
  void statusHintCarriesSizeAndIncognitoOnly() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QLabel* hint = win.statusHint;
    QVERIFY(hint);
    QCOMPARE(hint->text(), QStringLiteral("?"));
    // It is the COLLAPSED state's readout: with the tool rows up, the size line under them
    // already says this, so the "?" would only repeat it.
    QVERIFY2(!hint->isVisible(), "the hint stays out of the way while the tool rows are up");
    const QString size =
        QString("%1 × %2 px").arg(canvas->imageWidth()).arg(canvas->imageHeight());
    QVERIFY2(hint->toolTip().contains(size), "the bubble names the image size");
    QCOMPARE(hint->toolTip().split('\n').size(), 1);   // size only, nothing else

    // Collapsed tool rows: the header row stays, and NOW the hint appears — with the size
    // line hidden alongside the rows, its bubble is the only place these facts are left.
    win.actToolbars->setChecked(false);
    QTRY_VERIFY(!win.actToolbars->isChecked());
    // Exactly ONE of the two readouts is up — polled for, not timed: the size line reads as gone only
    // once the fold's finish step hides the rows, and the fold's duration is not this test's business.
    QTRY_VERIFY_WITH_TIMEOUT(hint->isVisible() && !win.imageSizeInfo->isVisible(), 3000);
    QVERIFY2(hint->toolTip().contains(size), "…still carrying the size");

    // Incognito adds its line — and only its line.
    win.actIncognito->setChecked(true);
    QTRY_VERIFY(win.incognito);
    const QStringList lines = hint->toolTip().split('\n');
    QCOMPARE(lines.size(), 2);
    QVERIFY2(lines[0].contains(size), "the size stays first");
    QCOMPARE(lines[1], QStringLiteral("Incognito — not saved"));

    // …and it leaves again when incognito does.
    win.actIncognito->setChecked(false);
    QTRY_VERIFY(!win.incognito);
    QVERIFY2(!hint->toolTip().contains("Incognito"),
             "the incognito line goes with the mode");
    QCOMPARE(hint->toolTip().split('\n').size(), 1);
    win.actToolbars->setChecked(true);
    beat();
  }

  // The desktop stays as quiet as the browser: no toast for a routine success the user can
  // already see (an image appearing, settings applying, the session restoring).
  void routineActionsDoNotToast() {
    // MainWindow's definitions are split across TUs in feature folders — scan them all.
    const QString appDir = QStringLiteral(__FILE__).section('/', 0, -3) + "/src/app";
    QString src;
    QDirIterator it(appDir, {"MainWindow*.cpp", "StencilFileSync.cpp"}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      QFile f(it.next());
      QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable("cannot read " + f.fileName()));
      src += QString::fromUtf8(f.readAll());
    }
    QVERIFY2(!src.isEmpty(), "could not read the mainWindow sources");
    const QStringList banned{"Image loaded", "Image opened", "Opened from Stencil", "Opening…",
                             "Restored last session", "Session saved", "Settings saved",
                             "Assistant settings saved", "Shortcuts updated", "Blank recoloured",
                             "Crop canceled", "Blank image canceled"};
    for (const QString& msg : banned)
      QVERIFY2(!src.contains("notify->success(\"" + msg) && !src.contains("notify->info(\"" + msg),
               qPrintable(QString("\"%1\" has no twin in the browser — it should not toast").arg(msg)));
    // …while the ones the browser DOES show are still there.
    QVERIFY2(src.contains("Project saved"), "Project saved has a browser twin and must stay");
    QVERIFY2(src.contains("Image cropped"), "Image cropped has a browser twin and must stay");
  }

  // Closing is IMMEDIATE on every path — no "Quit Stencil?" confirmation for close() (the ✕, ⌘Q,
  // app-menu, Dock-Quit and Alt+F4 equivalents); the window simply closes.
  void closeHasNoConfirmation() {
    MainWindow win(nullptr, false);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    bool sawDialog = false;
    QTimer::singleShot(300, &win, [&win, &sawDialog] {
      if (auto* box = win.findChild<QMessageBox*>()) {
        sawDialog = true;
        box->reject();  // unblock if a dialog wrongly appeared
      }
    });
    win.close();
    QTRY_VERIFY(!win.isVisible());
    QVERIFY(!sawDialog);
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chromeShortcuts.gui.moc"
