// MainWindow GUI e2e — a script a stencil:// link carries (app/ScriptHostLaunch) opens in the
// Script window whatever its scriptMode, runs nothing until the user presses Run, and reaches the
// window only once a linked picture has landed. Helpers: MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../../support/recordingSink.hpp"
#include "ScriptBuffer.hpp"
#include "ScriptDialog.hpp"
#include "launchOptions.hpp"

using stencil::gui::ScriptDialog;

namespace {

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
  stencil::test::RecordingSink* notices = nullptr;
  QString shownText;
  qint64 keyAtShow = 0;   // the canvas original's cacheKey when the window came up

  // The modal Script window the host raises: read from a tick of its own loop, then closed.
  void closeScriptWindowWhenShown() {
    shownText.clear();
    auto* closer = new QTimer(win.get());
    QObject::connect(closer, &QTimer::timeout, win.get(), [this, closer] {
      auto* dlg = qobject_cast<ScriptDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      shownText = dlg->script();
      keyAtShow = win->canvas->getOriginalImage().cacheKey();
      closer->stop();
      dlg->reject();
    });
    closer->start(20);
  }

  // The Script window, Run pressed from a tick of its own loop, closed once `until` holds.
  void runScriptWindowUntil(std::function<bool()> until) {
    auto* runner = new QTimer(win.get());
    auto ran = std::make_shared<bool>(false);
    auto ticks = std::make_shared<int>(0);
    QObject::connect(runner, &QTimer::timeout, win.get(), [runner, ran, ticks, until] {
      auto* dlg = qobject_cast<ScriptDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      if (!*ran) { *ran = true; emit dlg->runRequested(); return; }
      if (!until() && ++*ticks < 250) return;   // 5 s, then the case's own check fails it
      runner->stop();
      dlg->reject();
    });
    runner->start(20);
  }

  void openWindow() {
    stencil::model::ScriptBuffer::instance().setText(QString());
    win = std::make_unique<MainWindow>(nullptr, false);
    win->resize(1000, 760);
    win->show();
    QVERIFY(QTest::qWaitForWindowExposed(win.get()));
    auto owned = std::make_unique<stencil::test::RecordingSink>();
    notices = owned.get();
    win->notify->setSystemSink(std::move(owned));
    win->notify->setChannel(stencil::gui::NotifyChannel::SYSTEM);
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }
  void cleanup() { win.reset(); }

  void eitherModeOpensTheWindowAndRunsNothing() {
    for (const QString mode : {QStringLiteral("run"), QStringLiteral("open"), QString()}) {
      openWindow();
      win->canvas->loadFromImage(flat(QColor(200, 40, 40)));
      closeScriptWindowWhenShown();
      win->openStencilUrl(linkOf(QStringLiteral("@filter bw\n"), mode));
      QTRY_COMPARE(shownText, QStringLiteral("@filter bw\n"));
      QCOMPARE(stencil::model::ScriptBuffer::instance().getText(), QStringLiteral("@filter bw\n"));
      QCOMPARE(win->canvas->getImageFilter(), QStringLiteral("none"));   // nothing ran
      QVERIFY2(!notices->shown.contains(QStringLiteral("Script executed successfully")), qPrintable(mode));
      win.reset();
    }
  }

  void theScriptWaitsForTheLinkedPicture() {
    openWindow();
    win->canvas->loadFromImage(flat(QColor(200, 40, 40)));
    const qint64 before = win->canvas->getOriginalImage().cacheKey();
    closeScriptWindowWhenShown();
    win->parts.scriptHost.adoptLinkedScript(QStringLiteral("@filter bw\n"), before);
    QTest::qWait(300);
    QVERIFY2(shownText.isEmpty(), "the window came up before the picture landed");
    win->canvas->loadFromImage(flat(QColor(20, 40, 200), QSize(30, 20)));
    QTRY_COMPARE(shownText, QStringLiteral("@filter bw\n"));
    QVERIFY(keyAtShow != before);
  }

  // Rule 9: a linked script that names a local picture is refused on Run, and the picture stays;
  // once the user has changed the script it is theirs, and the same open goes through.
  void aLinkedScriptOpensNoLocalFileUntilTheUserChangesIt() {
    openWindow();
    win->canvas->loadFromImage(flat(QColor(200, 40, 40)));
    const qint64 before = win->canvas->getOriginalImage().cacheKey();
    const QString script = QStringLiteral("@source %1:\n  @filter bw\n").arg(guiTestImage());
    const auto refused = [this] {
      return std::any_of(notices->shown.begin(), notices->shown.end(),
                         [](const QString& s) { return s.contains(QStringLiteral("web images only")); });
    };
    runScriptWindowUntil(refused);
    win->parts.scriptHost.adoptLinkedScript(script, std::nullopt);
    QTRY_VERIFY2(refused(), "a linked script opened a local picture");
    QTRY_VERIFY(!QApplication::activeModalWidget());
    QCOMPARE(win->canvas->getOriginalImage().cacheKey(), before);
    QVERIFY(stencil::model::ScriptBuffer::instance().isFromLink());

    stencil::model::ScriptBuffer::instance().setText(script + QStringLiteral("\n"));   // the user's edit
    QVERIFY(!stencil::model::ScriptBuffer::instance().isFromLink());
    const auto opened = [this, before] { return win->canvas->getOriginalImage().cacheKey() != before; };
    runScriptWindowUntil(opened);
    win->acts.script->trigger();
    QTRY_VERIFY2(opened(), "the user's own script could not open the local picture");
    QTRY_VERIFY(!QApplication::activeModalWidget());
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
