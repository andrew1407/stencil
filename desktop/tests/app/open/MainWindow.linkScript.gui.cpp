// MainWindow GUI e2e — a script a stencil:// link carries (app/ScriptHostLaunch): "open" puts it in
// the Script window, "run" asks first and runs it with web sources only, a declined run opens the
// window, and a linked picture lands before the script reaches it. Helpers: MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "ScriptBuffer.hpp"
#include "ScriptDialog.hpp"
#include "launchOptions.hpp"

using stencil::gui::ScriptDialog;

namespace {

  struct RecordingSink : stencil::gui::NotificationSink {
    QStringList shown;
    bool show(const stencil::gui::Notice& n) override { shown << n.text; return true; }
    bool isAvailable() const override { return true; }
    void setActive(bool) override {}
  };

  QImage flat(const QColor& c, QSize size = QSize(40, 30)) {
    QImage img(size, QImage::Format_ARGB32);
    img.fill(c);
    return img;
  }

  QUrl linkOf(const QString& script, const QString& mode) {
    QUrl u(QStringLiteral("stencil://open"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("script"), QString(script).replace(QLatin1Char('%'), QStringLiteral("%25")));
    q.addQueryItem(QStringLiteral("scriptMode"), mode);
    u.setQuery(q);
    return u;
  }

}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  std::unique_ptr<MainWindow> win;
  RecordingSink* notices = nullptr;
  QString shownText;

  // The modal Script window the host raises: read from a tick of its own loop, then closed.
  void closeScriptWindowWhenShown() {
    shownText.clear();
    auto* closer = new QTimer(win.get());
    QObject::connect(closer, &QTimer::timeout, win.get(), [this, closer] {
      auto* dlg = qobject_cast<ScriptDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      shownText = dlg->script();
      closer->stop();
      dlg->reject();
    });
    closer->start(20);
  }

  void openWindow() {
    stencil::model::ScriptBuffer::instance().setText(QString());
    win = std::make_unique<MainWindow>(nullptr, false);
    win->resize(1000, 760);
    win->show();
    QVERIFY(QTest::qWaitForWindowExposed(win.get()));
    auto owned = std::make_unique<RecordingSink>();
    notices = owned.get();
    win->notify->setSystemSink(std::move(owned));
    win->notify->setChannel(stencil::gui::NotifyChannel::SYSTEM);
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }
  void cleanup() { win.reset(); }

  void aConfirmedRunAppliesTheScript() {
    openWindow();
    win->canvas->loadFromImage(flat(QColor(200, 40, 40)));
    QStringList asked;
    win->parts.scriptHost.confirmLinkedRun = [&asked](const QString& text) { asked << text; return true; };
    win->openStencilUrl(linkOf(QStringLiteral("@filter bw\n"), QStringLiteral("run")));
    QTRY_COMPARE(asked, QStringList{QStringLiteral("@filter bw\n")});
    QTRY_VERIFY(notices->shown.contains(QStringLiteral("Script executed successfully")));
    QCOMPARE(stencil::model::ScriptBuffer::instance().getText(), QStringLiteral("@filter bw\n"));
    const QRgb px = win->canvas->renderToImage(false).pixel(5, 5);
    QVERIFY2(qRed(px) == qGreen(px) && qGreen(px) == qBlue(px), "the filter landed");
  }

  void aLinkedScriptNeverReadsALocalFile() {
    openWindow();
    win->canvas->loadFromImage(flat(QColor(200, 40, 40)));
    win->parts.scriptHost.confirmLinkedRun = [](const QString&) { return true; };
    win->openStencilUrl(linkOf(QStringLiteral("@filter sepia\n@source /etc/hosts:\n  @filter bw\n"),
                               QStringLiteral("run")));
    QTRY_VERIFY(std::any_of(notices->shown.begin(), notices->shown.end(), [](const QString& s) {
      return s.contains(QStringLiteral("line 2")) && s.contains(QStringLiteral("web images only"));
    }));
    QCOMPARE(win->canvas->getImageFilter(), QStringLiteral("sepia"));   // the edit before it stays
  }

  void aDeclinedRunOpensTheWindowWithTheText() {
    openWindow();
    win->canvas->loadFromImage(flat(QColor(200, 40, 40)));
    win->parts.scriptHost.confirmLinkedRun = [](const QString&) { return false; };
    closeScriptWindowWhenShown();
    win->openStencilUrl(linkOf(QStringLiteral("@filter bw\n"), QStringLiteral("run")));
    QTRY_COMPARE(shownText, QStringLiteral("@filter bw\n"));
    QCOMPARE(win->canvas->getImageFilter(), QStringLiteral("none"));   // nothing ran
  }

  void openModeFillsTheWindowAndRunsNothing() {
    openWindow();
    int asked = 0;
    win->parts.scriptHost.confirmLinkedRun = [&asked](const QString&) { ++asked; return true; };
    closeScriptWindowWhenShown();
    win->openStencilUrl(linkOf(QStringLiteral("@crop 10%\n"), QStringLiteral("open")));
    QTRY_COMPARE(shownText, QStringLiteral("@crop 10%\n"));
    QCOMPARE(asked, 0);
  }

  void theScriptWaitsForTheLinkedPicture() {
    openWindow();
    win->canvas->loadFromImage(flat(QColor(200, 40, 40)));
    qint64 keyAtAsk = 0;
    win->parts.scriptHost.confirmLinkedRun = [this, &keyAtAsk](const QString&) {
      keyAtAsk = win->canvas->getOriginalImage().cacheKey();
      return false;
    };
    const qint64 before = win->canvas->getOriginalImage().cacheKey();
    win->parts.scriptHost.adoptLinkedScript(QStringLiteral("@filter bw\n"), false, before);
    QTest::qWait(300);
    QCOMPARE(keyAtAsk, qint64(0));   // still waiting on the picture
    closeScriptWindowWhenShown();
    win->canvas->loadFromImage(flat(QColor(20, 40, 200), QSize(30, 20)));
    QTRY_VERIFY(keyAtAsk != 0);
    QVERIFY(keyAtAsk != before);
    QTRY_COMPARE(shownText, QStringLiteral("@filter bw\n"));
  }

  void anOverCapScriptIsLeftOutAndSaid() {
    openWindow();
    QUrl u(QStringLiteral("stencil://open"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("script"), QString(stencil::gui::launchScriptMaxChars() + 1, QLatin1Char('a')));
    u.setQuery(q);
    win->openStencilUrl(u);
    QTRY_VERIFY(std::any_of(notices->shown.begin(), notices->shown.end(),
                            [](const QString& s) { return s.contains(QStringLiteral("was left out")); }));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.linkScript.gui.moc"
