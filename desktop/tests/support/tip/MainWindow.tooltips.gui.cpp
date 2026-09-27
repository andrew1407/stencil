// MainWindow GUI e2e — Every toolbar tooltip's text, against the browser's own copy.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindowTip.gui.hpp"
#include <QAccessible>
#include <QTextDocument>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Hover text is a shared contract: a control that exists in BOTH apps says the same thing, so the
  // browser's toolbar.js is its source of truth. Menu LABELS and desktop-only affordances may differ.
  void toolbarTooltipsMatchTheBrowser() {
    // The browser's toolbar markup + the shared copy canon it interpolates from.
    stencil::test::BrowserMarkup browser;
    for (const char* part : {"browser/js/ui/toolbar/toolbar.js", "browser/js/ui/toolbar/topbar.js",
                             "browser/js/ui/toolbar/sections.js", "browser/js/ui/toolbar/pageSections.js"})
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
        {win.acts.open, nullptr, "load-image-btn"},
        {win.acts.openAnother, nullptr, "open-image-btn"},
        {win.acts.saveImage, nullptr, "save-image"},
        {win.acts.copyImage, nullptr, "copy-image"},
        {win.acts.openIn, nullptr, "open-in-btn"},
        {win.acts.projects, nullptr, "projects-btn"},
        {win.acts.openProjectFile, nullptr, "open-project-btn"},
        {win.acts.stencilLiveSync, nullptr, "live-sync-btn"},
        {win.acts.deleteProjectFile, nullptr, "delete-project-btn"},
        {win.acts.connect, nullptr, "connect-btn"},
        {win.acts.links, nullptr, "links-btn"},
        {win.acts.description, nullptr, "description-btn"},
        {win.acts.keywords, nullptr, "keywords-btn"},
        {win.acts.chat, nullptr, "chat-btn"},
        {win.acts.crop, nullptr, "crop-image"},
        {win.acts.rotateLeft, nullptr, "rotate-left"},
        {win.acts.rotateRight, nullptr, "rotate-right"},
        {win.acts.undo, nullptr, "undo"},
        {win.acts.redo, nullptr, "redo"},
        {win.acts.fit, nullptr, "zoom-fit"},
        {win.acts.downloadJson, nullptr, "download-json"},
        {win.acts.uploadJson, nullptr, "upload-json-btn"},
        {win.acts.copyLayout, nullptr, "copy-json-btn"},
        {win.acts.clearProject, nullptr, "clear-storage"},
        {win.acts.theme, nullptr, "theme-toggle"},
        {win.acts.incognito, nullptr, "incognito-toggle"},
        {win.acts.info, nullptr, "info-btn"},
        {nullptr, win.tools.imageFilter, "image-filter"},
        {nullptr, win.tools.compareCombo, "compare-mode"},
        {nullptr, win.tools.filterColorBtn, "filter-color"},
        {nullptr, win.nameBar.blankColorBtn, "blank-color-btn"},
        {nullptr, win.zoom, "zoom-input"},
        {nullptr, win.units.pageSize, "page-size"},
        {nullptr, win.units.unitCombo, "unit-select"},
        {nullptr, win.tools.lineColorBtn, "line-color"},
        {nullptr, win.tools.lineThickness, "line-thickness"},
        {nullptr, win.tools.lineStyle, "line-style"},
        {nullptr, win.tools.pointColorBtn, "point-color"},
        {nullptr, win.tools.pointSize, "point-size"},
        {nullptr, win.tools.drawModeBtn, "draw-mode-toggle"},
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
        QCOMPARE(bound->shortcut(), QKeySequence(win.keys.value(hk, QString())));
        QVERIFY2(plain.contains("(" + bound->shortcut().toString(QKeySequence::NativeText) + ")"),
                 qPrintable(QString("#%1: no chord on \"%2\"").arg(p.browserId, plain)));
      }
    }
    // …and the shortcut the shared registry defines for a control really is on it, or the
    // tooltip has no keycap to draw and the chord does nothing.
    QCOMPARE(win.acts.stencilLiveSync->shortcut(), QKeySequence(win.keys.value("toggleLiveSync", "Ctrl+Shift+Y")));
    QCOMPARE(win.acts.deleteProjectFile->shortcut(), QKeySequence(win.keys.value("deleteProject", "Ctrl+Shift+Backspace")));
  }

  // macOS Hover Text (⌘ held) and screen readers speak a control's accessible description, which Qt
  // fills from the raw toolTip(): a rich tip must read as the words it draws, a table row as one line.
  void richTooltipsReadAsTheirTextNeverTheirMarkup() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // What main.cpp does to every plain tip, so each control reads as the app draws it, keycaps included.
    for (QWidget* w : QApplication::allWidgets())
      if (const QString rich = stencil::gui::enrichedToolTip(w->toolTip()); !rich.isEmpty()) w->setToolTip(rich);
    win.chatSession->refreshLlmStatus();
    auto* more = win.chatDock->findChild<QToolButton*>("chatMore");
    QVERIFY(more && more->toolTip().contains("<table"));
    const auto bare = [](QString s) {
      s.remove(QChar::ObjectReplacementCharacter);
      return s.remove(QRegularExpression("\\s"));
    };
    int rich = 0;
    for (QWidget* w : QApplication::allWidgets()) {
      if (!Qt::mightBeRichText(w->toolTip())) continue;
      ++rich;
      const QString said = QAccessible::queryAccessibleInterface(w)->text(QAccessible::Description);
      const QString who = w->objectName().isEmpty() ? QString::fromLatin1(w->metaObject()->className()) : w->objectName();
      QVERIFY2(!said.contains('<') && !said.contains("&nbsp;") && !said.contains("&amp;"),
               qPrintable(QString("%1 reads out its markup: %2").arg(who, said.left(120))));
      QTextDocument drawn;
      drawn.setHtml(w->toolTip());
      // Every drawn character is said, in order; a keycap picture is said as its key.
      const QString want = bare(drawn.toPlainText()), got = bare(said);
      qsizetype at = 0;
      for (const QChar c : want)
        if (at >= 0) at = got.indexOf(c, at) < 0 ? -1 : got.indexOf(c, at) + 1;
      QVERIFY2(!want.isEmpty() && at > 0, qPrintable(QString("%1 says \"%2\", its tip draws \"%3\"")
                                                        .arg(who, said, drawn.toPlainText())));
      if (w == more) {
        QCOMPARE(got, want);
        QVERIFY2(said.contains(QRegularExpression("^Provider\\s+\\S", QRegularExpression::MultilineOption)),
                 qPrintable("the status table's row is not one line: " + said));
      }
    }
    QVERIFY2(rich > 20, qPrintable(QString("only %1 rich tooltips to read").arg(rich)));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.tooltips.gui.moc"
