// MainWindow GUI e2e — The draw toggles as one button: its width across modes, and the shared face swap.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowPaint.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Browser parity: the single #draw-toggle and .btn-draw-fixed.
  void drawToggleIsOneButtonAndModeKeepsWidth() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    const auto buttonsFor = [&win](const QString& actionText) {
      QList<QToolButton*> out;
      for (QToolButton* b : win.findChildren<QToolButton*>())
        if (b->defaultAction() && b->defaultAction()->text() == actionText) out.append(b);
      return out;
    };

    // Idle: exactly one draw button, and it says Start.
    QCOMPARE(buttonsFor("Start Drawing").size(), 1);
    QCOMPARE(buttonsFor("Stop Drawing").size(), 0);
    QToolButton* draw = buttonsFor("Start Drawing").first();
    const QSize idleSize = draw->size();
    // Icon AND text, like the browser's toggle — not a bare icon.
    QCOMPARE(draw->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    QCOMPARE(draw->text(), QString("Start"));
    QVERIFY2(!draw->icon().isNull(), "the toggle keeps its play icon beside the label");
    // The pinned width is measured after the toolbar is styled; measuring it before the
    // themed icon and stylesheet padding exist yields a button that clips its own label.
    QVERIFY2(draw->width() >= draw->sizeHint().width(), "pinned width must not clip the label");

    QAction* start = actionByText(&win, "Start Drawing");
    QVERIFY(start);
    start->trigger();
    QTRY_VERIFY(canvas->getIsDrawing());
    // The SAME button now stops — not a second button appearing beside it.
    QTRY_COMPARE(draw->defaultAction()->text(), QString("Stop Drawing"));
    QCOMPARE(buttonsFor("Start Drawing").size(), 0);
    QCOMPARE(buttonsFor("Stop Drawing").size(), 1);
    QCOMPARE(draw->size(), idleSize);
    // The button mirrors its default action's iconText on a later beat than the
    // action swap above, so this one waits too.
    QTRY_COMPARE(draw->text(), QString("Stop"));
    QVERIFY2(!draw->icon().isNull(), "the stop state keeps its icon too");
    QVERIFY2(draw->width() >= draw->sizeHint().width(), "pinned width must not clip the label");
    QVERIFY2(draw->isEnabled(), "must stay clickable while drawing — that is how you stop");

    draw->defaultAction()->trigger();
    QTRY_VERIFY(!canvas->getIsDrawing());
    QTRY_COMPARE(draw->defaultAction()->text(), QString("Start Drawing"));
    beat();

    // Line <-> Rect: the label swaps, the geometry does not.
    QToolButton* mode = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->text() == "Line" || b->text() == "Rect") { mode = b; break; }
    QVERIFY(mode);
    const int modeWidth = mode->width();
    QVERIFY(modeWidth > 0);
    mode->click();
    QTRY_COMPARE(mode->text(), QString("Rect"));
    QCOMPARE(mode->width(), modeWidth);
    mode->click();
    QTRY_COMPARE(mode->text(), QString("Line"));
    QCOMPARE(mode->width(), modeWidth);
    beat();
  }
  // The two Draw toggles are pinned so a label swap cannot resize them and shove the row, but the pin
  // must be a MEASUREMENT of the widest label, so the check re-measures rather than naming a number.
  void drawTogglesAreNoWiderThanTheirWidestLabel() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    settleLayout(&win, 200);   // the pin is taken once the toolbar is built and shown

    const auto naturalWidest = [](QToolButton* b, const QStringList& faces) {
      const QString keep = b->text();
      int widest = 0;
      for (const QString& f : faces) {
        b->setText(f);
        widest = qMax(widest, b->sizeHint().width());
      }
      b->setText(keep);
      return widest;
    };
    struct Case { QToolButton* btn; QStringList faces; const char* what; };
    const QList<Case> cases = {
        {win.startDrawBtn, {QStringLiteral("Start"), QStringLiteral("Stop")}, "Start/Stop"},
        {win.drawModeBtn, {QStringLiteral("Line"), QStringLiteral("Rect")}, "Line/Rect"}};
    for (const Case& c : cases) {
      QVERIFY(c.btn);
      QVERIFY2(c.btn->minimumWidth() == c.btn->maximumWidth(),
               qPrintable(QString("%1: the width is not pinned at all").arg(c.what)));
      const int want = naturalWidest(c.btn, c.faces);
      QVERIFY2(c.btn->width() >= want,
               qPrintable(QString("%1: pinned %2 < widest label %3 — the face would be clipped")
                              .arg(c.what).arg(c.btn->width()).arg(want)));
      QVERIFY2(c.btn->width() <= want,
               qPrintable(QString("%1: pinned %2 vs widest label %3 — %4px of dead space")
                              .arg(c.what).arg(c.btn->width()).arg(want).arg(c.btn->width() - want)));
    }
    beat();
  }
  // Both Draw toggles cross over through the ONE shared swap (support/faceSwap.hpp): the button never
  // resizes, a burst of toggles always lands on the REAL state, and reduced motion goes to the end.
  void drawTogglesSwapTheirFaceAndConverge() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });

    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QToolButton* btn = win.startDrawBtn;
    QToolButton* mode = win.drawModeBtn;
    QVERIFY(btn && mode);
    const QSize drawSize = btn->size();
    const QSize modeSize = mode->size();

    // ── Start → Stop: the word is exchanged at the pivot, not at the click.
    QAction* start = actionByText(&win, "Start Drawing");
    QVERIFY(start);
    start->trigger();
    QVERIFY2(canvas->getIsDrawing(), "the drawing state itself must not wait for the animation");
    QVERIFY2(stencil::gui::faceSwapping(btn), "the toggle snapped instead of swapping");
    QCOMPARE(btn->text(), QString("Start"));   // still the outgoing face
    QCOMPARE(btn->size(), drawSize);           // …and the row has not shifted
    QTRY_COMPARE(btn->text(), QString("Stop"));
    QTRY_VERIFY(!stencil::gui::faceSwapping(btn));
    QCOMPARE(btn->size(), drawSize);
    QCOMPARE(btn->property("drawToggle").toString(), QString("on"));
    QVERIFY2(btn->styleSheet().isEmpty(), "the swap's colour override outlived it");

    // Line ↔ Rect: the same swap and the same pinned box, but — unlike Start/Stop — a PERMANENT accent
    // fill it carries itself, having no idle/on pair and no QAction for styleDangerToolButtons to reach.
    QCOMPARE(mode->property("toolFill").toString(), QString("accent"));
    QCOMPARE(mode->text(), QString("Line"));
    mode->click();
    QVERIFY2(stencil::gui::faceSwapping(mode), "the mode toggle snapped instead of swapping");
    QCOMPARE(mode->size(), modeSize);
    QTRY_COMPARE(mode->text(), QString("Rect"));
    QTRY_VERIFY(!stencil::gui::faceSwapping(mode));
    QCOMPARE(mode->size(), modeSize);
    QVERIFY2(mode->toolTip().contains("Rectangle"), "the tooltip did not follow the mode");
    QCOMPARE(mode->property("toolFill").toString(), QString("accent"));   // survives the swap
    mode->click();
    QTRY_COMPARE(mode->text(), QString("Line"));
    QVERIFY(mode->toolTip().contains("Line"));

    // ── Rapid toggling (a held shortcut): each swap supersedes the one in flight, and
    // what the button ends up saying is the state the canvas is actually in.
    for (int i = 0; i < 6; ++i) {
      QAction* live = btn->defaultAction();
      QVERIFY(live && live->isEnabled());
      live->trigger();
      QTest::qWait(stencil::gui::FACE_SWAP_MS / 5);   // interrupt the swap in flight
      mode->click();
      QTest::qWait(stencil::gui::FACE_SWAP_MS / 5);
    }
    QTRY_VERIFY(!stencil::gui::faceSwapping(btn) && !stencil::gui::faceSwapping(mode));
    QCOMPARE(btn->text(), canvas->getIsDrawing() ? QString("Stop") : QString("Start"));
    QCOMPARE(btn->property("drawToggle").toString(),
             canvas->getIsDrawing() ? QString("on") : QString("idle"));
    QCOMPARE(btn->defaultAction()->text(),
             canvas->getIsDrawing() ? QString("Stop Drawing") : QString("Start Drawing"));
    QCOMPARE(mode->text(),
             canvas->getDrawMode() == CanvasWidget::DrawMode::RECT ? QString("Rect")
                                                                : QString("Line"));
    QCOMPARE(btn->size(), drawSize);
    QCOMPARE(mode->size(), modeSize);
    QVERIFY(btn->styleSheet().isEmpty() && mode->styleSheet().isEmpty());

    // ── Reduced motion: the end state at once, no animation to wait on.
    qputenv("STENCIL_NO_ANIM", "1");
    const bool wasDrawing = canvas->getIsDrawing();
    btn->defaultAction()->trigger();
    QCOMPARE(canvas->getIsDrawing(), !wasDrawing);
    QVERIFY2(!stencil::gui::faceSwapping(btn), "reduced motion still animated the swap");
    QCOMPARE(btn->text(), canvas->getIsDrawing() ? QString("Stop") : QString("Start"));
    QCOMPARE(btn->property("drawToggle").toString(),
             canvas->getIsDrawing() ? QString("on") : QString("idle"));
    mode->click();
    QVERIFY2(!stencil::gui::faceSwapping(mode), "reduced motion still animated the mode swap");
    QCOMPARE(mode->text(),
             canvas->getDrawMode() == CanvasWidget::DrawMode::RECT ? QString("Rect")
                                                                : QString("Line"));
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbarDraw.gui.moc"
