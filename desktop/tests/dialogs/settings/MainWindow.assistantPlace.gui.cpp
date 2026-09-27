// MainWindow GUI e2e — the assistant settings from every door: centred on the window before and
// after the anthropic rows grow it, deaf to presses inside it and its lists, and closed as a Cancel
// by a press outside. Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "SessionKey.hpp"
#include "../../../src/support/easeWindowHeight.hpp"
#include <QJsonDocument>

using stencil::gui::SearchComboBox;

namespace {
  const QString KEY = QStringLiteral("sk-ant-place-0123456789abcdef");
  // A refused loopback port, kept across the switch: the form probes, never Anthropic itself.
  const QString LOOPBACK = QStringLiteral("http://127.0.0.1:1");
  // The openImage case's own tolerance.
  constexpr int CENTRE_SLACK = 2;
  // Top-left plus half the size, as exec() centres: QRect::center() rounds an odd size down.
  QPoint midOf(const QWidget* w) { return w->geometry().topLeft() + QPoint(w->width() / 2, w->height() / 2); }

  // What one opening showed, read from inside its exec().
  struct Seen {
    bool opened = false;
    QPoint off0, off1;   // dialog centre minus window centre, before / after the anthropic rows
    int h0 = 0, h1 = 0;
    int drift = 0;       // the largest vertical miss while the height eased
    bool keptByInside = false, keptByProviderList = false, keptByModelList = false;
    bool closedByOutside = false;
    int result = -1;
  };

  // Where the outside press lands: a widget and a point in it.
  using Outside = std::function<std::pair<QWidget*, QPoint>()>;

  std::pair<QWidget*, QPoint> onWindowCorner(MainWindow& win) {
    const QPoint at(12, win.height() - 12);
    QWidget* under = win.childAt(at);
    if (!under) return {&win, at};
    return {under, under->mapFrom(&win, at)};
  }

  QPoint offCentre(const QDialog* dlg, const MainWindow& win) {
    return midOf(dlg) - midOf(&win);
  }

  bool pressInList(SearchComboBox* combo, QDialog* dlg) {
    combo->showPopup();
    QListView* list = combo->popupList();
    if (!list || !list->isVisible() || combo->count() == 0) return false;
    const QModelIndex row = list->currentIndex().isValid() ? list->currentIndex() : list->model()->index(0, 0);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, list->visualRect(row).center());
    QTest::qWait(40);
    return dlg->isVisible();
  }

  Seen probe(MainWindow& win, const std::function<void()>& open, const Outside& outside) {
    Seen s;
    bool done = false;
    QTimer poll;
    poll.setInterval(10);
    QObject::connect(&poll, &QTimer::timeout, [&] {
      auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      if (!dlg || dlg->objectName() != QLatin1String("assistantSettingsDialog")) return;
      poll.stop();
      s.opened = true;
      QTest::qWait(60);
      s.off0 = offCentre(dlg, win);
      s.h0 = dlg->height();
      auto* provider = static_cast<SearchComboBox*>(dlg->findChild<QComboBox*>("llmProvider"));
      provider->setCurrentIndex(provider->findData(QStringLiteral("anthropic")));
      QElapsedTimer t;
      t.start();
      while (t.elapsed() < stencil::support::WINDOW_RESIZE_MS + 200) {
        s.drift = std::max(s.drift, std::abs(offCentre(dlg, win).y()));
        QTest::qWait(12);
      }
      s.off1 = offCentre(dlg, win);
      s.h1 = dlg->height();
      QTest::keyClicks(dlg->findChild<QLineEdit*>("llmAnthropicKey"), KEY);
      QTest::mouseClick(dlg, Qt::LeftButton, {}, QPoint(6, dlg->height() / 2));
      QTest::qWait(40);
      s.keptByInside = dlg->isVisible();
      s.keptByProviderList = pressInList(provider, dlg);
      auto* model = static_cast<SearchComboBox*>(dlg->findChild<QComboBox*>("llmModel"));
      model->addItem(QStringLiteral("claude-place-probe"));
      s.keptByModelList = pressInList(model, dlg);
      const auto [target, at] = outside();
      QTest::mouseClick(target, Qt::LeftButton, {}, at);
      s.closedByOutside = !dlg->isVisible();
      s.result = dlg->result();
      if (dlg->isVisible()) dlg->reject();
      done = true;
    });
    poll.start();
    open();
    settle([&] { return done; }, 8000);
    poll.stop();
    return s;
  }
}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  // Sized inside the offscreen screen (800x800): exec() keeps a dialog on it, off a larger window's centre.
  static void showFitted(MainWindow& win) {
    win.resize(780, 740);
    win.show();
    win.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.llmProvider = QStringLiteral("ollama");
    win.settings.llmBaseUrl = LOOPBACK;
  }

  static QByteArray savedNow() {
    return QJsonDocument(stencil::gui::fileStore::settingsToJson(stencil::gui::fileStore::loadSettings())).toJson();
  }

  static void verify(const Seen& s, MainWindow& win, const QByteArray& before, const char* door) {
    const auto near = [](QPoint p) { return std::abs(p.x()) <= CENTRE_SLACK && std::abs(p.y()) <= CENTRE_SLACK; };
    const auto say = [door](const QString& what) { return QStringLiteral("%1: %2").arg(door, what); };
    QVERIFY2(s.opened, qPrintable(say("the assistant settings never opened")));
    QVERIFY2(near(s.off0), qPrintable(say(QString("opened off the window's centre by (%1,%2)")
                                              .arg(s.off0.x()).arg(s.off0.y()))));
    QVERIFY2(s.h1 > s.h0, qPrintable(say("the anthropic rows did not make it taller")));
    QVERIFY2(near(s.off1), qPrintable(say(QString("the anthropic rows moved it off-centre by (%1,%2)")
                                              .arg(s.off1.x()).arg(s.off1.y()))));
    QVERIFY2(s.drift <= CENTRE_SLACK, qPrintable(say(QString("it strayed %1px while easing").arg(s.drift))));
    QVERIFY2(s.keptByInside, qPrintable(say("a press inside the dialog closed it")));
    QVERIFY2(s.keptByProviderList, qPrintable(say("a press in the provider list closed it")));
    QVERIFY2(s.keptByModelList, qPrintable(say("a press in the model list closed it")));
    QVERIFY2(s.closedByOutside, qPrintable(say("a press outside left it open")));
    QCOMPARE(s.result, int(QDialog::Rejected));
    QCOMPARE(win.settings.llmProvider, QStringLiteral("ollama"));
    QVERIFY2(stencil::llm::SessionKey::instance().key().isEmpty(), qPrintable(say("the typed key was taken")));
    QCOMPARE(savedNow(), before);
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }
  void init() { stencil::llm::SessionKey::instance().forget(); }

  // The chord and View ▸ AI Assistant Settings… with no chat surface up.
  void chordAndViewMenuCentreAndCancelOutside() {
    MainWindow win(nullptr, false);
    showFitted(win);
    QVERIFY(QTest::qWaitForWindowActive(&win));   // a WindowShortcut needs the active window
    const QByteArray before = savedNow();
    const Outside corner = [&win] { return onWindowCorner(win); };
    verify(probe(win, [&] { QTest::keySequence(&win, win.acts.assistantSettings->shortcut()); }, corner),
           win, before, "Alt+Shift+G");
    QAction* viewRow = actionByText(&win, QStringLiteral("AI Assistant Settings…"));
    QVERIFY(viewRow);
    verify(probe(win, [&] { viewRow->trigger(); }, corner), win, before, "View menu");
  }

  // The docked chat's "…" Settings row and its Configure-provider card.
  void dockedChatDoorsCentreAndCancelOutside() {
    MainWindow win(nullptr, false);
    showFitted(win);
    win.acts.chat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible());
    const QByteArray before = savedNow();
    const Outside corner = [&win] { return onWindowCorner(win); };
    verify(probe(win, [&] { win.chatDock->moreRows.settings->trigger(); }, corner), win, before, "dock … Settings");
    verify(probe(win, [&] { emit win.chatDock->configureProviderRequested(win.chatDock->moreButton()); }, corner),
           win, before, "Configure provider");
  }

  // The compact chat popover — the small float by the chat icon — with motion on, so the height
  // EASES: it stays centred on the window throughout, and a press on the chat float closes it.
  void compactChatDoorCentresOnTheWindowNotTheChat() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    showFitted(win);
    win.openChatCompact(win.buttonForAction(win.acts.chat));
    QTRY_VERIFY(win.chatCompactShowing());
    QTest::qWait(600);   // past the float's own arrival
    const QByteArray before = savedNow();
    const QPoint chatAt(win.chatDock->width() - 6, win.chatDock->height() - 6);
    const Outside onChat = [&win, chatAt] { return std::pair<QWidget*, QPoint>{win.chatDock, chatAt}; };
    verify(probe(win, [&] { win.chatDock->moreRows.settings->trigger(); }, onChat), win, before, "compact … Settings");
    const Outside corner = [&win] { return onWindowCorner(win); };
    verify(probe(win, [&] { win.acts.assistantSettings->trigger(); }, corner), win, before, "chord over compact chat");
  }

  // The canvas menu's Assistant flyout: its Settings row closes the menu first, then opens the dialog.
  void canvasMenuFlyoutDoorCentresAndCancelsOutside() {
    MainWindow win(nullptr, false);
    showFitted(win);
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    const QByteArray before = savedNow();
    const auto open = [&win] {
      QTimer::singleShot(0, [] {
        QMenu* menu = nullptr;
        for (int i = 0; i < 200 && !menu; ++i) {
          menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
          if (!menu) QTest::qWait(10);
        }
        if (!menu) return;
        QAction* row = nullptr;
        for (QAction* a : menu->actions())
          if (a->text().startsWith("Assistant")) row = a;
        if (!row || !row->menu()) { menu->close(); return; }
        menu->setActiveAction(row);
        QTest::keyClick(menu, Qt::Key_Right);
        settle([row] { return row->menu()->isVisible(); }, 1000);
        auto* gear = row->menu()->findChild<QAction*>("chatMenuSettings");
        if (gear) gear->trigger(); else menu->close();
      });
      win.parts.canvasMenu.showContextMenu(win.mapToGlobal(QPoint(300, 300)));
    };
    verify(probe(win, open, [&win] { return onWindowCorner(win); }), win, before, "canvas menu flyout");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.assistantPlace.gui.moc"
