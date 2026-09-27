// MainWindow GUI e2e — a close that arrives while the window is suspended in a nested loop, or while a
// plan or a script waits on a load.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

namespace {

  struct CloseSpy : QObject {
    std::function<void()> onClose;
    bool eventFilter(QObject*, QEvent* e) override {
      if (e->type() == QEvent::Close) onClose();
      return false;
    }
  };

  // Every notice the window raises, whatever its lifetime on screen.
  struct RecordingSink : stencil::gui::NotificationSink {
    QStringList shown;
    bool show(const stencil::gui::Notice& n) override { shown << n.text; return true; }
    bool isAvailable() const override { return true; }
    void setActive(bool) override {}
  };

}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  static QPointer<MainWindow> openDeletingWindow() {
    auto* win = new MainWindow(nullptr, false);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->resize(1000, 760);
    win->show();
    return win;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The popover's loop holds a STACK dialog inside the window: the close ends the loop, the
  // dialog leaves the window, and only then does the window go.
  void closeInsidePopoverLoop() {
    QPointer<MainWindow> win = openDeletingWindow();
    QVERIFY(QTest::qWaitForWindowExposed(win.data()));
    QVERIFY(win->tools.logoBtn && win->tools.logoBtn->isVisible());
    QDialog dlg;
    win->pop.anchor = win->tools.logoBtn;
    QTimer::singleShot(150, win.data(), [win] { win->close(); });
    const int result = win->execMaybePopover(dlg);
    QCOMPARE(result, int(QDialog::Rejected));
    QVERIFY2(!win.isNull(), "the window outlives the loop it was suspended in");
    QVERIFY(dlg.parent() == nullptr);
    QTRY_VERIFY2(win.isNull(), "the close deferred by the loop still lands");
  }

  // A modal the window owns is rejected first, so its exec() unwinds before the window dies.
  void closeInsideModalExec() {
    QPointer<MainWindow> win = openDeletingWindow();
    QVERIFY(QTest::qWaitForWindowExposed(win.data()));
    QPointer<QDialog> dlg = new QDialog(win.data());
    QTimer::singleShot(150, win.data(), [win] { win->close(); });
    const int result = win->execMaybePopover(*dlg);
    QCOMPARE(result, int(QDialog::Rejected));
    QVERIFY2(!win.isNull(), "the window outlives the modal it was suspended in");
    QTRY_VERIFY2(win.isNull(), "the close deferred by the modal still lands");
    QVERIFY(dlg.isNull());
  }

  // A .stc load is a continuation too: the close lapses it, the script reports the load's
  // failure, and only then does the window go.
  void closeDuringScriptAwait() {
    QTcpServer silent;   // accepts and never answers, so the load waits
    QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
    const QString url = QStringLiteral("http://127.0.0.1:%1/never.png").arg(silent.serverPort());
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("wait.stc"));
    {
      QFile f(path);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(QStringLiteral("@source %1:\n  @filter bw\n").arg(url).toUtf8());
    }
    QPointer<MainWindow> win = openDeletingWindow();
    QVERIFY(QTest::qWaitForWindowExposed(win.data()));
    auto owned = std::make_unique<RecordingSink>();
    RecordingSink* notices = owned.get();
    win->notify->setSystemSink(std::move(owned));
    win->notify->setChannel(stencil::gui::NotifyChannel::SYSTEM);
    CloseSpy spy;
    QList<QStringList> seen;   // the notices raised by each close's arrival
    spy.onClose = [&] { seen << notices->shown; };
    win->installEventFilter(&spy);
    win->parts.scriptHost.runScriptFromFile(path);
    QVERIFY2(win->pop.loops.isEmpty() && win->pop.awaits.size() == 1,
             "the script waits with no nested loop under it");
    QVERIFY2(!win->close(), "a close during the load waits for the script's answer");
    QVERIFY(!win.isNull());
    QTRY_VERIFY2(win.isNull(), "the close deferred by the await still lands");
    const QString failed =
        QStringLiteral("Script failed at line 1 — openUrl: timed out loading %1").arg(url);
    QCOMPARE(seen.size(), 2);
    QVERIFY(!seen.at(0).contains(failed));
    QVERIFY2(seen.at(1).contains(failed), qPrintable(seen.at(1).join(QStringLiteral(" | "))));
  }

  // An op plan's await is a continuation, not a loop: the close lapses it, the plan answers with
  // the load's failure, and only then does the window go.
  void closeDuringPlanAwait() {
    QTcpServer silent;
    QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
    const QString url = QStringLiteral("http://127.0.0.1:%1/never.png").arg(silent.serverPort());
    QPointer<MainWindow> win = openDeletingWindow();
    QVERIFY(QTest::qWaitForWindowExposed(win.data()));
    CloseSpy spy;
    QList<QPair<bool, QString>> closes;   // planRunning, the transcript's last row
    spy.onClose = [&] {
      const auto& log = win->chatSession->chatMirrorLog;
      closes.append({win->chatSession->planRunning, log.isEmpty() ? QString() : log.last().text});
    };
    win->installEventFilter(&spy);
    stencil::llm::ChatMessage said;
    said.role = QStringLiteral("user");
    said.text = QStringLiteral("open %1").arg(url);
    win->chatSession->chatHistory.append(said);
    stencil::llm::LlmReply reply;
    reply.ok = true;
    reply.text = QStringLiteral("{\"version\":1,\"reply\":\"Opening.\",\"actions\":[{\"op\":\"openUrl\","
                                "\"url\":\"%1\"},{\"op\":\"filter\",\"mode\":\"bw\"}]}").arg(url);
    win->chatSession->onChatReply(reply);
    QVERIFY2(win->chatSession->planRunning && win->pop.loops.isEmpty(),
             "the plan waits with no nested loop under it");
    QVERIFY2(!win->close(), "a close during the await waits for the plan's answer");
    QVERIFY(!win.isNull());
    QTRY_VERIFY2(win.isNull(), "the close deferred by the await still lands");
    QCOMPARE(closes.size(), 2);
    QVERIFY(closes.at(0).first);
    QVERIFY2(!closes.at(1).first, "the plan answered before the window went");
    QVERIFY2(closes.at(1).second.startsWith(QStringLiteral("openUrl: timed out loading")),
             qPrintable(closes.at(1).second));
  }

  // Outside any loop the close is immediate, as it always was.
  void closeOutsideLoopIsImmediate() {
    QPointer<MainWindow> win = openDeletingWindow();
    QVERIFY(QTest::qWaitForWindowExposed(win.data()));
    QVERIFY(win->pop.loops.isEmpty() && win->pop.awaits.isEmpty());
    QVERIFY(win->close());
    QTRY_VERIFY(win.isNull());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.closeInLoop.gui.moc"
