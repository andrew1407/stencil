// MainWindow GUI e2e — Accent fills and the cursors that go with them, live and dead.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindowPaint.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A checkable toolbar toggle is NOT accent-filled at rest — the accent is what "on" looks like
  // (browser #chat-btn: ghost, .active fills).
  void checkableToggleFillsOnlyWhenOn() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 150);
    QAction* chat = win.acts.chat;
    QVERIFY(chat && chat->isCheckable());
    QToolButton* btn = qobject_cast<QToolButton*>(win.buttonForAction(chat));
    QVERIFY2(btn, "the AI Assistant icon is not on the toolbar");
    QVERIFY2(btn->property("toolFill").toString().isEmpty(),
             "a checkable toggle must not carry a permanent fill");
    const QColor accent = stencil::gui::accentPrimary(win.settings.accentColor);
    // The button's own background, read from a corner well inside the chip.
    const auto ground = [&] {
      QTest::qWait(30);
      const QImage im = btn->grab().toImage();
      return im.pixelColor(3, im.height() / 2);
    };
    const auto isAccent = [&](const QColor& c) {
      return qAbs(c.red() - accent.red()) < 50 && qAbs(c.green() - accent.green()) < 50
          && qAbs(c.blue() - accent.blue()) < 50;
    };
    const auto repolish = [](QWidget* w) { w->style()->unpolish(w); w->style()->polish(w); w->update(); };
    chat->setChecked(false);
    repolish(btn);
    QVERIFY2(!isAccent(ground()), "the toggle is filled while off");
    chat->setChecked(true);
    repolish(btn);
    QVERIFY2(isAccent(ground()), "the toggle does not fill when on");
    chat->setChecked(false);
  }
  // The chat toggle is the incognito toggle's bordered ghost (browser #chat-btn shares its box): the
  // same frame and ground at rest, under the pointer, pressed, lit and dead.
  void chatToggleWearsTheGhostBoxOfItsSiblingToggle() {
    MainWindow win(nullptr, false);
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 150);
    auto* chat = qobject_cast<QToolButton*>(win.buttonForAction(win.acts.chat));
    auto* incognito = qobject_cast<QToolButton*>(win.buttonForAction(win.acts.incognito));
    QVERIFY(chat && incognito && incognito->isEnabled());
    // Grabbed off the window, so a translucent ring is composited as drawn. The left edge at mid
    // height is the 1px border; three pixels in is the ground.
    const auto face = [&win](QToolButton* b, bool hovered, bool down) {
      b->setAttribute(Qt::WA_UnderMouse, hovered);
      b->setDown(down);
      b->style()->unpolish(b);
      b->style()->polish(b);
      QTest::qWait(20);
      const QImage im = win.grab(QRect(b->mapTo(&win, QPoint(0, 0)), b->size())).toImage();
      b->setDown(false);
      return std::pair<QColor, QColor>{im.pixelColor(0, im.height() / 2), im.pixelColor(3, im.height() / 2)};
    };
    const auto mismatch = [&](const char* state, bool hovered, bool down = false) {
      const auto [chatEdge, chatGround] = face(chat, hovered, down);
      const auto [twinEdge, twinGround] = face(incognito, hovered, down);
      if (!nearColor(chatEdge, twinEdge, 6))
        return QString("%1: the chat button's border is %2, the incognito toggle's %3")
            .arg(state, chatEdge.name(), twinEdge.name());
      if (!nearColor(chatGround, twinGround, 6))
        return QString("%1: the chat button's ground is %2, the incognito toggle's %3")
            .arg(state, chatGround.name(), twinGround.name());
      return QString();
    };
    QString why = mismatch("at rest", false);
    QVERIFY2(why.isEmpty(), qPrintable(why));
    why = mismatch("under the pointer", true);
    QVERIFY2(why.isEmpty(), qPrintable(why));
    why = mismatch("pressed", true, true);
    QVERIFY2(why.isEmpty(), qPrintable(why));
    // The mean of the pixels a glyph's line-art covers.
    const auto ink = [](QToolButton* b) {
      const QImage im = b->icon().pixmap(QSize(18, 18), 1.0).toImage();
      long r = 0, g = 0, bl = 0, n = 0;
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x)
          if (const QColor c = im.pixelColor(x, y); c.alpha() > 200) { r += c.red(); g += c.green(); bl += c.blue(); ++n; }
      return n ? QColor(int(r / n), int(g / n), int(bl / n)) : QColor();
    };
    for (QAction* a : {win.acts.chat, win.acts.incognito}) a->setChecked(true);
    why = mismatch("lit", false);
    if (why.isEmpty() && !nearColor(ink(chat), ink(incognito), 30))
      why = QString("lit: the chat glyph is %1, the incognito glyph %2").arg(ink(chat).name(), ink(incognito).name());
    for (QAction* a : {win.acts.chat, win.acts.incognito}) a->setChecked(false);
    QVERIFY2(why.isEmpty(), qPrintable(why));
    for (QAction* a : {win.acts.chat, win.acts.incognito}) a->setEnabled(false);
    why = mismatch("dead", false);
    for (QAction* a : {win.acts.chat, win.acts.incognito}) a->setEnabled(true);
    QVERIFY2(why.isEmpty(), qPrintable(why));
  }
  // Every accent-BACKED control wears the ink the ACCENT picked (theme.hpp onAccentInk), not a fixed
  // white glyph a yellow or sky accent would swallow. Browser twin: --on-accent.
  void filledControlsWearTheAccentsOwnInk() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    // The mean of the pixels a button's line-art actually covers.
    const auto glyph = [](QToolButton* b) {
      const QImage im = b->icon().pixmap(QSize(18, 18), 1.0).toImage();
      long r = 0, g = 0, bl = 0, n = 0;
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x) {
          const QColor c = im.pixelColor(x, y);
          if (c.alpha() > 200) { r += c.red(); g += c.green(); bl += c.blue(); ++n; }
        }
      return n ? QColor(int(r / n), int(g / n), int(bl / n)) : QColor();
    };
    // Both directions on the SAME button — a hard-coded ink cannot pass twice.
    for (const QString& accentKey : {QStringLiteral("violet"), QStringLiteral("yellow")}) {
      auto s = win.settings;
      s.accentColor = accentKey;
      win.applySettings(s, /*persist=*/false);
      const QColor ink = stencil::gui::onAccentInk(stencil::gui::accentPrimary(accentKey));
      const QString why = QStringLiteral(" (accent %1)").arg(accentKey);

      QToolButton* filled = nullptr;
      for (QToolButton* b : win.findChildren<QToolButton*>())
        if (b->property("toolFill").toString() == QLatin1String("accent")
            && b->isEnabled() && !b->icon().isNull()) { filled = b; break; }
      QVERIFY2(filled, "no enabled accent-filled toolbar button to sample");
      QTRY_VERIFY2_WITH_TIMEOUT(nearColor(glyph(filled), ink, 40),
                                qPrintable("a filled button's glyph is not the accent's ink" + why), 3000);

      // …and the stylesheet hands the same ink to every label on the accent.
      const QString qss = stencil::gui::buildStylesheet(win.painted.dark, accentKey);
      QVERIFY2(qss.contains("color: " + ink.name()),
               qPrintable("the QSS carries no on-accent ink" + why));
      // The empty state's "Open Image" paints its own label (support/share/OpenImageButton.hpp),
      // so it reads that ink out of the palette the sheet filled rather than the sheet.
      QVERIFY(win.tools.openImageBtn);
      QTRY_VERIFY2(win.tools.openImageBtn->palette().color(QPalette::ButtonText) == ink,
                   qPrintable("Open Image's own painter has no on-accent ink" + why));
    }
  }
  // Fit to window is FILLED like every other acting button (user decision; browser #zoom-fit) — it acts
  // at once and reports no state — and DISABLED it is the grey chip the ± steppers are, to the pixel.
  void fitToWindowIsFilledAndGreysWhenDead() {
    MainWindow win(nullptr, false);
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 150);
    QToolButton* btn = win.tools.zoomFitBtn;
    QVERIFY(btn);
    auto* stepper = qobject_cast<QToolButton*>(win.buttonForAction(win.acts.zoomOut));
    QVERIFY(stepper);
    const stencil::gui::Palette pal =
        stencil::gui::themePalette(win.painted.dark, win.settings.accentColor);
    const QColor accent = stencil::gui::accentPrimary(win.settings.accentColor);
    // Dead (no image yet): the shared grey chip, the stepper's own face, no accent in it.
    QVERIFY2(!btn->isEnabled(), "the fit button should start disabled, with no image");
    QVERIFY2(!stepper->isEnabled(), "the − stepper should start disabled too");
    {
      const QImage im = btn->grab().toImage();
      const QImage other = stepper->grab().toImage();
      const QColor face = im.pixelColor(im.width() / 2, 3);
      QVERIFY2(!nearColor(face, accent, 50), "a dead fit button is filled with the accent");
      QVERIFY2(nearColor(face, other.pixelColor(other.width() / 2, 3), 4),
               "a dead fit button does not wear the stepper's chip");
    }
    // …and once it can act, the fill every other acting button carries.
    openLoaded(win);
    QTRY_VERIFY(win.acts.fit->isEnabled());
    QTRY_COMPARE(btn->property("toolFill").toString(), QStringLiteral("accent"));
    const QImage live = btn->grab().toImage();
    QVERIFY2(nearColor(live.pixelColor(live.width() / 2, 3), accent, 50),
             "an enabled fit button is not accent-filled");
    Q_UNUSED(pal);
  }
  // Clickable toolbar controls carry the hand cursor and dead ones the "no entry" (the browser's
  // `cursor: pointer` / `not-allowed`). Text fields and combos keep the I-beam and the arrow.
  void toolbarControlsCarryTheHandCursor() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(60, 40, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());
    win.refreshActions();
    settleLayout(&win, 150);
    int live = 0;
    for (QToolBar* tb : win.findChildren<QToolBar*>())
      for (QAbstractButton* b : tb->findChildren<QAbstractButton*>()) {
        if (!b->isVisible()) continue;
        const Qt::CursorShape shape = b->cursor().shape();
        if (b->isEnabled()) {
          ++live;
          QVERIFY2(shape == Qt::PointingHandCursor,
                   qPrintable(QString("%1 has cursor %2, not the hand")
                                  .arg(b->objectName().isEmpty() ? b->text() : b->objectName())
                                  .arg(int(shape))));
        } else {
          QVERIFY2(shape == Qt::ForbiddenCursor || shape == Qt::PointingHandCursor,
                   "a dead control should not read as clickable");
        }
      }
    QVERIFY2(live > 5, "expected several live toolbar controls");
    // A control that goes dead swaps to the refusal cursor.
    QToolButton* crop = qobject_cast<QToolButton*>(win.buttonForAction(win.acts.crop));
    QVERIFY(crop);
    QCOMPARE(crop->cursor().shape(), Qt::PointingHandCursor);
    win.canvas->clearImage();
    win.refreshActions();
    QCOMPARE(crop->cursor().shape(), Qt::ForbiddenCursor);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbarFill.gui.moc"
