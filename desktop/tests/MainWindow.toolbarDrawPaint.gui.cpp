// MainWindow GUI e2e — How the draw toggles PAINT: the accent per state, sibling glyphs, readable faces.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowPaint.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The Start/Stop toggle is an ACCENT toggle, not a status light (browser #draw-toggle):
  // OUTLINED while idle — accent ring, accent glyph, neutral face — and accent-FILLED with
  // the on-accent ink while a session is live. The bug this locks down: it carried the
  // sections' permanent toolFill, so both states were the same filled accent chip and the
  // button said nothing about which one you were in. The accent is the USER's, so every
  // assertion is made again after switching it — a hard-coded colour cannot pass twice.
  void drawToggleWearsTheThemeAccentPerState() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QToolButton* btn = win.startDrawBtn_;
    QVERIFY(btn);
    QVERIFY2(btn->property("toolFill").toString().isEmpty(),
             "the draw toggle must opt out of the section fill — its accent IS its state");

    // The chip's own ground, read well inside it (clear of the glyph and the word).
    const auto ground = [btn] {
      QTest::qWait(30);
      const QImage im = btn->grab().toImage();
      return im.pixelColor(3, im.height() / 2);
    };
    // …and its 1px outline, at the same height.
    const auto outline = [btn] {
      const QImage im = btn->grab().toImage();
      return im.pixelColor(0, im.height() / 2);
    };
    // The glyph's tint: the mean of the pixels the line-art actually covers.
    const auto glyph = [btn] {
      const QImage im = btn->icon().pixmap(QSize(18, 18), 1.0).toImage();
      long r = 0, g = 0, b = 0, n = 0;
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x) {
          const QColor c = im.pixelColor(x, y);
          if (c.alpha() > 200) { r += c.red(); g += c.green(); b += c.blue(); ++n; }
        }
      return n ? QColor(int(r / n), int(g / n), int(b / n)) : QColor();
    };

    for (const QString& accentKey : {QStringLiteral("violet"), QStringLiteral("grass")}) {
      auto s = win.settings_;
      s.accentColor = accentKey;
      win.applySettings(s, /*persist=*/false);
      const QColor accent = stencil::gui::accentPrimary(accentKey);
      const QString why = QStringLiteral(" (accent %1)").arg(accentKey);
      // The re-theme repaints the window and SWAPS this button's face; 80ms was enough only
      // when the accent had not really moved. Wait for the glyph itself to arrive.
      const QColor ink = stencil::gui::themePalette(win.paintedDark_, accentKey).textMain;
      QTRY_VERIFY_WITH_TIMEOUT(nearColor(glyph(), ink, 40), 3000);

      // ── Idle: the plain UI outline and the theme's own ink, with NO accent anywhere on
      // it (user decision) — the accent is what the RUNNING state says, and saying it in
      // both states said nothing about which one you were in.
      QVERIFY(!canvas->isDrawing());
      QCOMPARE(btn->property("drawToggle").toString(), QString("idle"));
      QVERIFY2(!nearColor(outline(), accent, 40), qPrintable("idle wears the accent ring" + why));
      QVERIFY2(!nearColor(ground(), accent, 50), qPrintable("idle is accent-FILLED" + why));
      QVERIFY2(nearColor(glyph(), ink, 40), qPrintable("the idle ▶ is not the theme's ink" + why));

      // ── Drawing: filled, in the on-accent ink the app's other filled accent controls
      // use (chatDock's send/attach/gear) — white on violet, near-black on grass.
      QAction* start = actionByText(&win, "Start Drawing");
      QVERIFY(start);
      start->trigger();
      QTRY_VERIFY(canvas->isDrawing());
      QTRY_COMPARE(btn->property("drawToggle").toString(), QString("on"));
      // The property flips at the face swap's PIVOT, with the glyph still turning in —
      // so every pixel sample below waits for the face to land rather than reading the
      // frame that happens to be up.
      QTRY_VERIFY2_WITH_TIMEOUT(nearColor(ground(), accent, 50),
                                qPrintable("drawing is not accent-filled" + why), 3000);
      QTRY_VERIFY2_WITH_TIMEOUT(nearColor(glyph(), stencil::gui::onAccentInk(accent), 40),
                                qPrintable("the ■ is not the on-accent foreground" + why), 3000);
      btn->defaultAction()->trigger();
      QTRY_VERIFY(!canvas->isDrawing());
      QTRY_COMPARE(btn->property("drawToggle").toString(), QString("idle"));
    }

    // ── Disabled still looks disabled: with no image there is nothing to draw on, and a
    // toggle you cannot press must not wear the accent in either shape.
    MainWindow empty(nullptr, false);
    empty.resize(1400, 700);
    empty.show();
    QVERIFY(QTest::qWaitForWindowExposed(&empty));
    settleLayout(&empty, 150);
    QToolButton* dead = empty.startDrawBtn_;
    QVERIFY(dead);
    QVERIFY2(!dead->isEnabled(), "the draw toggle is live with no image loaded");
    const QColor deadAccent = stencil::gui::accentPrimary(empty.settings_.accentColor);
    const QImage im = dead->grab().toImage();
    QVERIFY2(!nearColor(im.pixelColor(3, im.height() / 2), deadAccent, 50),
             "a disabled draw toggle is accent-filled");
    QVERIFY2(!nearColor(im.pixelColor(0, im.height() / 2), deadAccent, 40),
             "a disabled draw toggle keeps its accent ring");
    beat();
  }
  // The Line/Rect toggle's two faces must read as SIBLINGS — one drawing vocabulary, not a
  // stroked PENCIL (an edit verb, and the rename affordance's own glyph) beside a solid
  // slab. Both are now outlines of the same weight on the same grid.
  void drawModeGlyphsAreSiblings() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QToolButton* mode = win.drawModeBtn_;
    QVERIFY(mode);
    const char* GLYPH = stencil::gui::FACE_GLYPH_PROPERTY;
    QCOMPARE(mode->property(GLYPH).toString(), QString("line"));
    // The glyph is RECORDED when the swap settles, so let it land before reading it.
    mode->click();
    QTRY_COMPARE(mode->text(), QString("Rect"));
    QTRY_COMPARE(mode->property(GLYPH).toString(), QString("rect"));
    mode->click();
    QTRY_COMPARE(mode->text(), QString("Line"));
    QTRY_COMPARE(mode->property(GLYPH).toString(), QString("line"));

    // …and they really are the same KIND of picture. An OUTLINE is hollow where a filled
    // slab is solid, and the two faces carry a comparable amount of ink — which is what
    // "siblings" means here, and what a pencil-beside-a-slab pair failed.
    const auto glyph = [](const QString& name) {
      return stencil::gui::themedIcon(name, QColor(Qt::black), 32, 1.0)
          .pixmap(32, 32).toImage().convertToFormat(QImage::Format_ARGB32);
    };
    const auto ink = [](const QImage& im) {
      int on = 0;
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x)
          if (qAlpha(im.pixel(x, y)) > 60) on++;
      return double(on) / (im.width() * im.height());
    };
    // Well inside the rectangle, and well off both the strokes and the line's diagonal.
    const auto solidInside = [](const QImage& im) {
      return qAlpha(im.pixel(im.width() * 3 / 10, im.height() * 3 / 10)) > 60;
    };
    const QImage line = glyph("line"), rect = glyph("rect"), slab = glyph("rect-filled");
    QVERIFY2(solidInside(slab), "rect-filled is the SLAB this pair must not be");
    QVERIFY2(!solidInside(rect), "the rect face is an outline");
    QVERIFY2(!solidInside(line), "…and so is the line face");
    const double li = ink(line), ri = ink(rect);
    QVERIFY2(qMax(li, ri) < 2.0 * qMin(li, ri),
             qPrintable(QString("the pair is lopsided: line %1 vs rect %2").arg(li).arg(ri)));
    beat();
  }
  // The Start/Stop and Line/Rect faces read as WORDS: a size up from the toolbar's dense
  // default, with the glyph+label pair CENTRED in the button. Qt anchors a text-beside-icon
  // label at the left of the content rect and keeps its own slack on the right, so equal
  // padding drew the pair off-centre in its box — the theme
  // moves that slack to the left. Pins both halves.
  void drawFaceButtonsAreCentredAndReadable() {
    MainWindow win(nullptr, false);
    win.resize(1500, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    settleLayout(&win, 300);
    for (QToolButton* b : { win.startDrawBtn_, win.drawModeBtn_ }) {
      QVERIFY(b);
      const QString who = b->text();
      QVERIFY2(b->font().pixelSize() >= 12,
               qPrintable(who + " kept the toolbar's small font: "
                          + QString::number(b->font().pixelSize())));
      // Where the face's ink sits inside the box, ignoring the 1px border.
      // Photographed through the WINDOW, not the button: a QSS-styled child grabs empty
      // under the offscreen platform until it has painted once in its own right.
      const QImage im = win.grab(QRect(b->mapTo(&win, QPoint(0, 0)), b->size())).toImage();
      // Sampled INSIDE the box, clear of its 1px outline (which is ink of its own).
      const QRgb bg = im.pixel(5, im.height() / 2);
      int left = -1, right = -1;
      for (int x = 5; x < im.width() - 5; ++x)
        for (int y = 6; y < im.height() - 6; ++y) {
          const QRgb c = im.pixel(x, y);
          if (qAbs(qRed(c) - qRed(bg)) + qAbs(qGreen(c) - qGreen(bg))
                  + qAbs(qBlue(c) - qBlue(bg)) > 90) {
            if (left < 0) left = x;
            right = x;
            break;
          }
        }
      QVERIFY2(left > 0 && right > left, qPrintable(who + " painted no face at all"));
      // Centred within a few pixels — the glyphs carry their own transparent margins, so
      // this is about balance, not a pixel identity.
      const int slack = qAbs(left - (im.width() - 1 - right));
      QVERIFY2(slack <= 12, qPrintable(QString("%1 sits off-centre: %2px left, %3px right")
                                           .arg(who).arg(left).arg(im.width() - 1 - right)));
      QVERIFY2(left <= 16, qPrintable(QString("%1 is pushed in from the left (%2px)")
                                          .arg(who).arg(left)));
      // …and the word is not welded to the glyph: the widest empty column run INSIDE the
      // face is the gap between them (Qt's own is a fixed 4px — FACE_ICON_GAP adds the rest).
      int gap = 0, run = 0;
      for (int x = left; x <= right; ++x) {
        bool ink = false;
        for (int y = 6; y < im.height() - 6 && !ink; ++y) {
          const QRgb c = im.pixel(x, y);
          ink = qAbs(qRed(c) - qRed(bg)) + qAbs(qGreen(c) - qGreen(bg))
                    + qAbs(qBlue(c) - qBlue(bg)) > 90;
        }
        if (ink) { gap = std::max(gap, run); run = 0; } else { run++; }
      }
      QVERIFY2(gap >= 6, qPrintable(QString("%1's glyph and word are welded (%2px apart)")
                                        .arg(who).arg(gap)));
    }
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbarDrawPaint.gui.moc"
