// MainWindow GUI e2e — The toolbar's sections and their controls: draw toggles, colour swatches,
// filters, the export split and the settings row.
// Shared ground (helpers, the loaded window, the motion pins) is in mainWindow.gui.hpp.
#include "mainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The Share button exists only where the OS has a share sheet of its own — hidden on
  // Linux, where none does. Browser parity: supportsShareFiles() (utils.js) keeps
  // #share-image display:none on every browser that turns files down, rather than
  // offering a button that can only apologise.
  void shareButtonOnlyWhereTheOsShares() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());   // the IMAGE cluster shows its icons
    QTest::qWait(120);

    const bool shares = stencil::support::shareSheetAvailable();
    QCOMPARE(win.actShareImage_->isVisible(), shares);
    QToolButton* btn = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->defaultAction() == win.actShareImage_) btn = b;
    QVERIFY2(btn, "the Share action has no toolbar button at all");
    QCOMPARE(btn->isVisible(), shares);
    if (shares) {
      QVERIFY(win.actShareImage_->isEnabled());
      return;
    }
    // …and the chord is dead with it: Qt will not enable an invisible action, so the
    // refresh that follows every image load cannot bring it back.
    QVERIFY2(!win.actShareImage_->isEnabled(), "the Share chord is live with no share sheet");
    win.actShareImage_->setEnabled(true);
    QVERIFY2(!win.actShareImage_->isEnabled(), "an invisible Share action took enabling");
  }

  // Browser parity: both colour swatches are present and captioned. defaultPointColor was
  // persisted but had no swatch, so it was only settable by editing settings.json — and two
  // identical swatches need captions to tell apart. Since the style rows became NAMED
  // sections (makeToolSection, like the main row and like the browser's LINE / POINT
  // clusters), the naming is two-level: an uppercase section header plus an inline field
  // label. Both halves are pinned — a swatch under a bare "Color" with no group header is
  // exactly as ambiguous as the unlabelled swatch this test was written for.
  void toolbarExposesCaptionedLineAndPointColourSwatches() {
    MainWindow win(nullptr, false);

    const auto labelWithText = [&win](const QString& needle) {
      for (QLabel* l : win.findChildren<QLabel*>())
        if (l->text().contains(needle, Qt::CaseInsensitive)) return true;
      return false;
    };
    const auto sectionNamed = [&win](const QString& title) {
      for (QLabel* l : win.findChildren<QLabel*>())
        if (l->objectName() == "sectionLabel" && l->text().compare(title, Qt::CaseInsensitive) == 0)
          return true;
      return false;
    };
    QVERIFY2(sectionNamed("Line"), "the line group needs a section header");
    QVERIFY2(sectionNamed("Point"), "the point group needs a section header");
    QVERIFY2(labelWithText("Color"), "each colour swatch still needs its own field label");
    // The rows carry captions at all — the browser names every toolbar cluster. The
    // filter combo opens EDIT (browser order), so there is no separate Filter section.
    QVERIFY2(!sectionNamed("Filter"), "the filter combo lives in EDIT, like the browser's");
    QVERIFY2(sectionNamed("Edit") && sectionNamed("View"), "the edit/view groups are named");
    QVERIFY2(sectionNamed("Zoom") && sectionNamed("Page") && sectionNamed("Formula")
                 && sectionNamed("Data") && sectionNamed("Settings"),
             "the last row's groups are named, in the browser's order");

    QToolButton* pointSwatch = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->toolTip().contains("Point color")) pointSwatch = b;
    QVERIFY2(pointSwatch, "the toolbar must offer a default point-colour swatch");
    QVERIFY2(!pointSwatch->icon().isNull(), "the swatch paints its current colour");
  }

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
    QTRY_VERIFY(canvas->isDrawing());
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
    QTRY_VERIFY(!canvas->isDrawing());
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

    const auto near = [](const QColor& a, const QColor& b, int tol) {
      return qAbs(a.red() - b.red()) < tol && qAbs(a.green() - b.green()) < tol
             && qAbs(a.blue() - b.blue()) < tol;
    };
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
      QTRY_VERIFY_WITH_TIMEOUT(near(glyph(), ink, 40), 3000);

      // ── Idle: the plain UI outline and the theme's own ink, with NO accent anywhere on
      // it (user decision) — the accent is what the RUNNING state says, and saying it in
      // both states said nothing about which one you were in.
      QVERIFY(!canvas->isDrawing());
      QCOMPARE(btn->property("drawToggle").toString(), QString("idle"));
      QVERIFY2(!near(outline(), accent, 40), qPrintable("idle wears the accent ring" + why));
      QVERIFY2(!near(ground(), accent, 50), qPrintable("idle is accent-FILLED" + why));
      QVERIFY2(near(glyph(), ink, 40), qPrintable("the idle ▶ is not the theme's ink" + why));

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
      QTRY_VERIFY2_WITH_TIMEOUT(near(ground(), accent, 50),
                                qPrintable("drawing is not accent-filled" + why), 3000);
      QTRY_VERIFY2_WITH_TIMEOUT(near(glyph(), stencil::gui::onAccentInk(accent), 40),
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
    QTest::qWait(150);
    QToolButton* dead = empty.startDrawBtn_;
    QVERIFY(dead);
    QVERIFY2(!dead->isEnabled(), "the draw toggle is live with no image loaded");
    const QColor deadAccent = stencil::gui::accentPrimary(empty.settings_.accentColor);
    const QImage im = dead->grab().toImage();
    QVERIFY2(!near(im.pixelColor(3, im.height() / 2), deadAccent, 50),
             "a disabled draw toggle is accent-filled");
    QVERIFY2(!near(im.pixelColor(0, im.height() / 2), deadAccent, 40),
             "a disabled draw toggle keeps its accent ring");
    beat();
  }

  // Both Draw toggles cross over through the ONE shared swap (support/faceSwap.hpp): the
  // glyph turns and the word fades out, they are exchanged at the invisible pivot, and the
  // new pair turns back in. What must hold whatever the user does: the button never
  // resizes, a burst of toggles always lands on the REAL state, and reduced motion goes
  // straight to the end state. (The suite runs with STENCIL_NO_ANIM=1, so this case takes
  // it off for the animated half and puts it back for the last one.)
  void drawTogglesSwapTheirFaceAndConverge() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });

    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QToolButton* btn = win.startDrawBtn_;
    QToolButton* mode = win.drawModeBtn_;
    QVERIFY(btn && mode);
    const QSize drawSize = btn->size();
    const QSize modeSize = mode->size();

    // ── Start → Stop: the word is exchanged at the pivot, not at the click.
    QAction* start = actionByText(&win, "Start Drawing");
    QVERIFY(start);
    start->trigger();
    QVERIFY2(canvas->isDrawing(), "the drawing state itself must not wait for the animation");
    QVERIFY2(stencil::gui::faceSwapping(btn), "the toggle snapped instead of swapping");
    QCOMPARE(btn->text(), QString("Start"));   // still the outgoing face
    QCOMPARE(btn->size(), drawSize);           // …and the row has not shifted
    QTRY_COMPARE(btn->text(), QString("Stop"));
    QTRY_VERIFY(!stencil::gui::faceSwapping(btn));
    QCOMPARE(btn->size(), drawSize);
    QCOMPARE(btn->property("drawToggle").toString(), QString("on"));
    QVERIFY2(btn->styleSheet().isEmpty(), "the swap's colour override outlived it");

    // ── Line ↔ Rect: the same swap, the same pinned box, and — unlike Start/Stop — a
    // PERMANENT accent fill (it has no idle/on pair of its own, and no QAction for
    // styleDangerToolButtons' pass to reach, so it carries the property itself). The
    // browser's #draw-mode-toggle is a bare <button>, filled at rest for the same reason.
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
      QTest::qWait(stencil::gui::kFaceSwapMs / 5);   // interrupt the swap in flight
      mode->click();
      QTest::qWait(stencil::gui::kFaceSwapMs / 5);
    }
    QTRY_VERIFY(!stencil::gui::faceSwapping(btn) && !stencil::gui::faceSwapping(mode));
    QCOMPARE(btn->text(), canvas->isDrawing() ? QString("Stop") : QString("Start"));
    QCOMPARE(btn->property("drawToggle").toString(),
             canvas->isDrawing() ? QString("on") : QString("idle"));
    QCOMPARE(btn->defaultAction()->text(),
             canvas->isDrawing() ? QString("Stop Drawing") : QString("Start Drawing"));
    QCOMPARE(mode->text(),
             canvas->drawMode() == CanvasWidget::DrawMode::Rect ? QString("Rect")
                                                                : QString("Line"));
    QCOMPARE(btn->size(), drawSize);
    QCOMPARE(mode->size(), modeSize);
    QVERIFY(btn->styleSheet().isEmpty() && mode->styleSheet().isEmpty());

    // ── Reduced motion: the end state at once, no animation to wait on.
    qputenv("STENCIL_NO_ANIM", "1");
    const bool wasDrawing = canvas->isDrawing();
    btn->defaultAction()->trigger();
    QCOMPARE(canvas->isDrawing(), !wasDrawing);
    QVERIFY2(!stencil::gui::faceSwapping(btn), "reduced motion still animated the swap");
    QCOMPARE(btn->text(), canvas->isDrawing() ? QString("Stop") : QString("Start"));
    QCOMPARE(btn->property("drawToggle").toString(),
             canvas->isDrawing() ? QString("on") : QString("idle"));
    mode->click();
    QVERIFY2(!stencil::gui::faceSwapping(mode), "reduced motion still animated the mode swap");
    QCOMPARE(mode->text(),
             canvas->drawMode() == CanvasWidget::DrawMode::Rect ? QString("Rect")
                                                                : QString("Line"));
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
    const char* kGlyph = stencil::gui::kFaceGlyphProperty;
    QCOMPARE(mode->property(kGlyph).toString(), QString("line"));
    // The glyph is RECORDED when the swap settles, so let it land before reading it.
    mode->click();
    QTRY_COMPARE(mode->text(), QString("Rect"));
    QTRY_COMPARE(mode->property(kGlyph).toString(), QString("rect"));
    mode->click();
    QTRY_COMPARE(mode->text(), QString("Line"));
    QTRY_COMPARE(mode->property(kGlyph).toString(), QString("line"));

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

  // The two Draw toggles are pinned so a label swap can't resize them and shove the row —
  // but the pin must be a MEASUREMENT of the widest label, never a generous guess, or the
  // short face ("Start", "Line") sits in a pool of dead space. Font/locale-independent:
  // the check re-measures rather than naming a number.
  void drawTogglesAreNoWiderThanTheirWidestLabel() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTest::qWait(200);   // the pin is taken once the toolbar is built and shown

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
        {win.startDrawBtn_, {QStringLiteral("Start"), QStringLiteral("Stop")}, "Start/Stop"},
        {win.drawModeBtn_, {QStringLiteral("Line"), QStringLiteral("Rect")}, "Line/Rect"}};
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

  // Copy-to-clipboard ships the RENDERED image: filter plus drawn lines (browser
  // renderExportCanvas parity).
  void copyImageIncludesTheDrawnLines() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    // One fat red line across the middle, straight through the canvas model.
    stencil::core::Line line;
    line.color = "#ff0000";
    line.thickness = 6;
    line.points.push_back({4.0, 20.0});
    line.points.push_back({36.0, 20.0});
    win.canvas_->setLines({line});
    win.dataExport_->copyImageToClipboard();
    const QImage copied = QGuiApplication::clipboard()->image();
    QVERIFY(!copied.isNull());
    // The render ships the default centered crop, so don't pin the size — scan for
    // the red stroke instead: any strongly-red pixel proves the overlay rode along.
    bool sawLine = false;
    for (int y = 0; y < copied.height() && !sawLine; ++y)
      for (int x = 0; x < copied.width() && !sawLine; ++x) {
        const QColor c = copied.pixelColor(x, y);
        if (c.red() > 200 && c.green() < 80 && c.blue() < 80) sawLine = true;
      }
    QVERIFY2(sawLine, "expected the drawn red line in the copied image");
  }

  // FEATURE: Cmd+C / the toolbar Copy button's plain click default to
  // the CURRENT image (tint + lines/points) — same as the browser, same as download,
  // always has (an earlier desktop-only "Ctrl+C defaults to tint" swap was reverted).
  // actCopyImageTint_ ("Filter Only", Ctrl+Alt+C) stays its own separate, fixed variant.
  void copyDefaultIsCurrentImage() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    win.applyImageFilter(QStringLiteral("custom"));
    win.applyTintColor(QColor(200, 30, 30));   // a strong, easy-to-detect red tint
    // "current" vs "tint" only actually differ by the lines/points overlay — add one
    // so the two variants render to genuinely different images below.
    stencil::core::Line line;
    line.color = "#00ff00";
    line.thickness = 6;
    line.points.push_back({4.0, 20.0});
    line.points.push_back({36.0, 20.0});
    win.canvas_->setLines({line});

    // The default gesture (actCopyImage_, Ctrl+C) copies the CURRENT (full) image.
    win.actCopyImage_->trigger();
    const QImage current = QGuiApplication::clipboard()->image();
    QVERIFY(!current.isNull());
    QCOMPARE(current, win.canvas_->renderToImage(QStringLiteral("current")));

    // "Filter Only" (actCopyImageTint_) copies the filtered image with no overlay instead.
    win.actCopyImageTint_->trigger();
    const QImage tinted = QGuiApplication::clipboard()->image();
    QVERIFY(!tinted.isNull());
    QCOMPARE(tinted, win.canvas_->renderToImage(QStringLiteral("tint")));
    QVERIFY2(current != tinted, "current and tint must actually render differently here");
  }

  // FEATURE: "Filter Only" would render byte-identical to "Original" with
  // no filter applied, so it's hidden (not just greyed) until one actually is — live as
  // the filter is toggled, not just on the next unrelated refresh.
  void filterOnlyHiddenWithNoFilterApplied() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    win.refreshActions();
    QVERIFY2(!win.actCopyImageTint_->isVisible(), "Filter Only shows with no filter applied");
    QVERIFY2(!win.actSaveImageTint_->isVisible(), "Filter Only shows with no filter applied");

    win.applyImageFilter(QStringLiteral("sepia"));
    QVERIFY2(win.actCopyImageTint_->isVisible(), "Filter Only should show once a filter is active");
    QVERIFY2(win.actSaveImageTint_->isVisible(), "Filter Only should show once a filter is active");

    win.applyImageFilter(QStringLiteral("none"));
    QVERIFY2(!win.actCopyImageTint_->isVisible(), "Filter Only should hide again once the filter clears");
    QVERIFY2(!win.actSaveImageTint_->isVisible(), "Filter Only should hide again once the filter clears");
  }

  // FEATURE: "Current"'s OWN row (actCopyImageCurrentRow_/
  // actSaveImageCurrentRow_) would render byte-identical to Original/Filter Only with
  // nothing drawn — hidden until there's something to overlay, same reasoning as Filter
  // Only. A SEPARATE action from actCopyImage_/actSaveImage_ (the toolbar buttons' own,
  // which stay visible/enabled throughout — a QToolButton mirrors its action's
  // visibility, so hiding THOSE would take the toolbar icon down with them) but firing
  // the identical operation.
  void currentRowHiddenWithNoLinesButToolbarButtonStays() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    win.refreshActions();
    QVERIFY2(!win.actCopyImageCurrentRow_->isVisible(), "Current's row shows with nothing drawn");
    QVERIFY2(!win.actSaveImageCurrentRow_->isVisible(), "Current's row shows with nothing drawn");
    QVERIFY2(win.actCopyImage_->isVisible(), "the toolbar's own Copy action must stay visible regardless");
    QVERIFY2(win.actSaveImage_->isVisible(), "the toolbar's own Download action must stay visible regardless");
    QVERIFY(win.actCopyImage_->isEnabled());

    stencil::core::Line line;
    line.color = "#00ff00";
    line.points.push_back({4.0, 20.0});
    line.points.push_back({36.0, 20.0});
    win.canvas_->setLines({line});
    win.refreshActions();
    QVERIFY2(win.actCopyImageCurrentRow_->isVisible(), "Current's row should show once something is drawn");
    QVERIFY2(win.actSaveImageCurrentRow_->isVisible(), "Current's row should show once something is drawn");

    // Clicking the row performs the exact same thing as the toolbar button.
    win.actCopyImageCurrentRow_->trigger();
    const QImage viaRow = QGuiApplication::clipboard()->image();
    win.actCopyImage_->trigger();
    QCOMPARE(QGuiApplication::clipboard()->image(), viaRow);

    win.canvas_->setLines({});
    win.refreshActions();
    QVERIFY2(!win.actCopyImageCurrentRow_->isVisible(), "Current's row should hide again once lines are cleared");
    QVERIFY2(!win.actSaveImageCurrentRow_->isVisible(), "Current's row should hide again once lines are cleared");
    QVERIFY2(win.actCopyImage_->isVisible(), "the toolbar's own Copy action is still untouched");
  }

  // REGRESSION: MenuHotkeyChips only ever placed/hid a row's
  // chip on the menu's OWN aboutToShow — an action going invisible out from under an
  // ALREADY-OPEN menu (e.g. turning the filter off while its download-options popup is
  // still up) left that chip floating at its last valid position, overlapping whatever
  // row now sits there instead. Needs the live poll (menuHotkeys.hpp installPlacer).
  void filterOnlyChipHidesLiveWhileItsMenuStaysOpen() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    win.applyImageFilter(QStringLiteral("sepia"));   // Filter Only visible before the popup opens

    QWidget* saveBtn = win.buttonForAction(win.actSaveImage_);
    QVERIFY(saveBtn);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, saveBtn->rect().center(),
                          saveBtn->mapToGlobal(saveBtn->rect().center()));
    QApplication::sendEvent(saveBtn, &ctx);
    QMenu* menu = win.saveImageOptionsMenu_;
    const bool opened = menu && menu->isVisible();

    auto chipOver = [&](QAction* act) -> stencil::gui::TipBody* {
      const QRect r = menu->actionGeometry(act);
      for (QLabel* l : menu->findChildren<QLabel*>())
        if (auto* c = dynamic_cast<stencil::gui::TipBody*>(l))
          if (!c->isHidden() && c->geometry().intersects(r)) return c;
      return nullptr;
    };
    // Captured into locals and the menu closed BEFORE any assertion — an early QVERIFY2
    // return must never leave the menu open, or it outlives `win` and crashes on teardown
    // (exportOptionsPopupIsNotWiderThanItsContent's own comment has the full story).
    bool chippedWhileActive = false, stillChippedAfter = true;
    if (opened) {
      chippedWhileActive = chipOver(win.actSaveImageTint_) != nullptr;
      // Turn the filter off WHILE the popup stays open — no click, no reopen — and
      // give the live poll (menuHotkeys.hpp) a moment to catch up.
      win.applyImageFilter(QStringLiteral("none"));
      for (int i = 0; i < 20 && stillChippedAfter; ++i) {
        QTest::qWait(20);
        stillChippedAfter = chipOver(win.actSaveImageTint_) != nullptr;
      }
      menu->close();
    }
    QVERIFY2(opened, "the download options popup never opened");
    QVERIFY2(chippedWhileActive, "Filter Only should be chipped while the filter is active");
    QVERIFY2(!stillChippedAfter, "Filter Only's chip is still floating after the filter cleared");
  }

  // FEATURE: "With Compare" is a SEPARATE action (actCopyImageSplit_/
  // actSaveImageSplit_), not a relabeling of "Current" — actCopyImage_/actSaveImage_
  // always read/perform "Current", comparing or not. The split action is only VISIBLE
  // while a split compare view is active, and only then does it borrow the real
  // Ctrl+C/Ctrl+Shift+D shortcut from its "Current" sibling (syncSplitCopyDownloadSlot()) —
  // giving the shortcut back the moment compare turns off. The literal "Filter Only" row
  // (actCopyImageTint_) is unaffected either way — it never follows compare state.
  void copyDownloadSplitTakesThePrimaryGesture() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    // "Current"'s own row (actCopyImageCurrentRow_) only shows once something is
    // drawn — see currentRowHiddenWithNoLinesButToolbarButtonStays. This test's own
    // regression block below needs it visible to find its chip.
    {
      stencil::core::Line line;
      line.points.push_back({4.0, 20.0});
      line.points.push_back({36.0, 20.0});
      win.canvas_->setLines({line});
    }

    win.refreshActions();
    QCOMPARE(win.actCopyImage_->text(), QString("Current (Tint + Lines/Points)"));
    QCOMPARE(win.actSaveImage_->text(), QString("Current (Tint + Lines/Points)"));
    QVERIFY2(!win.actCopyImageSplit_->isVisible(), "With Compare shows outside compare");
    QVERIFY2(!win.actSaveImageSplit_->isVisible(), "With Compare shows outside compare");
    const QKeySequence copyShortcut = win.actCopyImage_->shortcut();
    const QKeySequence saveShortcut = win.actSaveImage_->shortcut();
    QVERIFY2(!copyShortcut.isEmpty(), "Current should carry the real Ctrl+C outside compare");

    // Prime MenuHotkeyChips' per-action combo cache with "Current"'s Ctrl+C BEFORE
    // compare mode ever turns on — the ordinary way a user would have already opened
    // this popup at some point. The real regression only shows up on a SECOND open,
    // once the shortcut has since moved elsewhere (below).
    {
      QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
      QVERIFY(copyBtn);
      QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                            copyBtn->mapToGlobal(copyBtn->rect().center()));
      QApplication::sendEvent(copyBtn, &ctx);
      QVERIFY2(win.copyImageOptionsMenu_->isVisible(), "priming popup never opened");
      win.copyImageOptionsMenu_->close();
    }

    win.canvas_->setCompareMode(QStringLiteral("vertical"));
    win.refreshActions();
    // "Current" never relabels — it's still there, unaffected, beside the new row.
    QCOMPARE(win.actCopyImage_->text(), QString("Current (Tint + Lines/Points)"));
    QCOMPARE(win.actSaveImage_->text(), QString("Current (Tint + Lines/Points)"));
    QCOMPARE(win.actCopyImageSplit_->text(), QString("With Compare"));
    QCOMPARE(win.actSaveImageSplit_->text(), QString("With Compare"));
    QVERIFY2(win.actCopyImageSplit_->isVisible(), "With Compare must show while comparing");
    QVERIFY2(win.actSaveImageSplit_->isVisible(), "With Compare must show while comparing");
    // The shortcut moved onto the split action; "Current" is left with none (Qt would
    // otherwise flag two enabled actions sharing one shortcut as ambiguous).
    QCOMPARE(win.actCopyImageSplit_->shortcut(), copyShortcut);
    QCOMPARE(win.actSaveImageSplit_->shortcut(), saveShortcut);
    QVERIFY(win.actCopyImage_->shortcut().isEmpty());
    QVERIFY(win.actSaveImage_->shortcut().isEmpty());
    QVERIFY2(win.copyImageOptionsMenu_->actions().contains(win.actCopyImageSplit_),
             "the toolbar popup never got the split row");
    QCOMPARE(win.copyImageOptionsMenu_->actions().first(), win.actCopyImageSplit_);  // leads
    QCOMPARE(win.saveImageOptionsMenu_->actions().first(), win.actSaveImageSplit_);  // leads

    // REGRESSION: MenuHotkeyChips never deleted a row's chip widget on
    // teardown (menuHotkeys.hpp's destructor only restored the action's text/shortcut) —
    // it just sat there, orphaned but still parented (and visible) on the persistent
    // menu. Reopening the SAME popup here, now with a 4th row ahead of it shifting every
    // later row down one slot, lands the leftover chip from the earlier "priming" open
    // squarely on top of whatever row now occupies its old screen position — visually a
    // hotkey combo "still showing" on a row that has none any more.
    {
      QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
      QVERIFY(copyBtn);
      QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                            copyBtn->mapToGlobal(copyBtn->rect().center()));
      QApplication::sendEvent(copyBtn, &ctx);
      QMenu* menu = win.copyImageOptionsMenu_;
      const bool opened = menu && menu->isVisible();
      // Captured into locals and the menu closed BEFORE any assertion — an early
      // QVERIFY2 return must never leave the menu open, or it outlives `win` and
      // crashes on teardown (exportOptionsPopupIsNotWiderThanItsContent's own comment
      // has the full story).
      bool currentChipped = false, splitChipped = false;
      if (opened) {
        const QRect currentRect = menu->actionGeometry(win.actCopyImageCurrentRow_);
        const QRect splitRect = menu->actionGeometry(win.actCopyImageSplit_);
        for (QLabel* l : menu->findChildren<QLabel*>()) {
          auto* chip = dynamic_cast<stencil::gui::TipBody*>(l);
          if (!chip || chip->isHidden()) continue;
          if (chip->geometry().intersects(currentRect)) currentChipped = true;
          if (chip->geometry().intersects(splitRect)) splitChipped = true;
        }
        menu->close();
      }
      QVERIFY2(opened, "the copy-image options popup never opened");
      QVERIFY2(!currentChipped, "Current still shows a hotkey chip while a comparison is active");
      QVERIFY2(splitChipped, "With Compare should carry the chip while comparing");
    }

    win.actCopyImageSplit_->trigger();
    const QImage copiedSplit = QGuiApplication::clipboard()->image();
    QVERIFY(!copiedSplit.isNull());
    QCOMPARE(copiedSplit, win.canvas_->renderToImage(QStringLiteral("split")));

    // "Current" stays reachable — via the menu, with no hotkey of its own right now —
    // and still means the plain edited frame, not the split, even while comparing.
    win.actCopyImage_->trigger();
    QCOMPARE(QGuiApplication::clipboard()->image(), win.canvas_->renderToImage(QStringLiteral("current")));

    // The literal "Filter Only" row never auto-switches to the split composite just
    // because a compare view is active — it keeps rendering tint-only, no overlay.
    win.actCopyImageTint_->trigger();
    QCOMPARE(QGuiApplication::clipboard()->image(), win.canvas_->renderToImage(QStringLiteral("tint")));

    // Turning compare back off hides the split action again and gives "Current" back its shortcut.
    win.canvas_->setCompareMode(QStringLiteral("none"));
    win.refreshActions();
    QCOMPARE(win.actCopyImage_->text(), QString("Current (Tint + Lines/Points)"));
    QCOMPARE(win.actSaveImage_->text(), QString("Current (Tint + Lines/Points)"));
    QVERIFY(!win.actCopyImageSplit_->isVisible());
    QVERIFY(!win.actSaveImageSplit_->isVisible());
    QCOMPARE(win.actCopyImage_->shortcut(), copyShortcut);
    QCOMPARE(win.actSaveImage_->shortcut(), saveShortcut);
    win.actCopyImage_->trigger();
    QCOMPARE(QGuiApplication::clipboard()->image(), win.canvas_->renderToImage(QStringLiteral("current")));
  }

  // Destructive toolbar buttons wear the browser's `.danger.btn-icon` face: a SOLID red
  // fill with a glyph that is not itself red (a red glyph on a red fill is an empty
  // button). A QToolButton re-copies its action's icon on every QEvent::ActionChanged.
  void dangerToolButtonsAreFilledRed() {
    MainWindow win;
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QList<QToolButton*> filled;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->property("toolFill").toString() == QLatin1String("danger")) filled << b;
    QVERIFY2(!filled.isEmpty(), "no destructive toolbar button carries the danger fill");

    const QColor danger = stencil::gui::themePalette(false).danger;
    const QColor dangerDark = stencil::gui::themePalette(true).danger;
    // The glyph as the BUTTON draws it: white, and specifically not either danger red.
    const auto glyphIsWhite = [&](QToolButton* b) {
      const QImage im = b->icon().pixmap(18, 18).toImage();
      int white = 0, red = 0;
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x) {
          const QColor c = im.pixelColor(x, y);
          if (c.alpha() < 60) continue;
          const auto near = [&c](const QColor& d) {
            return qAbs(c.red() - d.red()) < 45 && qAbs(c.green() - d.green()) < 45
                && qAbs(c.blue() - d.blue()) < 45;
          };
          if (near(QColor(Qt::white))) ++white;
          else if (near(danger) || near(dangerDark)) ++red;
        }
      return white > 0 && red == 0;
    };
    for (QToolButton* b : filled) {
      QVERIFY2(glyphIsWhite(b), qPrintable(QString("%1: the glyph is not white on the red fill")
                                               .arg(b->defaultAction()->text())));
      // Anything that flips the action re-syncs the button — the glyph must survive it.
      QAction* a = b->defaultAction();
      const bool was = a->isEnabled();
      a->setEnabled(!was);
      a->setEnabled(was);
      win.refreshActions();
      QVERIFY2(glyphIsWhite(b), qPrintable(QString("%1: the action's red glyph came back after a refresh")
                                               .arg(a->text())));
      // …while the ACTION — what the menus and the canvas context menu render —
      // keeps the NEUTRAL glyph: the browser paints every menu icon in
      // --text-muted and reserves the red for this filled button (a red mark on a
      // plain menu row was a desktop-only invention, and unreadable in dark).
      const QImage menu = a->icon().pixmap(18, 18).toImage();
      bool menuRed = false;
      for (int y = 0; y < menu.height() && !menuRed; ++y)
        for (int x = 0; x < menu.width(); ++x) {
          const QColor c = menu.pixelColor(x, y);
          if (c.alpha() < 60) continue;
          const auto near = [&c](const QColor& d) {
            return qAbs(c.red() - d.red()) < 45 && qAbs(c.green() - d.green()) < 45
                && qAbs(c.blue() - d.blue()) < 45;
          };
          if (near(danger) || near(dangerDark)) { menuRed = true; break; }
        }
      QVERIFY2(!menuRed,
               qPrintable(QString("%1: the menu entry is still painted danger red").arg(a->text())));
    }
  }

  // A disabled toolbar icon must READ as disabled: `color: MUTED` never reaches a
  // rasterised pixmap (the browser's `.ic` gets it from currentColor), so themedIcon
  // carries a faded QIcon::Disabled variant. Checked at 1x AND 2x — a dpr-tagged pixmap
  // paints at LOGICAL size into a device-sized target.
  void disabledIconsTakeTheMutedInk() {
    const auto inkBox = [](const QImage& im) {
      int minx = im.width(), miny = im.height(), maxx = -1, maxy = -1;
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x)
          if (im.pixelColor(x, y).alpha() > 20) {
            minx = qMin(minx, x); maxx = qMax(maxx, x);
            miny = qMin(miny, y); maxy = qMax(maxy, y);
          }
      return maxx < 0 ? QRect() : QRect(minx, miny, maxx - minx + 1, maxy - miny + 1);
    };
    const auto meanAlpha = [](const QImage& im) {
      double sum = 0;
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x) sum += im.pixelColor(x, y).alphaF();
      return sum / (im.width() * im.height());
    };
    for (qreal dpr : {1.0, 2.0}) {
      const QIcon ic = stencil::gui::themedIcon("download", QColor("#e0e0e0"), 18, dpr);
      const QImage on = ic.pixmap(18, 18, QIcon::Normal).toImage();
      const QImage off = ic.pixmap(18, 18, QIcon::Disabled).toImage();
      const QString at = QString(" (at %1x)").arg(dpr);
      QCOMPARE(off.size(), on.size());
      // Same glyph, same place, same size — only the ink is lighter.
      QVERIFY2(inkBox(off) == inkBox(on),
               qPrintable(QString("disabled glyph moved/resized%1: %2 vs %3")
                              .arg(at, QDebug::toString(inkBox(off)), QDebug::toString(inkBox(on)))));
      // …and at FULL strength, re-inked rather than faded: the browser's disabled button
      // paints its .ic in --disabled-text at opacity 1, and a faded dark glyph was a ghost
      // on the light theme's pale disabled chip.
      const double a = meanAlpha(on), b = meanAlpha(off);
      QVERIFY2(b > 0.0, qPrintable("a disabled glyph must still be visible" + at));
      QVERIFY2(b > a * 0.9, qPrintable(QString("faded, not re-inked%1: %2 vs %3").arg(at).arg(a).arg(b)));
      // The ink is the theme's --disabled-text — what the stylesheet greys the LABEL to.
      const auto densest = [](const QImage& im) {
        QColor best;
        int bestA = -1;
        for (int y = 0; y < im.height(); ++y)
          for (int x = 0; x < im.width(); ++x) {
            const QColor c = im.pixelColor(x, y);
            if (c.alpha() > bestA) { bestA = c.alpha(); best = c; }
          }
        return best;
      };
      const QColor muted = QGuiApplication::palette().color(QPalette::Disabled, QPalette::WindowText);
      const QColor got = densest(off);
      QVERIFY2(qAbs(got.red() - muted.red()) <= 8 && qAbs(got.green() - muted.green()) <= 8
                   && qAbs(got.blue() - muted.blue()) <= 8,
               qPrintable(QString("disabled ink %1, wanted the muted %2%3")
                              .arg(got.name(), muted.name(), at)));
      QVERIFY2(got != QColor("#e0e0e0"), qPrintable("still the enabled colour" + at));
    }
  }

  // Every control in a toolbar section sits on ONE centre line. The QVBoxLayout used to
  // hand a short section's spare height to its caption, pushing combos/inputs below the
  // icon rows they sit beside (browser parity: one flex row, centred).
  void toolbarSectionControlsShareOneCentreLine() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(200);
    // A section row is the widget whose sibling is the "sectionLabel" caption. The tool
    // run WRAPS (support/wrapRow.hpp), so the baseline is shared per LINE — sections are
    // grouped by where the flow put them, not by which toolbar they belong to.
    QHash<int, QList<QPair<QString, int>>> byRow;
    for (QWidget* rowWidget : win.findChildren<QWidget*>()) {
      QWidget* section = rowWidget->parentWidget();
      if (!section || !section->findChild<QLabel*>("sectionLabel")) continue;
      if (rowWidget->findChild<QLabel*>("sectionLabel")) continue;   // that's the caption itself
      if (!section->parentWidget() || !section->isVisible()) continue;
      const int line = section->mapTo(&win, QPoint(0, 0)).y();
      for (QWidget* c : rowWidget->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!c->isVisible() || c->height() <= 0) continue;
        if (!qobject_cast<QToolButton*>(c) && !qobject_cast<QComboBox*>(c)
            && !qobject_cast<QLineEdit*>(c) && !qobject_cast<QCheckBox*>(c)) continue;
        byRow[line] << qMakePair(QString("%1(%2)").arg(c->metaObject()->className(), c->objectName()),
                                 c->mapTo(&win, QPoint(0, c->height() / 2)).y());
      }
    }
    QVERIFY2(!byRow.isEmpty(), "no toolbar sections were found");
    int rowsChecked = 0;
    for (auto it = byRow.constBegin(); it != byRow.constEnd(); ++it) {
      const auto& controls = it.value();
      if (controls.size() < 2) continue;
      ++rowsChecked;
      const int centre = controls.first().second;
      for (const auto& c : controls)
        QVERIFY2(qAbs(c.second - centre) <= 1,
                 qPrintable(QString("%1 sits at y=%2, the row centre is %3")
                                .arg(c.first).arg(c.second).arg(centre)));
    }
    QVERIFY2(rowsChecked >= 2, "expected at least two populated toolbar rows");
  }

  // The labelled Open Image button centres its icon+text. Qt left-aligns a
  // text-beside-icon label inside a hint that reserves more room on the right, so it sat
  // visibly off-centre; the fix redistributes the button's padding and this measures the
  // result, since it is tuned to the style's own slack.
  void openImageButtonLabelIsCentred() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.canvas_->clearImage();
    win.refreshActions();
    QTest::qWait(200);
    QVERIFY2(win.openImageBtn_->isVisible(), "the labelled button is not on screen");
    const QImage im = win.openImageBtn_->grab().toImage();
    // Ink is everything that is not the button's own fill (glyph + label are white on it).
    const QColor fill = im.pixelColor(1, im.height() / 2);
    int minx = im.width(), maxx = -1;
    for (int y = 0; y < im.height(); ++y)
      for (int x = 0; x < im.width(); ++x) {
        const QColor c = im.pixelColor(x, y);
        if (qAbs(c.red() - fill.red()) + qAbs(c.green() - fill.green())
            + qAbs(c.blue() - fill.blue()) < 60) continue;
        minx = qMin(minx, x); maxx = qMax(maxx, x);
      }
    QVERIFY2(maxx > minx, "no label ink found on the button");
    const int left = minx, right = im.width() - 1 - maxx;
    QVERIFY2(qAbs(left - right) <= 2,
             qPrintable(QString("icon+text is off-centre: %1px left, %2px right").arg(left).arg(right)));
    // …and the label is the browser's 14px, not the smaller platform default.
    QCOMPARE(win.openImageBtn_->font().pixelSize(), 14);
  }

  // A checkable toolbar toggle is NOT accent-filled at rest — the accent is what "on"
  // looks like (browser #chat-btn: ghost, .active fills). It went permanently filled when
  // every section button was given a fill.
  void checkableToggleFillsOnlyWhenOn() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(150);
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

  // Nothing to zoom without an image, so the whole ZOOM cluster is dead until one is
  // loaded — the browser gates zoom-in / zoom-out / zoom-fit / zoom-input on exactly that
  // (drawingApp.updateButtons), and the desktop row used to stay live and no-op.
  void zoomClusterNeedsAnImage() {
    MainWindow win(nullptr, false);
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(150);
    QVERIFY(win.zoom_);
    for (QAction* a : {win.actZoomIn_, win.actZoomOut_, win.actFit_}) {
      QVERIFY(a);
      QVERIFY2(!a->isEnabled(), qPrintable(a->text() + " is live with no image loaded"));
    }
    QVERIFY2(!win.zoom_->isEnabled(), "the % field is live with no image loaded");
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.actFit_->isEnabled());
    QVERIFY(win.actZoomIn_->isEnabled() && win.actZoomOut_->isEnabled());
    QVERIFY2(win.zoom_->isEnabled(), "the % field stayed dead with an image loaded");
  }

  // Every accent-BACKED control wears the ink the ACCENT picked (theme.hpp onAccentInk).
  // The bug this locks down: a fixed white glyph, which a yellow or sky accent all but
  // swallowed. Browser twin: --on-accent.
  void filledControlsWearTheAccentsOwnInk() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    const auto near = [](const QColor& a, const QColor& b, int tol) {
      return qAbs(a.red() - b.red()) < tol && qAbs(a.green() - b.green()) < tol
             && qAbs(a.blue() - b.blue()) < tol;
    };
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
      QTRY_VERIFY2_WITH_TIMEOUT(near(glyph(filled), ink, 40),
                                qPrintable("a filled button's glyph is not the accent's ink" + why), 3000);

      // …and the stylesheet hands the same ink to every label on the accent.
      const QString qss = stencil::gui::buildStylesheet(win.paintedDark_, accentKey);
      QVERIFY2(qss.contains("color: " + ink.name()),
               qPrintable("the QSS carries no on-accent ink" + why));
      // The empty state's "Open Image" paints its own label (support/openImageButton.hpp),
      // so it reads that ink out of the palette the sheet filled rather than the sheet.
      QVERIFY(win.openImageBtn_);
      QTRY_VERIFY2(win.openImageBtn_->palette().color(QPalette::ButtonText) == ink,
                   qPrintable("Open Image's own painter has no on-accent ink" + why));
    }
  }

  // Fit to window is FILLED like every other acting button (user decision; browser twin:
  // #zoom-fit) — pressing it acts at once, it reports no state. What stays its own is the
  // DISABLED face: the browser's faded outline rather than a filled chip, since it ends
  // the ZOOM row beside a plain field. Asserts both halves.
  void fitToWindowIsFilledAndFadesWhenDead() {
    MainWindow win(nullptr, false);
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(150);
    QToolButton* btn = win.zoomFitBtn_;
    QVERIFY(btn);
    QVERIFY2(btn->property("toolGhost").toBool(),
             "the ghost tag still drives its disabled face");
    const stencil::gui::Palette pal =
        stencil::gui::themePalette(win.paintedDark_, win.settings_.accentColor);
    const auto near = [](const QColor& a, const QColor& b, int tol) {
      return qAbs(a.red() - b.red()) < tol && qAbs(a.green() - b.green()) < tol
             && qAbs(a.blue() - b.blue()) < tol;
    };
    const QColor accent = stencil::gui::accentPrimary(win.settings_.accentColor);
    // Dead (no image yet): the faded outline, and no accent anywhere in it.
    QVERIFY2(!btn->isEnabled(), "the fit button should start disabled, with no image");
    {
      const QImage im = btn->grab().toImage();
      QVERIFY2(!near(im.pixelColor(im.width() / 2, 3), accent, 50),
               "a dead fit button is filled with the accent");
    }
    // …and once it can act, the fill every other acting button carries.
    openLoaded(win);
    QTRY_VERIFY(win.actFit_->isEnabled());
    QTest::qWait(120);
    QCOMPARE(btn->property("toolFill").toString(), QStringLiteral("accent"));
    const QImage live = btn->grab().toImage();
    QVERIFY2(near(live.pixelColor(live.width() / 2, 3), accent, 50),
             "an enabled fit button is not accent-filled");
    Q_UNUSED(pal);
  }

  // A dead combo has to LOOK dead (browser: button:disabled drops the .accent-dd-trigger to
  // --disabled-bg/--disabled-text). The Qt stylesheet painted every QComboBox in the live
  // input colours, so the image-filter picker with no image loaded was pixel-identical to a
  // working one — nothing showed it was unavailable.
  void disabledSelectReadsAsDisabled() {
    MainWindow win(nullptr, false);
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(150);
    QComboBox* filter = win.imageFilter_;
    QVERIFY(filter);
    // The face colour, plus how loudly the text/caret stand out against it.
    const auto face = [](QWidget* w) {
      QTest::qWait(30);
      const QImage im = w->grab().toImage();
      const QColor ground = im.pixelColor(2, im.height() / 2);
      double ink = 0;
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x) {
          const QColor c = im.pixelColor(x, y);
          ink += qAbs(c.red() - ground.red()) + qAbs(c.green() - ground.green())
                 + qAbs(c.blue() - ground.blue());
        }
      return std::pair<QColor, double>{ground, ink / (im.width() * im.height())};
    };
    QVERIFY2(!filter->isEnabled(), "the filter picker is live with no image loaded");
    const auto dead = face(filter);
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(filter->isEnabled());
    const auto live = face(filter);
    QVERIFY2(dead.first != live.first, "a disabled combo keeps the live input background");
    QVERIFY2(dead.second < live.second * 0.9,
             "a disabled combo's text is as loud as a live one's");
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
    QTest::qWait(150);
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
    QTest::qWait(300);
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
      // face is the gap between them (Qt's own is a fixed 4px — kFaceIconGap adds the rest).
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

  // The toolbar closes with a SETTINGS cluster, mirroring the browser's last
  // group: theme · fullscreen · incognito · gear · palette · info. Every button
  // drives the EXISTING QAction, so the toolbar and the menu bar stay in step in
  // both directions — the incognito one lights up like the browser's whichever
  // side toggles it.
  void toolbarHasTheBrowsersSettingsSection() {
    MainWindow win(nullptr, false);
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QWidget* section = win.settingsSection_;
    QVERIFY2(section, "no SETTINGS section on the toolbar");
    QLabel* caption = section->findChild<QLabel*>("sectionLabel");
    QVERIFY2(caption, "the section has no caption");
    QCOMPARE(caption->text(), QStringLiteral("SETTINGS"));   // same styling as its siblings

    // The six controls, in the browser's order: the two state TOGGLES first (incognito ·
    // fullscreen), then the theme switch, then the three that open dialogs — gear
    // (Shortcuts) · palette (Visuals) · info (Help). actAccent_ is NOT here — it's the
    // logo's own popover, with no toolbar icon of its own in the browser.
    QList<QAction*> got;
    for (QToolButton* b : section->findChildren<QToolButton*>())
      if (b->defaultAction()) got << b->defaultAction();
    const QList<QAction*> want{win.actIncognito_, win.actFullscreen_, win.actTheme_,
                               win.actShortcuts_, win.actSettings_, win.actInfo_};
    QCOMPARE(got.size(), want.size());
    for (int i = 0; i < want.size(); ++i)
      QVERIFY2(got.at(i) == want.at(i),
               qPrintable(QString("slot %1 is %2, expected %3")
                              .arg(i)
                              .arg(got.at(i)->text(), want.at(i)->text())));
    for (QToolButton* b : section->findChildren<QToolButton*>()) {
      QVERIFY2(!b->icon().isNull(), qPrintable(b->defaultAction()->text() + " has no glyph"));
      QCOMPARE(b->property("toolSection").toString(), QStringLiteral("Settings"));
    }

    // Each button triggers its action: the two toggles flip, and the three that
    // open something are wired (checked via the action's own connections below).
    QToolButton* incognitoBtn = nullptr;
    for (QToolButton* b : section->findChildren<QToolButton*>())
      if (b->defaultAction() == win.actIncognito_) incognitoBtn = b;
    QVERIFY(incognitoBtn);
    QVERIFY2(incognitoBtn->isCheckable(), "the incognito button must show a lit state");
    QVERIFY(!win.incognito_);
    incognitoBtn->click();                       // toolbar → state + menu bar
    QTRY_VERIFY2(win.incognito_, "the toolbar button did not turn incognito on");
    QVERIFY(win.actIncognito_->isChecked() && incognitoBtn->isChecked());
    win.actIncognito_->setChecked(false);        // menu bar → toolbar button
    QTRY_VERIFY(!win.incognito_);
    QVERIFY2(!incognitoBtn->isChecked(), "the toolbar button kept its lit state");

    // Theme flips both ways from the toolbar too.
    const QString before = win.settings_.themeMode;
    for (QToolButton* b : section->findChildren<QToolButton*>())
      if (b->defaultAction() == win.actTheme_) b->click();
    QTRY_VERIFY2(win.settings_.themeMode != before, "the theme button did nothing");

    // The palette button opens the SAME accent popover the logo does.
    QCOMPARE(win.actAccent_->objectName(), QStringLiteral("actAccent"));

    // …and it must actually be ON SCREEN. Asserting only that the actions exist
    // passed happily while the user could not see the section at all: the row
    // overflowed and QToolBar's "»" swallowed it, leaving a separator after DATA
    // and nothing after that. So: visible, non-empty, and fully inside the
    // toolbar's own rect — at laptop widths, and with the custom-page cm inputs
    // showing (that is the state the report came from), which is what made the
    // row too wide.
    QToolBar* row = win.findChild<QToolBar*>("mainToolbar");   // the one wrapping run
    QVERIFY(row);
    const int custom = win.units_.pageSize->findData(QStringLiteral("custom"));
    QVERIFY(custom >= 0);
    for (const int width : {1950, 1400, 1100, 975}) {
      win.resize(width, 850);
      win.units_.pageSize->setCurrentIndex(custom);   // the widest state of this row
      QTest::qWait(200);
      const QString at = QString("at %1px: ").arg(width);
      QVERIFY2(section->isVisible(), qPrintable(at + "the SETTINGS section is not visible"));
      QVERIFY2(section->width() > 0 && section->height() > 0,
               qPrintable(at + "the SETTINGS section collapsed to nothing"));
      QVERIFY2(row->rect().contains(section->geometry()),
               qPrintable(at + "the SETTINGS section is outside the toolbar (" +
                          QDebug::toString(section->geometry()) + " in " +
                          QDebug::toString(row->rect()) + ")"));
      QVERIFY2(row->sizeHint().width() <= width,
               qPrintable(at + QString("the row needs %1px and would overflow into \"»\"")
                                   .arg(row->sizeHint().width())));
      // …and it sits AFTER Data, which is where the browser puts it.
      QWidget* data = nullptr;
      for (QLabel* l : row->findChildren<QLabel*>("sectionLabel"))
        if (l->text() == QLatin1String("DATA")) data = l->parentWidget();
      QVERIFY2(data, qPrintable(at + "no DATA section on this row"));
      QVERIFY2(section->x() > data->x(), qPrintable(at + "SETTINGS is not after DATA"));
      for (QToolButton* b : section->findChildren<QToolButton*>())
        QVERIFY2(b->isVisible(), qPrintable(at + b->defaultAction()->text() + " is hidden"));
    }
    beat();
  }

  // DESCRIPTION & ATTRIBUTES (browser parity): the cluster sits between IMAGE and
  // PROJECTS with Description · Keywords · Links in that order, and all three follow ONE
  // rule — a saved, non-incognito project — with the reason on the tooltip otherwise.
  // Links used to live in IMAGE and gate on an image; it moved with the browser's.
  void descriptionSectionFollowsImageAndGatesOnASavedProject() {
    MainWindow win(nullptr, false);
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QWidget* section = nullptr;
    QWidget* image = nullptr;
    QWidget* projects = nullptr;
    for (QLabel* l : win.findChildren<QLabel*>("sectionLabel")) {
      if (l->text() == QLatin1String("DESCRIPTION & ATTRIBUTES")) section = l->parentWidget();
      else if (l->text() == QLatin1String("IMAGE")) image = l->parentWidget();
      else if (l->text() == QLatin1String("PROJECTS")) projects = l->parentWidget();
    }
    QVERIFY2(section, "no DESCRIPTION & ATTRIBUTES section on the toolbar");
    QVERIFY(image && projects);
    QCOMPARE(section->parentWidget(), image->parentWidget());   // the same row
    QVERIFY2(section->x() > image->x() && section->x() < projects->x(),
             "the section is not between IMAGE and PROJECTS");
    QList<QAction*> got;
    for (QToolButton* b : section->findChildren<QToolButton*>())
      if (b->defaultAction()) got << b->defaultAction();
    const QList<QAction*> want{win.actDescription_, win.actKeywords_, win.actLinks_};
    QCOMPARE(got, want);
    QVERIFY2(!win.imageSection_->isAncestorOf(win.buttonForAction(win.actLinks_)),
             "Links is still in the IMAGE section");
    // Menu bar: the trio sits together where Links lives.
    QMenu* projectMenu = nullptr;
    for (QMenu* m : win.menuBar()->findChildren<QMenu*>())
      if (m->actions().contains(win.actLinks_)) projectMenu = m;
    QVERIFY(projectMenu);
    const int di = projectMenu->actions().indexOf(win.actDescription_);
    QVERIFY(di >= 0);
    QCOMPARE(projectMenu->actions().at(di + 1), win.actKeywords_);
    QCOMPARE(projectMenu->actions().at(di + 2), win.actLinks_);
    // The shared registry's chords are on the actions.
    QCOMPARE(win.actDescription_->shortcut(), QKeySequence(win.hotkey("openDescription", "Alt+Shift+D")));
    QCOMPARE(win.actKeywords_->shortcut(), QKeySequence(win.hotkey("openKeywords", "Alt+Shift+K")));
    // …and the popover gestures reach all three.
    for (QAction* a : want) QVERIFY(win.pop_.dialogActions.contains(a));

    // No project: all three dead, each with its reason on the tooltip.
    const auto reasonShown = [](QAction* a) {
      return a->toolTip().contains("\n— " + a->property(stencil::gui::kTipReasonProperty).toString());
    };
    for (QAction* a : want) {
      QVERIFY2(!a->isEnabled(), qPrintable(a->text() + " is enabled with no project"));
      QVERIFY2(reasonShown(a), qPrintable(a->text() + ": no reason on the tooltip"));
    }
    QCOMPARE(win.actDescription_->property(stencil::gui::kTipReasonProperty).toString(),
             QStringLiteral("Save the project first to add a description"));
    QCOMPARE(win.actKeywords_->property(stencil::gui::kTipReasonProperty).toString(),
             QStringLiteral("Save the project first to add keywords"));
    QCOMPARE(win.actLinks_->property(stencil::gui::kTipReasonProperty).toString(),
             QStringLiteral("Save the project first to add links"));

    // A saved project: all three live, the reason gone.
    // Idempotent against the persisted test store: a copy left by an earlier run (the
    // dialog writes through fileStore) would be found first and carry the "After".
    win.projectList_.erase(std::remove_if(win.projectList_.begin(), win.projectList_.end(),
                                          [](const stencil::gui::Project& p) { return p.meta.id == "meta-gui"; }),
                           win.projectList_.end());
    stencil::gui::Project pr;
    pr.meta.id = "meta-gui";
    pr.meta.name = "Meta";
    pr.meta.description = "Before";
    win.projectList_.push_back(pr);
    win.activeProjectId_ = "meta-gui";
    win.refreshActions();
    for (QAction* a : want) {
      QVERIFY2(a->isEnabled(), qPrintable(a->text() + " is dead with a saved project"));
      QVERIFY2(!reasonShown(a), qPrintable(a->text() + ": the reason lingers"));
    }
    // Incognito takes them away again.
    win.actIncognito_->setChecked(true);
    for (QAction* a : want) QVERIFY2(!a->isEnabled(), qPrintable(a->text() + " survives incognito"));
    win.actIncognito_->setChecked(false);
    for (QAction* a : want) QVERIFY(a->isEnabled());

    // The dialogs open pre-filled and write back through the store.
    QTimer::singleShot(0, [&] {
      QDialog* dlg = nullptr;
      for (int i = 0; i < 200 && !dlg; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) QTest::qWait(10);
      }
      QVERIFY(dlg);
      QCOMPARE(dlg->objectName(), QStringLiteral("stencilDescriptionDialog"));
      auto* area = dlg->findChild<QPlainTextEdit*>("descriptionText");
      QVERIFY(area);
      QCOMPARE(area->toPlainText(), QStringLiteral("Before"));
      area->setPlainText("After");
      dlg->findChild<QPushButton*>("descriptionSave")->click();
    });
    win.actDescription_->trigger();
    QTest::qWait(50);
    QCOMPARE(QString::fromStdString(win.findProject("meta-gui")->meta.description), QStringLiteral("After"));
    QTimer::singleShot(0, [&] {
      QDialog* dlg = nullptr;
      for (int i = 0; i < 200 && !dlg; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) QTest::qWait(10);
      }
      QVERIFY(dlg);
      QCOMPARE(dlg->objectName(), QStringLiteral("stencilKeywordsDialog"));
      auto* area = dlg->findChild<QPlainTextEdit*>("keywordsText");
      QVERIFY(area);
      area->setPlainText("Plan, kitchen plan");
      QTest::keyClick(area, Qt::Key_Return);   // Enter saves the list
    });
    win.actKeywords_->trigger();
    QTest::qWait(50);
    QCOMPARE(win.findProject("meta-gui")->meta.keywords, std::vector<std::string>({"plan", "kitchen"}));
    // …and leave no trace in the store for the next run.
    win.projectList_.erase(std::remove_if(win.projectList_.begin(), win.projectList_.end(),
                                          [](const stencil::gui::Project& p) { return p.meta.id == "meta-gui"; }),
                           win.projectList_.end());
    stencil::gui::fileStore::saveProjects(win.projectList_);
  }

  // ── The toolbar's clusters, in the browser's order ──────────────────────────
  // One sequence across both surfaces (browser js/ui/toolbar.js, pinned there by
  // ui-markup.test.js): Image · Description & attributes · Projects · Connections & chat ·
  // Edit / Line · Point / Draw · View / Zoom · Page · Formula · Data · Settings. The rows
  // are where this app's non-wrapping toolbars break that one sequence, so the check is
  // the concatenation of the rows top to bottom.
  void toolbarSectionsFollowTheBrowsersOrder() {
    MainWindow win(nullptr, false);
    win.resize(1600, 950);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(200);
    QList<QToolBar*> bars = win.findChildren<QToolBar*>();
    std::sort(bars.begin(), bars.end(), [](QToolBar* a, QToolBar* b) {
      return a->mapTo(a->window(), QPoint(0, 0)).y() < b->mapTo(b->window(), QPoint(0, 0)).y();
    });
    QStringList sections;
    for (QToolBar* tb : bars)
      for (QLabel* l : tb->findChildren<QLabel*>())
        if (l->objectName() == QLatin1String("sectionLabel")) sections << l->text();
    const QStringList want{"IMAGE", "DESCRIPTION & ATTRIBUTES", "PROJECTS",
                           "CONNECTIONS & CHAT", "EDIT", "LINE", "POINT", "DRAW", "VIEW",
                           "ZOOM", "PAGE", "FORMULA", "DATA", "SETTINGS"};
    QCOMPARE(sections, want);
    // Where the rows BREAK that sequence is a packing decision — the browser re-wraps the
    // same run with the window and a QToolBar cannot — so every row has to survive a narrow
    // window on its own. With the formula fields showing and the widest page state chosen,
    // none of them may fall back on QToolBar's "»", which is how SETTINGS once vanished.
    win.allowFormulas_->setChecked(true);
    const int custom = win.units_.pageSize->findData(QStringLiteral("custom"));
    QVERIFY(custom >= 0);
    const int a3 = win.units_.pageSize->findData(QStringLiteral("A3"));
    QVERIFY(a3 >= 0);
    win.units_.pageSize->setCurrentIndex(a3);   // the everyday state, whatever the settings hold
    QTest::qWait(150);
    for (const int width : {1400, 1100, 1000}) {
      win.resize(width, 950);
      QTest::qWait(250);
      for (QToolBar* tb : bars) {
        if (tb->objectName() == QLatin1String("headerToolbar")) continue;
        for (QWidget* c : tb->findChildren<QWidget*>())
          if (c->metaObject()->className() == QLatin1String("QToolBarExtension"))
            QVERIFY2(!c->isVisible(),
                     qPrintable(QString("at %1px the %2 row overflows into \"»\"")
                                    .arg(width).arg(tb->objectName())));
      }
    }
    // …and once more with the custom page's W × H boxes out — they add ~160px to the PAGE
    // cluster (squeezable, but only so far), so that state is checked one step wider.
    win.units_.pageSize->setCurrentIndex(custom);
    QTest::qWait(150);
    for (const int width : {1400, 1100}) {
      win.resize(width, 950);
      QTest::qWait(250);
      for (QToolBar* tb : bars) {
        if (tb->objectName() == QLatin1String("headerToolbar")) continue;
        for (QWidget* c : tb->findChildren<QWidget*>())
          if (c->metaObject()->className() == QLatin1String("QToolBarExtension"))
            QVERIFY2(!c->isVisible(),
                     qPrintable(QString("at %1px (custom page) the %2 row overflows into \"»\"")
                                    .arg(width).arg(tb->objectName())));
      }
    }
    win.units_.pageSize->setCurrentIndex(a3);   // this suite shares the real settings file
  }

  // A squeezed window WRAPS its tool row (support/wrapRow.hpp) — the browser's flex-wrap.
  // QToolBar's own answer is the "»" overflow, where a widget action is not drawn at all.
  void narrowToolbarRowsWrapInsteadOfLosingSections() {
    MainWindow win;
    win.resize(1500, 950);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(200);

    QStringList captions;
    for (QLabel* l : win.findChildren<QLabel*>("sectionLabel"))
      if (l->isVisible()) captions << l->text();
    QVERIFY2(captions.contains("EDIT"), "the wide window shows EDIT to begin with");
    QToolBar* main = win.findChild<QToolBar*>("mainToolbar");
    QVERIFY(main);
    const int oneLine = main->height();

    for (const int width : {1100, 900, 760}) {
      win.resize(width, 950);
      QTest::qWait(300);
      for (QLabel* l : win.findChildren<QLabel*>("sectionLabel")) {
        if (!captions.contains(l->text())) continue;
        QVERIFY2(l->isVisible(),
                 qPrintable(QString("at %1px the %2 section is gone").arg(width).arg(l->text())));
        const QPoint tl = l->mapTo(&win, QPoint(0, 0));
        QVERIFY2(tl.x() >= 0 && tl.x() < win.width(),
                 qPrintable(QString("at %1px %2 sits off the window at x=%3")
                                .arg(width).arg(l->text()).arg(tl.x())));
      }
      // …and no row falls back on the overflow button to get there.
      for (QToolBar* tb : win.findChildren<QToolBar*>()) {
        if (tb->objectName() == QLatin1String("headerToolbar")) continue;
        for (QWidget* c : tb->findChildren<QWidget*>())
          if (c->metaObject()->className() == QLatin1String("QToolBarExtension"))
            QVERIFY2(!c->isVisible(),
                     qPrintable(QString("at %1px the %2 row overflows into \"»\"")
                                    .arg(width).arg(tb->objectName())));
      }
    }
    // …and each hairline runs the full height of its line (browser .ctrl-sep stretch).
    for (QFrame* sep : win.findChildren<QFrame*>("toolWrapSep")) {
      if (!sep->isVisible()) continue;
      const int y = sep->mapTo(&win, QPoint(0, 0)).y();
      int tallest = 0;
      for (QLabel* l : win.findChildren<QLabel*>("sectionLabel")) {
        QWidget* sect = l->parentWidget();
        if (!sect || !sect->isVisible()) continue;
        if (qAbs(sect->mapTo(&win, QPoint(0, 0)).y() - y) > 4) continue;   // another line
        tallest = qMax(tallest, sect->height());
      }
      if (tallest <= 0) continue;
      QVERIFY2(sep->height() >= tallest,
               qPrintable(QString("a divider stops %1px short of its line (%2 vs %3)")
                              .arg(tallest - sep->height()).arg(sep->height()).arg(tallest)));
    }
    // Wrapped, not merely squeezed: the row that no longer fits is TALLER, because the
    // cluster that fell off the end went onto a second line.
    QVERIFY2(main->height() > oneLine,
             qPrintable(QString("the main row never wrapped: %1px at both widths").arg(oneLine)));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "mainWindow.toolbar.gui.moc"
