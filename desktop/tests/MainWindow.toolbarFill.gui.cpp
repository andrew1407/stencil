// MainWindow GUI e2e — Accent fills and the cursors that go with them, live and dead.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowPaint.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A checkable toolbar toggle is NOT accent-filled at rest — the accent is what "on"
  // looks like (browser #chat-btn: ghost, .active fills). It went permanently filled when
  // every section button was given a fill.
  void checkableToggleFillsOnlyWhenOn() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 150);
    QAction* chat = win.actChat_;
    QVERIFY(chat && chat->isCheckable());
    QToolButton* btn = qobject_cast<QToolButton*>(win.buttonForAction(chat));
    QVERIFY2(btn, "the AI Assistant icon is not on the toolbar");
    QVERIFY2(btn->property("toolFill").toString().isEmpty(),
             "a checkable toggle must not carry a permanent fill");
    const QColor accent = stencil::gui::accentPrimary(win.settings_.accentColor);
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
  // Every accent-BACKED control wears the ink the ACCENT picked (theme.hpp onAccentInk).
  // The bug this locks down: a fixed white glyph, which a yellow or sky accent all but
  // swallowed. Browser twin: --on-accent.
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
      auto s = win.settings_;
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
      const QString qss = stencil::gui::buildStylesheet(win.paintedDark_, accentKey);
      QVERIFY2(qss.contains("color: " + ink.name()),
               qPrintable("the QSS carries no on-accent ink" + why));
      // The empty state's "Open Image" paints its own label (support/OpenImageButton.hpp),
      // so it reads that ink out of the palette the sheet filled rather than the sheet.
      QVERIFY(win.openImageBtn_);
      QTRY_VERIFY2(win.openImageBtn_->palette().color(QPalette::ButtonText) == ink,
                   qPrintable("Open Image's own painter has no on-accent ink" + why));
    }
  }
  // Fit to window is FILLED like every other acting button (user decision; browser twin:
  // #zoom-fit) — pressing it acts at once, it reports no state. DISABLED it is the same
  // grey chip as the − / + steppers beside it, to the pixel. Asserts both halves.
  void fitToWindowIsFilledAndGreysWhenDead() {
    MainWindow win(nullptr, false);
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 150);
    QToolButton* btn = win.zoomFitBtn_;
    QVERIFY(btn);
    auto* stepper = qobject_cast<QToolButton*>(win.buttonForAction(win.actZoomOut_));
    QVERIFY(stepper);
    const stencil::gui::Palette pal =
        stencil::gui::themePalette(win.paintedDark_, win.settings_.accentColor);
    const QColor accent = stencil::gui::accentPrimary(win.settings_.accentColor);
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
    QTRY_VERIFY(win.actFit_->isEnabled());
    QTRY_COMPARE(btn->property("toolFill").toString(), QStringLiteral("accent"));
    const QImage live = btn->grab().toImage();
    QVERIFY2(nearColor(live.pixelColor(live.width() / 2, 3), accent, 50),
             "an enabled fit button is not accent-filled");
    Q_UNUSED(pal);
  }
  // Clickable toolbar controls carry the hand cursor, and dead ones the "no entry" —
  // the browser's `button { cursor: pointer }` / `button:disabled { cursor: not-allowed }`.
  // Text fields and combos are left alone: the browser shows the I-beam and arrow there too.
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
    int live = 0, dead = 0;
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
          ++dead;
          QVERIFY2(shape == Qt::ForbiddenCursor || shape == Qt::PointingHandCursor,
                   "a dead control should not read as clickable");
        }
      }
    QVERIFY2(live > 5, "expected several live toolbar controls");
    // A control that goes dead swaps to the refusal cursor.
    QToolButton* crop = qobject_cast<QToolButton*>(win.buttonForAction(win.actCrop_));
    QVERIFY(crop);
    QCOMPARE(crop->cursor().shape(), Qt::PointingHandCursor);
    win.canvas_->clearImage();
    win.refreshActions();
    QCOMPARE(crop->cursor().shape(), Qt::ForbiddenCursor);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbarFill.gui.moc"
