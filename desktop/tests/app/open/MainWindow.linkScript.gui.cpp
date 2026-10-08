// MainWindow GUI e2e — a script a stencil:// link carries (app/ScriptHostLaunch) opens in the
// Script window whatever its scriptMode, runs nothing until the user presses Run, and reaches the
// window only once a linked picture has landed. Helpers: MainWindow.gui.hpp.
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
