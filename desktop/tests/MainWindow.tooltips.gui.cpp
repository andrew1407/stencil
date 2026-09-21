// MainWindow GUI e2e — Every toolbar tooltip's text, against the browser's own copy.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowTip.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Hover text is a shared contract: a control that exists in BOTH apps says the same thing, so the
  // browser's toolbar.js is its source of truth. Menu LABELS and desktop-only affordances may differ.
  void toolbarTooltipsMatchTheBrowser() {
    // The browser's toolbar markup + the shared copy canon it interpolates from.
    stencil::test::BrowserMarkup browser;
    for (const char* part : {"browser/js/ui/toolbar/toolbar.js", "browser/js/ui/toolbar/toolbarTopbar.js",
                             "browser/js/ui/toolbar/toolbarSections.js", "browser/js/ui/toolbar/toolbarPageSections.js"})
      QVERIFY2(browser.load(QString::fromLatin1(part)), part);
    const auto browserTag = [&browser](const QString& id) { return browser.tag(id); };
    const auto attrOf = [&browser](const QString& t, const QString& a) {
      return browser.attr(t, a);
    };
    const auto browserTip = [&browser](const QString& id) { return browser.tip(id); };
    // A desktop tooltip is "<text> (<shortcut>)" plus a "— reason" line while the control is disabled
    // (browser composeControlTitle); the shortcut is drawn as a keycap, so only the text is compared.
    auto textOf = [](QString tip) {
      tip.remove(QRegularExpression("\\n\u2014 [^\\n]*$"));
      const QRegularExpressionMatch m = QRegularExpression("\\s*\\(([^()]*)\\)\\s*$").match(tip);
      if (m.hasMatch() && stencil::gui::isKeyCombo(m.captured(1))) tip = tip.left(m.capturedStart()).trimmed();
      return tip;
    };

    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    struct Pair { QAction* act; QWidget* widget; const char* browserId; };
    const QList<Pair> pairs{
        {win.actOpen, nullptr, "load-image-btn"},
        {win.actOpenAnother, nullptr, "open-image-btn"},
        {win.actSaveImage, nullptr, "save-image"},
        {win.actCopyImage, nullptr, "copy-image"},
        {win.actOpenIn, nullptr, "open-in-btn"},
        {win.actProjects, nullptr, "projects-btn"},
        {win.actOpenProjectFile, nullptr, "open-project-btn"},
        {win.actStencilLiveSync, nullptr, "live-sync-btn"},
        {win.actDeleteProjectFile, nullptr, "delete-project-btn"},
        {win.actConnect, nullptr, "connect-btn"},
        {win.actLinks, nullptr, "links-btn"},
        {win.actDescription, nullptr, "description-btn"},
        {win.actKeywords, nullptr, "keywords-btn"},
        {win.actChat, nullptr, "chat-btn"},
        {win.actCrop, nullptr, "crop-image"},
        {win.actRotateLeft, nullptr, "rotate-left"},
        {win.actRotateRight, nullptr, "rotate-right"},
        {win.actUndo, nullptr, "undo"},
        {win.actRedo, nullptr, "redo"},
        {win.actFit, nullptr, "zoom-fit"},
        {win.actDownloadJson, nullptr, "download-json"},
        {win.actUploadJson, nullptr, "upload-json-btn"},
        {win.actCopyLayout, nullptr, "copy-json-btn"},
        {win.actClearProject, nullptr, "clear-storage"},
        {win.actTheme, nullptr, "theme-toggle"},
        {win.actIncognito, nullptr, "incognito-toggle"},
        {win.actInfo, nullptr, "info-btn"},
        {nullptr, win.imageFilter, "image-filter"},
        {nullptr, win.compareCombo, "compare-mode"},
        {nullptr, win.filterColorBtn, "filter-color"},
        {nullptr, win.nameBar.blankColorBtn, "blank-color-btn"},
        {nullptr, win.zoom, "zoom-input"},
        {nullptr, win.units.pageSize, "page-size"},
        {nullptr, win.units.unitCombo, "unit-select"},
        {nullptr, win.lineColorBtn, "line-color"},
        {nullptr, win.lineThickness, "line-thickness"},
        {nullptr, win.lineStyle, "line-style"},
        {nullptr, win.pointColorBtn, "point-color"},
        {nullptr, win.pointSize, "point-size"},
        {nullptr, win.drawModeBtn, "draw-mode-toggle"},
        {nullptr, win.nameBar.edit, "project-name-edit"},
        {nullptr, win.nameBar.colorBtn, "project-color-btn"},
        {nullptr, win.nameBar.accept, "project-name-accept"},
        {nullptr, win.nameBar.cancel, "project-name-cancel"},
    };
    for (const Pair& p : pairs) {
      // Both sides go through textOf: the browser writes its key into the data-title, the desktop hands the
      // same key to the rich tooltip as a keycap — the WORDS are what has to match.
      const QString want = textOf(browserTip(QString::fromLatin1(p.browserId)));
      QVERIFY2(!want.isEmpty(), qPrintable(QString("no browser control #%1").arg(p.browserId)));
      QVERIFY2(p.act || p.widget, p.browserId);
      // The plain text a widget's tooltip was composed from (the app renders it rich in
      // place; this suite's plain QApplication does not).
      QObject* target = p.act ? static_cast<QObject*>(p.act) : p.widget;
      const QString plain = p.act ? p.act->toolTip()
                            : target->property(stencil::gui::PLAIN_TIP_PROPERTY).isValid()
                                ? target->property(stencil::gui::PLAIN_TIP_PROPERTY).toString()
                                : p.widget->toolTip();
      const QString got = textOf(plain);
      QVERIFY2(got == want,
               qPrintable(QString("#%1: desktop says \"%2\", the browser says \"%3\"")
                              .arg(p.browserId, got, want)));
      // …and why it is greyed out, verbatim (data-disabled-reason): carried by every control
      // the browser gives one to, and on the tooltip exactly while the control is disabled.
      const QString tag = browserTag(QString::fromLatin1(p.browserId));
      const QString wantReason = attrOf(tag, "data-disabled-reason");
      const QString gotReason = target->property(stencil::gui::TIP_REASON_PROPERTY).toString();
      QVERIFY2(gotReason == wantReason,
               qPrintable(QString("#%1: desktop reason \"%2\", the browser's \"%3\"")
                              .arg(p.browserId, gotReason, wantReason)));
      if (!wantReason.isEmpty()) {
        const bool disabled = p.act ? !p.act->isEnabled() : !p.widget->isEnabled();
        QVERIFY2(plain.contains("\n\u2014 " + wantReason) == disabled,
                 qPrintable(QString("#%1 (%2): tooltip \"%3\"")
                                .arg(p.browserId, disabled ? "disabled" : "enabled", plain)));
      }
      // A widget with a data-hk-title wears that binding's chord (an action wears its own).
      const QString hk = attrOf(tag, "data-hk-title");
      if (p.widget && !hk.isEmpty()) {
        auto* bound = qobject_cast<QAction*>(
            p.widget->property(stencil::gui::TIP_HOTKEY_PROPERTY).value<QObject*>());
        QVERIFY2(bound, qPrintable(QString("#%1 wears no chord for %2").arg(p.browserId, hk)));
        QCOMPARE(bound->shortcut(), QKeySequence(win.hotkey(hk, QString())));
        QVERIFY2(plain.contains("(" + bound->shortcut().toString(QKeySequence::NativeText) + ")"),
                 qPrintable(QString("#%1: no chord on \"%2\"").arg(p.browserId, plain)));
      }
    }
    // …and the shortcut the shared registry defines for a control really is on it, or the
    // tooltip has no keycap to draw and the chord does nothing.
    QCOMPARE(win.actStencilLiveSync->shortcut(), QKeySequence(win.hotkey("toggleLiveSync", "Ctrl+Shift+Y")));
    QCOMPARE(win.actDeleteProjectFile->shortcut(), QKeySequence(win.hotkey("deleteProject", "Ctrl+Shift+Backspace")));
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.tooltips.gui.moc"
