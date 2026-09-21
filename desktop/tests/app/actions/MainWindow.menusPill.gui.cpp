// MainWindow GUI e2e — The Assistant flyout's splitter: the app's pill affordance and its resize cursor.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindowMenu.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The handle wears the app's pill, centred on the INPUT column it resizes (not the panel), grows and
  // accents on hover, restores the cursor on leave, and the size the drag lands on outlives the menu.
  void assistantComposerSplitterWearsThePill() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());  // a working image, so a plan has something to hit

    // ── assistant ON ──
    win.settings.llmProvider = "ollama";
    win.settings.llmBaseUrl = "http://localhost:11434";
    // A mock transport answers synchronously with a canned op-plan, so nothing
    // touches the network (the same seam LlmClient.headless.cpp uses).
    MockChatTransport mock;
    // A real op-plan in ollama's response shape. Built through QJsonDocument
    // rather than a raw string literal — moc chokes on those (empty .moc).
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Sepia applied\","
                      "\"actions\":[{\"op\":\"filter\",\"mode\":\"sepia\"}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient = std::make_unique<stencil::llm::LlmClient>(&mock);

    bool splitterResized = false;
    QList<int> splitterSizes;
    int pillRest = 0, pillHot = 0, handleWidth = 0;
    bool cursorBefore = true, cursorOnHandle = false, cursorAfter = true;
    double pillCenterX = -1, inputCenterX = -1, panelCenterX = -1;
    bool assistantOpened = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = findMenu();
      if (!menu) return;
      QMenu* sub = openSub(menu, "Assistant");
      assistantOpened = sub != nullptr;
      if (!sub) { menu->close(); return; }
      auto* panel = sub->findChild<QWidget*>("chatMenuPanel");
      auto* input = sub->findChild<QPlainTextEdit*>("chatMenuInput");
      auto* sendBtn = sub->findChild<QToolButton*>("chatMenuSend");
      auto* moreBtn = sub->findChild<QToolButton*>("chatMenuMore");
      if (!panel || !input || !sendBtn || !moreBtn) { menu->close(); return; }
      // The splitter handle carries the app's pill affordance, theme-coloured,
      // and grows/accents on hover — not Qt's dotted nub.
      if (auto* sp0 = panel->findChild<QSplitter*>("chatMenuSplitter")) {
        if (QWidget* h = sp0->handle(1)) {
          handleWidth = sp0->handleWidth();
          const QImage rest = h->grab().toImage();
          // Width of the painted pill: the widest run of pixels that differ
          // from the handle's own background (sampled at a corner).
          auto pillWidth = [](const QImage& img) {
            if (img.isNull()) return 0;
            const QRgb bg = img.pixel(0, 0);
            int best = 0;
            for (int y = 0; y < img.height(); ++y) {
              int run = 0;
              for (int x = 0; x < img.width(); ++x)
                if (img.pixel(x, y) != bg) ++run;
              best = qMax(best, run);
            }
            // grab() renders at the device pixel ratio — report LOGICAL px so
            // the bounds hold on a Retina display too.
            return qRound(best / img.devicePixelRatio());
          };
          pillRest = pillWidth(rest);
          // Centring, pinned by MEASUREMENT: the pill's painted centre must sit on the INPUT column the drag
          // resizes, not on the handle's full span, which also covers the send/attach/gear cluster.
          {
            const QRgb bg = rest.pixel(0, 0);
            int lo = INT_MAX, hi = -1;
            for (int y = 0; y < rest.height(); ++y)
              for (int x = 0; x < rest.width(); ++x)
                if (rest.pixel(x, y) != bg) { lo = qMin(lo, x); hi = qMax(hi, x); }
            if (hi >= 0) {
              const double dpr = rest.devicePixelRatio();
              const double cxInHandle = (lo / dpr + (hi + 1) / dpr) / 2.0;
              pillCenterX = h->mapTo(panel, QPoint(0, 0)).x() + cxInHandle;
              inputCenterX = input->mapTo(panel, QPoint(0, 0)).x() + input->width() / 2.0;
              panelCenterX = panel->width() / 2.0;
            }
          }
          // Hover it the way a user does, with a real move over the menu: the popup grab suppresses the
          // handle's own enter/leave, so the menu synthesises them and this asserts that plumbing too.
          const QPoint over = h->mapTo(sub, h->rect().center());
          // Real MouseMove events sent to the menu: QTest::mouseMove never reaches a NATIVE popup, so this
          // is the form that exercises the grab path both offscreen and headed.
          const auto moveTo = [sub](const QPoint& p) {
            QMouseEvent e(QEvent::MouseMove, QPointF(p), QPointF(sub->mapToGlobal(p)),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(sub, &e);
          };
          cursorBefore = QApplication::overrideCursor() != nullptr;
          moveTo(QPoint(over.x(), over.y() - 40));
          QTest::qWait(20);
          moveTo(over);
          QTest::qWait(200);  // the growth animation settles
          pillHot = pillWidth(h->grab().toImage());
          if (QCursor* oc = QApplication::overrideCursor())
            cursorOnHandle = oc->shape() == Qt::SplitVCursor;
          // …and both must revert when the pointer leaves.
          moveTo(QPoint(over.x(), over.y() - 40));
          QTest::qWait(60);
          cursorAfter = QApplication::overrideCursor() != nullptr;
        }
      }
      // Resizable composer: a splitter between transcript and composer, whose
      // drag redistributes a pinned total (the popup stays a sane size).
      if (auto* sp = panel->findChild<QSplitter*>("chatMenuSplitter")) {
        const QList<int> before = sp->sizes();
        sp->setSizes({before.at(0) - 40, before.at(1) + 40});
        const QList<int> after = sp->sizes();
        splitterResized = after.at(1) > before.at(1) &&
                          (after.at(0) + after.at(1)) == (before.at(0) + before.at(1));
        splitterSizes = after;
      }
      menu->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));

    QVERIFY2(assistantOpened, "the Assistant submenu did not open");
    QVERIFY2(splitterResized, "the menu composer is not resizable");
    QVERIFY2(handleWidth >= 6, "the splitter handle lost its grab area");
    // A centred pill at rest that GROWS on hover (44 → 68 px by design).
    QVERIFY2(pillRest >= 30 && pillRest <= 60,
             qPrintable(QString("resting pill is %1 px wide").arg(pillRest)));
    QVERIFY2(pillHot > pillRest,
             qPrintable(QString("pill did not grow on hover (%1 → %2)")
                            .arg(pillRest).arg(pillHot)));
    // The resize cursor: none before, SplitVCursor while over the handle, and
    // fully restored (not merely a different shape) once the pointer leaves.
    QVERIFY2(!cursorBefore, "an override cursor was already active");
    QVERIFY2(cursorOnHandle, "hovering the splitter handle showed no resize cursor");
    QVERIFY2(!cursorAfter, "the resize cursor leaked past the splitter handle");
    QVERIFY(pillCenterX >= 0);
    QVERIFY2(qAbs(pillCenterX - inputCenterX) <= 2.0,
             qPrintable(QString("pill centre %1 is off the input centre %2")
                            .arg(pillCenterX).arg(inputCenterX)));
    // Guard the regression itself: the input column is genuinely narrower than
    // the panel, so "centred on the panel" would be a visible miss.
    QVERIFY2(qAbs(panelCenterX - inputCenterX) > 10.0,
             "the input no longer differs from the panel centre — assertion is vacuous");

    // The panel outlives the menu, so the composer size the user dragged to
    // persists for the session.
    if (auto* sp = win.chatMenuPanel->findChild<QSplitter*>("chatMenuSplitter"))
      QCOMPARE(sp->sizes(), splitterSizes);

    win.llmClient.reset();  // drop the mock before it goes out of scope
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menusPill.gui.moc"
