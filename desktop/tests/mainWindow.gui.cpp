// Desktop GUI end-to-end, written with the Qt Test framework (QtTest). Unlike the other
// headless checks — which exercise CanvasWidget / fileStore in isolation — this drives the
// REAL MainWindow: it loads an image through the public OS-open path, triggers the actual
// toolbar/menu QActions (Rotate, Undo, Start Drawing), and sends real mouse clicks to the
// live canvas, asserting on observable widget state. It also drives fullscreen edge-hover reveal,
// sampling toolbar/panel geometry over time to assert the reveals animate smoothly (no flicker).
// Runs offscreen (QT_QPA_PLATFORM=offscreen), so it needs no display; registered with CTest.
#include "mainWindow.hpp"
#include "../src/support/themeSwapOverlay.hpp"
#include "../src/support/notifications.hpp"
#include "../src/support/disintegrateOverlay.hpp"
#include "../src/canvas/dropZonesOverlay.hpp"
#include "../src/canvas/canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "chatDock.hpp"
#include "../src/app/dataExportController.hpp"
#include "pillSplitter.hpp"
#include "../src/app/selectionPanel.hpp"
#include "../src/app/scrollReveal.hpp"
#include "fileStore.hpp"
#include "connectDialog.hpp"
#include "serverClient.hpp"
#include "llmSettingsForm.hpp"
#include "mediaLoader.hpp"
#include "popover.hpp"
#include "iconSet.hpp"
#include "guiHelpers.hpp"
#include "theme.hpp"
#include "modalReveal.hpp"
#include <QScopeGuard>
#include <QtTest>
#include <functional>
#include <QMenu>
#include <QMenuBar>
#include <QAbstractSpinBox>
#include <QClipboard>
#include <QCursor>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QListView>
#include "../src/support/searchCombo.hpp"
#include <QRegularExpression>
#include "../src/support/tipContent.hpp"
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QAction>
#include <QFile>
#include <QImage>
#include <QDir>
#include <QShortcut>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QApplication>
#include <QMessageBox>
#include <QAbstractButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QRadioButton>
#include <QWidgetAction>
#include <QCursor>
#include <QDateTime>
#include <QDockWidget>
#include <QGraphicsEffect>
#include <QGraphicsOpacityEffect>
#include <QDropEvent>
#include <QMimeData>
#include <QSignalSpy>
#include <QUrl>
#include <QJsonArray>
#include <QBuffer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QTableWidget>
#include <QMenu>
#include <QTimer>
#include <QToolBar>
#include <QVariantAnimation>
#include <memory>

#include "../src/canvas/incognitoOverlay.hpp"

using stencil::gui::MainWindow;
using stencil::gui::CanvasWidget;
using stencil::gui::ThemeSwapOverlay;
using stencil::gui::Settings;

namespace {
  // Comfortably past the f(x,y) idle-commit delay (mainWindow.cpp kFormulaCommitMs), so a
  // "stopped typing" wait can't race the timer on a loaded machine.
  constexpr int kFormulaSettleMs = 1600;

  // The shared box of the panel's two collapse chevrons (mainWindow kPanelToggleBox /
  // selectionPanel kToggleBox) — asserted equal so the pair can't drift apart.
  constexpr int kPanelChevronBox = 24;

  // Find a shared QAction by its visible label (the menu bar, toolbar, and context menu
  // all reuse the same QAction objects, so this reaches the real UI wiring).
  QAction* actionByText(const QWidget* w, const QString& text) {
    for (QAction* a : w->findChildren<QAction*>())
      if (a->text().compare(text, Qt::CaseInsensitive) == 0) return a;
    return nullptr;
  }

  // Injected LLM transport (the seam llmClient.headless.cpp uses): captures the
  // POST and answers synchronously with a canned response, so a chat send in
  // the GUI test never touches the network.
  struct MockChatTransport : stencil::llm::LlmTransport {
    QJsonObject body;             // last POSTed payload
    QList<QJsonObject> allBodies; // every POSTed payload, in order
    QByteArray response;          // canned provider reply
    QList<QByteArray> queue;      // per-POST replies, popped in order (else `response`)
    int status = 200;             // 0 + netError simulates a network failure
    QString netError;
    void postJson(const QUrl&, const QList<QPair<QByteArray, QByteArray>>&,
                  const QByteArray& b,
                  std::function<void(int, QByteArray, QString)> cb) override {
      body = QJsonDocument::fromJson(b).object();
      allBodies.append(body);
      cb(status, queue.isEmpty() ? response : queue.takeFirst(), netError);
    }
    void getJson(const QUrl&, const QList<QPair<QByteArray, QByteArray>>&,
                 std::function<void(int, QByteArray, QString)> cb) override {
      cb(200, QByteArrayLiteral("{}"), QString());
    }
  };

  // Asynchronous LLM transport: every POST is PARKED with its callback so the test
  // decides when answers land. MockChatTransport answers inline, which makes every
  // fan-out look like one request at a time — the only way to see how many requests
  // are really in flight at once is to hold them.
  struct DeferredChatTransport : stencil::llm::LlmTransport {
    struct Call {
      QJsonObject body;
      std::function<void(int, QByteArray, QString)> cb;
    };
    QList<Call> parked;
    int started = 0;        // POSTs ever made
    int peakInFlight = 0;   // the most ever outstanding at once
    QByteArray response;    // canned reply body
    int status = 200;
    QString netError;
    void postJson(const QUrl&, const QList<QPair<QByteArray, QByteArray>>&,
                  const QByteArray& b,
                  std::function<void(int, QByteArray, QString)> cb) override {
      parked.append(Call{QJsonDocument::fromJson(b).object(), std::move(cb)});
      ++started;
      peakInFlight = std::max(peakInFlight, static_cast<int>(parked.size()));
    }
    void getJson(const QUrl&, const QList<QPair<QByteArray, QByteArray>>&,
                 std::function<void(int, QByteArray, QString)> cb) override {
      cb(200, QByteArrayLiteral("{}"), QString());
    }
    void answerNext() {   // settle the oldest outstanding call
      if (parked.isEmpty()) return;
      const Call c = parked.takeFirst();
      c.cb(status, response, netError);
    }
  };

  // The first inline <img src="data:image/png;base64,…"> in a rich-text label, decoded.
  QImage pngOf(const QString& html) {
    const QString mark = QStringLiteral("base64,");
    const int at = html.indexOf(mark);
    if (at < 0) return QImage();
    const int end = html.indexOf(QLatin1Char('"'), at);
    if (end < 0) return QImage();
    return QImage::fromData(
        QByteArray::fromBase64(html.mid(at + mark.size(), end - at - mark.size()).toLatin1()),
        "PNG");
  }

  // Did anything actually draw? (a glyph rasterised into a fully transparent PNG
  // would satisfy every size assertion and show nothing).
  bool hasInk(const QImage& img) {
    for (int y = 0; y < img.height(); ++y)
      for (int x = 0; x < img.width(); ++x)
        if (qAlpha(img.pixel(x, y)) > 8) return true;
    return false;
  }

  // Every note/label text in the chat dock, joined — the notes a turn leaves.
  QString dockText(const QWidget* dock) {
    QString all;
    for (const QLabel* l : dock->findChildren<QLabel*>()) all += l->text() + QLatin1Char('\n');
    return all;
  }

  // Total points across all lines (committed + in-progress), for draw/undo assertions.
  int totalPoints(const CanvasWidget* c) {
    int n = 0;
    for (const auto& line : c->allLines()) n += static_cast<int>(line.points.size());
    return n;
  }

  // Does any card in the chat transcript carry this text? Cards keep their
  // rendered text on the body label's "chatBody" property (notes riding inside
  // an assistant bubble use "chatNote").
  bool chatTranscriptHas(const stencil::gui::ChatDock* dock, const QString& needle) {
    for (const QLabel* l : dock->findChildren<QLabel*>())
      if (l->property("chatBody").toString().contains(needle) ||
          l->property("chatNote").toString().contains(needle))
        return true;
    return false;
  }

  // The texts of the assistant reply bubbles in the transcript, in order. The
  // pending "…" card (body "…") and cards already handed to deleteLater are
  // excluded by flushing deferred deletes first.
  QStringList assistantBubbleTexts(const stencil::gui::ChatDock* dock) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QStringList out;
    for (const QFrame* f : dock->findChildren<QFrame*>()) {
      if (f->objectName() != QLatin1String("chatCardAssistant")) continue;
      for (const QLabel* l : f->findChildren<QLabel*>()) {
        const QString body = l->property("chatBody").toString();
        if (!body.isEmpty() && body != QStringLiteral("…")) out << body;
      }
    }
    return out;
  }

  // Optional watch-along pause: set STENCIL_GUI_SLOWMO=<ms> and run headed (no
  // QT_QPA_PLATFORM=offscreen) to actually see each step. No effect in CI (unset → 0).
  void beat() {
    bool ok = false;
    const int ms = qEnvironmentVariableIntValue("STENCIL_GUI_SLOWMO", &ok);
    if (ok && ms > 0) QTest::qWait(ms);
  }

  // A confirmation is a modal QMessageBox that blocks the triggering call (Quit, Delete Project
  // File, …). Arm this BEFORE triggering the action: it waits for the box to appear and clicks the
  // button whose label matches (the dialogs use custom "Quit"/"Cancel"/"Delete" buttons, not
  // standard Yes/No roles), letting the otherwise-blocked trigger() return with that answer.
  void dismissModal(const QString& buttonText) {
    QTimer::singleShot(0, [buttonText]() {
      for (int i = 0; i < 200; ++i) {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
          for (QAbstractButton* b : box->buttons())
            if (QString(b->text()).remove('&').compare(buttonText, Qt::CaseInsensitive) == 0) {
              b->click();
              return;
            }
          return;
        }
        QTest::qWait(5);
      }
    });
  }
}

class MainWindowGuiTest : public QObject {
  Q_OBJECT
  QString png_;

  // Build a shown MainWindow with our test image loaded; returns its live canvas.
  CanvasWidget* openLoaded(MainWindow& win) {
    win.resize(1000, 760);
    win.show();
    win.raise();
    win.activateWindow();
    beat();                       // (watch mode) empty editor
    win.openPathFromOS(png_);
    return win.findChild<CanvasWidget*>();
  }

 private slots:
  void initTestCase() {
    // The quit-confirmation test closes its window; keep that from ending the shared
    // QApplication (and the rest of the run) with it.
    qApp->setQuitOnLastWindowClosed(false);
    // A generous album-orientation image (larger than the tiny shared fixture) so the
    // fit-scaled canvas has room for well-separated draw clicks.
    QImage img(240, 160, QImage::Format_RGB32);
    img.fill(Qt::white);
    png_ = QDir::temp().filePath("stencil_gui_e2e_input.png");
    QVERIFY(img.save(png_, "PNG"));
  }

  // Regression: enabling the f(x,y) pill must reveal the x/y formula inputs, and they must
  // stay visible across an image load and window resizes (the state the user drives).
  // Hovering the project name REVEALS its ✎/🎨 affordances. At their natural size
  // (51×47 — QToolButton padding plus a default-sized icon) they were far taller than the
  // 28px name field, so the header grew the moment the cursor arrived and the whole window
  // jumped under it. Every control in that group is sized to the row instead.
  void nameHoverDoesNotResizeTheHeader() {
    MainWindow win(nullptr, false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(300);
    win.actToolbars_->setChecked(false);   // collapsed: the header is all there is
    QTest::qWait(400);
    QToolBar* hdr = win.headerToolbar_;
    QVERIFY(hdr);
    const int before = hdr->height();
    win.nameHover_ = true;
    win.refreshProjectNameButtons();
    QTest::qWait(200);
    QCOMPARE(hdr->height(), before);
    // …and the same going into edit mode, where ✓/✗ take their place.
    win.projectName_->setEnabled(true);
    win.enterNameEdit();
    QTest::qWait(200);
    QCOMPARE(hdr->height(), before);
  }

  // Two project actions the shared hotkeysConfig.json now carries: the trash reads
  // "Remove" and answers Ctrl+Alt+R, and Ctrl+Alt+N opens the toolbar name field for
  // inline editing — the keyboard route to the ✎ the browser grew at the same time.
  // (shareImage is in that config too, but browser-only: this app has no share action.)
  void projectRemoveAndRenameShortcuts() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    QCOMPARE(clear->shortcut(), QKeySequence("Ctrl+Alt+R"));
    QVERIFY2(clear->toolTip().startsWith("Remove"), qPrintable("trash tooltip: " + clear->toolTip()));

    QAction* rename = actionByText(&win, "Rename Project");
    QVERIFY2(rename, "Rename Project is discoverable as an action, not just a chord");
    QCOMPARE(rename->shortcut(), QKeySequence("Ctrl+Alt+N"));
    // Gated by the name field itself: a fresh window has no project, so nothing to rename.
    QVERIFY(!win.projectName_->isEnabled());
    QVERIFY(!rename->isEnabled());
    // An active project makes the name editable (updateProjectTitle), and the action follows.
    win.activeProjectId_ = QStringLiteral("test-project");
    win.refreshActions();
    QVERIFY(win.projectName_->isEnabled());
    QVERIFY2(rename->isEnabled(), "the action did not follow the name field");
    QVERIFY(!win.nameEditing_);
    rename->trigger();
    QTest::qWait(50);
    QVERIFY2(win.nameEditing_, "the chord did not enter inline rename");
    QVERIFY(!win.projectName_->isReadOnly());
    QCOMPARE(win.projectName_, win.focusWidget());
    win.cancelProjectName();
  }

  // Hover text is a shared contract: a control that exists in BOTH apps says the same
  // thing on hover, so the browser's toolbar.js is the source of truth for it (data-title,
  // or plain title where a control carries no rich tooltip). Menu LABELS are free to
  // differ — a menu row and a tooltip are different jobs — and so are desktop-only
  // affordances (Cycle Filter/Compare, the page/unit fields, the Settings dialog).
  // This is what drifted: the live-sync button described itself in its own words and,
  // having been built with no shortcut, showed no keycap at all (user report).
  void toolbarTooltipsMatchTheBrowser() {
    QFile f(QStringLiteral(__FILE__).section('/', 0, -4) + "/browser/js/ui/toolbar.js");
    QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable("cannot read " + f.fileName()));
    const QString js = QString::fromUtf8(f.readAll());
    QVERIFY(!js.isEmpty());

    // The browser's hover text for one control id: data-title when it has one (the rich
    // tooltip), else the plain title. HTML entities back to their characters.
    auto browserTip = [&js](const QString& id) {
      const QRegularExpression tag("<[a-zA-Z]+[^>]*\\bid=\"" + id + "\"[^>]*>");
      const QRegularExpressionMatch m = tag.match(js);
      if (!m.hasMatch()) return QString();
      const QString t = m.captured(0);
      QRegularExpressionMatch a = QRegularExpression("data-title=\"([^\"]*)\"").match(t);
      if (!a.hasMatch()) a = QRegularExpression("\\stitle=\"([^\"]*)\"").match(t);
      QString v = a.hasMatch() ? a.captured(1) : QString();
      return v.replace("&amp;", "&").replace("&#10;", "\n");
    };
    // A desktop tooltip is "<text> (<shortcut>)" — the shortcut is drawn as a keycap, so
    // only the text takes part in the comparison.
    auto textOf = [](QString tip) {
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
        {win.actOpen_, nullptr, "load-image-btn"},
        {win.actSaveImage_, nullptr, "save-image"},
        {win.actCopyImage_, nullptr, "copy-image"},
        {win.actOpenIn_, nullptr, "open-in-btn"},
        {win.actProjects_, nullptr, "projects-btn"},
        {win.actOpenProjectFile_, nullptr, "open-project-btn"},
        {win.actStencilLiveSync_, nullptr, "live-sync-btn"},
        {win.actDeleteProjectFile_, nullptr, "delete-project-btn"},
        {win.actConnect_, nullptr, "connect-btn"},
        {win.actLinks_, nullptr, "links-btn"},
        {win.actChat_, nullptr, "chat-btn"},
        {win.actCrop_, nullptr, "crop-image"},
        {win.actRotateLeft_, nullptr, "rotate-left"},
        {win.actRotateRight_, nullptr, "rotate-right"},
        {win.actUndo_, nullptr, "undo"},
        {win.actRedo_, nullptr, "redo"},
        {win.actFit_, nullptr, "zoom-fit"},
        {win.actDownloadJson_, nullptr, "download-json"},
        {win.actUploadJson_, nullptr, "upload-json-btn"},
        {win.actCopyLayout_, nullptr, "copy-json-btn"},
        {win.actClearProject_, nullptr, "clear-storage"},
        {win.actTheme_, nullptr, "theme-toggle"},
        {win.actIncognito_, nullptr, "incognito-toggle"},
        {win.actInfo_, nullptr, "info-btn"},
        {nullptr, win.imageFilter_, "image-filter"},
        {nullptr, win.filterColorBtn_, "filter-color"},
        {nullptr, win.blankColorBtn_, "blank-color-btn"},
        {nullptr, win.zoom_, "zoom-input"},
        {nullptr, win.pageSize_, "page-size"},
        {nullptr, win.unitCombo_, "unit-select"},
        {nullptr, win.lineColorBtn_, "line-color"},
        {nullptr, win.lineThickness_, "line-thickness"},
        {nullptr, win.lineStyle_, "line-style"},
        {nullptr, win.pointColorBtn_, "point-color"},
        {nullptr, win.pointSize_, "point-size"},
        {nullptr, win.drawModeBtn_, "draw-mode-toggle"},
        {nullptr, win.projectNameEdit_, "project-name-edit"},
        {nullptr, win.projectColorBtn_, "project-color-btn"},
        {nullptr, win.projectNameAccept_, "project-name-accept"},
        {nullptr, win.projectNameCancel_, "project-name-cancel"},
    };
    for (const Pair& p : pairs) {
      const QString want = browserTip(QString::fromLatin1(p.browserId));
      QVERIFY2(!want.isEmpty(), qPrintable(QString("no browser control #%1").arg(p.browserId)));
      QVERIFY2(p.act || p.widget, p.browserId);
      const QString got = textOf(p.act ? p.act->toolTip() : p.widget->toolTip());
      QVERIFY2(got == want,
               qPrintable(QString("#%1: desktop says \"%2\", the browser says \"%3\"")
                              .arg(p.browserId, got, want)));
    }
    // …and the shortcut the shared registry defines for a control really is on it, or the
    // tooltip has no keycap to draw and the chord does nothing.
    QCOMPARE(win.actStencilLiveSync_->shortcut(), QKeySequence(win.hotkey("toggleLiveSync", "Ctrl+Shift+Y")));
    QCOMPARE(win.actDeleteProjectFile_->shortcut(), QKeySequence(win.hotkey("deleteProject", "Ctrl+Shift+Backspace")));
  }

  // A dead icon says so under the pointer — the browser's `cursor: not-allowed` on a
  // disabled control. Qt applies no cursor to a DISABLED widget (it gets no mouse events),
  // so the row it sits in carries one for it; without that the toolbar answered a dead
  // Save/live-sync icon with the plain arrow, as if it were clickable.
  void disabledToolIconShowsTheBlockedCursor() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // A fresh window has no .stencil link, so live-sync is dead while Projects beside it
    // is live: one row, both states.
    QVERIFY(!win.actStencilLiveSync_->isEnabled());
    QToolButton* dead = nullptr;
    QToolButton* live = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>()) {
      if (!b->isVisible() || !b->parentWidget()) continue;
      if (!b->parentWidget()->property("toolRow").toBool()) continue;
      if (b->defaultAction() == win.actStencilLiveSync_) dead = b;
      if (b->defaultAction() == win.actProjects_ && !live) live = b;
    }
    QVERIFY2(dead, "no toolbar button for live sync");
    QWidget* row = dead->parentWidget();
    QVERIFY(row->hasMouseTracking());

    // The move Qt actually delivers: a disabled widget gets no mouse events, so the one
    // over a dead icon reaches the application filters with the BUTTON as its object and is
    // then thrown away. That is the path the cursor has to hang off — and what it sets is
    // an OVERRIDE cursor, the only one Qt applies without waiting to re-enter a widget.
    auto blocked = [] {
      const QCursor* c = QApplication::overrideCursor();
      return c && c->shape() == Qt::ForbiddenCursor;
    };
    auto moveOver = [&](QWidget* target) {
      const QPoint local(target->width() / 2, target->height() / 2);
      QMouseEvent me(QEvent::MouseMove, QPointF(local), target->mapToGlobal(local),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      qApp->sendEvent(target, &me);
    };
    QVERIFY(!blocked());
    moveOver(dead);
    QVERIFY2(blocked(), "a dead icon left the pointer unchanged");
    // Over a LIVE icon in the same row, and it goes back at once.
    QVERIFY2(live && live->parentWidget() == row, "no live icon beside it to move onto");
    moveOver(live);
    QVERIFY2(!blocked(), "the blocked cursor stuck over the live icon next door");
    moveOver(dead);
    QVERIFY(blocked());
    // The same again through the PLATFORM path (window → childAt → notify), which is what a
    // real pointer does — the synthetic sends above only prove the filter's arithmetic.
    moveOver(live);
    QTest::mouseMove(&win, win.mapFromGlobal(dead->mapToGlobal(dead->rect().center())));
    QTest::qWait(30);
    QVERIFY2(blocked(), "a real move over the dead icon left the plain cursor");
    // …and leaving the window clears it even with no move to land anywhere else.
    QEvent leave(QEvent::Leave);
    qApp->sendEvent(row, &leave);
    QVERIFY2(!blocked(), "leaving must put the cursor back");
    // A LIVE icon still carries its own hand cursor — nothing here touches that.
    if (live) QCOMPARE(live->cursor().shape(), Qt::PointingHandCursor);
  }

  // Every selector opens the app's OWN popup, never the platform one: macOS draws a
  // native combo popup itself — centred over the control, in its own palette — so a
  // toolbar of themed controls answered a click with a system menu (user report). The
  // browser makes the same swap (js/ui/customSelect.js, e2e custom-selects.spec.js).
  // The ONE exception on both sides is zoom: a number field with a preset list attached,
  // which works as it is.
  void everySelectorUsesTheAppsOwnPopup() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    int checked = 0;
    for (QComboBox* c : win.findChildren<QComboBox*>()) {
      if (c == win.zoom_) { QVERIFY2(c->isEditable(), "zoom is the editable exception"); continue; }
      // dynamic_cast, not qobject_cast: SearchComboBox is deliberately MOC-free.
      QVERIFY2(dynamic_cast<stencil::gui::SearchComboBox*>(c),
               qPrintable(QString("%1 still opens the platform popup").arg(c->objectName())));
      QCOMPARE(c->cursor().shape(), Qt::PointingHandCursor);
      ++checked;
    }
    QVERIFY2(checked >= 4, "no toolbar selectors were found to check");

    // …and opening one really puts OUR popup on screen, with the rows in it.
    QVERIFY(win.lineStyle_);
    win.lineStyle_->showPopup();
    QTest::qWait(60);
    QWidget* popup = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets())
      if (w->isVisible() && w->findChild<QWidget*>("searchComboPopup")) popup = w;
    QVERIFY2(popup, "the themed popup never appeared");
    auto* list = popup->findChild<QListView*>("searchComboList");
    QVERIFY(list);
    QCOMPARE(list->model()->rowCount(), win.lineStyle_->count());
    // A short list carries no search box — three rows need no filter.
    QVERIFY2(!popup->findChild<QLineEdit*>("searchComboSearch"), "a 3-row list grew a search box");
    win.lineStyle_->hidePopup();
    // The long ISO page list keeps its search box, which is what it was built for.
    win.pageSize_->showPopup();
    QTest::qWait(60);
    QWidget* pagePopup = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets())
      if (w->isVisible() && w->findChild<QWidget*>("searchComboPopup")) pagePopup = w;
    QVERIFY(pagePopup);
    QVERIFY2(pagePopup->findChild<QLineEdit*>("searchComboSearch"), "page formats lost their search");
    win.pageSize_->hidePopup();
  }

  // The f(x,y) pair is as wide as the browser's (#formula-x / #formula-y, 180px inline)
  // wherever the row has room — they were pinned to a 72px stub, which fits no real formula
  // — and give that width back on a narrow window instead of hiding behind QToolBar's "»".
  void formulaFieldsTakeTheBrowsersWidth() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* pill = win.findChild<QCheckBox*>("formulaPill");
    QVERIFY(pill && win.formulaX_ && win.formulaY_);
    if (!pill->isChecked()) pill->setChecked(true);
    QTRY_VERIFY(win.formulaX_->isVisible());
    QTRY_COMPARE(win.formulaX_->width(), 180);
    QCOMPARE(win.formulaY_->width(), 180);
    // …side by side on the browser's 6px gap, not spread out over the row's leftover width.
    QCOMPARE(win.formulaY_->x() - (win.formulaX_->x() + win.formulaX_->width()), 6);
    // A row with no slack squeezes them; the cluster itself stays out on the toolbar.
    win.resize(920, 800);
    QTest::qWait(120);
    QVERIFY2(win.formulaX_->isVisible(), "the formula fields hid instead of shrinking");
    QVERIFY2(win.formulaX_->width() < 180 && win.formulaX_->width() >= 72,
             qPrintable(QString("squeezed to %1px").arg(win.formulaX_->width())));
  }

  void formulaToggleRevealsInputs() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* pill = win.findChild<QCheckBox*>("formulaPill");
    QVERIFY(pill);
    QLineEdit* fx = nullptr;
    for (auto* e : win.findChildren<QLineEdit*>())
      if (e->placeholderText().startsWith("x(x)")) fx = e;
    QVERIFY(fx);
    if (pill->isChecked()) { pill->setChecked(false); QTest::qWait(30); }
    QVERIFY(!fx->isVisible());
    QTest::mouseClick(pill, Qt::LeftButton, Qt::NoModifier, pill->rect().center());
    QTest::qWait(60);
    QVERIFY2(fx->isVisible(), "formula inputs should appear when f(x,y) is enabled");
    win.openPathFromOS(png_);
    QTest::qWait(120);
    QVERIFY2(fx->isVisible(), "formula inputs should survive an image load");
    win.resize(720, 800); QTest::qWait(80);
    win.resize(1200, 800); QTest::qWait(80);
    QVERIFY2(fx->isVisible(), "formula inputs should survive window resizes");
  }

  // The logo's hover fx (browser animations.css logoPulse / logoRaysSpin parity):
  // hovering the mark shows a window-level overlay that owns the pixels (the button
  // icon blanks so the pulsing copy never doubles a static one), the loop genuinely
  // ADVANCES, and leaving stops every animation (no idle timers) and hands the icon
  // back. The overlay is larger than the button — glow + rays paint in a margin
  // around it, never by resizing the toolbar row.
  void logoHoverFxPulsesWhileHoveredOnly() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* logo = win.logoBtn_;
    QVERIFY(logo);
    QWidget* fx = win.findChild<QWidget*>("logoHoverFx");
    QVERIFY2(fx, "logo hover fx overlay not installed");
    QVERIFY(!fx->isVisible());
    auto iconBlank = [logo] {
      const QImage im = logo->icon().pixmap(logo->iconSize()).toImage();
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x)
          if (qAlpha(im.pixel(x, y)) != 0) return false;
      return true;
    };
    QVERIFY(!iconBlank());   // at rest the button paints the mark itself

    const QPointF c(logo->rect().center());
    QEnterEvent enter(c, c, logo->mapToGlobal(logo->rect().center()));
    QApplication::sendEvent(logo, &enter);
    QVERIFY2(fx->isVisible(), "hover-enter must show the fx overlay");
    QVERIFY(fx->property("fxActive").toBool());
    QVERIFY2(iconBlank(), "the overlay owns the mark while animating (icon blanked)");
    QVERIFY2(fx->width() > logo->width() && fx->height() > logo->height(),
             "fx overlay must give the glow/rays room AROUND the button");
    const qreal b0 = fx->property("pulseBeat").toReal();
    const qreal a0 = fx->property("raysAngle").toReal();
    QTest::qWait(250);
    QVERIFY2(fx->property("pulseBeat").toReal() != b0, "pulse beat did not advance");
    QVERIFY2(fx->property("raysAngle").toReal() != a0, "ray rotation did not advance");
    QVERIFY(!fx->grab().isNull());   // painting the fx offscreen must not crash

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(logo, &leave);
    QVERIFY2(!fx->isVisible(), "hover-leave must hide the fx overlay");
    QVERIFY(!fx->property("fxActive").toBool());
    for (QVariantAnimation* a : fx->findChildren<QVariantAnimation*>())
      QVERIFY2(a->state() != QAbstractAnimation::Running,
               "an fx animation kept running after hover-leave");
    QVERIFY2(!iconBlank(), "leave must hand the mark back to the button icon");
  }

  // A popover must close on a click OUTSIDE it — canvas, toolbar, anywhere — from BOTH
  // routes that open the accent picker (right-click sticky, hold-Alt peek), and whether
  // Qt delivers that press or not: exec() is application-modal, so the platform DROPS
  // presses aimed at the blocked main window and the app-wide filter never sees them.
  // That is why the theme-colour popup ignored outside clicks on a real desktop while
  // every test synthesising a press straight into a widget passed (user report).
  void accentPopoverClosesOnOutsideClick() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* logo = win.logoBtn_;
    QVERIFY(logo && win.canvas_);
    // With a picture loaded, so a canvas press is an ordinary editing press and not the
    // empty canvas's "create a blank image" invitation (another modal dialog).
    QImage pic(320, 240, QImage::Format_RGB32);
    pic.fill(Qt::darkCyan);
    win.canvas_->loadFromImage(pic);
    QTRY_VERIFY(win.canvas_->hasImage());
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();   // typingFocus gate off
    const QPoint c = logo->rect().center();
    QToolButton* other = nullptr;   // a toolbar icon that is not the logo
    for (auto it = win.popoverButtons_.cbegin(); it != win.popoverButtons_.cend(); ++it)
      if (it.value() == win.actConnect_) other = static_cast<QToolButton*>(it.key());
    QVERIFY(other);

    enum Route { Sticky, Peek };
    // Open the picker by `route`, press outside on `target`, and report whether the
    // popover opened and then went.
    const auto outsidePressCloses = [&](Route route, QWidget* target, const char* what) {
      bool opened = false, closed = false, notModal = false;
      QTimer::singleShot(120, &win, [&] {
        opened = win.activePopover_ &&
                 win.activePopover_->objectName() == QLatin1String("accentPopover") &&
                 win.activePopover_->isVisible();
        // THE mechanism: the popover must not be MODAL. An application-modal dialog
        // marks this window blockedByModalWindow, and Qt then drops every press aimed
        // at it before any filter runs — which is exactly why the outside click did
        // nothing on the user's machine. No modal widget ⇒ the press is delivered.
        notModal = !QApplication::activeModalWidget() && win.activePopover_ &&
                   !win.activePopover_->isModal() && win.isEnabled();
        const QPoint local = target->rect().center();
        const QPoint at = target->mapToGlobal(local);
        // A real press, press + release, exactly as the window system delivers it now
        // that the popover no longer blocks this window.
        QMouseEvent press(QEvent::MouseButtonPress, local, at, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &press);
        QMouseEvent rel(QEvent::MouseButtonRelease, local, at, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(target, &rel);
        closed = !win.activePopover_ || win.activePopover_->isHidden();
        if (win.activePopover_ && !win.activePopover_->isHidden()) win.activePopover_->reject();
      });
      if (route == Sticky) {
        QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
        QApplication::sendEvent(logo, &ctx);      // blocks in exec until the timer acts
      } else {
        logo->setAttribute(Qt::WA_UnderMouse, true);
        QTest::keyPress(&win, Qt::Key_Alt);       // ditto, via the peek
        logo->setAttribute(Qt::WA_UnderMouse, false);
        QTest::keyRelease(&win, Qt::Key_Alt);
      }
      QVERIFY2(opened, qPrintable(QString("%1: the popover never opened").arg(what)));
      QVERIFY2(notModal,
               qPrintable(QString("%1: the popover is application-modal — Qt will drop "
                                  "the outside press before any filter sees it").arg(what)));
      QVERIFY2(closed, qPrintable(QString("%1: the popover survived a press outside it")
                                      .arg(what)));
      QVERIFY(!win.activePopover_);
      // The closing click is spent: no icon may re-open what it just dismissed.
      QVERIFY2(!win.popoverClickTimer_->isActive(),
               qPrintable(QString("%1: the dismissing click armed a re-open").arg(what)));
      QVERIFY2(!win.logoClickTimer_->isActive(),
               qPrintable(QString("%1: the dismissing click armed the accent cycle").arg(what)));
      QTest::qWait(320);   // past both deferred-click delays…
      QVERIFY2(!win.activePopover_, qPrintable(QString("%1: it came back").arg(what)));
      // …and the nested loop really unwound: the OUTER loop is running our timers again.
      bool alive = false;
      QTimer::singleShot(0, &win, [&alive] { alive = true; });
      QTest::qWait(60);
      QVERIFY2(alive, qPrintable(QString("%1: the nested event loop leaked").arg(what)));
    };

    outsidePressCloses(Sticky, win.canvas_, "sticky + canvas press");
    outsidePressCloses(Sticky, other, "sticky + toolbar press");
    outsidePressCloses(Peek, win.canvas_, "peek + canvas press");
    outsidePressCloses(Peek, other, "peek + toolbar press");
    // The logo itself: a press on it closes a STICKY popover (the peek's own no-op rule
    // is checked in logoAccentPopoverPicksDirectly) and must not cycle the accent.
    const QString accentBefore = win.settings_.accentColor;
    outsidePressCloses(Sticky, logo, "sticky + logo press");
    QCOMPARE(win.settings_.accentColor, accentBefore);

    // …and the other half of the rule: a press INSIDE the popover, or in a NESTED dialog
    // the popover opened (a confirm, a colour picker), leaves it alone — and Escape
    // still closes it.
    bool insideKept = false, nestedKept = false, escapeClosed = false;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.activePopover_.data();
      if (pop) {
        const auto pressAt = [](QWidget* w) {
          const QPoint local = w->rect().center();
          QMouseEvent press(QEvent::MouseButtonPress, local, w->mapToGlobal(local),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
          QApplication::sendEvent(w, &press);
        };
        pressAt(pop);
        insideKept = win.activePopover_ && !win.activePopover_->isHidden();
        // A nested dialog on top of the popover: its presses belong to another window.
        auto nested = std::make_unique<QDialog>(pop);
        nested->setObjectName(QStringLiteral("nestedOverPopover"));
        nested->resize(120, 80);
        nested->show();
        QTest::qWait(30);
        pressAt(nested.get());
        nestedKept = win.activePopover_ && !win.activePopover_->isHidden() &&
                     win.activePopover_.data() == pop;
        nested->close();
        QTest::qWait(30);
        QTest::keyClick(pop, Qt::Key_Escape);
        escapeClosed = !win.activePopover_ || win.activePopover_->isHidden();
      }
      if (win.activePopover_ && !win.activePopover_->isHidden()) win.activePopover_->reject();
    });
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);
    QVERIFY2(insideKept, "a press INSIDE the popover must not dismiss it");
    QVERIFY2(nestedKept, "a press in a NESTED dialog must not dismiss the popover under it");
    QVERIFY2(escapeClosed, "Escape must still close the popover");
    QVERIFY(!win.activePopover_);

    // Losing the KEYBOARD closes it too — the path that survives when the window system
    // The popover lives in this window, so an app-focus loss is what ends it (a click
    // elsewhere in the window is the press rule's job). The exemption: a NESTED dialog
    // the popover opened took that focus for us.
    bool deactClosedSticky = false, deactKeptWithNested = false;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.activePopover_.data();
      if (pop) {
        auto nested = std::make_unique<QDialog>(pop);
        nested->resize(120, 80);
        nested->show();
        QTest::qWait(30);
        QEvent deact1(QEvent::WindowDeactivate);
        QApplication::sendEvent(&win, &deact1);   // …handing focus to OUR nested window
        deactKeptWithNested = win.activePopover_ && !win.activePopover_->isHidden();
        nested->close();
        nested.reset();
        QTest::qWait(30);
        QEvent deact2(QEvent::WindowDeactivate);
        QApplication::sendEvent(&win, &deact2);   // …now the app really lost focus
        deactClosedSticky = !win.activePopover_ || win.activePopover_->isHidden();
      }
      if (win.activePopover_ && !win.activePopover_->isHidden()) win.activePopover_->reject();
    });
    QContextMenuEvent ctx2(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx2);
    QVERIFY2(deactKeptWithNested,
             "a nested window's activation must not close the popover under it");
    QVERIFY2(deactClosedSticky, "losing focus must close even a STICKY popover");
    QVERIFY(!win.activePopover_);

    // ── One open, one close, both animated, nothing re-shown ──
    // The popover used to be a small frameless TOP-LEVEL window, and on the user's macOS
    // build nothing animated it: a ghost flown under it (it stayed hidden behind the
    // window until that unmapped — the "blink"), its own windowOpacity, and its own
    // geometry were all driven correctly in Qt and rendered by nobody. It is a CHILD
    // WIDGET of the main window now, so its grow/shrink are ordinary in-window
    // animations, like the toasts and the logo overlay. (Animations are off for the
    // suite; this case needs them.)
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] {
      if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim);
    });
    // The lifecycle as it happens, in order: the popover's own show/hide, every box the
    // hosting overlay takes (its animation, in main-window coordinates), and any ghost
    // the reveal machinery might fly — there must be none.
    struct Trace : QObject {
      QStringList seq;
      QSet<QObject*> dialogs;
      QElapsedTimer clock;
      qint64 pressedAt = -1, hidAt = -1, collapsedAt = -1;
      QList<QRect> opening, closing;   // overlay boxes, before and after the press
      bool dismissed = false;
      bool eventFilter(QObject* o, QEvent* e) override {
        auto* w = qobject_cast<QWidget*>(o);
        if (w && w->objectName() == QLatin1String("accentPopover")) {
          if (e->type() == QEvent::Show) { seq << QStringLiteral("show"); dialogs.insert(o); }
          if (e->type() == QEvent::Hide) {
            seq << QStringLiteral("hide");
            hidAt = clock.isValid() ? clock.elapsed() : -1;
          }
        }
        if (w && w->objectName() == QLatin1String("popoverOverlay") &&
            (e->type() == QEvent::Resize || e->type() == QEvent::Move)) {
          (dismissed ? closing : opening) << w->geometry();
          if (dismissed) collapsedAt = clock.elapsed();
        }
        if (w && w->objectName() == QLatin1String("stencilModalGhost") &&
            e->type() == QEvent::Show)
          seq << QStringLiteral("ghost");
        return false;
      }
    } trace;
    trace.clock.start();
    qApp->installEventFilter(&trace);
    const auto removeTrace = qScopeGuard([&] { qApp->removeEventFilter(&trace); });

    for (QWidget* target : {static_cast<QWidget*>(win.canvas_), static_cast<QWidget*>(logo),
                            static_cast<QWidget*>(other)}) {
      trace.seq.clear();
      trace.dialogs.clear();
      trace.pressedAt = trace.hidAt = trace.collapsedAt = -1;
      trace.opening.clear();
      trace.closing.clear();
      trace.dismissed = false;
      QRect openBox;
      bool wasTopLevel = true, hadOverlay = false;
      QTimer::singleShot(400, &win, [&] {   // …once the open animation has landed
        const QPoint local = target->rect().center();
        const QPoint at = target->mapToGlobal(local);
        trace.pressedAt = trace.clock.elapsed();
        // THE property: no window of its own. That is what made three animated closes
        // invisible, and what the in-window overlay fixes.
        if (win.activePopover_) wasTopLevel = win.activePopover_->isWindow();
        hadOverlay = win.popoverOverlay_ && win.popoverOverlay_->isVisible();
        if (win.popoverOverlay_) openBox = win.popoverOverlay_->geometry();
        trace.dismissed = true;
        QMouseEvent pr(QEvent::MouseButtonPress, local, at, Qt::LeftButton, Qt::LeftButton,
                       Qt::NoModifier);
        QApplication::sendEvent(target, &pr);
        QMouseEvent rl(QEvent::MouseButtonRelease, local, at, Qt::LeftButton, Qt::NoButton,
                       Qt::NoModifier);
        QApplication::sendEvent(target, &rl);
      });
      QContextMenuEvent open(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
      QApplication::sendEvent(logo, &open);
      QTest::qWait(700);   // past both deferred-click delays and the closing flight
      const QString what = QStringLiteral("dismiss on %1").arg(target->objectName().isEmpty()
                                                                   ? target->metaObject()->className()
                                                                   : target->objectName());
      const QString seq = trace.seq.join(QLatin1Char(','));
      QCOMPARE(trace.dialogs.size(), 1);   // ONE popover instance, never a second
      QVERIFY2(trace.seq.count(QStringLiteral("show")) == 1,
               qPrintable(QString("%1: shown %2x — something re-showed it (%3)")
                              .arg(what).arg(trace.seq.count(QStringLiteral("show"))).arg(seq)));
      QVERIFY2(trace.seq.count(QStringLiteral("hide")) == 1,
               qPrintable(QString("%1: hidden %2x (%3)")
                              .arg(what).arg(trace.seq.count(QStringLiteral("hide"))).arg(seq)));
      // No ghost at all: the popover animates itself now, and a ghost is the thing that
      // could be revealed from under a window and read as a re-open.
      QVERIFY2(!seq.contains(QLatin1String("ghost")),
               qPrintable(QString("%1: a ghost was flown for an in-window popover (%2)")
                              .arg(what, seq)));
      QVERIFY2(seq == QLatin1String("show,hide"),
               qPrintable(QString("%1: unexpected lifecycle — got %2").arg(what, seq)));
      // Never a show AFTER a hide: nothing is re-shown, by construction.
      QVERIFY2(!seq.contains(QLatin1String("hide,show")),
               qPrintable(QString("%1: the popover was re-shown after closing (%2)")
                              .arg(what, seq)));
      // …and it is a CHILD WIDGET, hosted by an in-window overlay.
      QVERIFY2(!wasTopLevel, qPrintable(QString("%1: the popover is still a top-level "
                                                "window — nothing will animate it").arg(what)));
      QVERIFY2(hadOverlay, qPrintable(QString("%1: no in-window overlay hosted it").arg(what)));
      // Both animations really stepped, in the window's own coordinates: out of the icon
      // on the way in, back into it on the way out.
      QVERIFY2(trace.opening.size() >= 3,
               qPrintable(QString("%1: %2 steps opening — it did not grow out of the icon")
                              .arg(what).arg(trace.opening.size())));
      QVERIFY2(trace.closing.size() >= 3,
               qPrintable(QString("%1: %2 steps closing — the collapse did not run")
                              .arg(what).arg(trace.closing.size())));
      QRect first = trace.opening.first();   // the smallest box the grow started from
      for (const QRect& r : trace.opening)
        if (r.width() * r.height() < first.width() * first.height()) first = r;
      const QRect last = trace.closing.last();
      const QRect logoBox = QRect(logo->mapTo(&win, QPoint(0, 0)), logo->size());
      QVERIFY2(first.width() * first.height() * 4 < openBox.width() * openBox.height(),
               qPrintable(QString("%1: opened from %2x%3 — not from the icon")
                              .arg(what).arg(first.width()).arg(first.height())));
      QVERIFY2(openBox.isValid() && last.width() * last.height() * 4 <
                                        openBox.width() * openBox.height(),
               qPrintable(QString("%1: ended at %2x%3 from %4x%5 — barely shrank")
                              .arg(what).arg(last.width()).arg(last.height())
                              .arg(openBox.width()).arg(openBox.height())));
      QVERIFY2((last.center() - logoBox.center()).manhattanLength() <
                   (openBox.center() - logoBox.center()).manhattanLength(),
               qPrintable(QString("%1: it did not collapse TOWARD the icon").arg(what)));
      // The collapse took real time — the dialog itself hides at once (its picture is
      // frozen into the overlay), so it is the OVERLAY's last step that dates the end.
      QVERIFY2(trace.collapsedAt - trace.pressedAt >= 120,
               qPrintable(QString("%1: collapsed in %2 ms — it snapped shut, no animation")
                              .arg(what).arg(trace.collapsedAt - trace.pressedAt)));
      QVERIFY(!win.activePopover_);
    }
    beat();
  }

  // Logo accent-preset picker as a FIRST-CLASS popover (openAccentPicker via
  // execMaybePopover; logoBtn_ registered in popoverButtons_ with actAccent_):
  //  • right-click opens it STICKY (rows = accentPresets, chip + label, one ✓-marked
  //    current row; Alt gymnastics don't close it; a row click applies via the
  //    click-cycle's applySettings path and closes);
  //  • hold-Alt PEEKS it exactly like every other popover icon (prompt open on the
  //    keypress, Alt release rejects it at once);
  //  • Alt-GLIDE logo → Connections swaps popovers with a single instance at a time;
  //  • mid-peek, a LEFT-click on the logo is a no-op and a RIGHT-press PROMOTES the
  //    same popover to sticky; the popover's own WindowDeactivate ends a peek;
  //  • plain click still cycles, Alt+click stays inert.
  // The popovers are modal (exec), so every mid-open interaction runs from timers
  // scheduled before the blocking call.
  void logoAccentPopoverPicksDirectly() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* logo = win.logoBtn_;
    QVERIFY(logo);
    const QString original = win.settings_.accentColor;   // persisted — restored below
    const auto& presets = stencil::gui::accentPresets();
    QVERIFY(!presets.empty());
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();   // typingFocus gate off
    const QPoint c = logo->rect().center();
    int pick = -1;
    for (size_t i = 0; i < presets.size(); ++i)
      if (presets[i].key != original) { pick = int(i); break; }
    QVERIFY(pick >= 0);

    // ── STICKY right-click route ──
    bool stickyOpened = false, rowsOk = false, stickySurvivedAlt = false;
    bool picksOk = true, escapeClosedAfterPicks = false;
    QString lastPick;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.activePopover_.data();
      stickyOpened = pop && pop->objectName() == QLatin1String("accentPopover") && pop->isVisible();
      if (pop) {
        int currentCount = 0;
        rowsOk = true;
        for (const auto& a : presets) {
          auto* row = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + a.key);
          rowsOk = rowsOk && row && row->text() == a.label && !row->icon().isNull();
          if (row && row->property("currentAccent").toBool()) ++currentCount;
        }
        rowsOk = rowsOk && currentCount == 1;
      }
      // Sticky: Alt press/release must NOT close it (only a peek dies with the key).
      QTest::keyPress(&win, Qt::Key_Alt);
      QTest::keyRelease(&win, Qt::Key_Alt);
      stickySurvivedAlt = win.activePopover_ && !win.activePopover_->isHidden();
      // Picking a colour APPLIES it and LEAVES THE POPOVER OPEN: the list is for trying
      // colours against the live app, so three picks in a row must all land, each moving
      // the ✓ to the row just clicked, with the popover never closing under the cursor.
      for (int i = 0; i < 3 && pop; ++i) {
        const QString key = presets[size_t((pick + i) % int(presets.size()))].key;
        auto* row = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + key);
        if (!row) { picksOk = false; break; }
        row->click();
        QTest::qWait(30);
        picksOk = picksOk && win.settings_.accentColor == key &&
                  win.activePopover_ && !win.activePopover_->isHidden();
        int marked = 0;
        for (const auto& a : presets) {
          auto* r = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + a.key);
          if (!r) continue;
          if (r->property("currentAccent").toBool()) ++marked;
          picksOk = picksOk && r->property("currentAccent").toBool() == (a.key == key);
        }
        picksOk = picksOk && marked == 1;
        lastPick = key;
      }
      // …and it closes the ordinary way: Escape.
      if (win.activePopover_) {
        QTest::keyClick(win.activePopover_.data(), Qt::Key_Escape);
        escapeClosedAfterPicks = !win.activePopover_ || win.activePopover_->isHidden();
      }
      if (win.activePopover_ && !win.activePopover_->isHidden()) win.activePopover_->reject();
    });
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's exec until the timer acts
    QVERIFY2(stickyOpened, "right-click did not open the accent popover");
    QVERIFY2(rowsOk, "popover rows must be the preset list with one ✓-marked current row");
    QVERIFY2(stickySurvivedAlt, "the sticky popover must survive an Alt press/release");
    QVERIFY2(picksOk, "each colour pick must apply, move the ✓, and leave the popover open");
    QVERIFY2(escapeClosedAfterPicks, "Escape must still close it after picking colours");
    QCOMPARE(win.settings_.accentColor, lastPick);
    QVERIFY2(!win.logoClickTimer_->isActive(), "the popover routes must not arm the click-cycle");
    QVERIFY(!win.activePopover_);

    // ── Alt+click stays inert (no popover, no accent cycle armed) ──
    QTest::mouseClick(logo, Qt::LeftButton, Qt::AltModifier, c);
    QVERIFY2(!win.activePopover_, "Alt+click must not open a popover");
    QVERIFY2(!win.logoClickTimer_->isActive(), "Alt+click must not arm the click-cycle timer");

    // ── PEEK: plain Alt hold over the logo opens promptly; Alt release closes ──
    // (hover simulated via WA_UnderMouse — the state a real Enter leaves behind; the
    // Alt KeyPress loop and glide poll accept it alongside the cursor check).
    logo->setAttribute(Qt::WA_UnderMouse, true);
    bool peekOpened = false, releaseClosed = false;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.activePopover_.data();
      peekOpened = pop && pop->objectName() == QLatin1String("accentPopover") && pop->isVisible();
      QTest::keyRelease(&win, Qt::Key_Alt);   // ends the peek on the spot
      releaseClosed = !win.activePopover_ || win.activePopover_->isHidden();
      if (win.activePopover_ && !win.activePopover_->isHidden()) win.activePopover_->reject();
    });
    QTest::keyPress(&win, Qt::Key_Alt);   // blocks in the peek's exec
    QVERIFY2(peekOpened, "holding Alt over the logo did not peek the accent popover");
    QVERIFY2(releaseClosed, "releasing Alt must close the peeked popover at once");
    QVERIFY(!win.activePopover_);

    // ── GLIDE logo → Connections: one popover at a time, swapped by the system ──
    QToolButton* connBtn = nullptr;
    for (auto it = win.popoverButtons_.cbegin(); it != win.popoverButtons_.cend(); ++it)
      if (it.value() == win.actConnect_) connBtn = static_cast<QToolButton*>(it.key());
    QVERIFY2(connBtn, "no popover button registered for the Connections action");
    win.altHeldForTest_ = true;   // the glide poll's stand-in for a physically held Alt
    bool accentFirst = false;
    QTimer::singleShot(120, &win, [&] {
      accentFirst = win.activePopover_ &&
                    win.activePopover_->objectName() == QLatin1String("accentPopover");
      // The pointer glides off the logo onto the Connections icon…
      logo->setAttribute(Qt::WA_UnderMouse, false);
      connBtn->setAttribute(Qt::WA_UnderMouse, true);
      // …and the glide poll (80ms) rejects this popover, then opens the next one.
    });
    QTest::keyPress(&win, Qt::Key_Alt);   // returns once the glide rejects the accent popover
    QVERIFY2(accentFirst, "the glide phase did not start from the accent popover");
    bool swapped = false, glideClosed = false;
    QTimer::singleShot(150, &win, [&] {   // fires inside the Connections popover's exec
      QDialog* pop = win.activePopover_.data();
      swapped = pop && qobject_cast<stencil::gui::ConnectDialog*>(pop) && pop->isVisible() &&
                win.findChildren<QDialog*>("accentPopover").isEmpty();   // single instance
      connBtn->setAttribute(Qt::WA_UnderMouse, false);
      win.altHeldForTest_ = false;
      QTest::keyRelease(&win, Qt::Key_Alt);   // closes the glided-to popover too
      glideClosed = !win.activePopover_ || win.activePopover_->isHidden();
      if (win.activePopover_ && !win.activePopover_->isHidden()) win.activePopover_->reject();
    });
    QTest::qWait(700);   // lets the deferred altPeekOpen(Connections) run + close
    QVERIFY2(swapped, "the glide did not swap to the Connections popover (single instance)");
    QVERIFY2(glideClosed, "Alt release did not close the glided-to popover");
    QVERIFY(!win.activePopover_);

    // ── Mid-peek gestures: left-click on the logo is a NO-OP; right-press PROMOTES ──
    logo->setAttribute(Qt::WA_UnderMouse, true);
    bool noopKept = false, cycleNotArmed = false, promoted = false;
    QTimer::singleShot(120, &win, [&] {
      if (win.activePopover_) {
        QTest::mousePress(logo, Qt::LeftButton, Qt::NoModifier, c);
        QTest::mouseRelease(logo, Qt::LeftButton, Qt::NoModifier, c);
        noopKept = win.activePopover_ && !win.activePopover_->isHidden();
        cycleNotArmed = !win.logoClickTimer_->isActive();
        QTest::mousePress(logo, Qt::RightButton, Qt::NoModifier, c);   // promote to sticky
        QTest::mouseRelease(logo, Qt::RightButton, Qt::NoModifier, c);
        QTest::keyRelease(&win, Qt::Key_Alt);   // promoted → the release must NOT close it
        promoted = win.activePopover_ && !win.activePopover_->isHidden();
      }
      if (win.activePopover_ && !win.activePopover_->isHidden()) win.activePopover_->reject();
    });
    QTest::keyPress(&win, Qt::Key_Alt);
    QVERIFY2(noopKept, "a left-click on the logo must not dismiss its peeked popover");
    QVERIFY2(cycleNotArmed, "a left-click during the peek armed the accent cycle");
    QVERIFY2(promoted, "a right-press during the peek must promote it past the Alt release");
    QVERIFY(!win.activePopover_);

    // ── Losing the app's focus ends a peek (Cmd-Tab eats the keyup). The popover is a
    // child widget of this window, so it is THIS window's deactivation that says so ──
    bool deactClosed = false;
    QTimer::singleShot(120, &win, [&] {
      if (win.activePopover_) {
        QEvent deact(QEvent::WindowDeactivate);
        QApplication::sendEvent(&win, &deact);
        deactClosed = !win.activePopover_ || win.activePopover_->isHidden();
      }
      if (win.activePopover_ && !win.activePopover_->isHidden()) win.activePopover_->reject();
      QTest::keyRelease(&win, Qt::Key_Alt);
    });
    QTest::keyPress(&win, Qt::Key_Alt);
    QVERIFY2(deactClosed, "the peeked popover must close when the app loses focus");
    QVERIFY(!win.activePopover_);
    logo->setAttribute(Qt::WA_UnderMouse, false);

    // ── Alt with the cursor NOT over any popover icon opens nothing ──
    win.move(400, 300);   // the offscreen cursor's resting point goes cold too
    QTest::qWait(30);
    QTest::keyPress(&win, Qt::Key_Alt);
    QTest::keyRelease(&win, Qt::Key_Alt);
    QVERIFY2(!win.activePopover_, "Alt away from the icons must not open a popover");

    // ── A PLAIN click still cycles: it arms the deferred timer ──
    QTest::mouseClick(logo, Qt::LeftButton, Qt::NoModifier, c);
    QVERIFY2(win.logoClickTimer_->isActive(), "plain click no longer arms the accent cycle");
    win.logoClickTimer_->stop();

    // Leave the persisted accent as we found it — the settings are shared across tests.
    auto restore = win.settings_;
    restore.accentColor = original;
    win.applySettings(restore, true);
  }

  // Regression: the f(x,y) pair commits when typing SETTLES, not per keystroke. A formula
  // is typed one character at a time, so an expression still being written must not be
  // judged (no "⚠ invalid" flashing mid-word) nor applied — it is only flagged once the
  // user stops. Mirrors browser settingsController.wireFormulaInputs.
  void formulaCommitsOnIdlePauseNotPerKeystroke() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    // Wide enough that the inline formula inputs stay on the row rather than folding
    // into the toolbar's ≫ extension — this test is about the debounce, not the layout.
    win.resize(1600, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* pill = win.findChild<QCheckBox*>("formulaPill");
    QVERIFY(pill);
    if (!pill->isChecked()) {
      QTest::mouseClick(pill, Qt::LeftButton, Qt::NoModifier, pill->rect().center());
      QTest::qWait(60);
    }
    QLineEdit* fx = nullptr;
    for (auto* e : win.findChildren<QLineEdit*>())
      if (e->placeholderText().startsWith("x(x)")) fx = e;
    QVERIFY(fx);
    // By object name, not by its text: the label is icon-only (the words live on its
    // tooltip, as in the browser), so matching on "invalid" found nothing.
    QLabel* err = win.findChild<QLabel*>("formulaError");
    QVERIFY(err);
    // A window restores the persisted formulas, so start from a known-empty field and let
    // that clear settle (leaving no pending commit to race the assertions below).
    fx->clear();
    QTest::qWait(kFormulaSettleMs);
    QVERIFY(!err->isVisible());

    fx->setFocus();
    QTest::keyClicks(fx, "(x+", Qt::NoModifier, 40);   // half-written, a key at a time
    QVERIFY2(!err->isVisible(), "an expression still being typed must not be flagged");
    QTest::qWait(kFormulaSettleMs);                    // stop typing → the pair commits
    QVERIFY2(err->isVisible(), "a settled, unparseable expression IS flagged");

    QTest::keyClicks(fx, "1)", Qt::NoModifier, 40);    // finish it: valid again
    QCOMPARE(fx->text(), QStringLiteral("(x+1)"));
    QVERIFY2(!err->isVisible(), "fixing the expression clears the error indicator on the spot");
    QTest::qWait(kFormulaSettleMs);
    QVERIFY(!err->isVisible());

    // Leave the persisted formula as we found it — the settings are shared across tests.
    fx->clear();
    QTest::qWait(kFormulaSettleMs);
  }

  // Fullscreen edge-hover: prove the top toolbars and the right points panel REVEAL WITH AN
  // ANIMATION (they pass through intermediate sizes, not an instant pop) and do so MONOTONICALLY
  // (no size oscillation = no flicker), then hide + fully restore on exit with no lingering effect.
  void fullscreenRevealAnimatesSmoothly() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    auto maxBarHeight = [&win] {
      int m = 0;
      for (QToolBar* b : win.findChildren<QToolBar*>())
        if (b->isVisible()) m = std::max(m, b->height());
      return m;
    };
    auto anyBarVisible = [&win] {
      for (QToolBar* b : win.findChildren<QToolBar*>()) if (b->isVisible()) return true;
      return false;
    };
    // Sample a size getter every ~16ms across the ~200ms animation; return the series.
    auto sample = [](auto getter) {
      QList<int> s;
      for (int i = 0; i < 20; ++i) { s.append(getter()); QTest::qWait(16); }
      return s;
    };
    auto hasIntermediate = [](const QList<int>& s, int full) {   // some value strictly inside (0, full)
      for (int v : s) if (v > 2 && v < full - 2) return true;
      return false;
    };
    auto nonDecreasing = [](const QList<int>& s) {
      for (int i = 1; i < s.size(); ++i) if (s[i] < s[i - 1] - 1) return false;   // 1px slack
      return true;
    };
    auto nonIncreasing = [](const QList<int>& s) {
      for (int i = 1; i < s.size(); ++i) if (s[i] > s[i - 1] + 1) return false;
      return true;
    };

    QAction* fs = actionByText(&win, "Enter Fullscreen");
    QVERIFY(fs);
    fs->trigger();                                   // ENTER fullscreen (bars + panel hidden)
    QTest::qWait(120);
    QVERIFY(!anyBarVisible());                        // nothing shown until the cursor hits an edge

    // --- Top toolbars: cursor to the top band → animated slide-in ---
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() / 2, 40)));
    const QList<int> up = sample(maxBarHeight);
    const int full = up.isEmpty() ? 0 : up.last();
    QVERIFY2(full > 10, "toolbars should have revealed to a real height");
    QVERIFY2(hasIntermediate(up, full), "toolbar reveal popped instantly (no intermediate heights)");
    QVERIFY2(nonDecreasing(up), "toolbar reveal height oscillated (flicker)");

    // Cursor well below the keep-zone → animated slide-out.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() / 2, win.height() - 40)));
    const QList<int> down = sample(maxBarHeight);
    QVERIFY2(nonIncreasing(down), "toolbar hide height oscillated (flicker)");
    QTest::qWait(120);

    // --- Right points panel: cursor to the right edge → animated slide-in reveal ---
    QWidget* panel = nullptr;
    for (QWidget* dw : win.findChildren<QWidget*>())
      if (QString(dw->metaObject()->className()).contains("SelectionPanel")) { panel = dw; break; }
    if (panel) {
      QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 2, win.height() / 2)));
      auto panelW = [panel] { return panel->isVisible() ? panel->width() : 0; };
      const QList<int> pin = sample(panelW);
      const int pfull = pin.isEmpty() ? 0 : pin.last();
      if (pfull > 10) {   // reveal fired (setPos is a soft no-op on some offscreen builds)
        QVERIFY2(hasIntermediate(pin, pfull), "panel reveal popped instantly (no intermediate widths)");
        QVERIFY2(nonDecreasing(pin), "panel reveal width oscillated (flicker)");
      } else {
        qWarning("panel reveal did not fire (cursor setPos likely a no-op offscreen)");
      }
    }

    // --- Exit: everything restored, no lingering graphics effect ---
    fs->trigger();
    QTest::qWait(250);
    QVERIFY(win.isVisible());
    QVERIFY(anyBarVisible());
    for (QToolBar* b : win.findChildren<QToolBar*>()) QVERIFY(b->graphicsEffect() == nullptr);
  }

  // Entering/leaving fullscreen plays the canvas STRETCHING out of (and minimising
  // back into) its old viewport box — MainWindow::beginFullscreenZoom, the desktop
  // twin of the FLIP in browser/js/ui/motion.js. The contract that must hold whatever
  // the window manager does with the geometry is that the ramp only ever ENDS on the
  // zoom the user picked: the motion is decoration, never a zoom change. Offscreen the
  // resize may not land at all, in which case it correctly plays nothing.
  void fullscreenStretchPreservesZoom() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    win.setZoom(0.5);
    const double chosen = canvas->scale();
    QCOMPARE(chosen, 0.5);

    QAction* fs = actionByText(&win, "Enter Fullscreen");
    QVERIFY(fs);
    fs->trigger();
    // Outlast the ramp itself plus the bounded wait for the window manager's resize.
    QTRY_VERIFY_WITH_TIMEOUT(!win.fsZoomAnim_, 3000);
    QCOMPARE(canvas->scale(), chosen);  // entering never changed the user's zoom

    fs->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!win.fsZoomAnim_, 3000);
    QCOMPARE(canvas->scale(), chosen);  // …and neither did leaving
  }

  // A palette change gets the flood-from-the-centre wipe (support/themeSwapOverlay.hpp,
  // the desktop twin of themeSwap in browser/js/ui/motion.js). The contract worth pinning
  // is WHEN it plays: on a real theme/accent change, never on the boot pass or on the many
  // re-applies that resolve to the same palette — and it must always clean itself up.
  void themeSwapWipesOnlyOnRealChanges() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 640);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // The overlay is deliberately MOC-free, so it is found by object name, not by type.
    auto overlays = [&win] {
      return win.findChildren<QWidget*>(QString::fromLatin1(ThemeSwapOverlay::kObjectName),
                                        Qt::FindDirectChildrenOnly).size();
    };
    // Boot already ran applyTheme(); nothing should be mid-wipe.
    QTRY_COMPARE(overlays(), 0);

    // Re-applying the SAME palette is a no-op, however many times it is asked for.
    win.applyTheme();
    win.applyTheme();
    QCOMPARE(overlays(), 0);

    // A real flip does wipe…
    Settings flipped = win.settings_;
    flipped.themeMode = win.paintedDark_ ? "light" : "dark";
    win.applySettings(flipped, /*persist=*/false);
    QCOMPARE(overlays(), 1);
    // …and reaps itself when the animation lands, leaving no lingering child.
    QTRY_VERIFY_WITH_TIMEOUT(overlays() == 0, 3000);

    // An accent change is a palette change too.
    Settings accented = win.settings_;
    accented.accentColor = win.settings_.accentColor == "grass" ? "violet" : "grass";
    win.applySettings(accented, /*persist=*/false);
    QCOMPARE(overlays(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(overlays() == 0, 3000);
    // The wipe is decoration: it must never swallow input from the live window under it.
    QCOMPARE(win.settings_.accentColor, accented.accentColor);
  }

  // …but ONE flip at a time. A second press while the wipe plays would restyle the window
  // under an overlay still holding the PREVIOUS snapshot, and the two palettes tear across
  // each other — hammering the toolbar button was visibly breaking the window. The browser
  // gets this free (a new view transition supersedes the one in flight); here the press is
  // dropped until the wipe has finished.
  void themeToggleIsIgnoredMidWipe() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 640);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto overlays = [&win] {
      return win.findChildren<QWidget*>(QString::fromLatin1(ThemeSwapOverlay::kObjectName),
                                        Qt::FindDirectChildrenOnly).size();
    };
    const QString original = win.settings_.themeMode;
    QTRY_COMPARE(overlays(), 0);

    win.toggleTheme();
    const QString mid = win.settings_.themeMode;
    QCOMPARE(overlays(), 1);
    // Dropped, not queued: two presses mid-wipe leave the palette exactly where it was.
    win.toggleTheme();
    win.toggleTheme();
    QCOMPARE(win.settings_.themeMode, mid);
    QCOMPARE(overlays(), 1);
    // …and the toggle is live again the moment the wipe has reaped itself.
    QTRY_VERIFY_WITH_TIMEOUT(overlays() == 0, 3000);
    win.toggleTheme();
    QVERIFY2(win.settings_.themeMode != mid, "the toggle stayed blocked after the wipe ended");

    // Leave the persisted theme as we found it — the settings are shared across tests.
    QTRY_VERIFY_WITH_TIMEOUT(overlays() == 0, 3000);
    auto restore = win.settings_;
    restore.themeMode = original;
    win.applySettings(restore, true);
  }

  // Nothing the restyle touched may show its new colours before the circle gets there.
  // The colour chips (updateColorSwatch) carry a palette-coloured frame, and applySettings
  // used to re-issue them BEFORE applyTheme() grabbed its snapshot — so the pickers were
  // baked into the snapshot already light while the window around them was still dark, and
  // stayed that way until the wipe finally reached them (user report).
  void themeSwapSnapshotStillWearsTheOldPalette() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1100, 720);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto overlays = [&win] {
      return win.findChildren<QWidget*>(QString::fromLatin1(ThemeSwapOverlay::kObjectName),
                                        Qt::FindDirectChildrenOnly).size();
    };
    QTRY_COMPARE(overlays(), 0);
    QVERIFY(win.lineColorBtn_ && win.lineColorBtn_->isVisible());

    const QImage before = win.grab().toImage();
    Settings flipped = win.settings_;
    flipped.themeMode = win.paintedDark_ ? "light" : "dark";
    win.applySettings(flipped, /*persist=*/false);
    QCOMPARE(overlays(), 1);
    // The wipe has not ticked yet, so the whole window is still the snapshot.
    const QImage during = win.grab().toImage();

    const qreal dpr = before.devicePixelRatio();
    auto deviceRect = [dpr](QWidget* w, QWidget* top) {
      const QRect r(w->mapTo(top, QPoint(0, 0)), w->size());
      return QRect(qRound(r.x() * dpr), qRound(r.y() * dpr),
                   qRound(r.width() * dpr), qRound(r.height() * dpr));
    };
    for (QToolButton* chip : {win.lineColorBtn_, win.pointColorBtn_}) {
      const QRect r = deviceRect(chip, &win);
      QVERIFY2(before.rect().contains(r), "the chip is off-window; nothing was compared");
      QCOMPARE(during.copy(r), before.copy(r));
    }
    QTRY_VERIFY_WITH_TIMEOUT(overlays() == 0, 3000);
  }

  // Clearing the image must leave the idle affordance REACHABLE. The scroll area is not
  // widgetResizable, so relaxing the canvas's size constraints does not shrink a widget
  // already sized to a big image — the "Open an image / create a blank one" hint then
  // paints centred in a huge off-screen rect and the editor looks like a dead scrolling
  // void with no way to start again.
  // The incognito frame DRAWS ON clockwise from the top-left rather than blinking into
  // place, and retracts the same way — the desktop half of the browser's four staggered
  // .ig-edge elements. framePath is pure, so the order is checkable without a display.
  void incognitoFrameDrawsClockwiseFromTheTopLeft() {
    using stencil::gui::IncognitoOverlay;
    const QRectF box(0, 0, 200, 100);

    QVERIFY2(IncognitoOverlay::framePath(box, 0.0).isEmpty(), "nothing is drawn at rest");

    // An eighth in: half the TOP edge, and nothing else has started.
    const QPainterPath eighth = IncognitoOverlay::framePath(box, 0.125);
    QCOMPARE(eighth.boundingRect().width(), 100.0);
    QCOMPARE(eighth.boundingRect().height(), 0.0);
    QCOMPARE(eighth.boundingRect().top(), 0.0);

    // Past the first quarter the right edge is running, still along the top-right.
    const QPainterPath half = IncognitoOverlay::framePath(box, 0.5);
    QCOMPARE(half.boundingRect().width(), 200.0);
    QCOMPARE(half.boundingRect().height(), 100.0);   // right edge fully down
    QVERIFY2(half.boundingRect().left() == 0.0, "the bottom edge has not started");

    // Closed: the full perimeter, and it only closes at the very end.
    const QPainterPath done = IncognitoOverlay::framePath(box, 1.0);
    QCOMPARE(done.boundingRect(), box);
    // The last edge CLIMBS from the bottom-left, so the loop closes at the top-left it
    // started from. currentPosition is where that edge has reached: 60% up at t=0.9.
    QCOMPARE(IncognitoOverlay::framePath(box, 0.9).currentPosition(), QPointF(0.0, 40.0));
    QCOMPARE(done.currentPosition(), QPointF(0.0, 0.0));

    // Out-of-range input is clamped, never extrapolated.
    QCOMPARE(IncognitoOverlay::framePath(box, 2.0).boundingRect(), box);
    QVERIFY(IncognitoOverlay::framePath(box, -1.0).isEmpty());
    QVERIFY2(IncognitoOverlay::framePath(QRectF(), 1.0).isEmpty(), "an empty viewport draws nothing");
  }

  // The "?" beside the project name is the ONLY readout left once the tool rows
  // are collapsed, so it must survive that collapse — and its bubble carries the
  // A window shortcut pressed while that window is up CLOSES it (it used to re-open the same
  // dialog, so nothing appeared to happen), and another window's shortcut SWAPS to it rather
  // than stacking a second window on top. The dialog carries its own copies of those chords
  // because a modal event loop never lets the main window's actions fire.
  void windowShortcutsToggleAndSwap() {
    MainWindow win(nullptr, false);
    openLoaded(win);
    QVERIFY2(!win.actInfo_->shortcut().isEmpty(), "help carries a shortcut");
    QVERIFY2(!win.actShortcuts_->shortcut().isEmpty(),
             "the shortcuts window has one of its own now");
    QVERIFY2(!win.actSettings_->shortcut().isEmpty(), "…and so does Settings");
    QVERIFY(win.hotkeyActions_.contains(QStringLiteral("openHotkeys")));
    QVERIFY(win.hotkeyActions_.contains(QStringLiteral("openVisuals")));
    QVERIFY2(!win.actInfo_->shortcut().toString().contains(QStringLiteral("F1")),
             "help left the lone F1 for the Alt+letter family");

    int settingsAsked = 0;
    connect(win.actSettings_, &QAction::triggered, &win, [&] { settingsAsked++; });

    bool sawOwn = false, sawOther = false, parked = false;
    QTimer::singleShot(0, &win, [&] {
      QDialog* shown = nullptr;
      for (QDialog* d : win.findChildren<QDialog*>())
        if (d->isVisible()) shown = d;
      QVERIFY(shown);
      // While it is up, the main window's own copies of these chords are parked, so the two
      // cannot fire ambiguously at each other.
      parked = win.actInfo_->shortcutContext() == Qt::WidgetShortcut;
      // Its own chord…
      for (QShortcut* sc : shown->findChildren<QShortcut*>()) {
        if (sc->key() == win.actInfo_->shortcut()) { sawOwn = true; emit sc->activated(); }
      }
      QVERIFY2(!shown->isVisible(), "its own shortcut closed the window");
      // …and another window's chord is wired too, queued to open after this one unwinds.
      for (QShortcut* sc : shown->findChildren<QShortcut*>())
        if (sc->key() == win.actSettings_->shortcut()) sawOther = true;
    });
    win.openInfo();

    QVERIFY2(sawOwn, "the dialog carried its own opener's chord");
    QVERIFY2(sawOther, "…and the other windows' chords, for swapping");
    QVERIFY2(parked, "the main window's duplicate was parked while the dialog owned it");
    QVERIFY2(win.actInfo_->shortcutContext() != Qt::WidgetShortcut,
             "…and handed back when the dialog closed");
    QCOMPARE(settingsAsked, 0);   // nothing was swapped to in this pass
    beat();
  }

  // Opening the assistant puts the caret in its box — otherwise the first thing you type
  // goes to the canvas shortcuts instead of the prompt you meant to write.
  void openingTheChatFocusesItsInput() {
    MainWindow win(nullptr, false);
    openLoaded(win);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(win.chatDock_->input_->hasFocus(), 3000);
    beat();
  }

  // ⌥⌫ in the chat box deleted the selected LINE (the canvas's deleteLine shortcut claimed the
  // chord app-wide) instead of the word behind the cursor. A focused text box owns the standard
  // editing chords; the action keeps working everywhere else.
  void wordDeleteInTheChatBoxDeletesAWordNotALine() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    stencil::core::Lines seeded;                 // something for deleteLine to bite on
    seeded.push_back(stencil::core::Line{{{10, 10}, {40, 40}}});
    canvas->setLines(seeded);
    canvas->selectLineByIndex(0);
    const int lines = static_cast<int>(canvas->lines().size());

    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    QPlainTextEdit* input = win.chatDock_->input_;
    input->setFocus();
    QTRY_VERIFY(input->hasFocus());
    input->setPlainText(QStringLiteral("crop the portrait"));
    input->moveCursor(QTextCursor::End);

    QTest::keyClick(input, Qt::Key_Backspace, Qt::AltModifier);

    QCOMPARE(input->toPlainText(), QStringLiteral("crop the "));
    QCOMPARE(static_cast<int>(canvas->lines().size()), lines);   // the drawing is untouched
    beat();
  }

  // An incognito session could be published to a SERVER but never kept locally, so the
  // assistant's `save` — and "make this a normal project" — dead-ended in "incognito mode —
  // saving is disabled", stranding the picture in a session that could not be saved at all.
  // Incognito stops the app writing BY ITSELF; an explicit save is the user, not the app.
  void incognitoPromotesToALocalProjectInsteadOfRefusing() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    const int before = static_cast<int>(win.projectList_.size());
    win.actIncognito_->setChecked(true);
    QTRY_VERIFY(win.incognito_);

    // The assistant's pathless save: it promotes rather than failing.
    QString err;
    QVERIFY2(win.chatSaveProject(QString(), QString(), &err),
             qPrintable(QStringLiteral("save refused while incognito: %1").arg(err)));
    QVERIFY2(!win.incognito_, "the session left incognito with the save");
    QCOMPARE(static_cast<int>(win.projectList_.size()), before + 1);
    QVERIFY2(canvas->hasImage(), "the picture survived the promotion");
    beat();
  }

  // Writing a FILE the user named is the toolbar's Save Image…, not project promotion — so it
  // works while incognito, and the picture stays incognito afterwards.
  void incognitoStillWritesAFileTheUserNamed() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    win.actIncognito_->setChecked(true);
    QTRY_VERIFY(win.incognito_);
    const int before = static_cast<int>(win.projectList_.size());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString out = dir.path() + QStringLiteral("/from-incognito.png");
    QString err;
    QVERIFY2(win.chatSaveProject(QStringLiteral("shot"), out, &err),
             qPrintable(QStringLiteral("file save refused: %1").arg(err)));
    QVERIFY2(QFileInfo::exists(out), "the file the user asked for is on disk");
    QVERIFY2(win.incognito_, "an export is not a promotion — the session stays incognito");
    QCOMPARE(static_cast<int>(win.projectList_.size()), before);
    beat();
  }

  // A folder destination gets the file the model names inside it (the empty-Downloads bug:
  // the echo guard demanded the whole path verbatim, so the destination was silently dropped).
  void aNamedFolderTakesTheFileTheAssistantNames() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString err;
    QVERIFY2(win.chatSaveProject(QStringLiteral("portrait-bw"), dir.path(), &err),
             qPrintable(QStringLiteral("folder save refused: %1").arg(err)));
    const QStringList written = QDir(dir.path()).entryList(QDir::Files);
    QCOMPARE(written.size(), 1);
    QVERIFY2(written.first().endsWith(QStringLiteral(".png")),
             "a folder destination writes the picture, named after the save");
    beat();
  }

  // image size plus, while incognito, the "not saved" line. Those two facts and
  // nothing else (the canvas pill that used to say it is gone).
  void statusHintCarriesSizeAndIncognitoOnly() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QLabel* hint = win.statusHint_;
    QVERIFY(hint);
    QCOMPARE(hint->text(), QStringLiteral("?"));
    // It is the COLLAPSED state's readout: with the tool rows up, the size line under them
    // already says this, so the "?" would only repeat it.
    QVERIFY2(!hint->isVisible(), "the hint stays out of the way while the tool rows are up");
    const QString size =
        QString("%1 × %2 px").arg(canvas->imageWidth()).arg(canvas->imageHeight());
    QVERIFY2(hint->toolTip().contains(size), "the bubble names the image size");
    QCOMPARE(hint->toolTip().split('\n').size(), 1);   // size only, nothing else

    // Collapsed tool rows: the header row stays, and NOW the hint appears — with the size
    // line hidden alongside the rows, its bubble is the only place these facts are left.
    win.actToolbars_->setChecked(false);
    QTRY_VERIFY(!win.actToolbars_->isChecked());
    QTest::qWait(400);   // the rows slide out
    QVERIFY2(hint->isVisible(), "the hint appears with the Controls collapsed");
    QVERIFY2(!win.imageSizeInfo_->isVisible(), "…and the size line goes with the rows");
    QVERIFY2(hint->toolTip().contains(size), "…still carrying the size");

    // Incognito adds its line — and only its line.
    win.actIncognito_->setChecked(true);
    QTRY_VERIFY(win.incognito_);
    const QStringList lines = hint->toolTip().split('\n');
    QCOMPARE(lines.size(), 2);
    QVERIFY2(lines[0].contains(size), "the size stays first");
    QCOMPARE(lines[1], QStringLiteral("Incognito — not saved"));

    // …and it leaves again when incognito does.
    win.actIncognito_->setChecked(false);
    QTRY_VERIFY(!win.incognito_);
    QVERIFY2(!hint->toolTip().contains("Incognito"),
             "the incognito line goes with the mode");
    QCOMPARE(hint->toolTip().split('\n').size(), 1);
    win.actToolbars_->setChecked(true);
    beat();
  }

  void clearingImageBringsBackTheIdleAffordance() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1200, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    CanvasWidget* canvas = win.canvas_;
    QVERIFY(canvas);
    const QSize idle = canvas->size();

    QImage big(1600, 2200, QImage::Format_ARGB32);
    big.fill(Qt::darkCyan);
    canvas->loadFromImage(big);
    QTRY_VERIFY(canvas->hasImage());
    QVERIFY2(canvas->height() > win.scroll_->viewport()->height(),
             "the fixture must be taller than the viewport, or this proves nothing");

    canvas->clearImage();
    QTRY_VERIFY(!canvas->hasImage());
    QCOMPARE(canvas->size(), idle);
    QVERIFY2(canvas->width() <= win.scroll_->viewport()->width()
                 && canvas->height() <= win.scroll_->viewport()->height(),
             "the cleared canvas must fit its viewport, or the idle hint is scrolled away");
  }

  void loadsImageAndEnablesActions() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);  // load settles on the event loop
    QVERIFY(canvas->imageWidth() > 0);
    QVERIFY(canvas->imageHeight() > 0);

    QAction* rotate = actionByText(&win, "Rotate Right");
    QVERIFY(rotate);
    QVERIFY(rotate->isEnabled());            // an image makes the transform actions live
    QVERIFY(!win.windowTitle().isEmpty());
  }

  void rotateActionsRoundTrip() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    const int w0 = canvas->imageWidth(), h0 = canvas->imageHeight();
    const int r0 = canvas->rotationQuarters();
    // The page crop of our landscape test image is non-square, so a quarter turn
    // produces an observable W↔H swap below (guards the swap assertion's premise).
    QVERIFY2(w0 != h0, "the page crop should be non-square so the rotation swap is observable");

    QAction* right = actionByText(&win, "Rotate Right");
    QAction* left = actionByText(&win, "Rotate Left");
    QVERIFY(right && left);

    beat();
    right->trigger();
    QCOMPARE(canvas->rotationQuarters(), (r0 + 1) % 4);
    // A quarter turn swaps the visible (cropped) dimensions — proof the rotation
    // actually transformed the image, not merely bumped the quarter-turn counter.
    QCOMPARE(canvas->imageWidth(), h0);
    QCOMPARE(canvas->imageHeight(), w0);
    beat();

    left->trigger();                                       // undo the quarter turn
    QCOMPARE(canvas->rotationQuarters(), r0);
    beat();
    QCOMPARE(canvas->imageWidth(), w0);                    // exact state restored
    QCOMPARE(canvas->imageHeight(), h0);
  }

  void drawWithMouseThenUndo() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(canvas->width() > 0 && canvas->height() > 0);

    // Enter drawing mode via the real "Start Drawing" action, then click three
    // well-separated points on the canvas — the same left-click path the app uses.
    QAction* start = actionByText(&win, "Start Drawing");
    QVERIFY(start && start->isEnabled());
    start->trigger();

    const int W = canvas->width(), H = canvas->height();
    for (const QPoint& p : { QPoint(W * 0.35, H * 0.35), QPoint(W * 0.6, H * 0.45), QPoint(W * 0.45, H * 0.65) }) {
      QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, p);
      beat();
    }
    // Each left-click press adds exactly one point: the canvas widget is fixed to
    // the scaled-image size with a zero-offset widget→image mapping, so all three
    // clicks land inside the image (no letterboxing to miss) and none are deduped.
    QCOMPARE(totalPoints(canvas), 3);

    // Commit the line via the "New Line" action — this is what pushes an undo snapshot.
    QAction* newLine = actionByText(&win, "New Line");
    QVERIFY(newLine);
    newLine->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 1);   // committed line landed
    QVERIFY(canvas->canUndo());
    QVERIFY(!canvas->canRedo());
    beat();

    // The Undo action steps back the history stack, dropping the committed line.
    QAction* undo = actionByText(&win, "Undo");
    QVERIFY(undo);
    undo->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 0);
    QVERIFY(canvas->canRedo());          // undo made a redo available
    beat();

    // Redo re-applies it via the real action: the committed line comes back,
    // exactly as the toolbar / Ctrl+Shift+Z would restore it.
    QAction* redo = actionByText(&win, "Redo");
    QVERIFY(redo && redo->isEnabled());
    redo->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 1);
    QVERIFY(!canvas->canRedo());
    beat();
  }

  // A bare Delete/Backspace inside the selection panel's lists removes the current row —
  // the points table already did this; the Lines list is the parity addition (the browser's
  // focused .lines-row / coordinates row take the same key). Scoped by widget focus, so the
  // global Alt+Delete on the canvas selection is untouched.
  void deleteKeyRemovesRowInSelectionLists() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(canvas->width() > 0 && canvas->height() > 0);

    // Commit two lines through the real drawing path.
    QAction* start = actionByText(&win, "Start Drawing");
    QAction* newLine = actionByText(&win, "New Line");
    QVERIFY(start && newLine);
    const int W = canvas->width(), H = canvas->height();
    const QList<QList<QPoint>> shapes{
        {QPoint(W * 0.25, H * 0.25), QPoint(W * 0.45, H * 0.35)},
        {QPoint(W * 0.55, H * 0.55), QPoint(W * 0.75, H * 0.65), QPoint(W * 0.65, H * 0.8)},
    };
    for (const QList<QPoint>& shape : shapes) {
      start->trigger();
      for (const QPoint& p : shape) { QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, p); beat(); }
      newLine->trigger();
    }
    QCOMPARE(static_cast<int>(canvas->lines().size()), 2);

    // --- Lines tab: Delete on the current row removes that line ---
    auto* linesList = win.findChild<QListWidget*>("linesList");
    QVERIFY(linesList);
    QTRY_COMPARE(linesList->count(), 2);
    QVERIFY2(linesList->focusPolicy() != Qt::NoFocus, "the list must accept focus for a scoped Delete");
    linesList->setCurrentRow(0);
    QTest::keyClick(linesList, Qt::Key_Delete);
    QTRY_COMPARE(static_cast<int>(canvas->lines().size()), 1);
    // The current row survives the repopulate, so a second press deletes again.
    QCOMPARE(linesList->currentRow(), 0);
    QTest::keyClick(linesList, Qt::Key_Backspace);
    QTRY_COMPARE(static_cast<int>(canvas->lines().size()), 0);

    // --- Points table: same key, unchanged behaviour ---
    start->trigger();
    for (const QPoint& p : { QPoint(W * 0.3, H * 0.3), QPoint(W * 0.5, H * 0.4), QPoint(W * 0.4, H * 0.6) }) {
      QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, p);
      beat();
    }
    QCOMPARE(totalPoints(canvas), 3);
    auto* points = win.findChild<QTableWidget*>();
    QVERIFY(points);
    QTRY_COMPARE(points->rowCount(), 3);
    points->setCurrentCell(1, 0);
    QTest::keyClick(points, Qt::Key_Delete);
    QTRY_COMPARE(totalPoints(canvas), 2);
    beat();
  }

  // Alt+Shift dragging a line must move EVERY point — including when Shift lifts a beat
  // before the mouse button, which is how the gesture naturally ends. The regression left
  // all but the grabbed segment's endpoints snapped back to their pre-drag spots.
  void wholeLineDragMovesEveryPoint() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(canvas->width() > 0 && canvas->height() > 0);

    const double s = canvas->scale();
    stencil::core::Line line;
    line.points = {{20, 20}, {60, 20}, {100, 40}, {140, 60}};
    canvas->setLines({line});
    const auto orig = canvas->lines()[0].points;

    auto sendMouse = [&](QEvent::Type t, const QPointF& pos, Qt::MouseButton btn,
                         Qt::MouseButtons btns, Qt::KeyboardModifiers mods) {
      QMouseEvent ev(t, pos, canvas->mapToGlobal(pos.toPoint()), btn, btns, mods);
      QCoreApplication::sendEvent(canvas, &ev);
    };

    // Grab the FIRST segment's midpoint (widget space = image * scale), drag by a known
    // delta with Shift ALREADY RELEASED on the move and the release, then let go.
    const QPointF grab(40 * s, 20 * s);
    const QPointF drop((40 + 30) * s, (20 + 25) * s);
    sendMouse(QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton,
              Qt::AltModifier | Qt::ShiftModifier);
    sendMouse(QEvent::MouseMove, drop, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, drop, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    beat();

    const auto& moved = canvas->lines()[0].points;
    QCOMPARE(static_cast<int>(moved.size()), 4);
    for (std::size_t i = 0; i < moved.size(); ++i) {
      QVERIFY2(std::abs(moved[i].x - (orig[i].x + 30)) < 0.5 &&
                   std::abs(moved[i].y - (orig[i].y + 25)) < 0.5,
               qPrintable(QString("point %1 carries the full drag delta").arg(i)));
    }
  }

  // Alt+Delete routes by selection: a focused POINT narrows it to that point (the line
  // survives); with no focused point it deletes the selected line — and the shared
  // hotkeysConfig label says so.
  void altDeleteRoutesBySelection() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    stencil::core::Line line;
    line.points = {{30, 30}, {80, 30}, {80, 80}};
    canvas->setLines({line});

    QAction* del = actionByText(&win, "Delete Selected Line (Point if focused)");
    QVERIFY2(del, "the routed delete action is discoverable under its new label");

    // Click-select a POINT → the chord deletes just that point.
    canvas->selectLineAt(30, 30);
    QCOMPARE(canvas->selectedPoint(), 0);
    del->trigger();
    QTRY_COMPARE(static_cast<int>(canvas->lines().size()), 1);
    QCOMPARE(static_cast<int>(canvas->lines()[0].points.size()), 2);

    // Select the LINE via a segment (no focused point) → the chord deletes the line.
    canvas->selectLineAt(80, 55);
    QVERIFY(canvas->selectedLineIdx() == 0 && canvas->selectedPoint() == -1);
    del->trigger();
    QTRY_COMPARE(static_cast<int>(canvas->lines().size()), 0);
  }

  // Hover cross-highlight plumbing: moving the mouse over a point emits
  // canvasHoverChanged (panel row tints), and the panel-driven setListHover* calls are
  // safe to drive directly (canvas ring/glow — the reverse direction).
  void hoverCrossHighlightSignals() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    const double s = canvas->scale();
    stencil::core::Line line;
    line.points = {{40, 40}, {90, 40}};
    canvas->setLines({line});

    QSignalSpy spy(canvas, &CanvasWidget::canvasHoverChanged);
    QMouseEvent over(QEvent::MouseMove, QPointF(40 * s, 40 * s),
                     canvas->mapToGlobal(QPoint(40 * s, 40 * s)), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas, &over);
    QTRY_VERIFY(spy.count() >= 1);
    const QList<QVariant> args = spy.last();
    QCOMPARE(args.at(0).toInt(), 0);   // line 0
    QCOMPARE(args.at(1).toInt(), 0);   // point 0
    QCOMPARE(args.at(2).toInt(), 0);   // over line 0 (tints the Lines-list row)

    // Leaving the canvas clears the hover (all -1) so panel tints drop too.
    spy.clear();
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(canvas, &leave);
    QTRY_VERIFY(spy.count() >= 1);
    QCOMPARE(spy.last().at(0).toInt(), -1);

    // Reverse direction: the panel rows drive the canvas ring/glow without incident.
    canvas->setListHoverLine(0);
    canvas->setListHoverPoint(1);
    canvas->setListHoverLine(-1);
    canvas->setListHoverPoint(-1);
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
    QCOMPARE(draw->text(), QString("Stop"));
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

  void filterActionAppliesToCanvas() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    // The context-menu filter options are hosted QRadioButtons (an exclusive QButtonGroup) so
    // picking one keeps the menu open; find them by their "filterValue" property and drive them.
    // The radios live inside QWidgetActions' default widgets (setDefaultWidget reparents them out
    // of the window until a menu shows them), so reach them via the actions, not win's children.
    auto filterRadio = [&](const QString& value) -> QRadioButton* {
      for (QWidgetAction* a : win.findChildren<QWidgetAction*>())
        if (QWidget* dw = a->defaultWidget())
          for (QRadioButton* r : dw->findChildren<QRadioButton*>())
            if (r->property("filterValue").toString() == value) return r;
      return nullptr;
    };

    // Normalize to a known baseline via the real "None" radio: applyImageFilter PERSISTS the
    // chosen mode to settings, so a prior run/test can start this canvas non-"none". Drive it
    // rather than assuming the default (order-safe).
    QRadioButton* none = filterRadio("none");
    QVERIFY(none);
    none->setChecked(true);
    QCOMPARE(canvas->imageFilter(), QString("none"));

    // Check the SHARED filter path (toggling the radio runs the real applyImageFilter, which also
    // syncs the toolbar combo) and lands the mode on the live canvas.
    QRadioButton* bw = filterRadio("bw");
    QVERIFY(bw && bw->isEnabled());
    bw->setChecked(true);
    QCOMPARE(canvas->imageFilter(), QString("bw"));      // menu/toolbar wiring reached the canvas
    beat();

    // Switching filters is live and mutually exclusive (one button group).
    QRadioButton* sepia = filterRadio("sepia");
    QVERIFY(sepia);
    sepia->setChecked(true);
    QCOMPARE(canvas->imageFilter(), QString("sepia"));
    QVERIFY(!bw->isChecked());                            // exclusive group cleared the old mode
    beat();

    none->setChecked(true);   // leave the persisted filter clean for other tests/runs
  }

  void clearAllActionEmptiesCanvas() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(canvas->width() > 0 && canvas->height() > 0);

    // Draw and commit a line so there is something to clear.
    QAction* start = actionByText(&win, "Start Drawing");
    QVERIFY(start && start->isEnabled());
    start->trigger();
    const int W = canvas->width(), H = canvas->height();
    for (const QPoint& p : { QPoint(W * 0.4, H * 0.4), QPoint(W * 0.6, H * 0.55) }) {
      QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, p);
      beat();
    }
    QAction* newLine = actionByText(&win, "New Line");
    QVERIFY(newLine);
    newLine->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 1);

    // The destructive "Clear All Lines" action (canvas context menu + Edit menu
    // reuse it) wipes every committed and in-progress point — no confirm on the
    // lines action (unlike Projects ▸ Clear All), so it runs straight through.
    QAction* clear = actionByText(&win, "Clear All Lines");
    QVERIFY(clear && clear->isEnabled());
    clear->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 0);
    QCOMPARE(totalPoints(canvas), 0);   // nothing committed or in-progress remains
    beat();
  }

  // The trash "Clear Project" action (mirrors the browser #clear-storage button) is
  // visible for a local editor, confirms, and — on Yes — resets to the empty
  // "Open an image" canvas. The confirm reuses the modal-dismiss helper.
  void clearProjectResetsToBlankEditor() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    QVERIFY(clear->isVisible());   // shown for a local/temporary editor (hidden only for server projects)

    dismissModal("Yes");      // blocks on the confirm until the timer clicks Yes
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);   // reset to a blank editor
    QCOMPARE(static_cast<int>(canvas->lines().size()), 0);
    beat();
  }

  // Opening a .stencil project file (the real OS-open / drag / file-arg path) decodes its
  // embedded image and adopts its layout — image + lines + rotation — into the live canvas.
  void opensStencilProjectFile() {
    // Author a .stencil bundling the test PNG's bytes + a one-line, quarter-rotated layout.
    QByteArray png;
    {
      QFile f(png_);
      QVERIFY(f.open(QIODevice::ReadOnly));
      png = f.readAll();
    }
    stencil::core::Lines lines;
    stencil::core::Line l;
    l.points = {{10, 10}, {40, 40}};
    l.color = "#ff0000";
    lines.push_back(l);
    stencil::gui::fileStore::ProjectFileData pf;
    pf.name = "GUI Project";
    pf.imageExt = "png";
    pf.imageBytes = png;
    pf.imageWidth = 240;
    pf.imageHeight = 160;
    pf.layout = stencil::gui::fileStore::buildLayoutJson(240, 160, lines, "none", "#7c3aed", {}, 1, {});
    const QString path = QDir::temp().filePath("stencil_gui_e2e_project.stencil");
    {
      QFile wf(path);
      QVERIFY(wf.open(QIODevice::WriteOnly | QIODevice::Truncate));
      wf.write(stencil::gui::fileStore::buildProjectFile(pf));
    }

    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    win.openPathFromOS(path);   // routes *.stencil -> openProjectFile
    CanvasWidget* canvas = win.findChild<CanvasWidget*>();
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QCOMPARE(canvas->rotationQuarters(), 1);                 // layout rotation adopted
    QVERIFY(totalPoints(canvas) > 0);                        // the line was adopted
    beat();
  }

  // Live sync: a project linked to a .stencil with live-sync ON auto-saves edits back to the
  // file (debounced). Drives openProjectFile linking → the "Live Sync with File" toggle →
  // an edit → onCanvasChanged → scheduleStencilAutosave → flushStencilAutosave writing the file.
  void liveSyncAutosavesEditsToFile() {
    QByteArray png;
    { QFile f(png_); QVERIFY(f.open(QIODevice::ReadOnly)); png = f.readAll(); }
    stencil::gui::fileStore::ProjectFileData pf;
    pf.name = "Live";
    pf.imageExt = "png";
    pf.imageBytes = png;
    pf.imageWidth = 240;
    pf.imageHeight = 160;
    pf.layout = stencil::gui::fileStore::buildLayoutJson(240, 160, {}, "none", "#7c3aed", {}, 0, {});   // rotation 0
    const QString path = QDir::temp().filePath("stencil_gui_livesync.stencil");
    { QFile wf(path); QVERIFY(wf.open(QIODevice::WriteOnly | QIODevice::Truncate)); wf.write(stencil::gui::fileStore::buildProjectFile(pf)); }

    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    win.openPathFromOS(path);
    CanvasWidget* canvas = win.findChild<CanvasWidget*>();
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QCOMPARE(canvas->rotationQuarters(), 0);

    QAction* live = actionByText(&win, "Live Sync with File");
    QVERIFY(live);
    QVERIFY(live->isEnabled());     // enabled because the project is file-linked
    live->setChecked(true);         // toggled → toggleStencilLiveSync(true)

    QAction* rotate = actionByText(&win, "Rotate Right");
    QVERIFY(rotate);
    rotate->trigger();
    QCOMPARE(canvas->rotationQuarters(), 1);

    // Auto-save is debounced (~800ms) — wait for the linked file to reflect the rotation.
    auto fileRotation = [&]() -> int {
      QFile rf(path);
      if (!rf.open(QIODevice::ReadOnly)) return -1;
      stencil::gui::fileStore::ProjectFileData out;
      QString err;
      if (!stencil::gui::fileStore::parseProjectFile(rf.readAll(), out, &err)) return -1;
      int w = 0, h = 0;
      stencil::core::CropRect crop;
      int rot = 0;
      stencil::gui::fileStore::parseLayoutJson(out.layout, w, h, &crop, &rot);
      return rot;
    };
    QTRY_COMPARE_WITH_TIMEOUT(fileRotation(), 1, 4000);   // the edit auto-saved into the linked file
    beat();
  }

  // Deleting the linked .stencil file removes it from disk and unlinks the project (the live-sync
  // + delete actions disable), while the project stays open in the canvas. Drives openProjectFile
  // linking → the "Delete Project File (.stencil)" action → the confirm (auto-clicked "Delete").
  void deletesLinkedProjectFile() {
    QByteArray png;
    { QFile f(png_); QVERIFY(f.open(QIODevice::ReadOnly)); png = f.readAll(); }
    stencil::gui::fileStore::ProjectFileData pf;
    pf.name = "Doomed";
    pf.imageExt = "png";
    pf.imageBytes = png;
    pf.imageWidth = 240;
    pf.imageHeight = 160;
    pf.layout = stencil::gui::fileStore::buildLayoutJson(240, 160, {}, "none", "#7c3aed", {}, 0, {});
    const QString path = QDir::temp().filePath("stencil_gui_delete.stencil");
    { QFile wf(path); QVERIFY(wf.open(QIODevice::WriteOnly | QIODevice::Truncate)); wf.write(stencil::gui::fileStore::buildProjectFile(pf)); }

    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    win.openPathFromOS(path);
    CanvasWidget* canvas = win.findChild<CanvasWidget*>();
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    QAction* del = actionByText(&win, "Delete Project File (.stencil)");
    QVERIFY(del);
    QVERIFY(del->isEnabled());       // enabled because the project is file-linked
    QVERIFY(QFile::exists(path));

    dismissModal("Delete");          // auto-click "Delete" on the confirm modal
    del->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(path), 4000);   // the file was removed from disk
    QVERIFY(!del->isEnabled());      // unlinked → the delete action disables again
    QVERIFY(canvas->hasImage());     // the project itself stays open in the editor
    beat();
  }

  // The AI-Assistant chat dock: the checkable toolbar/View action opens and closes it;
  // unlike the deliberately pinned selection panel it is dockable on all four sides and
  // floatable/closable, defaults LEFT on a fresh run, hosts the transcript/input in a
  // user-resizable splitter, gates Send on input/busy state, and turns clipboard-pasted
  // images into attachments (plain text pastes normally).
  void chatDockToggles() {
    // A saved dock layout would restore whatever area the last run used; clear it so
    // this asserts the FIRST-RUN default (left, matching the browser).
    {
      stencil::gui::Settings s = stencil::gui::fileStore::loadSettings();
      s.windowState.clear();
      stencil::gui::fileStore::saveSettings(s);
    }
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(dock);
    QCOMPARE(dock->allowedAreas(), Qt::AllDockWidgetAreas);
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetMovable));
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetFloatable));
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetClosable));
    QCOMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);  // first-run default
    // The COMPOSER acts on a drop; the DOCK swallows the ones that miss it, so a
    // gesture aimed at the chat can never reach the window and offer to open the
    // image as a project (browser chatPanel.js parity). The composer's QPlainTextEdit
    // declines drops too, or it would swallow one and paste the path as text.
    QVERIFY(dock->acceptDrops());
    auto* inputArea = dock->findChild<QWidget*>("chatInputArea");
    QVERIFY(inputArea);
    QVERIFY(inputArea->acceptDrops());  // image/video drops become chat attachments
    auto* chatInput = dock->findChild<QPlainTextEdit*>("chatInput");
    QVERIFY(chatInput && !chatInput->acceptDrops() && !chatInput->viewport()->acceptDrops());
    // …cued by an animated icon over the composer, hidden until a drag arrives.
    auto* cue = dock->findChild<QWidget*>("chatDropCue");
    QVERIFY(cue && cue->isHidden());

    // The composer's resize grip is the SHARED pill (pillSplitter.hpp), not a
    // stylesheet handle: the stylesheet one could only be narrowed by a symmetric
    // margin, so it stretched with the panel into a fat accent band across the dock.
    auto* chatSplitter = dock->findChild<QSplitter*>("chatSplitter");
    QVERIFY(chatSplitter);
    QVERIFY2(dynamic_cast<stencil::gui::PillSplitterHandle*>(chatSplitter->handle(1)) != nullptr,
             "the dock's grip is not the shared pill handle");
    // A grab of the empty dock, for eyeballing the composer end-to-end.
    if (qEnvironmentVariableIsSet("STENCIL_GUI_SHOTS")) {
      dock->resize(360, 520);
      QTest::qWait(50);
      dock->grab().save(QString::fromLocal8Bit(qgetenv("STENCIL_GUI_SHOTS")) + "/dock-empty.png");
    }

    // Resizable input area: transcript over input in a vertical, non-collapsible splitter.
    auto* splitter = dock->findChild<QSplitter*>("chatSplitter");
    QVERIFY(splitter);
    QCOMPARE(splitter->orientation(), Qt::Vertical);
    QCOMPARE(splitter->count(), 2);
    QVERIFY(!splitter->childrenCollapsible());

    // By objectName, NOT by text: the dock's internal toggleViewAction shares
    // the "AI Assistant" label.
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(chat);
    QVERIFY(chat->isCheckable());
    chat->setChecked(false);         // normalize (a restored layout may have opened it)
    QTRY_VERIFY(!dock->isVisible());
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());

    // The sparkle toolbar button mirrors actChat (setDefaultAction): same
    // toggle, checked while the dock is open (browser sparkle-button parity).
    QToolButton* chatBtn = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->defaultAction() == chat) { chatBtn = b; break; }
    QVERIFY(chatBtn);
    QVERIFY(chatBtn->isChecked());
    chatBtn->click();
    QTRY_VERIFY(!dock->isVisible());
    chatBtn->click();
    QTRY_VERIFY(dock->isVisible());

    // Right-side docking must work even though the fixed-width selection panel owns
    // that area — nesting provides the drop slots (regression: the chat dock could
    // not be pinned to the right at all).
    QVERIFY(win.isDockNestingEnabled());
    auto* selPanel = win.findChild<QDockWidget*>("selectionPanelDock");
    QVERIFY(selPanel && win.dockWidgetArea(selPanel) == Qt::RightDockWidgetArea);
    win.addDockWidget(Qt::RightDockWidgetArea, dock);
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QTRY_VERIFY(dock->isVisible());
    win.addDockWidget(Qt::LeftDockWidgetArea, dock);  // restore for the rest of the slot
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);

    // First tear-off adopts the compact default size (not the docked span that
    // used to stretch the floating panel across the whole window).
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QTRY_COMPARE(dock->size(), QSize(385, 480));   // +5px of room for the row "…"
    dock->setFloating(false);
    QTRY_VERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);

    // Send gating: disabled while the input is empty, enabled once text lands.
    auto* input = dock->findChild<QPlainTextEdit*>("chatInput");
    auto* send = dock->findChild<QToolButton*>("chatSend");
    QVERIFY(input && send);
    auto* gearBtn = dock->findChild<QToolButton*>("chatGear");  // settings target
    auto* moreBtn = dock->findChild<QToolButton*>("chatMore");   // the … overflow
    QVERIFY(gearBtn && moreBtn);
    // The status dot is a BADGE on the … TRIGGER (browser .conn-status parity):
    // child of the button, no layout slot of its own. The gear itself now lives
    // in the menu, so it is hidden — the dot has to ride what stays visible.
    auto* dot = dock->findChild<QLabel*>("chatStatusDot");
    QVERIFY(dot && dot->parentWidget() == moreBtn);
    // Composer = send + …, in that order, both the filled-accent buttons.
    QVERIFY(send->x() < moreBtn->x());
    QVERIFY(send->property("chatAccent").toBool());
    QVERIFY(moreBtn->property("chatAccent").toBool());
    // Attach / clear / settings are reachable ONLY through the … menu now.
    auto* attachBtn = dock->findChild<QToolButton*>("chatAttach");
    auto* clearInRow = dock->findChild<QToolButton*>("chatClear");
    QVERIFY(attachBtn && clearInRow);
    QVERIFY(attachBtn->isHidden() && gearBtn->isHidden());
    QVERIFY(moreBtn->menu());
    QStringList items;
    for (QAction* a : moreBtn->menu()->actions())
      if (!a->isSeparator()) items << a->text();
    QCOMPARE(items, (QStringList{"Add image", "Clear history", "Settings"}));
    // The branded header IS the title bar (no double header).
    QVERIFY(dock->titleBarWidget());
    QVERIFY(dock->titleBarWidget()->findChild<QLabel*>("chatHeaderTitle"));

    // Card container: the dock content renders on the controls-panel colour —
    // DISTINCT from the canvas backdrop behind it — with the themed 1px
    // border styling applied to the whole card.
    {
      const QImage bodyImg = dock->widget()->grab().toImage();
      const QColor cardBg = bodyImg.pixelColor(bodyImg.width() / 2, 4);
      const QImage centralImg = win.centralWidget()->grab().toImage();
      const QColor canvasBg =
          centralImg.pixelColor(centralImg.width() / 2, centralImg.height() / 2);
      QVERIFY(cardBg != canvasBg);
      QVERIFY(dock->styleSheet().contains("border:1px solid"));
    }
    QVERIFY(!send->isEnabled());
    input->setPlainText("make it sepia");
    QVERIFY(send->isEnabled());
    input->clear();
    QVERIFY(!send->isEnabled());

    // Clipboard paste: an image on the clipboard becomes an attachment…
    auto* chatDock = qobject_cast<stencil::gui::ChatDock*>(dock);
    QVERIFY(chatDock);
    QCOMPARE(chatDock->attachedImages().size(), 0);
    QImage clip(20, 10, QImage::Format_RGB32);
    clip.fill(Qt::red);
    QGuiApplication::clipboard()->setImage(clip);
    input->setFocus();
    QTest::keySequence(input, QKeySequence::Paste);
    QCOMPARE(chatDock->attachedImages().size(), 1);
    QVERIFY(input->toPlainText().isEmpty());  // consumed as an attachment, not text
    // …while plain text still pastes normally (no extra attachment).
    QGuiApplication::clipboard()->setText("hello there");
    QTest::keySequence(input, QKeySequence::Paste);
    QCOMPARE(input->toPlainText(), QString("hello there"));
    QCOMPARE(chatDock->attachedImages().size(), 1);
    chatDock->clearAttachments();
    input->clear();

    // Cohesive-column chrome (browser parity): in-content header title and a
    // BORDERLESS transcript (browser .chat-transcript parity — the card look
    // comes from the dock's own panel background + border, not an inner frame).
    QVERIFY(dock->findChild<QLabel*>("chatHeaderTitle"));
    auto* scrollArea = dock->findChild<QScrollArea*>();
    QVERIFY(scrollArea && scrollArea->frameShape() == QFrame::NoFrame);

    // Empty-state suggestion chips: shown while the transcript is empty; a
    // click PREFILLS the composer (never sends); gone once the first card lands.
    auto* suggest = dock->findChild<QWidget*>("chatSuggest");
    QVERIFY(suggest && suggest->isVisible());
    // Chips lay out INLINE with wrapping (flow layout), not one per row.
    QVERIFY(suggest->layout());
    QVERIFY(!qobject_cast<QVBoxLayout*>(suggest->layout()));
    QVERIFY(suggest->layout()->hasHeightForWidth());
    const auto chips = dock->findChildren<QPushButton*>("chatSuggestChip");
    QCOMPARE(chips.size(), 4);
    chips.first()->click();
    QCOMPARE(input->toPlainText(), QString("Make it sepia"));
    QVERIFY(send->isEnabled());  // prefilled, not sent
    QVERIFY(suggest->isVisible());
    input->clear();
    chatDock->appendAssistant("done");  // first transcript card
    QVERIFY(!suggest->isVisible());

    // The attach-routing sniffers the paste/drop flows share (mediaLoader).
    QVERIFY(stencil::gui::isVideoFileName("clip.MP4"));
    QVERIFY(stencil::gui::isVideoFileName("/tmp/a.webm"));
    QVERIFY(!stencil::gui::isVideoFileName("photo.png"));
    QVERIFY(stencil::gui::isImageFileName("photo.JPEG"));
    QVERIFY(!stencil::gui::isImageFileName("clip.mp4"));

    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

  // The chat toolbar icon answers the same popover gestures as every dialog-opening
  // icon (browser chat-btn parity, chatPanel.js openCompact): a double-click opens
  // the chat as a COMPACT FLOATING window pinned next to the icon by the shared
  // popover placement — never the docked edge slide — and the gesture while it is
  // already open re-pins it there instead of hiding it. A plain click still
  // toggles, deferred one double-click interval by the shared gesture machinery
  // (whose trailing-release swallow this exercises: without it the deferred click
  // re-armed and toggled the just-opened chat straight back off).
  void chatIconPopoverGesture() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    win.raise();
    win.activateWindow();   // the typing-guard check below needs a focus owner
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    QToolButton* btn = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->defaultAction() == chat) { btn = b; break; }
    QVERIFY(btn);

    // A real double-click is press, release, dblclick, release — all four must go
    // through the gesture filter (QTest::mouseDClick waits internally, which the
    // deferred-click timer turns into a plain click first).
    const QPoint hit = btn->rect().center();
    const auto send = [btn, hit](QEvent::Type t) {
      QMouseEvent e(t, QPointF(hit), btn->mapToGlobal(hit), Qt::LeftButton,
                    t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                    Qt::NoModifier);
      QApplication::sendEvent(btn, &e);
    };
    const auto doubleClick = [&send] {
      send(QEvent::MouseButtonPress);
      send(QEvent::MouseButtonRelease);
      send(QEvent::MouseButtonDblClick);
      send(QEvent::MouseButtonRelease);
    };

    // While a text control has FOCUS, Alt belongs to the typing: with the cursor
    // resting on the icon, pressing Alt must NOT open the peek. Checked FIRST —
    // before any floating-dock activation, which the offscreen platform cannot
    // hand back (no raise()), leaves setFocus without an active window. The zoom
    // combo's inner line edit is the focusable text control used for it — which
    // needs an image, since the ZOOM cluster is dead without one.
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.zoom_->isEnabled());
    QCursor::setPos(btn->mapToGlobal(hit));
    QLineEdit* zoomEdit = win.zoom_->lineEdit();
    QVERIFY(zoomEdit);
    zoomEdit->setFocus();
    // The guard reads QApplication::focusWidget() — the editable combo is its
    // line edit's FOCUS PROXY, so that is what focus lands on.
    QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(win.zoom_));
    QTest::keyPress(zoomEdit, Qt::Key_Alt);
    QTest::keyRelease(zoomEdit, Qt::Key_Alt);
    QTest::qWait(50);
    QVERIFY2(!dock->isVisible(), "Alt while typing must not open the peek");
    win.zoom_->clearFocus();
    QTRY_VERIFY(QApplication::focusWidget() != win.zoom_);

    doubleClick();
    QTRY_VERIFY(dock->isVisible());
    QVERIFY2(dock->isFloating(), "the popover gesture must float the dock, not slide it in docked");
    QVERIFY(chat->isChecked());
    // Pinned next to the icon at the compact size — the shared placement rule.
    const QRect btnRect(btn->mapToGlobal(QPoint(0, 0)), btn->size());
    const QRect expect = stencil::support::popoverRect(
        btnRect, win.chatDock_->floatingDefaultSize(), btn->screen()->availableGeometry());
    QTRY_COMPARE(dock->geometry().topLeft(), expect.topLeft());
    QCOMPARE(dock->size(), expect.size());
    // The deferred single-click must NOT fire off the dblclick's trailing release
    // and yank the chat back shut (the swallow-release regression).
    QTest::qWait(QApplication::doubleClickInterval() + 300);
    QVERIFY2(dock->isVisible(), "the dblclick's trailing release must not re-arm the deferred toggle");
    QVERIFY(chat->isChecked());

    // The gesture while ALREADY open re-pins compact — it never hides.
    doubleClick();
    QTest::qWait(50);
    QVERIFY(dock->isVisible());
    QVERIFY(dock->isFloating());
    QVERIFY(chat->isChecked());

    // A plain click still toggles it off — after the double-click interval.
    send(QEvent::MouseButtonPress);
    send(QEvent::MouseButtonRelease);
    QTRY_VERIFY_WITH_TIMEOUT(!dock->isVisible(), QApplication::doubleClickInterval() + 2000);
    QVERIFY(!chat->isChecked());

    // Alt while hovering (the peek route, browser altHover parity): rest the
    // cursor on the icon and press Alt — the compact float opens with no click —
    // and it is HOLD-to-peek: releasing Alt closes it again.
    QCursor::setPos(btn->mapToGlobal(hit));
    QTest::qWait(20);
    QTest::keyPress(&win, Qt::Key_Alt);
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(dock->isFloating());
    QVERIFY(chat->isChecked());
    QTest::keyRelease(&win, Qt::Key_Alt);
    QTRY_VERIFY2(!dock->isVisible(), "releasing Alt must close what the peek opened");
    QVERIFY(!chat->isChecked());

    // ENGAGED peek: move the cursor INTO the peeked window before releasing Alt —
    // it stays open (sticky), instead of being yanked out from under the user.
    QCursor::setPos(btn->mapToGlobal(hit));
    QTest::qWait(20);
    QTest::keyPress(&win, Qt::Key_Alt);
    QTRY_VERIFY(dock->isVisible());
    QCursor::setPos(dock->frameGeometry().center());
    QTest::qWait(20);
    QTest::keyRelease(&win, Qt::Key_Alt);
    QTest::qWait(50);
    QVERIFY2(dock->isVisible(), "releasing Alt with the cursor inside must keep the peek open");
    QVERIFY(chat->isChecked());
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());

    // A DELIBERATE (dblclick) open is sticky: an Alt press+release leaves it be.
    doubleClick();
    QTRY_VERIFY(dock->isVisible());
    QTest::keyPress(&win, Qt::Key_Alt);
    QTest::keyRelease(&win, Qt::Key_Alt);
    QTest::qWait(50);
    QVERIFY2(dock->isVisible(), "Alt release must never close a dblclick-opened popover");
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());

    // A DISABLED icon opens nothing — mini window included — and must not arm a
    // stale popover anchor that would pin the NEXT dialog to it.
    QCursor::setPos(btn->mapToGlobal(hit));
    QTest::qWait(20);
    chat->setEnabled(false);
    doubleClick();
    QTest::qWait(80);
    QVERIFY2(!dock->isVisible(), "dblclick on a disabled icon must not open the popover");
    QTest::keyPress(&win, Qt::Key_Alt);
    QTest::keyRelease(&win, Qt::Key_Alt);
    QTest::qWait(50);
    QVERIFY2(!dock->isVisible(), "Alt-peek on a disabled icon must not open the popover");
    chat->setEnabled(true);

    dock->setFloating(false);  // leave the shared default placement for later slots
    beat();
  }

  // The chat dock is session-transient (browser full-reset-on-reload parity): a
  // saved window layout must NOT resurrect it — every launch starts hidden at
  // the default left placement. The selection panel DOES restore, and its
  // toggle action must track the restored visibility so the header chevron
  // ("Hide panel", Alt+X) still works after a restart (regression: the pre-show
  // isVisible() sync left the action unchecked, so the chevron no-opped).
  void chatDockSessionTransientAndPanelToggleAfterRestore() {
    {
      stencil::gui::Settings s = stencil::gui::fileStore::loadSettings();
      s.windowState.clear();
      stencil::gui::fileStore::saveSettings(s);
    }
    // Session 1: open + float the chat dock, then persist the layout the way
    // closeEvent does.
    {
      MainWindow win(nullptr, false);
      win.resize(1200, 800);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      auto* dock = win.findChild<QDockWidget*>("llmChatDock");
      auto* chat = win.findChild<QAction*>("actChat");
      QVERIFY(dock && chat);
      chat->setChecked(true);
      QTRY_VERIFY(dock->isVisible());
      dock->setFloating(true);
      QTRY_VERIFY(dock->isFloating());
      stencil::gui::Settings s = stencil::gui::fileStore::loadSettings();
      s.windowState = QString::fromLatin1(win.saveState().toBase64());
      stencil::gui::fileStore::saveSettings(s);
    }
    // Session 2: chat dock reset to hidden/docked-left/unchecked; the selection
    // panel's toggle is synced to the RESTORED visibility and the chevron works.
    {
      MainWindow win(nullptr, false);
      win.resize(1200, 800);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      auto* dock = win.findChild<QDockWidget*>("llmChatDock");
      QVERIFY(dock);
      QVERIFY(dock->isHidden());
      QVERIFY(!dock->isFloating());
      QCOMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
      auto* chat = win.findChild<QAction*>("actChat");
      QVERIFY(chat && !chat->isChecked());

      auto* selPanel = win.findChild<QDockWidget*>("selectionPanelDock");
      QVERIFY(selPanel && !selPanel->isHidden());
      QAction* panelAct = nullptr;
      for (QAction* a : win.findChildren<QAction*>())
        if (a->text() == "Selection Panel") { panelAct = a; break; }
      QVERIFY(panelAct);
      QVERIFY(panelAct->isChecked());  // synced to the restored visibility
      // Header chevron path: collapseRequested → actPanel_ → animated hide.
      QVERIFY(QMetaObject::invokeMethod(selPanel, "collapseRequested"));
      QTRY_VERIFY(selPanel->isHidden());
      QVERIFY(!panelAct->isChecked());
    }
    {
      stencil::gui::Settings s = stencil::gui::fileStore::loadSettings();
      s.windowState.clear();
      stencil::gui::fileStore::saveSettings(s);
    }
    beat();
  }

  // Assistant completions landing while the chat dock is HIDDEN surface as a
  // clickable bottom-left toast (browser closedToast parity): success/failure
  // text truncated to ~90 chars, auto-anchored bottom-left, click = open the
  // dock + dismiss; an open dock shows no toast.
  void chatToastWhenDockHidden() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());

    // Success (chat-only plan) while hidden → success toast with the reply.
    stencil::llm::LlmReply ok;
    ok.ok = true;
    ok.text = "{\"version\":1,\"reply\":\"All done, boss\",\"actions\":[]}";
    win.onChatReply(ok);
    auto* toast = win.findChild<QWidget*>("chatToast");
    QVERIFY(toast);
    QTRY_VERIFY(toast->isVisible());
    auto* label = toast->findChild<QLabel*>();
    QVERIFY(label);
    QVERIFY(label->text().contains("Assistant finished"));
    QVERIFY(label->text().contains("All done, boss"));
    // Anchored bottom-left (18 px inset).
    QCOMPARE(toast->x(), 18);
    QVERIFY(toast->geometry().bottom() > win.height() - 40);

    // Click → the dock opens through the normal path and the toast dismisses.
    QTest::mouseClick(toast, Qt::LeftButton);
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(chat->isChecked());
    QTRY_VERIFY(!toast->isVisible());

    // Failure while hidden → error toast, truncated to the ~90-char bound.
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    stencil::llm::LlmReply bad;
    bad.ok = false;
    bad.failure = stencil::llm::LlmFailure::Http;
    bad.error = QString(200, QChar('x'));
    win.onChatReply(bad);
    QTRY_VERIFY(toast->isVisible());
    QVERIFY(label->text().startsWith("Assistant failed"));
    QVERIFY(label->text().size() <= 90);
    QVERIFY(label->text().endsWith(QChar(0x2026)));
    QTest::mouseClick(toast, Qt::LeftButton);  // dismiss + reopen for the next check
    QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY(!toast->isVisible());

    // Open dock: completions must NOT raise a toast.
    win.onChatReply(ok);
    QVERIFY(!toast->isVisible());
    beat();
  }

  // While an assistant turn is in flight, the send button becomes STOP
  // (enabled, "Stop the response"), Enter is a no-op (single-turn guard), and
  // clicking STOP turns the pending "…" card into a muted "Stopped." without
  // an assistant history push or a toast; the composer then returns to normal.
  void chatStopWhileBusy() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    auto* input = dock->findChild<QPlainTextEdit*>("chatInput");
    auto* send = dock->findChild<QToolButton*>("chatSend");
    QVERIFY(input && send);

    // Simulate an in-flight turn: exactly the state onChatSend sets up.
    win.chatDock_->showPending();
    win.chatDock_->setBusy(true);
    QVERIFY(send->isEnabled());  // STOP mode is always clickable
    QCOMPARE(send->toolTip(), QString("Stop the response"));

    // Enter while busy is ignored (single-turn): the input keeps its text and
    // no send fires (a send would clear it).
    input->setPlainText("second question");
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(input->toPlainText(), QString("second question"));
    input->clear();

    // Click STOP → the abort flag is set; then the canceled reply lands.
    const int histBefore = win.chatHistory_.size();
    QTest::mouseClick(send, Qt::LeftButton);
    QVERIFY(win.chatStopRequested_);
    win.chatDock_->setBusy(false);  // what the chat completion wrapper does
    stencil::llm::LlmReply canceled;
    canceled.ok = false;
    canceled.failure = stencil::llm::LlmFailure::Transport;
    canceled.error = "Operation canceled";
    win.onChatReply(canceled);

    // The pending card became "Stopped."; nothing was pushed or toasted.
    bool stoppedShown = false;
    for (QLabel* l : dock->findChildren<QLabel*>())
      if (l->text() == QString("Stopped.")) stoppedShown = true;
    QVERIFY(stoppedShown);
    QCOMPARE(win.chatHistory_.size(), histBefore);
    auto* toast = win.findChild<QWidget*>("chatToast");
    QVERIFY(!toast || !toast->isVisible());

    // Composer back to normal: send glyph/tooltip restored, guard cleared.
    QVERIFY(!win.chatStopRequested_);
    QCOMPARE(send->toolTip(), QString("Send (Enter)"));
    QVERIFY(!send->isEnabled());  // idle + empty input gates send again
    input->setPlainText("hello");
    QVERIFY(send->isEnabled());
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

  // "Clear the conversation" (browser parity): a trash ghost in the branded
  // title bar, AHEAD of the four placement chevrons, disabled while a turn is
  // in flight. Clicking wipes every transcript card + the attachments, brings
  // the empty-state suggestion chips back, and drops the model-side
  // conversation state (chatHistory_ and the per-conversation caches) — while
  // deliberately leaving the provider settings and the working image alone.
  // Also covers the card APPEAR animation: each new card fades in from an
  // opacity effect and lands fully visible, overlapping appends included.
  // Copy-to-clipboard ships the RENDERED image: filter plus drawn lines (browser
  // renderExportCanvas parity). The old no-overlay copy dropped the user's edits.
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

  void chatClearConversation() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = qobject_cast<stencil::gui::ChatDock*>(
        win.findChild<QDockWidget*>("llmChatDock"));
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY(dock->width() > 200);  // let the open slide settle before geometry

    // The button lives in the COMPOSER row (browser #chat-clear parity), after
    // the settings gear — NOT in the title bar with the placement chevrons.
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);
    QVERIFY(!title->findChild<QToolButton*>("chatClear"));
    auto* clearBtn = dock->findChild<QToolButton*>("chatClear");
    QVERIFY(clearBtn);
    QVERIFY(clearBtn->toolTip().isEmpty());  // no tooltip — the menu label says it (user decision)
    // Both live in the … menu now — hidden buttons have no meaningful geometry,
    // so their identity is what matters, not their x order.
    auto* gearBtn = dock->findChild<QToolButton*>("chatGear");
    QVERIFY(gearBtn);
    QVERIFY(gearBtn->isHidden() && clearBtn->isHidden());

    // A conversation: two cards, an attachment, and the model-side state a
    // turn leaves behind.
    auto* scrollArea = dock->findChild<QScrollArea*>();
    QVERIFY(scrollArea && scrollArea->widget());
    QWidget* transcript = scrollArea->widget();
    const auto cardCount = [transcript] {
      // Cards are the only direct QFrame children of the transcript column
      // (the suggestion block is a plain QWidget).
      return transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)
          .size();
    };
    dock->appendUser("make it sepia");
    dock->appendAssistant("done");
    QCOMPARE(cardCount(), 2);
    QImage att(8, 8, QImage::Format_RGB32);
    att.fill(Qt::blue);
    dock->addAttachmentImage(att);
    QCOMPARE(dock->attachedImages().size(), 1);

    // A drag onto the COMPOSER attaches; the cue shows while it hovers and goes on
    // drop. Driven through the composer's own event filter, which is where the whole
    // gesture now lives (the dock itself declines drops).
    {
      auto* inputArea = dock->findChild<QWidget*>("chatInputArea");
      auto* cue = dock->findChild<QWidget*>("chatDropCue");
      auto* inputBox = dock->findChild<QPlainTextEdit*>("chatInput");
      QVERIFY(inputArea && cue && inputBox);
      // Aim at the INPUT BOX: the cue (and the attach) belong to it alone — a drag
      // over the composer's buttons or the chip tray neither lights nor attaches.
      const QPoint at = inputBox->mapTo(inputArea, inputBox->rect().center());
      QImage dragged(12, 12, QImage::Format_RGB32);
      dragged.fill(Qt::red);
      QMimeData mime;
      mime.setImageData(dragged);
      const int before = dock->attachedImages().size();
      QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QVERIFY(qApp->sendEvent(inputArea, &enter));
      QVERIFY2(enter.isAccepted(), "the composer refused an image drag");
      QVERIFY2(!cue->isHidden(), "no drop cue while a drag hovers the composer");
      if (qEnvironmentVariableIsSet("STENCIL_GUI_SHOTS")) {
        QTest::qWait(50);
        inputArea->grab().save(QString::fromLocal8Bit(qgetenv("STENCIL_GUI_SHOTS")) + "/dock-dropcue.png");
      }
      // Off the box (the buttons' corner) the cue goes out mid-drag…
      const QPoint offBox(inputArea->width() - 3, inputArea->height() - 3);
      QDragMoveEvent wander(offBox, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QVERIFY(qApp->sendEvent(inputArea, &wander));
      QVERIFY2(cue->isHidden(), "the cue lit over the composer's buttons");
      // …and back over it the cue returns; the drop there attaches.
      QDragMoveEvent back(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QVERIFY(qApp->sendEvent(inputArea, &back));
      QVERIFY2(!cue->isHidden(), "the cue did not come back over the input box");
      QDropEvent drop(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QVERIFY(qApp->sendEvent(inputArea, &drop));
      QVERIFY2(drop.isAccepted(), "the composer refused an image drop");
      QCOMPARE(dock->attachedImages().size(), before + 1);
      QVERIFY2(cue->isHidden(), "the cue outlived the drop");
      // …and the queued chip's thumbnail carries a hover preview (28px tells you
      // nothing about which screenshot it is).
      auto* tray = dock->findChild<QWidget*>("chatAttachTray");
      QVERIFY(tray);
      bool previewed = false;
      for (QLabel* pic : tray->findChildren<QLabel*>())
        if (!pic->pixmap().isNull() && !pic->findChildren<QObject*>().isEmpty()) previewed = true;
      QVERIFY2(previewed, "no hover preview installed on the attachment chip");
      // A NAMED attachment says its name; only an unnamed one (a dropped bitmap, as
      // above) falls back to the dimensions. And the thumbnail carries no tooltip —
      // it opens the hover preview, and a tooltip on top of that put two popups on
      // screen at once, the tooltip covering the picture it was describing.
      dock->clearAttachments();
      dock->addAttachmentImage(dragged, QStringLiteral("cat.png"));
      QStringList chipTexts;
      bool thumbHasTooltip = false;
      for (QLabel* l : tray->findChildren<QLabel*>()) {
        if (l->pixmap().isNull()) chipTexts << l->text();
        else if (!l->toolTip().isEmpty()) thumbHasTooltip = true;
      }
      QVERIFY2(chipTexts.contains("cat.png"), qPrintable("chip shows: " + chipTexts.join('|')));
      QVERIFY2(!thumbHasTooltip, "the thumbnail must not duplicate the hover preview in a tooltip");
      // Put the queue back the way the surrounding case staged it (one image).
      dock->clearAttachments();
      dock->addAttachmentImage(att);
    }

    // What the user attached is SHOWN, inside the user's own bubble: the card
    // carries a pixmap label (it used to say "[1 image(s) attached]" and nothing
    // more). The browser/extension render the same strip.
    {
      const auto before =
          transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly);
      dock->appendUser("and this one", dock->attachedImages());
      const auto after =
          transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly);
      QCOMPARE(after.size(), before.size() + 1);
      QFrame* userCard = after.last();
      QCOMPARE(userCard->objectName(), QStringLiteral("chatCardUser"));
      int thumbs = 0;
      QString body;
      for (QLabel* l : userCard->findChildren<QLabel*>()) {
        if (!l->pixmap().isNull()) thumbs++;
        else body += l->text();
      }
      QCOMPARE(thumbs, 1);
      QVERIFY(body.contains("and this one"));
      QVERIFY(!body.contains("image(s) attached"));   // the count-only text is gone
      // A turn with nothing attached stays a plain bubble.
      dock->appendUser("no images here");
      QFrame* plain =
          transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly).last();
      int plainThumbs = 0;
      for (QLabel* l : plain->findChildren<QLabel*>())
        if (!l->pixmap().isNull()) plainThumbs++;
      QCOMPARE(plainThumbs, 0);
    }
    stencil::llm::ChatMessage m;
    m.role = "user";
    m.text = "make it sepia";
    win.pushChatHistory(m);
    win.chatVideoPath_ = "/tmp/clip.mp4";
    win.chatVideoFrames_ = 42;
    win.chatImageDigest_ = QByteArray("digest");
    win.chatImageEncoded_.data = QByteArray("cached");
    QVERIFY(!win.chatHistory_.isEmpty());
    auto* suggest = dock->findChild<QWidget*>("chatSuggest");
    QVERIFY(suggest && !suggest->isVisible());  // hidden by the first card

    // Frozen while a turn is in flight (like attach).
    auto* attachBtn = dock->findChild<QToolButton*>("chatAttach");
    QVERIFY(attachBtn);
    dock->setBusy(true);
    QVERIFY(!clearBtn->isEnabled());
    QVERIFY(!attachBtn->isEnabled());
    dock->setBusy(false);
    QVERIFY(clearBtn->isEnabled());

    // Click: transcript emptied, chips back, attachments + model state dropped;
    // the provider config survives.
    const QString providerBefore = win.currentLlmSettings().provider;
    QTest::mouseClick(clearBtn, Qt::LeftButton);
    QTRY_COMPARE(cardCount(), 0);   // cards go through deleteLater
    // The empty state is held back for the length of the scatter: showing it in the
    // same tick put the chips under particles that were still falling, and the clear
    // read as happening twice (browser chatView.js restoreEmptyState parity). The wait
    // is keyed off rows being REMOVED, not off the scatter animating — offscreen (and
    // on a hidden dock) there are no particles, and the empty state must still not
    // beat the wipe.
    QVERIFY2(!suggest->isVisible(), "the chips came back before the wipe finished");
    QTRY_VERIFY_WITH_TIMEOUT(suggest->isVisible(),
                             stencil::gui::DisintegrateOverlay::kMs + 3000);
    QCOMPARE(dock->attachedImages().size(), 0);
    QVERIFY(dock->attachedVideoPath().isEmpty());
    QVERIFY(win.chatHistory_.isEmpty());
    QVERIFY(win.chatVideoPath_.isEmpty());
    QCOMPARE(win.chatVideoFrames_, 0);
    QVERIFY(win.chatImageDigest_.isEmpty());
    QVERIFY(win.chatImageEncoded_.data.isEmpty());
    QCOMPARE(win.currentLlmSettings().provider, providerBefore);

    // Appear animation: a fresh card starts transparent behind its own opacity
    // effect and animates to fully visible. Overlapping appends each own their
    // animation, so all of them land at 1.0 and at their resting margins.
    dock->appendUser("again");
    dock->appendAssistant("sure");
    dock->appendError("nope");
    const auto cards =
        transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly);
    QCOMPARE(cards.size(), 3);
    for (QFrame* card : cards) {
      auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect());
      QVERIFY(fx);
      QCOMPARE(fx->opacity(), 0.0);  // starts hidden, before the loop spins
    }
    for (QFrame* card : cards) {
      auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect());
      QTRY_COMPARE(fx->opacity(), 1.0);
      QVERIFY(card->layout());
      QTRY_COMPARE(card->layout()->contentsMargins().top(), 6);  // slide resolved
    }
    // Clearing mid-animation must not crash (the card owns its animation).
    dock->appendAssistant("mid-flight");
    QTest::mouseClick(clearBtn, Qt::LeftButton);
    QTRY_COMPARE(cardCount(), 0);
    QTest::qWait(200);  // let any surviving animation tick would-be-dangling

    // A card that SCROLLS while it fades must not crash. ScrollReveal installs its own
    // DissolveEffect on cards near a viewport edge, and setGraphicsEffect deletes the
    // effect already there — so the fade's animation was left writing to freed memory
    // and the app died in QGraphicsOpacityEffect::setOpacity one frame later. The fade
    // now claims the card (kEnteringProperty) exactly as the entrance animation does.
    {
      for (int i = 0; i < 6; i++) {
        dock->appendUser(QStringLiteral("question %1").arg(i));
        dock->appendAssistant(QStringLiteral("a reply long enough to wrap and take real height %1").arg(i));
      }
      // Long enough for every ENTRANCE animation to finish and drop its own claim on
      // the effect — otherwise the assertion below passes for the wrong reason.
      QTest::qWait(400);
      dock->clearConversation();          // every card starts fading…
      // …and the INVARIANT holds from the first frame: every leaving card claims its
      // graphics effect, which is the flag ScrollReveal::apply() skips on. Without the
      // claim ScrollReveal calls setGraphicsEffect on a fading card, Qt deletes the
      // effect the fade's animation writes to, and the next frame is a use-after-free
      // (the crash the app died of). The crash itself cannot be reproduced offscreen:
      // the transcript never becomes scrollable there, so ScrollReveal returns before
      // installing anything — hence the invariant, not the symptom.
      int claimed = 0, fading = 0;
      for (QFrame* card : transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!card->graphicsEffect()) continue;
        fading++;
        if (card->property(stencil::gui::ScrollReveal::kEnteringProperty).toBool()) claimed++;
      }
      QVERIFY2(fading > 0, "no card was actually fading — the guard would be vacuous");
      QCOMPARE(claimed, fading);
      for (int i = 0; i < 12; i++) {      // …while the transcript relayouts under them
        dock->resize(dock->width(), 300 + (i % 3) * 60);
        QTest::qWait(30);                 // each wait lets the animation driver tick
      }
      QTRY_VERIFY_WITH_TIMEOUT(cardCount() == 0,
                               stencil::gui::DisintegrateOverlay::kMs + 3000);
    }

    // A LONG wrapped reply must not be cut off by its own bubble: the label's wrapped
    // height is RESERVED (applyBubbleWidths), because heightForWidth is only a hint and
    // the transcript's layout does not re-ask once it has sized a card. The reply used
    // to end mid-sentence at the card's bottom edge.
    {
      const QString essay =
          QStringLiteral("The layout is drawn on a 794x1123 px page, so the rectangle sits at "
                         "roughly 250,400-544,723 — tell me if the centre is off. The three "
                         "variants are rotated, tinted and cropped. I couldn't open an "
                         "incognito tab: that needs a URL you gave me in this conversation.");
      dock->appendAssistant(essay);
      QTest::qWait(250);   // past the appear animation, which offsets the card's margins
      QLabel* body = nullptr;
      for (QLabel* l : transcript->findChildren<QLabel*>())
        if (l->text() == essay) body = l;
      QVERIFY(body);
      const int wrapped = body->heightForWidth(body->width());
      QVERIFY2(body->height() >= wrapped,
               qPrintable(QStringLiteral("the reply is clipped: %1px tall for %2px of text")
                              .arg(body->height()).arg(wrapped)));
      QVERIFY2(body->parentWidget()->height() >= body->height(),
               "the bubble is shorter than the text inside it");
    }

    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

  // Chat persistence (llm-contract.md §12): with the opt-in ON a settled
  // conversation is filed on the active LOCAL project's record, reopening the
  // project replays it into the history + dock cards, the trash also deletes
  // the persisted copy, and with the opt-in OFF (the default) nothing is
  // written. Uses the friend seam to drive the persist/restore pipeline
  // directly (no mock LLM needed — the seam sits after the reply parsing).
  void chatPersistsWithProject() {
    using stencil::gui::fileStore::parseChatDoc;
    using stencil::gui::Project;
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    win.settings_.saveChatsWithProject = true;

    // A local project to file the chat under.
    win.adoptCanvasAsLocalProject();
    QVERIFY(!win.activeProjectId_.isEmpty());
    const QString projectId = win.activeProjectId_;

    // A settled turn: history push + the persist that onChatReply's tail runs.
    stencil::llm::ChatMessage u;
    u.role = "user";
    u.text = "make it sepia";
    stencil::llm::ChatMessage a;
    a.role = "assistant";
    a.text = "Sepia applied.";
    win.pushChatHistory(u);
    win.pushChatHistory(a);
    // The doc is built from what was DISPLAYED (§12.1), so mirror the two rows
    // the send/reply paths would have posted.
    win.chatMirror("You", u.text, false);
    win.chatMirror("Assistant", a.text, false);
    win.persistActiveChat();
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY(pr);
      QCOMPARE(parseChatDoc(pr->chat).size(), 2);   // saved, text-only, in order
    }

    // Reopening the project replays the saved conversation: replay history AND
    // dock transcript cards (restoreChatFromDoc via loadProjectIntoCanvas).
    win.resetChatState();
    QVERIFY(win.chatHistory_.isEmpty());
    QVERIFY(win.loadProjectIntoCanvas(projectId));
    QCOMPARE(win.chatHistory_.size(), 2);
    QCOMPARE(win.chatHistory_.last().text, QString("Sepia applied."));
    {
      auto* dock = qobject_cast<stencil::gui::ChatDock*>(
          win.findChild<QDockWidget*>("llmChatDock"));
      QVERIFY(dock);
      auto* scrollArea = dock->findChild<QScrollArea*>();
      QVERIFY(scrollArea && scrollArea->widget());
      const auto cards = scrollArea->widget()->findChildren<QFrame*>(
          QString(), Qt::FindDirectChildrenOnly);
      QCOMPARE(cards.size(), 2);   // one card per restored turn
    }

    // The trash clears the persisted copy too (§12.2).
    win.onChatClear();
    QVERIFY(win.chatHistory_.isEmpty());
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY(pr && pr->chat.isEmpty());
    }

    // Opt-in OFF (the default): a turn leaves the record untouched.
    win.settings_.saveChatsWithProject = false;
    win.pushChatHistory(u);
    win.persistActiveChat();
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY(pr && pr->chat.isEmpty());
    }

    // Tidy the dev state dir: drop the project this test created.
    dismissModal("Yes");
    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);
    beat();
  }

  // §12.1 WRITE side: the persisted document is the DISPLAYED transcript, never
  // chatHistory_ — that is the model's view, carrying the §7 continuation note
  // and the held round-1 reply the dock never showed. The doc rides the .stencil
  // file and the server "chat" kind to the browser, the bot and the consoles, so
  // internal text written here can no longer be filtered out anywhere.
  void chatDocSavesOnlyTheDisplayedTranscript() {
    using stencil::gui::fileStore::parseChatDoc;
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const QString& json) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", json}}}})
          .toJson(QJsonDocument::Compact);
    };
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    win.ensureChatMenuPanel();
    win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel_->show();

    // Every card's body + in-card notes, per surface — the displayed transcript.
    const auto cardsOf = [](QWidget* surface) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QStringList out;
      for (QFrame* f : surface->findChildren<QFrame*>()) {
        const QString kind = f->objectName();
        if (!kind.startsWith(QLatin1String("chatCard")) ||
            kind == QLatin1String("chatCardMore"))
          continue;
        QStringList texts;
        for (QLabel* l : f->findChildren<QLabel*>()) {
          const QString b = l->property("chatBody").toString();
          const QString n = l->property("chatNote").toString();
          if (!b.isEmpty()) texts << b;
          else if (!n.isEmpty()) texts << n;
        }
        if (texts.size() == 1 && texts.first() == QStringLiteral("…")) continue;  // pending
        out << kind + QStringLiteral(": ") + texts.join(QStringLiteral(" ¶ "));
      }
      return out;
    };

    // A §7 continuation turn: round 1 loads without tracing, so its reply is
    // HELD and round 2's settled answer is the only bubble the user gets.
    const QString interim = QStringLiteral("Loading the blank page and cropping now.");
    const QString settled = QStringLiteral("Blank page ready, converted to black & white.");
    const QString ask = QStringLiteral("give me a blank page in b&w");
    mock.queue.append(wrap(QStringLiteral(
        "{\"version\":1,\"reply\":\"%1\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\"}]}")
                              .arg(interim)));
    mock.queue.append(wrap(
        QStringLiteral("{\"version\":1,\"reply\":\"%1\",\"actions\":[]}").arg(settled)));
    win.onChatSend(ask);
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QTest::qWait(200);

    // ── 1. the document is exactly the two displayed rows ──
    const QJsonObject doc = win.buildActiveChatDoc();
    const QByteArray json = QJsonDocument(doc).toJson();
    QVERIFY2(!json.contains("The working image is now"),
             qPrintable("the §7 continuation note was persisted: " + QString::fromUtf8(json)));
    QVERIFY2(!json.contains(interim.toUtf8()),
             qPrintable("the held interim reply was persisted: " + QString::fromUtf8(json)));
    const QJsonArray saved = parseChatDoc(doc);
    QCOMPARE(saved.size(), 2);
    QCOMPARE(saved.at(0).toObject().value("role").toString(), QString("user"));
    QCOMPARE(saved.at(0).toObject().value("text").toString(), ask);
    QCOMPARE(saved.at(1).toObject().value("role").toString(), QString("assistant"));
    QCOMPARE(saved.at(1).toObject().value("text").toString(), settled);
    QCOMPARE(doc.value("version").toInt(), 1);
    QVERIFY(doc.value("savedAt").toDouble() > 0);
    // The MODEL's view is untouched: the live conversation still replays both.
    bool noteInHistory = false, interimInHistory = false;
    for (const auto& m : win.chatHistory_) {
      if (m.text.contains(QStringLiteral("The working image is now"))) noteInHistory = true;
      if (m.text == interim) interimInHistory = true;
    }
    QVERIFY2(noteInHistory && interimInHistory,
             "chatHistory_ must keep the full model-side history");

    // ── 2. it round-trips to the same transcript on BOTH surfaces ──
    const QStringList before = cardsOf(win.chatDock_);
    win.restoreChatFromDoc(doc);
    QTRY_COMPARE(cardsOf(win.chatDock_).size(), 2);
    QCOMPARE(cardsOf(win.chatDock_), before);
    QCOMPARE(cardsOf(win.chatMenuPanel_), cardsOf(win.chatDock_));
    // …and re-saving the restored conversation is a fixed point.
    QCOMPARE(parseChatDoc(win.buildActiveChatDoc()), saved);

    // ── 3. defence in depth: an OLD-style doc (written before the machinery
    // filter) is laundered on read — the §7 note never resurfaces on screen or
    // in the replay history; the interim reply is indistinguishable from
    // conversation and survives (browser sanitizeChatMessages parity) ──
    QJsonArray old;
    const auto row = [](const char* role, const QString& text) {
      return QJsonObject{{"role", QString::fromLatin1(role)}, {"text", text}};
    };
    old.append(row("user", ask));
    old.append(row("assistant", interim));
    old.append(row("user", QStringLiteral(
        "[The working image is now the picture those actions loaded — continue with it.]")));
    old.append(row("assistant", settled));
    // The write side filters too: the note never even reaches a new document.
    const QJsonObject oldDoc{{"version", 1},
                             {"savedAt", 42},
                             {"messages", old}};
    QCOMPARE(stencil::gui::fileStore::buildChatDoc(old, 42).value("messages").toArray().size(), 3);
    win.restoreChatFromDoc(oldDoc);
    QTRY_COMPARE(cardsOf(win.chatDock_).size(), 3);
    QCOMPARE(cardsOf(win.chatMenuPanel_), cardsOf(win.chatDock_));
    for (const QString& r : cardsOf(win.chatDock_))
      QVERIFY2(!r.contains(QStringLiteral("The working image is now")),
               qPrintable("an old doc put the continuation note on screen: " + r));
    QCOMPARE(win.chatHistory_.size(), 3);   // the model replays the same laundered view
    bool oldNoteInHistory = false;
    for (const auto& m : win.chatHistory_)
      if (m.text.contains(QStringLiteral("The working image is now"))) oldNoteInHistory = true;
    QVERIFY2(!oldNoteInHistory, "the §7 note must not be replayed from storage");

    // ── 4. the §12.1 bound: writers trim to the most recent 32 ──
    win.onChatClear();
    win.chatDock_->clearConversation();
    for (int i = 0; i < 40; ++i)
      win.chatMirror(i % 2 ? QStringLiteral("Assistant") : QStringLiteral("You"),
                     QStringLiteral("row %1").arg(i), false);
    const QJsonArray trimmed = parseChatDoc(win.buildActiveChatDoc());
    QCOMPARE(trimmed.size(), 32);
    QCOMPARE(trimmed.at(0).toObject().value("text").toString(), QString("row 8"));

    // ── 5. muted plumbing is never conversation ──
    win.onChatClear();
    win.chatDock_->clearConversation();
    win.chatMirror(QStringLiteral("You"), ask, false);
    win.chatError(QStringLiteral("Could not read the assistant's plan: bad op"), QString());
    win.chatMirror(QStringLiteral("Attached"), QStringLiteral("1 image(s)"), true);
    const QJsonArray onlyUser = parseChatDoc(win.buildActiveChatDoc());
    QCOMPARE(onlyUser.size(), 1);
    QCOMPARE(onlyUser.at(0).toObject().value("text").toString(), ask);

    win.onChatClear();
    win.chatDock_->clearConversation();
    QVERIFY(win.buildActiveChatDoc().isEmpty());   // nothing displayed ⇒ nothing filed
    win.llmClient_.reset();
    beat();
  }

  // The chat dock SLIDES in and out (browser chat-panel parity) instead of
  // popping: docked left, its WIDTH animates 0 → natural on open and back to 0
  // on close (then it hides). Sampled like fullscreenRevealAnimatesSmoothly.
  // Afterwards the size constraints must be released again (the min==max
  // pinning is animation-only) so the dock stays user-resizable, and a reopen
  // returns to the extent it had before the dismissal. Floating docks are their
  // own windows, so they just show/hide.
  void chatDockSlideAnimation() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    QCOMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);  // width axis

    // Sample the docked extent every ~16 ms across the slide (a hidden dock
    // counts as 0 — that IS its contribution to the layout).
    auto sample = [dock] {
      QList<int> s;
      for (int i = 0; i < 26; ++i) {
        s.append(dock->isVisible() ? dock->width() : 0);
        QTest::qWait(16);
      }
      return s;
    };
    auto hasIntermediate = [](const QList<int>& s, int full) {
      for (int v : s) if (v > 2 && v < full - 2) return true;
      return false;
    };

    // ── open: 0 → natural ──
    chat->setChecked(true);
    QVERIFY(dock->isVisible());  // shown immediately; it grows into place
    const QList<int> up = sample();
    QTRY_VERIFY(dock->width() > 200);
    const int full = dock->width();
    QVERIFY2(hasIntermediate(up, full),
             "chat dock popped open (no intermediate widths)");
    // The pinning is released at the end: still resizable, and the dock's own
    // 260 px floor is back (not clamped to the animation's last frame).
    QTRY_COMPARE(dock->maximumWidth(), QWIDGETSIZE_MAX);
    QCOMPARE(dock->minimumWidth(), 260);

    // ── close: natural → 0, then hidden ──
    chat->setChecked(false);
    QVERIFY(dock->isVisible());  // still on screen while it slides out
    const QList<int> down = sample();
    QTRY_VERIFY(!dock->isVisible());
    QVERIFY2(hasIntermediate(down, full),
             "chat dock popped shut (no intermediate widths)");
    for (int i = 1; i < down.size(); ++i)
      QVERIFY2(down[i] <= down[i - 1] + 1, "chat dock hide width oscillated");
    QTRY_COMPARE(dock->maximumWidth(), QWIDGETSIZE_MAX);

    // Reopening restores the extent it was dismissed at.
    chat->setChecked(true);
    QTRY_VERIFY(dock->width() > 200);
    QTest::qWait(400);
    QVERIFY2(qAbs(dock->width() - full) <= 8, "reopen lost the remembered width");

    // Floating: no edge to slide from — plain show/hide, and the tear-off size
    // is never clamped by a leftover animation constraint.
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    chat->setChecked(false);
    QVERIFY(!dock->isVisible());  // immediate, no slide
    chat->setChecked(true);
    QVERIFY(dock->isVisible());
    QTRY_COMPARE(dock->size(), QSize(385, 480));   // +5px of room for the row "…"
    dock->setFloating(false);
    QTRY_VERIFY(!dock->isFloating());
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

  // The chat dock's gear opens the DEDICATED assistant dialog (browser
  // llmSettingsModal parity) — provider / base URL / model / API key / server
  // only, none of the full Settings sheet's unrelated controls — and saving it
  // round-trips the provider through the normal persistence path.
  void chatGearOpensAssistantOnlySettings() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    auto* gear = dock->findChild<QToolButton*>("chatGear");
    QVERIFY(gear);

    const stencil::gui::Settings before = stencil::gui::fileStore::loadSettings();
    // Pick a provider that differs from whatever is configured now.
    const QString target =
        win.settings_.llmProvider == QLatin1String("openai-compat") ? "ollama"
                                                                   : "openai-compat";
    // The dialog is modal (exec blocks the click), so inspect + drive it from a
    // timer, the way dismissModal does for the confirmations.
    bool inspected = false, wrongControls = false, sawProvider = false;
    QString dialogName;
    QTimer::singleShot(0, [&] {
      QDialog* dlg = nullptr;
      for (int i = 0; i < 200 && !dlg; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) QTest::qWait(10);
      }
      if (!dlg) return;
      dialogName = dlg->objectName();
      auto* provider = dlg->findChild<QComboBox*>("llmProvider");
      sawProvider = provider != nullptr;
      // Assistant-only: none of the full Settings dialog's controls (autosave /
      // visibility checkboxes, thickness & page-size spin boxes, the page-size
      // combo) may be here. The one checkbox that DOES belong is the §12
      // save-chats opt-in (llmSaveChats) — an assistant setting.
      wrongControls = !dlg->findChildren<QDoubleSpinBox*>().isEmpty() ||
                      !dlg->findChildren<QSpinBox*>().isEmpty();
      for (QCheckBox* cb : dlg->findChildren<QCheckBox*>())
        if (cb->objectName() != QLatin1String("llmSaveChats")) wrongControls = true;
      for (QComboBox* c : dlg->findChildren<QComboBox*>())
        if (c->findData(QString("A4")) >= 0 || c->findText("A4") >= 0)
          wrongControls = true;
      // The LLM rows the browser modal has, all present.
      for (const char* name : {"llmBaseUrl", "llmModel", "llmApiKey", "llmServer"})
        if (!dlg->findChild<QWidget*>(name)) wrongControls = true;
      if (provider) {
        const int idx = provider->findData(target);
        if (idx >= 0) provider->setCurrentIndex(idx);
      }
      inspected = true;
      dlg->accept();  // Save
    });
    gear->click();  // blocks in exec() until the timer accepts

    QVERIFY2(inspected, "the gear did not open a modal dialog");
    QCOMPARE(dialogName, QString("assistantSettingsDialog"));
    QVERIFY(sawProvider);
    QVERIFY2(!wrongControls, "the assistant dialog carries unrelated settings");
    // Saved through the same path as the full dialog: live settings + the file.
    QCOMPARE(win.settings_.llmProvider, target);
    QCOMPARE(stencil::gui::fileStore::loadSettings().llmProvider, target);

    stencil::gui::fileStore::saveSettings(before);  // leave the user's config alone
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

  // §12.2: the save-chats toggle has to say who can READ a saved chat, right where it is
  // offered. A server project's chat file carries the project's own access, so everyone
  // the project is shared with can read a transcript of what the user asked for in their
  // own words — which is not what "Save chats with projects" sounds like it promises.
  // LlmSettingsForm is the single host of the toggle (both the full Settings sheet and
  // the assistant-only dialog embed it), so checking the form covers both places.
  void chatSaveDisclosureSitsAtTheToggle() {
    const stencil::gui::Settings defaults;
    stencil::gui::LlmSettingsForm form(defaults,
                                       stencil::gui::LlmSettingsForm::RowMode::HideRows);

    auto* cb = form.findChild<QCheckBox*>("llmSaveChats");
    QVERIFY2(cb, "the §12 save-chats opt-in is missing from the assistant settings");
    QVERIFY2(!cb->isChecked(), "chat persistence ships OFF — it is an explicit opt-in");

    // Visible, not hover-only: a tooltip nobody opens does not disclose anything.
    auto* hint = form.findChild<QLabel*>("llmSaveChatsHint");
    QVERIFY2(hint, "the sharing consequence has no visible label at the toggle");
    QVERIFY2(hint->text().contains("shared with"),
             qPrintable("the visible hint does not state who can read it: " + hint->text()));
    QVERIFY2(!hint->text().isEmpty() && hint->isVisibleTo(&form),
             "the hint is present but not shown alongside the checkbox");

    // The tooltip carries it too, with the local-vs-server split spelled out.
    const QString tip = cb->toolTip();
    QVERIFY2(tip.contains("shared with"), qPrintable("tooltip omits the sharing rule: " + tip));
    QVERIFY2(tip.contains("Local projects"), qPrintable("tooltip omits the local case: " + tip));
    beat();
  }

  // The canvas context menu carries an "Assistant ▸" submenu (browser parity):
  // a nested entry alongside Style / Image Filter / Transformation / Tooltip,
  // present only when a provider is configured, hosting a compact chat that
  // drives the SAME pipeline and the SAME history as the dock — and never
  // dismissing the menu while you type, send or receive.
  //
  // Regression guard baked in: the classic submenus must still hover-open. They
  // stopped doing so when the chat lived in the ROOT menu and its live-input
  // interception ran over every row; the chat now owns its own child menu, so
  // the root keeps stock QMenu behaviour.
  void contextMenuAssistantSubmenu() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);  // a working image, so a plan has something to hit

    // Hover a submenu parent the way a user does and wait for its child popup.
    // Open a submenu the way a user does. Hover first — that is the path the
    // regression broke — then fall back to the keyboard: QTest's synthetic
    // moves cannot drive a NATIVE popup grab (macOS), so a headed run would
    // otherwise fail on a harness limitation rather than a real one. Either
    // way the assertion is "the child popup opens", which is what regressed.
    auto openSub = [](QMenu* menu, const QString& title) -> QMenu* {
      QAction* parent = nullptr;
      for (QAction* a : menu->actions())
        if (a->text() == title) parent = a;
      if (!parent || !parent->menu()) return nullptr;
      // A move to the position the cursor already occupies produces no event at
      // all, and the first move into a freshly popped menu is routinely
      // swallowed — so nudge via a PLAIN row (never a submenu parent, whose
      // child popup would then cover the row we aim at) and retry the pair.
      for (int attempt = 0; attempt < 4 && !parent->menu()->isVisible(); ++attempt) {
        for (QAction* a : menu->actions()) {
          if (a->isSeparator() || a->menu() || !a->isEnabled()) continue;
          QTest::mouseMove(menu, menu->actionGeometry(a).center());
          break;
        }
        QTest::qWait(30);
        QTest::mouseMove(menu, menu->actionGeometry(parent).center());
        for (int i = 0; i < 40 && !parent->menu()->isVisible(); ++i) QTest::qWait(10);
      }
      if (!parent->menu()->isVisible()) {  // keyboard fallback
        menu->setActiveAction(parent);
        QTest::keyClick(menu, Qt::Key_Right);
        for (int i = 0; i < 100 && !parent->menu()->isVisible(); ++i) QTest::qWait(10);
      }
      return parent->menu()->isVisible() ? parent->menu() : nullptr;
    };
    // Keyboard path: make the parent current and press Right — deterministic,
    // and it doubles as the "arrows/Enter still drive the menu" assertion.
    auto openSubByKey = [](QMenu* menu, const QString& title) -> QMenu* {
      QAction* parent = nullptr;
      for (QAction* a : menu->actions())
        if (a->text() == title) parent = a;
      if (!parent || !parent->menu()) return nullptr;
      menu->setActiveAction(parent);
      QTest::keyClick(menu, Qt::Key_Right);
      for (int i = 0; i < 100 && !parent->menu()->isVisible(); ++i) QTest::qWait(10);
      return parent->menu()->isVisible() ? parent->menu() : nullptr;
    };
    auto findMenu = []() -> QMenu* {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      return menu;
    };

    // ── assistant OFF: no Assistant entry at all, nothing even built ──
    bool sawAssistantWhenOff = true, sawNormalAction = false, doubleSeparator = false;
    bool styleOpenedOff = false, filterOpenedOff = false;
    int separatorsOff = 0;
    win.settings_.llmProvider = "none";
    QTimer::singleShot(0, [&] {
      QMenu* menu = findMenu();
      if (!menu) return;
      sawAssistantWhenOff = false;
      QAction* prev = nullptr;
      doubleSeparator = false;
      for (QAction* a : menu->actions()) {
        if (a->text() == "Assistant") sawAssistantWhenOff = true;
        if (a->text().contains("Fullscreen")) sawNormalAction = true;
        // Nothing dangling: the entry's trailing separator must go with it.
        if (a->isSeparator() && prev && prev->isSeparator()) doubleSeparator = true;
        if (a->isSeparator()) ++separatorsOff;
        prev = a;
      }
      styleOpenedOff = openSub(menu, "Style") != nullptr;
      filterOpenedOff = openSubByKey(menu, "Image Filter\tAlt+B") != nullptr;
      menu->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));
    QVERIFY2(!sawAssistantWhenOff, "the Assistant entry showed with the assistant off");
    QVERIFY2(sawNormalAction, "the rest of the context menu went missing");
    QVERIFY2(!win.chatMenuAction_, "the chat panel was built despite provider=none");
    QVERIFY2(styleOpenedOff && filterOpenedOff, "submenus did not open (assistant off)");
    QVERIFY2(!doubleSeparator, "the hidden Assistant entry left a dangling separator");

    // ── assistant ON ──
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    // A mock transport answers synchronously with a canned op-plan, so nothing
    // touches the network (the same seam llmClient.headless.cpp uses).
    MockChatTransport mock;
    // A real op-plan in ollama's response shape. Built through QJsonDocument
    // rather than a raw string literal — moc chokes on those (empty .moc).
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Sepia applied\","
                      "\"actions\":[{\"op\":\"filter\",\"mode\":\"sepia\"}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    // The menu's attach button feeds the DOCK's attachment state; stage one
    // there and the menu-driven turn must carry it (working image + this one).
    QImage att(12, 8, QImage::Format_RGB32);
    att.fill(Qt::green);
    win.chatDock_->addAttachmentImage(att);

    bool styleOpened = false, filterOpened = false, assistantOpened = false;
    bool tooltipByKey = false, threeButtons = false, dotOnGear = false;
    bool assistantBeforeDrawing = false, buttonsMatchDock = false, splitterResized = false;
    bool noSeparatorBelowAssistant = false;
    int separatorsOn = 0;
    QList<int> splitterSizes;
    bool hintGone = false;
    int pillRest = 0, pillHot = 0, handleWidth = 0;
    bool cursorBefore = true, cursorOnHandle = false, cursorAfter = true;
    double pillCenterX = -1, inputCenterX = -1, panelCenterX = -1;
    int chipCount = 0;
    bool chipsShownEmpty = false, chipTextsMatchDock = false, chipPrefilled = false;
    bool chipDidNotSend = false, sendGatedEmpty = false, chipsHiddenAfterSend = true;
    bool attachFrozen = false;
    int transcriptCap = 0, postedImages = 0;
    bool typedThrough = false, subAliveAfterSend = false, rootAliveAfterSend = false;
    bool sendWasStop = false, stopSeen = false, stoppedRowSeen = false;
    bool escClosedSub = false;
    int rowsAfterSend = 0;
    QString posted;
    QTimer::singleShot(0, [&] {
      QMenu* menu = findMenu();
      if (!menu) return;
      // The classic submenus keep working with the Assistant entry present:
      // hover-open for two of them, plus the keyboard path (arrows/Right) which
      // must still drive the menu while nothing has focused the chat input.
      styleOpened = openSub(menu, "Style") != nullptr;
      filterOpened = openSub(menu, "Image Filter\tAlt+B") != nullptr;

      tooltipByKey = openSubByKey(menu, "Tooltip") != nullptr;

      // Ordering: the Assistant entry belongs in the TOP group, ahead of the
      // drawing actions — not tacked on at the bottom.
      int assistantIdx = -1, startDrawIdx = -1, i = 0;
      for (QAction* a : menu->actions()) {
        if (a->text() == "Assistant") assistantIdx = i;
        if (a->text().contains("Start Drawing") || a->text().contains("Stop Drawing"))
          startDrawIdx = i;
        ++i;
      }
      assistantBeforeDrawing =
          assistantIdx >= 0 && startDrawIdx >= 0 && assistantIdx < startDrawIdx;
      // No separator directly BENEATH the entry: it sits against the drawing
      // group. (The one above, after Fit, opens the section.)
      const QList<QAction*> acts = menu->actions();
      noSeparatorBelowAssistant =
          assistantIdx >= 0 && assistantIdx + 1 < acts.size() &&
          !acts.at(assistantIdx + 1)->isSeparator();
      for (QAction* a : acts)
        if (a->isSeparator()) ++separatorsOn;

      QMenu* sub = openSub(menu, "Assistant");
      assistantOpened = sub != nullptr;
      if (!sub) { menu->close(); return; }
      auto* panel = sub->findChild<QWidget*>("chatMenuPanel");
      auto* input = sub->findChild<QPlainTextEdit*>("chatMenuInput");
      auto* sendBtn = sub->findChild<QToolButton*>("chatMenuSend");
      auto* attachBtn = sub->findChild<QToolButton*>("chatMenuAttach");
      auto* gearBtn = sub->findChild<QToolButton*>("chatMenuGear");
      if (!panel || !input || !sendBtn || !attachBtn || !gearBtn) { menu->close(); return; }
      // The dock's three composer buttons, same order: send · attach · gear,
      // with the reachability dot riding on the gear.
      threeButtons = sendBtn->x() < attachBtn->x() && attachBtn->x() < gearBtn->x();
      // …and the DOCK's exact look: same 18 px glyphs, same 26 px box, same
      // accent treatment, so the two composers read identically.
      auto* dockSend = win.chatDock_->findChild<QToolButton*>("chatSend");
      buttonsMatchDock = dockSend && sendBtn->iconSize() == dockSend->iconSize() &&
                         sendBtn->size() == dockSend->size() &&
                         sendBtn->property("chatAccent").toBool() &&
                         attachBtn->iconSize() == QSize(20, 20) &&
                         gearBtn->iconSize() == QSize(20, 20) &&
                         sendBtn->width() == sendBtn->height() &&
                         attachBtn->size() == sendBtn->size() &&
                         gearBtn->size() == sendBtn->size() &&
                         sendBtn->width() >= 30;
      // The explanatory line is gone: an empty transcript stays blank, and no
      // label outside the transcript rows explains the panel.
      hintGone = true;
      for (QLabel* l : panel->findChildren<QLabel*>())
        if (l->text().contains("replies are applied")) hintGone = false;
      // Empty state = the DOCK's suggestion chips, same texts, clickable, and
      // they PREFILL the composer rather than sending.
      if (auto* chipBox = panel->findChild<QWidget*>("chatSuggest")) {
        const auto chips = chipBox->findChildren<QPushButton*>("chatSuggestChip");
        chipCount = chips.size();
        chipsShownEmpty = chipBox->isVisible();
        const auto dockChips =
            win.chatDock_->findChildren<QPushButton*>("chatSuggestChip");
        chipTextsMatchDock = dockChips.size() == chips.size();
        for (int i = 0; chipTextsMatchDock && i < chips.size(); ++i)
          if (chips.at(i)->text() != dockChips.at(i)->text()) chipTextsMatchDock = false;
        if (!chips.isEmpty()) {
          chips.first()->click();
          chipPrefilled = input->toPlainText() == QString("Make it sepia");
          chipDidNotSend = chipBox->isVisible();  // prefill only — no message
          input->clear();
        }
      }
      // The trio is ONE set: identical box, only the enabled state differs
      // (send is gated on non-empty input, exactly like the dock's).
      sendGatedEmpty = !sendBtn->isEnabled() && attachBtn->isEnabled() &&
                       gearBtn->isEnabled() &&
                       sendBtn->size() == attachBtn->size() &&
                       attachBtn->size() == gearBtn->size() &&
                       sendBtn->iconSize() == attachBtn->iconSize() &&
                       attachBtn->iconSize() == gearBtn->iconSize() &&
                       sendBtn->property("chatAccent").toBool() &&
                       attachBtn->property("chatAccent").toBool() &&
                       gearBtn->property("chatAccent").toBool();
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
          // Centring, pinned by MEASUREMENT: the pill's painted centre must sit
          // on the INPUT column the drag resizes — not on the handle's full
          // span, which also covers the send/attach/gear cluster (that was the
          // off-centre bug: 187 px vs the input's 138 px).
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
          // Hover it the way a user does — a real move over the menu. The popup
          // grab suppresses the handle's own enter/leave, so the menu has to
          // synthesise them; this asserts that plumbing as well as the pill.
          const QPoint over = h->mapTo(sub, h->rect().center());
          // Real MouseMove events sent to the menu — QTest::mouseMove never
          // reaches a NATIVE popup, so this is the form that exercises the
          // grab path both offscreen and headed.
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
      auto* dot = sub->findChild<QLabel*>("chatMenuStatusDot");
      dotOnGear = dot && dot->parentWidget() == gearBtn;
      // Room for a reply plus a couple of exchanges without scrolling.
      // The APPLIED height, not the constant: a plain maximumHeight let the
      // scroll area collapse to its content sizeHint (~2 rows).
      if (auto* scrollArea = panel->findChild<QScrollArea*>("chatMenuTranscript"))
        transcriptCap = scrollArea->height();
      QCOMPARE(input->placeholderText(),
               QString("Ask the assistant… (Enter sends, Shift+Enter newline)"));
      // It must not take focus implicitly — menu navigation stays live until
      // the user actually clicks into the input.
      QCOMPARE(input->focusPolicy(), Qt::ClickFocus);

      // Click into the input THROUGH the menu (the real popup path): focus
      // lands there and the submenu does not close.
      QTest::mouseClick(sub, Qt::LeftButton, {},
                        input->mapTo(sub, input->rect().center()));
      // Typing goes to the INPUT, not the menu's key navigation.
      QTest::keyClicks(sub, "make it sepia");
      typedThrough = input->toPlainText() == QString("make it sepia");
      // Enter sends instead of activating the highlighted menu item.
      QTest::keyClick(sub, Qt::Key_Return);
      posted = QString::fromUtf8(QJsonDocument(mock.body).toJson(QJsonDocument::Compact));
      const QJsonArray msgs = mock.body.value("messages").toArray();
      if (!msgs.isEmpty())
        postedImages = msgs.last().toObject().value("images").toArray().size();
      subAliveAfterSend = sub->isVisible();
      rootAliveAfterSend = menu->isVisible();
      rowsAfterSend = panel->findChildren<QLabel*>().size();
      if (auto* chipBox = panel->findChild<QWidget*>("chatSuggest"))
        chipsHiddenAfterSend = !chipBox->isVisible();

      // Busy → the send button becomes STOP; clicking it THROUGH the menu
      // aborts without closing anything.
      win.chatDock_->setBusy(true);
      win.chatMirrorBusy(true);
      win.chatMirrorPending(true);
      sendWasStop = sendBtn->toolTip() == QString("Stop the response");
      attachFrozen = !attachBtn->isEnabled();  // frozen mid-turn, like the dock
      QTest::mouseClick(sub, Qt::LeftButton, {},
                        sendBtn->mapTo(sub, sendBtn->rect().center()));
      stopSeen = win.chatStopRequested_ && sub->isVisible() && menu->isVisible();
      // The canceled reply turns the in-flight row into a muted "Stopped.".
      win.chatDock_->setBusy(false);
      win.chatMirrorBusy(false);
      stencil::llm::LlmReply canceled;
      canceled.ok = false;
      canceled.failure = stencil::llm::LlmFailure::Transport;
      canceled.error = "Operation canceled";
      win.onChatReply(canceled);
      for (QLabel* l : panel->findChildren<QLabel*>())
        if (l->text().contains("Stopped.")) stoppedRowSeen = true;

      // Escape belongs to the menu even with the input focused.
      QTest::keyClick(sub, Qt::Key_Escape);
      escClosedSub = !sub->isVisible();
      menu->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));

    QVERIFY2(styleOpened, "the Image / Layout submenu stopped hover-opening");
    QVERIFY2(filterOpened, "the Image Filter submenu stopped opening");
    QVERIFY2(tooltipByKey, "keyboard navigation no longer opens a submenu");
    QVERIFY2(assistantOpened, "the Assistant submenu did not open");
    QVERIFY2(assistantBeforeDrawing,
             "the Assistant entry is not in the top group (before Start Drawing)");
    QVERIFY2(noSeparatorBelowAssistant,
             "there is still a separator directly under the Assistant entry");
    // Adding the entry must not add (or drop) a separator anywhere.
    QCOMPARE(separatorsOn, separatorsOff);
    QVERIFY2(buttonsMatchDock, "the menu composer buttons do not match the dock's");
    QVERIFY2(splitterResized, "the menu composer is not resizable");
    QVERIFY2(hintGone, "the explanatory hint line is still in the submenu");
    QCOMPARE(chipCount, 4);
    QVERIFY2(chipsShownEmpty, "no suggestion chips in the menu's empty state");
    QVERIFY2(chipTextsMatchDock, "the menu chips differ from the dock's");
    QVERIFY2(chipPrefilled, "clicking a chip did not prefill the composer");
    QVERIFY2(chipDidNotSend, "clicking a chip sent instead of prefilling");
    QVERIFY2(chipsHiddenAfterSend, "the chips survived the first message");
    QVERIFY2(sendGatedEmpty,
             "the composer trio is not one set (size/box/accent) with send merely disabled");
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
    QVERIFY2(typedThrough, "keys typed at the menu never reached the chat input");
    QVERIFY2(!posted.isEmpty() && posted.contains("make it sepia"),
             "Enter in the menu did not send through the shared LLM client");
    QVERIFY2(subAliveAfterSend && rootAliveAfterSend, "the menu closed on send");
    QVERIFY2(rowsAfterSend >= 2, "the menu transcript did not record the exchange");
    QVERIFY2(sendWasStop, "the menu send button did not become STOP while busy");
    QVERIFY2(stopSeen, "clicking STOP in the menu closed it or did not abort");
    QVERIFY2(stoppedRowSeen, "the canceled turn did not render as Stopped.");
    QVERIFY2(escClosedSub, "Escape did not close the assistant submenu");
    QVERIFY2(threeButtons, "the menu composer lost the send/attach/gear trio");
    QVERIFY2(dotOnGear, "the provider status dot is not on the menu gear");
    QVERIFY2(attachFrozen, "attach stayed live during an in-flight turn");
    QVERIFY2(transcriptCap >= 140,
             qPrintable(QString("the menu transcript renders only %1 px tall")
                            .arg(transcriptCap)));
    // Working image + its §7 edge map + the attachment staged on the dock: the
    // menu send goes through the same attachment state the attach button feeds.
    QCOMPARE(postedImages, 3);

    // ONE conversation: the turn typed in the menu is in the shared history AND
    // rendered in the dock.
    QCOMPARE(win.chatHistory_.size(), 2);  // user + assistant
    QCOMPARE(win.chatHistory_.first().text, QString("make it sepia"));
    bool dockSawIt = false;
    for (QLabel* l : win.chatDock_->findChildren<QLabel*>())
      if (l->text().contains("make it sepia")) dockSawIt = true;
    QVERIFY2(dockSawIt, "the menu turn never reached the dock transcript");

    // The panel is a QWidgetAction owned by the WINDOW, so reopening the menu
    // (rebuilt from scratch on every right-click) keeps the transcript.
    bool survived = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = findMenu();
      if (!menu) return;
      QMenu* sub = openSubByKey(menu, "Assistant");
      if (sub)
        if (auto* panel = sub->findChild<QWidget*>("chatMenuPanel"))
          for (QLabel* l : panel->findChildren<QLabel*>())
            if (l->text().contains("make it sepia")) survived = true;
      menu->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));
    QVERIFY2(survived, "reopening the menu lost the chat transcript");
    // The panel outlives the menu, so the composer size the user dragged to
    // persists for the session.
    if (auto* sp = win.chatMenuPanel_->findChild<QSplitter*>("chatMenuSplitter"))
      QCOMPARE(sp->sizes(), splitterSizes);

    // The dock's trash clears both surfaces.
    win.onChatClear();
    QVERIFY(win.chatHistory_.isEmpty());
    QVERIFY(win.chatMenuPanel_);
    // The mirrored rows scatter and then go (dock parity), so they leave on the
    // event loop — a hidden row is already on its way out and doesn't count.
    const auto menuRowsLeft = [&win] {
      for (QLabel* l : win.chatMenuPanel_->findChildren<QLabel*>())
        if (!l->isHidden() && l->text().contains("make it sepia")) return true;
      return false;
    };
    QTRY_VERIFY2(!menuRowsLeft(), "clearing the conversation left the menu transcript");

    win.llmClient_.reset();  // drop the mock before it goes out of scope
    beat();
  }

  // One conversation, two views: the dock and the context-menu panel must show
  // the SAME rows in the SAME order however the turns were sent — no
  // duplicates, nothing missing, in either direction. Covers the awkward
  // cases: chatting with the dock CLOSED (it must still have every card when
  // opened later) and the menu panel being created LATE (it renders the
  // existing history instead of starting blank).
  void chatSurfacesStayInSync() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"ok reply\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);

    // The rendered rows of each surface, as (role, body) pairs.
    auto dockRows = [&win] {
      QList<QPair<QString, QString>> rows;
      auto* scrollArea = win.chatDock_->findChild<QScrollArea*>();
      if (!scrollArea || !scrollArea->widget()) return rows;
      // Read the row's IDENTITY from the widget properties, not from the card's
      // visual structure: the bubbles carry the role as colour + side (browser
      // parity), so there is no role caption label to read.
      for (QFrame* card : scrollArea->widget()->findChildren<QFrame*>(
               QString(), Qt::FindDirectChildrenOnly)) {
        for (QLabel* l : card->findChildren<QLabel*>()) {
          const QString role = l->property("chatRole").toString();
          if (role.isEmpty()) continue;
          rows.append({role, l->property("chatBody").toString()});
          break;  // one body per card
        }
      }
      return rows;
    };
    auto menuRows = [&win] {
      QList<QPair<QString, QString>> rows;
      if (!win.chatMenuPanel_) return rows;
      for (QLabel* l : win.chatMenuPanel_->findChildren<QLabel*>()) {
        const QString role = l->property("chatRole").toString();
        if (role.isEmpty()) continue;  // the empty-state hint, not a row
        rows.append({role, l->property("chatBody").toString()});
      }
      return rows;
    };
    // Send through the menu's own input, the way a user does.
    auto sendFromMenu = [&win](const QString& text) {
      QTimer::singleShot(0, [&win, text] {
        QMenu* menu = nullptr;
        for (int i = 0; i < 200 && !menu; ++i) {
          menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
          if (!menu) QTest::qWait(10);
        }
        if (!menu) return;
        QAction* parent = nullptr;
        for (QAction* a : menu->actions())
          if (a->text() == "Assistant") parent = a;
        if (!parent || !parent->menu()) { menu->close(); return; }
        menu->setActiveAction(parent);
        QTest::keyClick(menu, Qt::Key_Right);
        QMenu* sub = parent->menu();
        for (int i = 0; i < 100 && !sub->isVisible(); ++i) QTest::qWait(10);
        if (auto* input = sub->findChild<QPlainTextEdit*>("chatMenuInput")) {
          input->setFocus();
          input->setPlainText(text);
          QTest::keyClick(sub, Qt::Key_Return);
        }
        menu->close();
      });
      win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));
    };

    // ── 1. chat in the MENU while the dock is closed ──
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(chat);
    chat->setChecked(false);
    QTRY_VERIFY(!win.chatDock_->isVisible());
    sendFromMenu("from the menu");
    QCOMPARE(win.chatHistory_.size(), 2);

    // The dock was hidden throughout, yet holds the whole exchange — opening it
    // must not need a replay.
    chat->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    // QTRY_: the dock retires its pending "…" card with deleteLater (it can be
    // mid-appear-animation), so the two renderings converge a turn later.
    QTRY_COMPARE(dockRows(), menuRows());
    QCOMPARE(dockRows().size(), 2);

    // ── 2. now send from the DOCK, with both surfaces alive ──
    auto* dockInput = win.chatDock_->findChild<QPlainTextEdit*>("chatInput");
    QVERIFY(dockInput);
    dockInput->setPlainText("from the dock");
    QTest::keyClick(dockInput, Qt::Key_Return);
    QCOMPARE(win.chatHistory_.size(), 4);
    QTRY_COMPARE(dockRows(), menuRows());      // the menu saw the dock's turn
    QCOMPARE(dockRows().size(), 4);            // no duplicates on either side
    QCOMPARE(dockRows().at(2).second, QString("from the dock"));

    // ── 3. an error and a stop reach both, in order ──
    stencil::llm::LlmReply bad;
    bad.ok = false;
    bad.failure = stencil::llm::LlmFailure::Http;
    bad.error = "boom";
    win.onChatReply(bad);
    QTRY_COMPARE(dockRows(), menuRows());
    QCOMPARE(dockRows().last().first, QString("Error"));

    win.chatDock_->showPending();
    win.chatMirrorPending(true);
    win.chatDock_->setBusy(true);
    win.chatMirrorBusy(true);
    win.chatStopRequested_ = true;
    win.chatDock_->setBusy(false);
    win.chatMirrorBusy(false);
    stencil::llm::LlmReply canceled;
    canceled.ok = false;
    canceled.failure = stencil::llm::LlmFailure::Transport;
    canceled.error = "Operation canceled";
    win.onChatReply(canceled);
    QTRY_COMPARE(dockRows(), menuRows());
    QCOMPARE(dockRows().last().second, QString("Stopped."));
    if (qEnvironmentVariableIsSet("STENCIL_GUI_SHOTS")) {
      QTest::qWait(50);
      win.chatDock_->grab().save(QString::fromLocal8Bit(qgetenv("STENCIL_GUI_SHOTS")) + "/dock-stopped.png");
    }

    // ── 4. the dock's trash button (composer row) clears both surfaces and the history ──
    auto* clearBtn = win.chatDock_->findChild<QToolButton*>("chatClear");
    QVERIFY(clearBtn);
    QTest::mouseClick(clearBtn, Qt::LeftButton);
    QVERIFY(win.chatHistory_.isEmpty());
    QTRY_COMPARE(dockRows().size(), 0);
    QCOMPARE(menuRows().size(), 0);
    // …and BOTH empty states return, chips included (dock parity).
    auto* dockChips = win.chatDock_->findChild<QWidget*>("chatSuggest");
    auto* menuChips = win.chatMenuPanel_->findChild<QWidget*>("chatSuggest");
    QVERIFY(dockChips && menuChips);
    // Both empty states return — after their wipe, not during it (clearConversation
    // holds them back for the scatter's length, so this has to be a TRY).
    QTRY_VERIFY2_WITH_TIMEOUT(!dockChips->isHidden(),
                              "the dock's chips did not come back after Clear",
                              stencil::gui::DisintegrateOverlay::kMs + 3000);
    QTRY_VERIFY2_WITH_TIMEOUT(!menuChips->isHidden(),
                              "the menu's chips did not come back after Clear",
                              stencil::gui::DisintegrateOverlay::kMs + 3000);
    QCOMPARE(menuChips->findChildren<QPushButton*>("chatSuggestChip").size(), 4);

    win.llmClient_.reset();
    beat();
  }

  // The error card's Resend glyph is NEUTRAL in both themes, never the card's own
  // red: the browser's retry is a .chat-hbtn, which sets `color: var(--text-muted)`
  // itself and does not inherit the bubble's --danger. Painted red it sat red-on-red
  // in the danger wash and barely read (user report).
  void chatErrorRetryGlyphIsNeutral() {
    for (const QString mode : {QStringLiteral("light"), QStringLiteral("dark")}) {
      MainWindow win(nullptr, false);
      win.resize(1100, 760);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      win.settings_.themeMode = mode;
      win.applyTheme();
      win.actChat_->setChecked(true);
      QTRY_VERIFY(win.chatDock_->isVisible());
      win.chatDock_->appendError(
          QStringLiteral("Could not read the assistant's plan: \"clear\" is an "
                         "editor-settings op"),
          QStringLiteral("remove this project"));
      QToolButton* retry = win.chatDock_->findChild<QToolButton*>("chatRetry");
      QVERIFY2(retry, "the error card has no Resend button");
      QVERIFY2(retry->parentWidget()->objectName() == QLatin1String("chatCardError"),
               "the Resend button is not on the error card");
      const QImage glyph = retry->icon().pixmap(14, 14).toImage();
      QVERIFY(!glyph.isNull());
      const QColor danger = stencil::gui::themePalette(mode == "dark").danger;
      int ink = 0, red = 0;
      for (int y = 0; y < glyph.height(); ++y)
        for (int x = 0; x < glyph.width(); ++x) {
          const QColor c = glyph.pixelColor(x, y);
          if (c.alpha() < 60) continue;
          ++ink;
          if (qAbs(c.red() - danger.red()) < 45 && qAbs(c.green() - danger.green()) < 45
              && qAbs(c.blue() - danger.blue()) < 45)
            ++red;
        }
      QVERIFY2(ink > 0, qPrintable(mode + ": the Resend glyph rendered nothing"));
      QVERIFY2(red == 0, qPrintable(mode + ": the Resend glyph is still danger red"));
      beat();
    }
  }

  // The destructive project action: dead when there is nothing to clear, and its
  // MENU glyph neutral — the red is the toolbar button's fill (browser
  // `.danger.btn-icon`), never a red mark on a plain menu row. It used to stay
  // enabled on an empty editor, where it only asked a question and then "cleared"
  // a canvas that was already empty.
  // A dialog opened from the MENU BAR must know which row it came out of, so
  // support::revealDialog can grow the window from there when the toolbar icon is hidden.
  // Regression: the row used to be read off QApplication::activePopupWidget() inside the
  // triggered() handler — but Qt hides the menu first, so the rect was always empty and
  // every menu-opened dialog fell back to "drops in from above".
  void menuOpenedDialogRemembersItsRow() {
    MainWindow win;
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // Any dialog action that lives on both a menu and a toolbar icon.
    QAction* act = nullptr;
    QMenu* owner = nullptr;
    for (QMenu* m : win.menuBar()->findChildren<QMenu*>()) {
      for (QAction* a : m->actions())
        if (win.popoverDialogActions_.contains(a) && a->isEnabled()) { act = a; owner = m; break; }
      if (act) break;
    }
    QVERIFY2(act && owner, "no dialog action on the menu bar to test");

    owner->popup(win.mapToGlobal(QPoint(40, 40)));
    QVERIFY(QTest::qWaitForWindowExposed(owner));
    const QRect row = owner->actionGeometry(act);
    QVERIFY(row.isValid());
    // Hover the row the way a user does, then let the menu close and the action fire.
    QTest::mouseMove(owner, row.center());
    QTest::qWait(30);
    QVERIFY2(win.menuRowAction_ == act, "the hovered row was not recorded");
    const QRect rowGlobal(owner->mapToGlobal(row.topLeft()), row.size());
    QCOMPARE(win.menuRowRect_, rowGlobal);
    // Qt hides the menu and THEN activates the action, in the same pass of the event loop.
    // The record has to still be there at that point — this is exactly what reading
    // QApplication::activePopupWidget() inside triggered() got wrong.
    owner->close();
    QVERIFY2(win.menuRowAction_ == act, "the row was forgotten before the action fired");
    // The dialog itself blocks in exec(), so drive only the handler that stamps the anchor.
    win.dialogAnchorRect_ = (win.menuRowAction_ == act) ? win.menuRowRect_ : QRect();
    QCOMPARE(win.dialogAnchorRect_, rowGlobal);
    // …and the record does not linger: the next run from an icon/shortcut is not the menu's.
    QTest::qWait(30);
    QVERIFY2(!win.menuRowAction_, "the hovered row outlived its menu");
  }

  // Destructive toolbar buttons wear the browser's `.danger.btn-icon` face: a SOLID red
  // fill with a WHITE glyph. Two things have to hold at once — the stylesheet property
  // that paints the fill, and a glyph that is not itself red (a red glyph on a red fill
  // is an empty button, which is exactly how this first shipped).
  //
  // Regression: a QToolButton re-copies its default action's icon on every
  // QEvent::ActionChanged, so the first setEnabled/setVisible out of refreshActions put
  // the menu's red glyph back and the button went blank.
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

  // A disabled toolbar icon must READ as disabled: the stylesheet greys the chip and its
  // text, but `color: MUTED` never reaches a rasterised pixmap (the browser's `.ic` gets
  // it from currentColor). themedIcon carries a faded QIcon::Disabled variant.
  //
  // Checked at 1x AND 2x. The first version of this composited the faded copy from a
  // dpr-tagged pixmap, which paints at LOGICAL size into a device-sized target — every
  // disabled glyph came out half-size in the top-left corner, invisible at 1x where the
  // two sizes coincide.
  void disabledIconsAreFadedNotShrunk() {
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
      const QIcon ic = stencil::gui::themedIcon("download", QColor("#e0e0e0"), 18, false, dpr);
      const QImage on = ic.pixmap(18, 18, QIcon::Normal).toImage();
      const QImage off = ic.pixmap(18, 18, QIcon::Disabled).toImage();
      const QString at = QString(" (at %1x)").arg(dpr);
      QCOMPARE(off.size(), on.size());
      // Same glyph, same place, same size — only the ink is lighter.
      QVERIFY2(inkBox(off) == inkBox(on),
               qPrintable(QString("disabled glyph moved/resized%1: %2 vs %3")
                              .arg(at, QDebug::toString(inkBox(off)), QDebug::toString(inkBox(on)))));
      const double a = meanAlpha(on), b = meanAlpha(off);
      QVERIFY2(b > 0.0, qPrintable("a disabled glyph must still be visible" + at));
      QVERIFY2(b < a * 0.6, qPrintable(QString("not faded%1: %2 vs %3").arg(at).arg(a).arg(b)));
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
    // A section row is the widget whose sibling is the "sectionLabel" caption.
    QHash<QToolBar*, QList<QPair<QString, int>>> byRow;
    for (QWidget* rowWidget : win.findChildren<QWidget*>()) {
      QWidget* section = rowWidget->parentWidget();
      if (!section || !section->findChild<QLabel*>("sectionLabel")) continue;
      if (rowWidget->findChild<QLabel*>("sectionLabel")) continue;   // that's the caption itself
      auto* bar = qobject_cast<QToolBar*>(section->parentWidget());
      if (!bar) continue;
      for (QWidget* c : rowWidget->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!c->isVisible() || c->height() <= 0) continue;
        if (!qobject_cast<QToolButton*>(c) && !qobject_cast<QComboBox*>(c)
            && !qobject_cast<QLineEdit*>(c) && !qobject_cast<QCheckBox*>(c)) continue;
        byRow[bar] << qMakePair(QString("%1(%2)").arg(c->metaObject()->className(), c->objectName()),
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

  // The dialog reveal must START at the icon that opened it. This checks the flight
  // itself: support::revealDialog puts a snapshot QLabel in the window and animates its
  // geometry, so the ghost's first rect IS the origin the user sees.
  //
  // Regression: only eight actions recorded an anchor, so a dialog opened from the menu
  // bar, a shortcut, or any other icon grew out of whichever of those eight was used last
  // — or out of a box above the dialog when none had been.
  void dialogRevealStartsAtTheIconThatOpenedIt() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(150);
    // ctest runs this suite with STENCIL_NO_ANIM=1 (dialogs are driven immediately), and
    // revealDialog is a no-op under it — this test is about the flight, so turn it back on.
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    // The ghost is a plain QLabel child of the window; note the ones already there.
    const auto ghosts = [&] { return win.findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly); };
    const QSet<QLabel*> before(ghosts().begin(), ghosts().end());
    // The ghost is already in flight by the time we can look, so read the animation's
    // START value — that is the box the user sees the window come out of.
    const auto flightOrigin = [&](QWidget* anchor) {
      QDialog dlg(&win);
      dlg.resize(300, 200);
      stencil::support::revealDialog(dlg, anchor, QRect());
      dlg.show();
      QTest::qWait(50);          // past the 0-timer that builds the ghost, inside the 300ms flight
      QRect from;
      for (QLabel* l : ghosts()) {
        if (before.contains(l)) continue;
        for (QPropertyAnimation* an : l->findChildren<QPropertyAnimation*>())
          if (an->propertyName() == QByteArray("geometry")) from = an->startValue().toRect();
      }
      dlg.close();
      return from;
    };
    QToolButton* icon = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->isVisible() && b->property("toolSection").isValid()) { icon = b; break; }
    QVERIFY2(icon, "no visible toolbar icon to fly out of");
    const QRect origin = flightOrigin(icon);
    QVERIFY2(origin.isValid(), "no reveal ghost was created");
    const QRect want(icon->mapTo(&win, QPoint(0, 0)), icon->size());
    QVERIFY2(origin == want, qPrintable(QString("the flight starts at %1, the icon is at %2")
                                            .arg(QDebug::toString(origin), QDebug::toString(want))));
  }

  // pickColorAnimated is the getColor drop-in behind every colour swatch: same modal
  // contract (picked colour on OK, invalid QColor on cancel) plus the revealDialog
  // flight out of the anchor icon. The suite runs with STENCIL_NO_ANIM=1, so the
  // animation is re-enabled for the origin assertion, like the reveal tests above.
  void pickColorAnimatedMatchesGetColorAndFliesFromItsAnchor() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(150);
    QToolButton* icon = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->isVisible() && b->property("toolSection").isValid()) { icon = b; break; }
    QVERIFY2(icon, "no visible toolbar icon to anchor on");
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    const auto ghosts = [&] { return win.findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly); };
    const QSet<QLabel*> before(ghosts().begin(), ghosts().end());
    // exec() blocks, so a 0-timer drives the modal: read the in-flight ghost's start
    // rect (the box the user sees the picker come out of), then pick a colour and OK.
    QRect origin;
    QTimer::singleShot(0, [&] {
      for (int i = 0; i < 200; ++i) {
        if (auto* dlg = qobject_cast<QColorDialog*>(QApplication::activeModalWidget())) {
          QTest::qWait(50);   // past the 0-timer that builds the ghost, inside the flight
          for (QLabel* l : ghosts()) {
            if (before.contains(l)) continue;
            for (QPropertyAnimation* an : l->findChildren<QPropertyAnimation*>())
              if (an->propertyName() == QByteArray("geometry")) origin = an->startValue().toRect();
          }
          dlg->setCurrentColor(QColor("#12ab34"));
          dlg->accept();
          return;
        }
        QTest::qWait(5);
      }
    });
    const QColor picked =
        stencil::support::pickColorAnimated(QColor("#ffffff"), &win, "Test colour", icon);
    QCOMPARE(picked, QColor("#12ab34"));
    const QRect want(icon->mapTo(&win, QPoint(0, 0)), icon->size());
    QVERIFY2(origin == want,
             qPrintable(QString("the picker's flight starts at %1, the anchor icon is at %2")
                            .arg(QDebug::toString(origin), QDebug::toString(want))));
    // Cancel path: reject → invalid colour, exactly the getColor contract call sites rely on.
    QTimer::singleShot(0, [] {
      for (int i = 0; i < 200; ++i) {
        if (auto* dlg = qobject_cast<QColorDialog*>(QApplication::activeModalWidget())) {
          dlg->reject();
          return;
        }
        QTest::qWait(5);
      }
    });
    const QColor cancelled =
        stencil::support::pickColorAnimated(QColor("#ffffff"), &win, "Test colour", icon);
    QVERIFY2(!cancelled.isValid(), "cancel must return an invalid QColor");
  }

  // …and the anchor that feeds it follows whatever was triggered last, including actions
  // with no icon of their own — those must CLEAR it, not leave the previous icon in place.
  void revealAnchorFollowsTheTriggeredIcon() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(60, 40, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());   // rotate needs an image to be enabled
    QTest::qWait(150);
    QAction* first = win.actRotateLeft_;
    QAction* second = win.actRotateRight_;
    QVERIFY(first && second);
    QWidget* b1 = win.buttonForAction(first);
    QWidget* b2 = win.buttonForAction(second);
    QVERIFY2(b1 && b2 && b1 != b2, "expected a distinct toolbar button per action");
    first->trigger();
    QCOMPARE(win.dialogAnchor_.data(), b1);
    second->trigger();
    QCOMPARE(win.dialogAnchor_.data(), b2);
    // An action with no toolbar icon of its own clears the anchor instead of inheriting
    // the last icon — this is the "it flew out of the wrong button" case.
    // A bare action with no toolbar icon (and no slot of its own, so nothing opens).
    auto* iconless = new QAction("Menu-only command", &win);
    win.addAction(iconless);
    win.bindRevealAnchors();     // idempotent — binds whatever is not bound yet
    win.dialogAnchor_ = b2;
    iconless->trigger();
    QVERIFY2(!win.dialogAnchor_, "an icon-less action left the previous icon as the origin");
  }

  // End-to-end: open a dialog the way a user does and read where its flight STARTS.
  // Covers both origins — an icon-backed command flies out of its icon, and a menu-only
  // one (no icon at all) flies out of the menu row that was clicked.
  void openingADialogFliesFromWhatWasClicked() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(150);
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });

    // Reads the reveal ghost's start box while the dialog is blocked in exec(), then
    // closes it so trigger() returns.
    QRect start;
    const auto watchThenClose = [&] {
      QTimer::singleShot(140, &win, [&] {
        for (QLabel* l : win.findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly))
          for (QPropertyAnimation* an : l->findChildren<QPropertyAnimation*>())
            if (an->propertyName() == QByteArray("geometry")) start = an->startValue().toRect();
        if (QWidget* modal = QApplication::activeModalWidget()) modal->close();
      });
    };

    // ── an icon-backed dialog ──
    QWidget* icon = win.buttonForAction(win.actProjects_);
    QVERIFY2(icon && icon->isVisible(), "the Projects icon is not on the toolbar");
    start = QRect();
    watchThenClose();
    win.actProjects_->trigger();
    QTest::qWait(50);
    {
      const QRect want(icon->mapTo(&win, QPoint(0, 0)), icon->size());
      QVERIFY2(start == want, qPrintable(QString("icon case: flight starts at %1, icon at %2")
                                             .arg(QDebug::toString(start), QDebug::toString(want))));
    }

    // ── a menu-only dialog: no icon, so the clicked ROW is the origin ──
    QAction* act = win.actShortcuts_;
    QVERIFY2(act && !win.buttonForAction(act), "Customize Shortcuts should have no toolbar icon");
    QMenu* help = nullptr;
    for (QMenu* m : win.menuBar()->findChildren<QMenu*>())
      if (m->actions().contains(act)) { help = m; break; }
    QVERIFY2(help, "the Help menu does not carry it");
    help->popup(win.mapToGlobal(QPoint(60, 40)));
    QVERIFY(QTest::qWaitForWindowExposed(help));
    const QRect row = help->actionGeometry(act);
    QTest::mouseMove(help, row.center());
    QTest::qWait(30);
    help->close();
    start = QRect();
    watchThenClose();
    act->trigger();
    QTest::qWait(50);
    const QRect rowInWin(win.mapFromGlobal(help->mapToGlobal(row.topLeft())), row.size());
    QVERIFY2(start == rowInWin, qPrintable(QString("menu case: flight starts at %1, row at %2")
                                               .arg(QDebug::toString(start), QDebug::toString(rowInWin))));
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
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.actFit_->isEnabled());
    QVERIFY(win.actZoomIn_->isEnabled() && win.actZoomOut_->isEnabled());
    QVERIFY2(win.zoom_->isEnabled(), "the % field stayed dead with an image loaded");
  }

  // Fit to window keeps the browser's GHOST box (#zoom-fit) instead of the sections' accent
  // fill: at the end of the ZOOM row a filled accent square read as a third zoom step, so the
  // glyph SHAPE has to do the identifying. Asserts the outline is painted and the fill is not.
  void fitToWindowIsAnOutlinedGhost() {
    MainWindow win(nullptr, false);
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(150);
    QToolButton* btn = win.zoomFitBtn_;
    QVERIFY(btn);
    QVERIFY2(btn->property("toolGhost").toBool(), "the fit button lost its ghost tag");
    QVERIFY2(btn->property("toolFill").toString().isEmpty(),
             "the fit button must not carry a section fill");
    const stencil::gui::Palette pal =
        stencil::gui::themePalette(win.paintedDark_, win.settings_.accentColor);
    const QImage im = btn->grab().toImage();
    const auto near = [](const QColor& a, const QColor& b, int tol) {
      return qAbs(a.red() - b.red()) < tol && qAbs(a.green() - b.green()) < tol
             && qAbs(a.blue() - b.blue()) < tol;
    };
    // Left edge at mid-height is the 1px outline; the interior never carries the accent.
    QVERIFY2(near(im.pixelColor(0, im.height() / 2), pal.borderMain, 24),
             "the fit button has no outline");
    QVERIFY2(!near(im.pixelColor(im.width() / 2, 3),
                   stencil::gui::accentPrimary(win.settings_.accentColor), 50),
             "the fit button is accent-filled");
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
    win.openPathFromOS(png_);
    QTRY_VERIFY(filter->isEnabled());
    const auto live = face(filter);
    QVERIFY2(dead.first != live.first, "a disabled combo keeps the live input background");
    QVERIFY2(dead.second < live.second * 0.9,
             "a disabled combo's text is as loud as a live one's");
  }

  // The blank-image card gets the browser's glass sweep on hover (layout.css ui-shimmer):
  // a light band crossing it once. Asserts the band genuinely MOVES, not just appears.
  void blankImageCardShimmersOnHover() {
    MainWindow win;
    win.resize(900, 640);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.canvas_->clearImage();
    win.refreshActions();
    QTest::qWait(1300);   // past the clear-dust hold that hides the card
    // Brightest column of the card's mid row — where the band is right now.
    const auto bandX = [&] {
      const QImage im = win.canvas_->grab().toImage();
      const QColor ground = im.pixelColor(0, im.height() / 2);   // canvas backdrop
      const auto offCard = [&](const QColor& c) {
        return qAbs(c.red() - ground.red()) + qAbs(c.green() - ground.green())
             + qAbs(c.blue() - ground.blue()) < 40;
      };
      // Sample just under the card's top edge: the glyph and label sit lower and their
      // white ink would win the brightest-pixel search every time.
      int top = -1, bottom = -1;
      for (int yy = 0; yy < im.height(); ++yy)
        if (!offCard(im.pixelColor(im.width() / 2, yy))) { if (top < 0) top = yy; bottom = yy; }
      if (top < 0) return -1;
      const int y = top + (bottom - top) / 8;
      // The card's horizontal span on that row, inset past the dashed border — which is
      // lighter than the fill and would win the search at a fixed position every time.
      int left = -1, right = -1;
      for (int x = 0; x < im.width(); ++x)
        if (!offCard(im.pixelColor(x, y))) { if (left < 0) left = x; right = x; }
      if (left < 0 || right - left < 24) return -1;
      int best = -1;
      double brightest = -1;
      for (int x = left + 6; x <= right - 6; ++x) {
        const QColor c = im.pixelColor(x, y);
        const double lum = c.redF() + c.greenF() + c.blueF();
        if (lum > brightest) { brightest = lum; best = x; }   // the band is the palest part
      }
      return best;
    };
    const QPoint c(win.canvas_->width() / 2, win.canvas_->height() / 2);
    QMouseEvent move(QEvent::MouseMove, QPointF(c), win.canvas_->mapToGlobal(c),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(win.canvas_, &move);
    QTest::qWait(150);
    const int first = bandX();
    QTest::qWait(220);
    const int second = bandX();
    QVERIFY2(second > first,
             qPrintable(QString("the sweep does not travel: %1 then %2").arg(first).arg(second)));
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

  // The desktop stays as quiet as the browser: no toast for a routine success the user can
  // already see (an image appearing, settings applying, the session restoring).
  void routineActionsDoNotToast() {
    // MainWindow's definitions are split across several TUs — scan them all.
    const QString appDir = QStringLiteral(__FILE__).section('/', 0, -3) + "/src/app";
    QString src;
    for (const QString& name : QDir(appDir).entryList({"mainWindow*.cpp", "stencilFileSync.cpp"})) {
      QFile f(appDir + '/' + name);
      QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable("cannot read " + f.fileName()));
      src += QString::fromUtf8(f.readAll());
    }
    QVERIFY2(!src.isEmpty(), "could not read the mainWindow sources");
    const QStringList banned{"Image loaded", "Image opened", "Opened from Stencil", "Opening…",
                             "Restored last session", "Session saved", "Settings saved",
                             "Assistant settings saved", "Shortcuts updated", "Blank recoloured",
                             "Crop canceled", "Blank image canceled"};
    for (const QString& msg : banned)
      QVERIFY2(!src.contains("notify_->success(\"" + msg) && !src.contains("notify_->info(\"" + msg),
               qPrintable(QString("\"%1\" has no twin in the browser — it should not toast").arg(msg)));
    // …while the ones the browser DOES show are still there.
    QVERIFY2(src.contains("Project saved"), "Project saved has a browser twin and must stay");
    QVERIFY2(src.contains("Image cropped"), "Image cropped has a browser twin and must stay");
  }

  // macOS reads our raw pixels as if they were already in the display's space, so an sRGB
  // hex paints over-saturated on a P3 Mac while the browser — which colour-manages — shows
  // the same token quieter. theme.cpp encodes into the display space; this pins the result
  // to the value Chrome actually puts on screen for --accent (#7c3aed → #743ee4, measured).
  void accentMatchesTheBrowsersRenderedColour() {
    const QColor accent = stencil::gui::accentPrimary("violet");
#ifdef Q_OS_MACOS
    QCOMPARE(accent.name(), QStringLiteral("#743ee4"));
    // Neutrals must survive untouched — both spaces share D65 and the transfer curve.
    QCOMPARE(stencil::gui::themePalette(true).bgPage.name(), QStringLiteral("#1a1a1a"));
    QCOMPARE(stencil::gui::themePalette(false).bgPage.name(), QStringLiteral("#f0f0f0"));
    // …and the whole palette moves together, not just the accent.
    QCOMPARE(stencil::gui::themePalette(false).danger.name(), QStringLiteral("#c53b43"));
#else
    QCOMPARE(accent.name(), QStringLiteral("#7c3aed"));   // identity off macOS
#endif
  }

  // The chat header reads as chrome, not an accent badge: the "Assistant" label and its
  // mark take the theme text colour, and the CURRENT placement button is an accent glyph on
  // a container chip — the browser's .chat-dock-btn-active { color: accent; background:
  // var(--bg-container) }, not a filled accent square.
  void chatHeaderMatchesTheBrowserChrome() {
    MainWindow win;
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTest::qWait(400);
    auto* dock = win.findChild<stencil::gui::ChatDock*>();
    QVERIFY2(dock, "no chat dock");
    QLabel* title = dock->findChild<QLabel*>("chatHeaderTitle");
    QVERIFY2(title, "no header title");
    const stencil::gui::Palette pal = stencil::gui::themePalette(
        stencil::gui::resolveDark(win.settings_.themeMode), win.settings_.accentColor);
    QVERIFY2(title->styleSheet().contains(pal.textMain.name()),
             qPrintable("the Assistant label is not theme text: " + title->styleSheet()));
    QVERIFY2(!title->styleSheet().contains(pal.accent.name()),
             "the Assistant label still paints in the accent");
    // The active placement chip: container ground, and never the accent fill.
    bool sawActive = false;
    for (QToolButton* b : dock->findChildren<QToolButton*>()) {
      if (b->objectName() == QLatin1String("chatJumpBtn")) continue;  // transcript jump pills, not chips
      const QString qss = b->styleSheet();
      if (!qss.contains("background:")) continue;
      sawActive = true;
      QVERIFY2(qss.contains(pal.bgContainer.name()),
               qPrintable("the active placement button is not a container chip: " + qss));
      QVERIFY2(!qss.contains(pal.accent.name()),
               qPrintable("the active placement button is still accent-filled: " + qss));
    }
    QVERIFY2(sawActive, "no placement button is marked active");
    // The glyph must actually FILL its chip. The app-wide QToolButton rule pads 5x7 and
    // reserves a border, which inside a fixed 23px button squeezed the 13px mark down to
    // ~4px — half the browser's, and the reason the row read as murky.
    {
      QWidget* bar = dock->findChild<QWidget*>("chatTitleBar");
      QVERIFY(bar);
      const QImage im = bar->grab().toImage();
      const QColor ground = im.pixelColor(im.width() - 2, 1);
      // Measured INSIDE one button's own rect, so the bold "Assistant" label cannot stand
      // in for a glyph (it did, and the negative control passed).
      QToolButton* up = nullptr;
      for (QToolButton* b : bar->findChildren<QToolButton*>())
        if (b->toolTip().startsWith("Dock top")) up = b;
      QVERIFY2(up, "no dock-top button");
      const QRect r(up->mapTo(bar, QPoint(0, 0)), up->size());
      int widest = 0, run = 0;
      for (int x = r.left(); x <= r.right(); ++x) {
        bool ink = false;
        for (int y = r.top(); y <= r.bottom() && !ink; ++y) {
          const QColor c = im.pixelColor(x, y);
          ink = qAbs(c.red() - ground.red()) + qAbs(c.green() - ground.green())
              + qAbs(c.blue() - ground.blue()) > 40;
        }
        run = ink ? run + 1 : 0;
        widest = qMax(widest, run);
      }
      QVERIFY2(widest >= 6, qPrintable(QString("the chevron is squeezed: %1px of ink in a %2px chip")
                                           .arg(widest).arg(r.width())));
    }
    // Geometry parity with .chat-hbtn: a 23px chip holding a 13px glyph, 1px apart.
    for (QToolButton* b : dock->findChildren<QToolButton*>()) {
      if (!b->toolTip().contains("Dock ") && !b->toolTip().startsWith("Float")
          && !b->toolTip().startsWith("Close")) continue;
      QCOMPARE(b->size(), QSize(23, 23));
      QCOMPARE(b->iconSize(), QSize(13, 13));
      // Never disabled: QToolButton:disabled would repaint the active chip as a dead
      // bordered square with a dimmed glyph, which is what made the row look murky.
      QVERIFY2(b->isEnabled(), qPrintable("header button is disabled: " + b->toolTip()));
      QVERIFY2(!b->styleSheet().contains("border:1px"),
               qPrintable("header button has a border: " + b->styleSheet()));
    }
  }

  // Moving the chat between sides animates: it slides out of the old edge and back in at
  // the new one (the same extent slide the icon's open/close uses). It used to jump.
  void chatPlacementChangeAnimates() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible() && !dock->isFloating());
    QTest::qWait(500);          // let the open slide settle
    const int settled = dock->width();
    QVERIFY2(settled > 80, "the dock never reached its full width");
    // Ask for the opposite side and watch the extent actually move mid-flight.
    emit static_cast<stencil::gui::ChatDock*>(dock)->dockRequested(Qt::RightDockWidgetArea);
    bool sawCollapse = false;
    for (int i = 0; i < 20 && !sawCollapse; ++i) {
      QTest::qWait(20);
      const int e = dock->isFloating() ? settled
                                       : (win.dockWidgetArea(dock) == Qt::TopDockWidgetArea
                                          || win.dockWidgetArea(dock) == Qt::BottomDockWidgetArea)
                                             ? dock->height() : dock->width();
      if (e < settled / 2) sawCollapse = true;
    }
    QVERIFY2(sawCollapse, "the dock jumped to the new side without sliding out");
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QTRY_VERIFY2(dock->width() > settled / 2, "it never grew back at the new edge");
    QTest::qWait(400);

    // …and coming back from FLOAT slides in at the side you picked, rather than
    // appearing at full width (the floating branch used to skip the animation).
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QTest::qWait(300);
    emit static_cast<stencil::gui::ChatDock*>(dock)->dockRequested(Qt::LeftDockWidgetArea);
    bool sawNarrow = false;
    for (int i = 0; i < 20 && !sawNarrow; ++i) {
      QTest::qWait(15);
      if (!dock->isFloating() && dock->width() < settled / 2) sawNarrow = true;
    }
    QVERIFY2(sawNarrow, "docking from float snapped straight to full width");
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    QTRY_VERIFY2(dock->width() > settled / 2, "it never grew in from the float");
  }

  // A FLOATING chat opens out of the toolbar icon and shrinks back into it, like every
  // dialog. It used to blink in and out — the floating branch skipped animation entirely.
  void floatingChatFliesFromItsIcon() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(dock);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QTest::qWait(300);
    QWidget* icon = win.buttonForAction(win.actChat_);
    QVERIFY2(icon && icon->isVisible(), "no chat icon to fly from");
    const QRect iconRect(icon->mapTo(&win, QPoint(0, 0)), icon->size());
    // The flight is a snapshot QLabel in the MAIN window; its geometry animation says
    // where the motion begins.
    const auto flightStart = [&]() -> QRect {
      for (QLabel* l : win.findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly))
        for (QPropertyAnimation* a : l->findChildren<QPropertyAnimation*>())
          if (a->propertyName() == QByteArray("geometry")) return a->startValue().toRect();
      return QRect();
    };
    win.actChat_->setChecked(false);          // close: starts at the window, ends at the icon
    QTest::qWait(60);
    const QRect closing = flightStart();
    QVERIFY2(closing.isValid(), "closing a floating chat did not animate");
    QVERIFY2(closing != iconRect, "the close flight should START at the window, not the icon");
    QTest::qWait(400);
    win.actChat_->setChecked(true);           // open: starts at the icon
    QTest::qWait(60);
    QCOMPARE(flightStart(), iconRect);
  }

  // The two chat surfaces must render the SAME transcript. The panel replays the
  // rows the DOCK displayed, never chatHistory_ — that is the model's view, and
  // it carries the §7 continuation note ("[The working image is now …]") plus
  // every interim round's reply, none of which is a message to a user.
  void chatPanelMirrorsExactlyWhatTheDockShows() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const QString& json) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", json}}}})
          .toJson(QJsonDocument::Compact);
    };
    // Round 1 LOADS a picture without tracing it ⇒ the §7 continuation fires and
    // the dock HOLDS this reply; round 2 is the settled answer the user sees.
    const QString interim = QStringLiteral("Loading it into incognito and cropping now.");
    const QString settled = QStringLiteral("Black & white applied, cropped to a 3:4 portrait.");
    mock.queue.append(wrap(QStringLiteral(
        "{\"version\":1,\"reply\":\"%1\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\"}]}")
                              .arg(interim)));
    mock.queue.append(wrap(QStringLiteral(
        "{\"version\":1,\"reply\":\"%1\",\"actions\":[],"
        "\"warnings\":[]}").arg(settled)));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());

    win.onChatSend(QStringLiteral("make it b&w and crop to 3:4"));
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QTest::qWait(300);

    // The panel opens AFTER the turn — the lazy replay is where it used to
    // diverge. Show it so its rows lay out.
    win.ensureChatMenuPanel();
    win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel_->show();
    QTest::qWait(250);

    const auto rowsOf = [](QWidget* surface) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QStringList out;
      // Bodies AND the note lines that ride inside a card — the whole displayed
      // content, so a warning rendered as an extra row instead of an in-card note
      // shows up as a difference.
      for (QLabel* l : surface->findChildren<QLabel*>()) {
        const QString b = l->property("chatBody").toString();
        const QString n = l->property("chatNote").toString();
        if (!b.isEmpty() && b != QStringLiteral("…")) out << b;
        else if (!n.isEmpty()) out << n;
      }
      return out;
    };
    const QStringList dockRows = rowsOf(win.chatDock_);
    const QStringList panelRows = rowsOf(win.chatMenuPanel_);
    QCOMPARE(panelRows, dockRows);   // same rows, same order, same count

    // The internal continuation text is a message to the MODEL, never to a user.
    for (const QStringList& rows : {dockRows, panelRows})
      for (const QString& r : rows)
        QVERIFY2(!r.contains(QStringLiteral("The working image is now")),
                 qPrintable("the §7 continuation note reached a transcript: " + r));
    // …and the held interim reply is shown by neither, while the settled one is.
    QVERIFY2(!dockRows.contains(interim) && !panelRows.contains(interim),
             "the interim round-1 reply must not be displayed");
    QVERIFY2(dockRows.contains(settled) && panelRows.contains(settled),
             "the settled reply is missing");
    // It IS in the model's history — that is the point of the two being different.
    bool inHistory = false;
    for (const auto& m : win.chatHistory_)
      if (m.text.contains(QStringLiteral("The working image is now"))) inHistory = true;
    QVERIFY2(inHistory, "the continuation note should still reach the model");

    // ── warnings and an error card land in both, identically ──
    const QStringList warn{QStringLiteral("Skipped unknown op \"wobble\".")};
    win.chatDock_->appendAssistant(QStringLiteral("with a warning"), warn, {});
    win.chatMirror(QStringLiteral("Assistant"),
                   MainWindow::withChatWarnings(QStringLiteral("with a warning"), warn), false);
    win.chatHistory_.append({QStringLiteral("user"), QStringLiteral("again"), {}});
    win.chatError(QStringLiteral("Could not read the assistant's plan: bad op"), QString());
    QTest::qWait(200);
    QCOMPARE(rowsOf(win.chatMenuPanel_), rowsOf(win.chatDock_));

    // ── and the clear path empties both ──
    win.onChatClear();
    win.chatDock_->clearConversation();
    QTRY_VERIFY(rowsOf(win.chatDock_).isEmpty());
    QTRY_VERIFY(rowsOf(win.chatMenuPanel_).isEmpty());
    win.llmClient_.reset();
    beat();
  }

  // The same contract with BOTH surfaces up the whole time, across a §7
  // continuation, warnings, an error card, a late note — and a §12 restore of
  // the saved conversation. The saved doc is the MODEL's view: it carries the
  // continuation note and the interim reply the dock deliberately never showed,
  // so restoring it verbatim put internal text on screen (and only the surface
  // that replayed it, once the two were fed from different places).
  void chatPanelAndDockAgreeAcrossTurnsAndRestore() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const QString& json) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", json}}}})
          .toJson(QJsonDocument::Compact);
    };
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    // Round 1 of the §7 turn: the reply the dock HOLDS (the continuation's
    // settled answer replaces it), so no surface may ever show it.
    const QString interim = QStringLiteral(
        "Loading it into incognito, converting to black & white and cropping to portrait now.");
    // Side by side from the start, the way the report had them.
    win.ensureChatMenuPanel();
    win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel_->show();

    // Every CARD, in order: its kind plus every text it shows (body + the notes
    // riding inside it). Comparing this catches a missing row, an extra row, a
    // note rendered as its own card, and a differing body — all at once.
    const auto cardsOf = [](QWidget* surface) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QStringList out;
      for (QFrame* f : surface->findChildren<QFrame*>()) {
        const QString kind = f->objectName();
        if (!kind.startsWith(QLatin1String("chatCard")) ||
            kind == QLatin1String("chatCardMore"))
          continue;
        QStringList texts;
        for (QLabel* l : f->findChildren<QLabel*>()) {
          const QString b = l->property("chatBody").toString();
          const QString n = l->property("chatNote").toString();
          if (!b.isEmpty()) texts << b;
          else if (!n.isEmpty()) texts << n;
        }
        if (texts.size() == 1 && texts.first() == QStringLiteral("…")) continue;  // pending
        out << kind + QStringLiteral(": ") + texts.join(QStringLiteral(" ¶ "));
      }
      return out;
    };
    const auto same = [&](const char* what) {
      QTest::qWait(120);
      QVERIFY2(cardsOf(win.chatMenuPanel_) == cardsOf(win.chatDock_),
               qPrintable(QStringLiteral("%1: the two surfaces disagree\n  dock : %2\n  panel: %3")
                              .arg(QString::fromLatin1(what),
                                   cardsOf(win.chatDock_).join(QStringLiteral(" | ")),
                                   cardsOf(win.chatMenuPanel_).join(QStringLiteral(" | ")))));
    };
    const auto noInternalText = [&](const char* what) {
      for (QWidget* s : {static_cast<QWidget*>(win.chatDock_), win.chatMenuPanel_})
        for (const QString& row : cardsOf(s)) {
          QVERIFY2(!row.contains(QStringLiteral("The working image is now")),
                   qPrintable(QStringLiteral("%1: the §7 continuation note is displayed: %2")
                                  .arg(QString::fromLatin1(what), row)));
          QVERIFY2(!row.contains(interim),
                   qPrintable(QStringLiteral("%1: the held interim reply is displayed: %2")
                                  .arg(QString::fromLatin1(what), row)));
        }
    };

    // ── 1. a §7 continuation turn: ONE settled bubble on both surfaces ──
    mock.queue.append(wrap(QStringLiteral(
        "{\"version\":1,\"reply\":\"%1\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\"}]}")
                              .arg(interim)));
    const QString settled = QStringLiteral(
        "Black & white applied, cropped to a 3:4 portrait, and I traced the hair silhouette.");
    mock.queue.append(wrap(
        QStringLiteral("{\"version\":1,\"reply\":\"%1\",\"actions\":[]}").arg(settled)));
    win.onChatSend(QStringLiteral("make it b&w and crop to 3:4"));
    QTRY_VERIFY(!win.chatDock_->isBusy());
    same("continuation turn");
    noInternalText("continuation turn");
    QCOMPARE(cardsOf(win.chatDock_).size(), 2);   // the ask + the ONE settled reply

    // ── 2. the §12 round trip: saved conversation, reopened project ──
    const QJsonObject doc = win.buildActiveChatDoc();
    const QByteArray json = QJsonDocument(doc).toJson();
    QVERIFY2(!json.contains("The working image is now"),
             "the persisted doc must not carry the §7 continuation note (§12.1)");
    QVERIFY2(!json.contains(interim.toUtf8()),
             "the persisted doc must not carry the held interim reply (§12.1)");
    bool noteInHistory = false;   // the MODEL's view is untouched by the sanitising
    for (const auto& m : win.chatHistory_)
      if (m.text.contains(QStringLiteral("The working image is now"))) noteInHistory = true;
    QVERIFY2(noteInHistory, "the continuation note must still reach the model");
    win.restoreChatFromDoc(doc);
    QTRY_COMPARE(cardsOf(win.chatDock_).size(), 2);   // the wipe finished, 2 rows came back
    same("restored conversation");
    noInternalText("restored conversation");
    QVERIFY2(cardsOf(win.chatDock_).join(QChar('\n')).contains(settled),
             "the settled reply must survive the restore");

    // ── 3. warnings fold into the reply's own bubble on both ──
    mock.queue.append(wrap(QStringLiteral(
        "{\"version\":1,\"reply\":\"tinted\",\"actions\":[{\"op\":\"filter\",\"mode\":\"bw\"},"
        "{\"op\":\"wobble\"}]}")));
    win.onChatSend(QStringLiteral("make it grey"));
    QTRY_VERIFY(!win.chatDock_->isBusy());
    same("warnings turn");
    QVERIFY2(cardsOf(win.chatDock_).join(QChar('\n')).contains(QStringLiteral("wobble")),
             "the skipped-op warning is missing");

    // ── 4. an error card (a plan that will not validate) ──
    mock.queue.append(wrap(QStringLiteral(
        "{\"version\":1,\"reply\":\"here\",\"actions\":[{\"op\":\"blank\",\"color\":\"nope\"}]}")));
    win.onChatSend(QStringLiteral("break it"));
    QTRY_VERIFY(!win.chatDock_->isBusy());
    same("error turn");
    QVERIFY2(cardsOf(win.chatMenuPanel_).join(QChar('\n'))
                 .contains(QStringLiteral("chatCardError: Could not read")),
             "the panel is missing the error card");

    // ── 5. the late notes the §3 chain and the text-only retry report ──
    win.chatLateNote(QStringLiteral("The layout self-check kept the lines."));
    win.chatNote(QStringLiteral("This model is text-only — the image was not sent."));
    same("late notes");
    // …and a note the DOCK posts on its own (the attachment cap) is mirrored too.
    win.chatDock_->warnAttachmentCap();
    same("dock-posted note");

    // ── 6. clearing empties both ──
    win.onChatClear();
    win.chatDock_->clearConversation();
    QTRY_VERIFY(cardsOf(win.chatDock_).isEmpty());
    QTRY_VERIFY(cardsOf(win.chatMenuPanel_).isEmpty());
    win.llmClient_.reset();
    beat();
  }

  // A 401 from the SERVER provider is an expired session, not a broken assistant:
  // the card says which server and carries a labelled "Reconnect to <host>" that
  // opens Connections. A local provider's 401 stays an ordinary error card.
  void chatExpiredSessionCardOffersReconnect() {
    MainWindow win(nullptr, false);
    win.resize(1200, 820);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    win.chatHistory_.append({QStringLiteral("user"), QStringLiteral("crop it"), {}});

    stencil::llm::LlmReply expired;
    expired.ok = false;
    expired.failure = stencil::llm::LlmFailure::Expired;
    expired.expiredHost = QStringLiteral("localhost:8090");
    expired.error = QStringLiteral(
        "Your session on localhost:8090 has expired — reconnect to that server, then "
        "send this again.");
    win.onChatReply(expired);
    QTest::qWait(150);

    QFrame* card = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardError")) card = f;
    QVERIFY2(card, "no error card for the expired session");
    bool saidIt = false;
    for (QLabel* l : card->findChildren<QLabel*>())
      if (l->property("chatBody").toString().contains(QStringLiteral("has expired")) &&
          l->property("chatBody").toString().contains(QStringLiteral("localhost:8090")))
        saidIt = true;
    QVERIFY2(saidIt, "the card does not name the server or say the session expired");
    auto* cta = card->findChild<QPushButton*>(QStringLiteral("chatReconnectCta"));
    QVERIFY2(cta, "no Reconnect CTA on the expired card");
    QCOMPARE(cta->text(), QStringLiteral("Reconnect to localhost:8090"));
    QVERIFY2(card->findChild<QToolButton*>("chatRetry"),
             "the turn should still be resendable after signing in");

    // The CTA opens Connections (dismissed straight away here).
    bool opened = false;
    QTimer::singleShot(0, [&opened] {
      for (int i = 0; i < 80; ++i) {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
          opened = true;
          d->reject();
          return;
        }
        QTest::qWait(5);
      }
    });
    cta->click();
    QTest::qWait(60);
    QVERIFY2(opened, "the CTA did not open Connections");

    // …and an ordinary failure keeps the plain card (no CTA).
    stencil::llm::LlmReply plain;
    plain.ok = false;
    plain.failure = stencil::llm::LlmFailure::Http;
    plain.error = QStringLiteral("localhost:11434 answered: HTTP 401");
    win.onChatReply(plain);
    QTest::qWait(120);
    QFrame* last = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardError")) last = f;
    QVERIFY(last);
    QVERIFY2(!last->findChild<QPushButton*>(QStringLiteral("chatReconnectCta")),
             "a local provider's 401 must not offer a server reconnect");
    beat();
  }

  // A result that lands with no chat surface to show it must still reach the
  // user: a toast, plus an unread mark on the chat icon. The three gaps this
  // pins — a turn finishing DURING the close slide (the dock stays isVisible()
  // for 260ms), a turn owned by the context-menu panel, and the §3.2/§3.1 chain
  // finishing after the reply was announced — were all silent.
  void chatToastAndUnreadCoverEveryClosedState() {
    MainWindow win(nullptr, false);
    win.resize(1200, 820);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    const auto toast = [&]() -> QWidget* {
      return win.chatToast_ && win.chatToast_->isVisible() ? win.chatToast_ : nullptr;
    };
    const auto unreadShown = [&] {
      for (QLabel* d : win.findChildren<QLabel*>(QStringLiteral("chatUnreadDot")))
        if (d->isVisible()) return true;
      return false;
    };

    // Closed chat: the predicate says "nobody can see this".
    QVERIFY(!win.chatDock_->isVisible());
    QVERIFY2(win.chatSurfaceHidden(), "a closed chat should count as hidden");
    win.showChatToast(QStringLiteral("Assistant finished — done"), true);
    QVERIFY2(toast(), "no toast with the chat closed");
    QVERIFY2(unreadShown(), "no unread mark on the chat icon");

    // Opening clears the mark, and nothing toasts while the chat is up.
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    QVERIFY2(!unreadShown(), "the unread mark outlived the open");
    QVERIFY2(!win.chatSurfaceHidden(), "an open chat must not count as hidden");

    // ITEM A — mid-close: the dock is still isVisible() during its slide, but a
    // result landing then has nowhere to go, so it counts as hidden.
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    win.actChat_->setChecked(false);
    QVERIFY2(win.chatDock_->isVisible(), "the close should still be animating");
    QVERIFY2(win.chatSurfaceHidden(), "a chat mid-close must count as hidden");
    if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim);
    QTRY_VERIFY(!win.chatDock_->isVisible());

    // ITEM C — the context-menu panel is a chat surface too.
    win.ensureChatMenuPanel();
    win.chatMenuPanel_->setGeometry(20, 20, 340, 620);
    win.chatMenuPanel_->show();
    QTest::qWait(80);
    QVERIFY2(!win.chatSurfaceHidden(), "a visible menu panel must count as a surface");
    win.chatMenuPanel_->hide();
    QTest::qWait(80);
    QVERIFY2(win.chatSurfaceHidden(), "a dismissed menu panel leaves nothing to look at");

    // ITEM B — §3.0: settling a turn is not itself an event. Nothing runs after
    // the reply, so the terminal has no news of its own to toast.
    if (win.chatToast_) win.chatToast_->hide();
    win.setChatUnread(false);
    win.chatTurnSettled();
    QVERIFY2(!toast(), "the turn terminal must be silent — nothing runs after the reply");
    QVERIFY2(!unreadShown(), "…and it must not mark anything unread");
    beat();
  }

  // Incognito shows INLINE on the image-size line (browser parity: an accent, bold
  // "<icon> Incognito — not saved" tag beside the size, behind a muted "|" divider),
  // in both the loaded and the empty state, and only while incognito is on. The glyph
  // is the app's own themed incognito icon, never an emoji. The "?" hint keeps its own
  // bubble — this is an addition, not a replacement.
  void incognitoTagRidesTheImageSizeLine() {
    MainWindow win(nullptr, false);
    win.resize(1200, 820);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QLabel* info = win.imageSizeInfo_;
    QVERIFY(info);
    const QString tag = QStringLiteral("Incognito");

    // Empty editor, incognito OFF: the plain hint, no tag.
    QVERIFY(!info->text().contains(tag));
    QCOMPARE(info->textFormat(), Qt::PlainText);

    // Empty editor, incognito ON: the tag rides beside "No image loaded".
    win.actIncognito_->setChecked(true);
    QTRY_VERIFY2(info->text().contains(tag), "no incognito tag on the empty line");
    QVERIFY2(info->text().contains(QStringLiteral("No image loaded")),
             "the empty-state text was replaced instead of extended");
    QCOMPARE(info->textFormat(), Qt::RichText);
    const QColor accent =
        stencil::gui::themePalette(stencil::gui::resolveDark(win.settings_.themeMode),
                                   win.settings_.accentColor).accent;
    QVERIFY2(info->text().contains(accent.name()), "the tag is not accent-coloured");
    QVERIFY2(info->text().contains(QStringLiteral("font-weight:700")), "the tag is not bold");

    // The divider + the real icon, in whichever state the line is in.
    const QString muted =
        stencil::gui::themePalette(stencil::gui::resolveDark(win.settings_.themeMode),
                                   win.settings_.accentColor).textMuted.name();
    const auto checkTagChrome = [&](const char* state) {
      const QString html = info->text();
      QVERIFY2(html.contains(QStringLiteral("<span style=\"color:%1;\">|</span>").arg(muted)),
               qPrintable(QString("%1: no muted | divider before the tag").arg(state)));
      QVERIFY2(html.indexOf(QLatin1Char('|')) < html.indexOf(tag),
               qPrintable(QString("%1: the divider must sit BETWEEN the two facts").arg(state)));
      QVERIFY2(!html.contains(QString::fromUtf8("\xF0\x9F\x95\xB6")),
               qPrintable(QString("%1: the sunglasses EMOJI is still there").arg(state)));
      QVERIFY2(html.contains(QStringLiteral("<img src=\"data:image/png;base64,")),
               qPrintable(QString("%1: the tag carries no inline icon").arg(state)));
      QVERIFY2(html.contains(QStringLiteral("vertical-align:middle")),
               qPrintable(QString("%1: the glyph is not vertically centred").arg(state)));
      // …and it is a real, non-empty raster of the app's own incognito glyph.
      const QImage sent = pngOf(html);
      QVERIFY2(!sent.isNull() && sent.width() >= 12,
               qPrintable(QString("%1: the inline icon did not decode").arg(state)));
      QVERIFY2(hasInk(sent), qPrintable(QString("%1: the inline icon is blank").arg(state)));
    };
    checkTagChrome("empty");

    // …with an image loaded it sits beside the size.
    QImage pic(320, 240, QImage::Format_RGB32);
    pic.fill(Qt::darkCyan);
    win.canvas_->loadFromImage(pic);
    QTRY_VERIFY(win.canvas_->hasImage());
    win.updateImageSizeInfo();
    QVERIFY2(info->text().contains(QStringLiteral("Image Size:")) &&
                 info->text().contains(QString::number(win.canvas_->imageWidth())),
             "the size left the line");
    QVERIFY2(info->text().contains(tag), "no incognito tag beside the size");
    checkTagChrome("loaded");

    // …and it goes when incognito does — divider included, so a plain line never
    // ends in a dangling separator. The "?" bubble still carries the fact.
    win.actIncognito_->setChecked(false);
    QTRY_VERIFY2(!info->text().contains(tag), "the tag outlived incognito");
    QCOMPARE(info->textFormat(), Qt::PlainText);
    QVERIFY2(!info->text().contains(QLatin1Char('|')), "a divider survived the tag");
    QVERIFY2(!info->text().contains(QStringLiteral("<img")), "an icon survived the tag");
    win.actIncognito_->setChecked(true);
    QTRY_VERIFY(win.statusHint_->toolTip().contains(tag));
    win.actIncognito_->setChecked(false);

    // The glyph is rasterised for the SCREEN it will be shown on: at dpr 2 the same
    // 16 px element carries a 32 px PNG (offscreen runs at 1x, so pass the ratio in —
    // the Retina path would otherwise never be exercised here).
    for (const qreal dpr : {qreal(1), qreal(2)}) {
      const QString html =
          stencil::gui::inlineIconHtml(QStringLiteral("incognito"), accent, 16, QString(), dpr);
      QVERIFY(html.contains(QStringLiteral("width=\"16\"")));
      QCOMPARE(pngOf(html).width(), qRound(16 * dpr));
    }
    QVERIFY2(stencil::gui::inlineIconHtml(QStringLiteral("no-such-glyph"), accent, 16).isEmpty(),
             "an unknown glyph must degrade to nothing, not to a broken <img>");
    beat();
  }


  // The incognito indicator is DECOR: toggling it must not move, resize or reflow a
  // single other widget. It did — the inline glyph made the info line's box 2 px taller,
  // the info toolbar follows its only widget, and everything below it (canvas viewport,
  // points panel, the rows under them) dropped by those 2 px on every toggle (user
  // report: "the points panel jumps down a little", and the canvas with it).
  void incognitoToggleMovesNothing() {
    for (const QString& mode : {QStringLiteral("light"), QStringLiteral("dark")}) {
      MainWindow win(nullptr, false);
      win.resize(1200, 820);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      win.settings_.themeMode = mode;
      win.applyTheme();
      QTest::qWait(150);
      QVERIFY(win.actIncognito_ && !win.actIncognito_->isChecked());
      // With the assistant OPEN, so the dock is a real on-screen neighbour of the
      // canvas rather than a hidden widget whose geometry means nothing.
      win.actChat_->setChecked(true);
      QTRY_VERIFY(win.chatDock_->isVisible());
      QTest::qWait(300);

      // Every widget the tag could possibly push around, in window coordinates.
      const auto snapshot = [&win] {
        QMap<QString, QRect> out;
        // Only what is actually ON SCREEN: a hidden widget has no geometry to
        // disturb, and Qt re-lays hidden docks whenever it likes.
        const auto add = [&](const QString& name, QWidget* w) {
          if (w && w->isVisible()) out.insert(name, QRect(w->mapTo(&win, QPoint(0, 0)), w->size()));
        };
        add(QStringLiteral("canvas viewport"), win.scroll_->viewport());
        add(QStringLiteral("canvas"), win.canvas_);
        add(QStringLiteral("points panel"), win.selPanel_);
        add(QStringLiteral("chat dock"), win.chatDock_);
        add(QStringLiteral("coord readout"), win.status_);
        for (QToolBar* tb : win.findChildren<QToolBar*>())
          add(QStringLiteral("toolbar ") + tb->objectName(), tb);
        return out;
      };
      const auto same = [&](const QMap<QString, QRect>& a, const QMap<QString, QRect>& b,
                            const QString& what) {
        QCOMPARE(a.keys(), b.keys());
        for (auto it = a.cbegin(); it != a.cend(); ++it) {
          const QRect& was = it.value();
          const QRect& now = b.value(it.key());
          QVERIFY2(was == now,
                   qPrintable(QString("%1: %2 moved %3,%4 %5x%6 -> %7,%8 %9x%10")
                                  .arg(what, it.key())
                                  .arg(was.x()).arg(was.y()).arg(was.width()).arg(was.height())
                                  .arg(now.x()).arg(now.y()).arg(now.width()).arg(now.height())));
        }
      };

      for (const bool loaded : {false, true}) {
        if (loaded) {
          QImage pic(376, 501, QImage::Format_RGB32);
          pic.fill(QColor("#2a6f97"));
          win.canvas_->loadFromImage(pic);
          QTRY_VERIFY(win.canvas_->hasImage());
          win.updateImageSizeInfo();
        }
        QTest::qWait(150);
        const QString state = QStringLiteral("%1/%2").arg(mode, loaded ? "loaded" : "empty");
        const QMap<QString, QRect> before = snapshot();
        const int hintBefore = win.imageSizeInfo_->sizeHint().height();

        win.actIncognito_->setChecked(true);
        QTest::qWait(150);
        QVERIFY2(win.imageSizeInfo_->text().contains(QStringLiteral("Incognito")),
                 qPrintable(state + ": the tag never appeared — the check would be vacuous"));
        same(before, snapshot(), state + " on");
        QCOMPARE(win.imageSizeInfo_->sizeHint().height(), hintBefore);

        win.actIncognito_->setChecked(false);
        QTest::qWait(150);
        QVERIFY(!win.imageSizeInfo_->text().contains(QStringLiteral("Incognito")));
        same(before, snapshot(), state + " off again");
        QCOMPARE(win.imageSizeInfo_->sizeHint().height(), hintBefore);
      }
    }
    beat();
  }

  // "＋ Blank image" is a BUTTON, not the whole empty page. A left-click anywhere on the
  // empty canvas used to create a blank image — the card was only the drawing that
  // advertised it (user report), and the hand cursor covered the whole area too. Only
  // the card's own rect clicks, and only over it is the cursor a hand.
  void blankImageCardIsTheOnlyClickTarget() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    CanvasWidget* canvas = win.canvas_;
    QVERIFY(canvas && !canvas->hasImage());
    canvas->grab();   // painting the card is what computes its rect
    const QRect cardGlobal = canvas->idleCardGlobalRect();
    QVERIFY2(cardGlobal.isValid(), "the idle card was never painted");
    const QRect card(canvas->mapFromGlobal(cardGlobal.topLeft()), cardGlobal.size());
    QVERIFY2(canvas->rect().contains(card), "the card must sit inside the canvas");

    // Any creator dialog that opens is closed at once (it would block on exec), and
    // counted — a dialog appearing IS the observable "it created a blank image" step.
    int dialogs = 0;
    QTimer watchdog;
    connect(&watchdog, &QTimer::timeout, &win, [&] {
      if (QWidget* modal = QApplication::activeModalWidget()) {
        ++dialogs;
        modal->close();
      }
    });
    watchdog.start(20);

    QSignalSpy asked(canvas, &CanvasWidget::blankImageRequested);
    const auto pressAt = [&](const QPoint& p) {
      QMouseEvent press(QEvent::MouseButtonPress, QPointF(p), canvas->mapToGlobal(p),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(canvas, &press);
      QTest::qWait(60);
    };
    const auto moveTo = [&](const QPoint& p) {
      QMouseEvent move(QEvent::MouseMove, QPointF(p), canvas->mapToGlobal(p), Qt::NoButton,
                       Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(canvas, &move);
    };

    // ── OUTSIDE the card: bare page, in every direction that exists ──
    QList<QPoint> outside;
    for (const QPoint& p : {QPoint(4, 4),
                            QPoint(canvas->width() - 4, 4),
                            QPoint(4, canvas->height() - 4),
                            QPoint(canvas->width() - 4, canvas->height() - 4),
                            QPoint(canvas->width() / 2, card.top() - 12),
                            QPoint(card.left() - 12, card.center().y()),
                            QPoint(card.right() + 12, card.center().y()),
                            QPoint(canvas->width() / 2, card.bottom() + 12)})
      if (canvas->rect().contains(p) && !card.contains(p)) outside << p;
    QVERIFY2(outside.size() >= 4, "not enough bare-canvas points to test");
    for (const QPoint& p : outside) {
      moveTo(p);
      QVERIFY2(canvas->cursor().shape() != Qt::PointingHandCursor,
               qPrintable(QString("hand cursor on bare canvas at %1,%2").arg(p.x()).arg(p.y())));
      pressAt(p);
      QVERIFY2(asked.isEmpty(),
               qPrintable(QString("a click on bare canvas at %1,%2 asked for a blank image")
                              .arg(p.x()).arg(p.y())));
      QVERIFY2(dialogs == 0, "a click on bare canvas opened the blank-image creator");
      QVERIFY2(!canvas->hasImage(), "a click on bare canvas created an image");
    }

    // ── ON the card: the button works, cursor and all ──
    moveTo(card.center());
    QCOMPARE(canvas->cursor().shape(), Qt::PointingHandCursor);
    pressAt(card.center());
    QCOMPARE(asked.size(), 1);
    QTRY_VERIFY2(dialogs >= 1, "clicking the card did not open the blank-image creator");
    // …and its edges belong to it too (one pixel inside each corner).
    asked.clear();
    pressAt(card.topLeft() + QPoint(2, 2));
    QCOMPARE(asked.size(), 1);
    watchdog.stop();
    QTest::qWait(50);
    beat();
  }

  // The bottom-bar coordinate readout must follow the cursor over the image —
  // in every state the user can be in. A frozen readout reads as a frozen app.
  void coordReadoutFollowsTheCursor() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTest::qWait(200);
    QVERIFY(win.status_);

    // Synthesize a real hover over the canvas and read the status bar.
    const auto hoverAt = [&](const QPoint& p) {
      QMouseEvent move(QEvent::MouseMove, QPointF(p), canvas->mapToGlobal(p), Qt::NoButton,
                       Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(canvas, &move);
      QTest::qWait(20);
      return win.status_->text();
    };
    const auto readoutMoves = [&](const char* state) {
      const QString a = hoverAt(QPoint(canvas->width() / 3, canvas->height() / 3));
      const QString b = hoverAt(QPoint(canvas->width() * 2 / 3, canvas->height() * 2 / 3));
      QVERIFY2(a.contains(QLatin1String("Pixel (")),
               qPrintable(QString("%1: the readout is not showing coordinates (%2)")
                              .arg(QLatin1String(state), a)));
      QVERIFY2(a != b, qPrintable(QString("%1: the readout did not follow the cursor (%2)")
                                      .arg(QLatin1String(state), a)));
    };

    readoutMoves("plain");

    win.actIncognito_->setChecked(true);   // the state the report came from
    QTest::qWait(80);
    readoutMoves("incognito");
    win.actIncognito_->setChecked(false);

    win.actChat_->setChecked(true);        // …with the chat open over the layout
    QTRY_VERIFY(win.chatDock_->isVisible());
    QTest::qWait(200);
    readoutMoves("chat open");
    win.actChat_->setChecked(false);
    QTest::qWait(200);

    // COMPARE: the canvas is read-only there, but the readout is information,
    // not editing — it must keep following the cursor (it used to stop dead,
    // which is exactly what "the app is frozen" looked like).
    for (const char* mode : {"vertical", "horizontal"}) {
      win.setCompareModeUi(QString::fromLatin1(mode));
      QTest::qWait(120);
      QVERIFY2(win.canvas_->compareReadOnly(), "compare did not engage");
      readoutMoves(mode);
    }
    win.setCompareModeUi(QStringLiteral("none"));
    beat();
  }

  // COMPARE + hover tooltip: hovering a point (or a line) still labels its coordinates
  // while comparing — but ONLY over the half showing the EDITED image, the only place
  // the layout is drawn. Behind the "before" half, or in "original" (no layout at all),
  // there is nothing on screen to point at, so no tooltip.
  void compareTooltipOnlyOverTheEditedHalf() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTest::qWait(150);
    win.settings_.tooltipEnabled = true;    // independent of the machine's saved settings
    win.settings_.tooltipShowScreen = true;

    // One horizontal line across the 240x160 picture: a point in each half of a
    // centred vertical split, the segment between them crossing the divider.
    stencil::core::Line line;
    line.points = {{60, 100}, {180, 100}};
    canvas->setLines({line});
    const double s = canvas->scale();
    QLabel* body = win.tooltip_->findChild<QLabel*>();
    QVERIFY(body);

    // Hover an image-space spot; report what the tooltip says (empty = hidden).
    const auto hoverText = [&](double ix, double iy) {
      const QPoint p(qRound(ix * s), qRound(iy * s));
      QMouseEvent move(QEvent::MouseMove, QPointF(p), canvas->mapToGlobal(p), Qt::NoButton,
                       Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(canvas, &move);
      QTest::qWait(20);
      return win.tooltip_->isVisible() ? body->text() : QString();
    };
    const auto compareAt = [&](const char* mode, double split) {
      win.setCompareModeUi(QString::fromLatin1(mode));
      canvas->setCompareSplit(split);
      QTest::qWait(60);
    };

    // Baseline (compare off): BOTH points and the segment between them are labelled —
    // without this the "hidden" assertions below would pass on a tooltip that never shows.
    const QString leftOff = hoverText(60, 100), rightOff = hoverText(180, 100);
    QVERIFY2(leftOff.contains("Pixel") && leftOff.contains("60, 100"), qPrintable(leftOff));
    QVERIFY2(rightOff.contains("Pixel") && rightOff.contains("180, 100"), qPrintable(rightOff));
    const QString lineOff = hoverText(150, 100);   // mid-segment, off both points
    QVERIFY2(!lineOff.contains("Pixel") && lineOff.contains("60, 100 px") &&
                 lineOff.contains("180, 100 px"),
             qPrintable("line hover should list its endpoints: " + lineOff));

    // Vertical split at the middle (divider = image x 120): the right point and the
    // right stretch of the line keep their tooltip, the left ones lose it.
    compareAt("vertical", 0.5);
    QVERIFY(canvas->compareReadOnly());
    QVERIFY2(hoverText(180, 100).contains("180, 100"), "the visible point lost its tooltip");
    QVERIFY2(hoverText(60, 100).isEmpty(), "a point behind the original half was labelled");
    QVERIFY2(hoverText(150, 100).contains("px"), "the visible line lost its tooltip");
    QVERIFY2(hoverText(90, 100).isEmpty(), "a line behind the original half was labelled");

    // Slide the divider past the right point (x 216) — the same point is now hidden…
    canvas->setCompareSplit(0.9);
    QVERIFY2(hoverText(180, 100).isEmpty(), "the divider move did not hide the point");
    // …and back before the left one (x 24), which reveals it.
    canvas->setCompareSplit(0.1);
    QVERIFY2(hoverText(60, 100).contains("60, 100"), "the divider move did not reveal the point");

    // Horizontal split: the original is the TOP, so y decides. Divider y 80 leaves the
    // line (y 100) below it; y 144 puts it above.
    compareAt("horizontal", 0.5);
    QVERIFY2(hoverText(180, 100).contains("180, 100"), "the point below the divider lost its tooltip");
    canvas->setCompareSplit(0.9);
    QVERIFY2(hoverText(180, 100).isEmpty(), "a point above the divider was labelled");
    QVERIFY2(hoverText(150, 100).isEmpty(), "a line above the divider was labelled");

    // "original" shows no layout anywhere — nothing is ever labelled.
    compareAt("original", 0.5);
    QVERIFY2(hoverText(180, 100).isEmpty() && hoverText(150, 100).isEmpty(),
             "the original-only view still labelled the layout");
    // …and the Alt+Shift+O peek is the same view, so it gates the same way.
    win.setCompareModeUi(QStringLiteral("none"));
    canvas->setCompareHoldOriginal(true);
    QVERIFY2(hoverText(180, 100).isEmpty(), "the held peek still labelled the layout");
    canvas->setCompareHoldOriginal(false);

    // Back to normal: the tooltip returns everywhere.
    QVERIFY2(hoverText(60, 100).contains("60, 100"), "the tooltip did not come back");
    beat();
  }

  // The title-bar X leaves the SAME way the toolbar toggle does — a docked chat
  // slides into whichever edge it is docked to, a float flies into the icon —
  // for all four dock areas plus floating. It used to call QWidget::close() and
  // simply blink out. Reopening afterwards still works, with the transcript kept.
  void chatCloseButtonAnimatesFromEveryDockArea() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 820);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QToolButton* closeBtn = dock->findChild<QToolButton*>();
    // The X is the last ghost in the title bar; find it by its tooltip.
    closeBtn = nullptr;
    for (QToolButton* b : dock->findChildren<QToolButton*>())
      if (b->toolTip() == QLatin1String("Close assistant")) closeBtn = b;
    QVERIFY2(closeBtn, "no X in the chat title bar");
    win.chatDock_->appendUser(QStringLiteral("kept across the close"));

    // The float's exit is a snapshot flown inside the main window.
    const auto flight = [&win]() -> QRect {
      for (QLabel* l : win.findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly))
        for (QPropertyAnimation* a : l->findChildren<QPropertyAnimation*>())
          if (a->propertyName() == QByteArray("geometry")) return a->startValue().toRect();
      return QRect();
    };
    const auto settle = [] { QTest::qWait(700);
                             QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); };

    struct Area { Qt::DockWidgetArea area; const char* name; };
    const QVector<Area> areas{{Qt::LeftDockWidgetArea, "left"},
                              {Qt::RightDockWidgetArea, "right"},
                              {Qt::TopDockWidgetArea, "top"},
                              {Qt::BottomDockWidgetArea, "bottom"}};
    for (const Area& a : areas) {
      win.addDockWidget(a.area, dock);
      dock->setFloating(false);
      win.actChat_->setChecked(true);
      win.setChatShown(true, false);
      QTRY_VERIFY(dock->isVisible());
      settle();
      QCOMPARE(win.dockWidgetArea(dock), a.area);

      closeBtn->click();
      // Mid-slide: the extent animation is running and it has NOT blinked out.
      QVERIFY2(win.chatAnim_ != nullptr,
               qPrintable(QString("%1: the X closed with no animation").arg(a.name)));
      QVERIFY2(dock->isVisible(),
               qPrintable(QString("%1: the dock vanished before the slide").arg(a.name)));
      // …the slide runs toward that edge: width for left/right, height for top/bottom.
      const bool horiz = a.area == Qt::LeftDockWidgetArea || a.area == Qt::RightDockWidgetArea;
      const int before = horiz ? dock->width() : dock->height();
      QTest::qWait(120);
      const int during = horiz ? dock->width() : dock->height();
      QVERIFY2(during < before,
               qPrintable(QString("%1: the %2 never shrank (%3 -> %4)")
                              .arg(a.name, horiz ? "width" : "height")
                              .arg(before).arg(during)));
      QTRY_VERIFY2_WITH_TIMEOUT(!dock->isVisible(),
                                qPrintable(QString("%1: it never finished closing").arg(a.name)), 3000);
      QVERIFY2(!win.actChat_->isChecked(),
               qPrintable(QString("%1: the toolbar toggle stayed lit").arg(a.name)));
      settle();

      // …and it reopens cleanly, transcript intact.
      win.actChat_->setChecked(true);
      QTRY_VERIFY2(dock->isVisible(), qPrintable(QString("%1: it would not reopen").arg(a.name)));
      settle();
      bool kept = false;
      for (QLabel* l : dock->findChildren<QLabel*>())
        if (l->property("chatBody").toString() == QLatin1String("kept across the close")) kept = true;
      QVERIFY2(kept, qPrintable(QString("%1: the close lost the transcript").arg(a.name)));
    }

    // Floating: the X flies the window into the icon.
    dock->setFloating(true);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(dock->isVisible() && dock->isFloating());
    settle();
    const QRect windowBox(dock->mapToGlobal(QPoint(0, 0)), dock->size());
    closeBtn->click();
    QTest::qWait(60);
    const QRect from = flight();
    QVERIFY2(from.isValid(), "floating: the X closed with no flight");
    QWidget* icon = win.buttonForAction(win.actChat_);
    QVERIFY(icon);
    QVERIFY2(from != QRect(icon->mapTo(&win, QPoint(0, 0)), icon->size()),
             "floating: the flight started at the icon, not the window");
    QTRY_VERIFY2(!dock->isVisible(), "floating: it never finished closing");
    settle();
    win.actChat_->setChecked(true);
    QTRY_VERIFY2(dock->isVisible(), "floating: it would not reopen");
    Q_UNUSED(windowBox);
    beat();
  }

  // Opening the COMPACT chat while one is already on screen is a popover swap: the
  // outgoing shape animates out FIRST — a docked panel slides back into its edge, a
  // floating one flies into the icon — and only then does the compact float reveal.
  // Every route has to do it (right-click on the icon, Alt-hover peek, the toolbar
  // action), from BOTH shapes: the float route used to skip it entirely.
  void compactChatSwapAnimatesFromEveryRoute() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(dock);
    auto* icon = qobject_cast<QToolButton*>(win.buttonForAction(win.actChat_));

    // The outgoing FLOAT's exit is a snapshot QLabel flown inside the main window;
    // its geometry animation starts at the window's box, never at the icon.
    const auto flight = [&win]() -> QRect {
      for (QLabel* l : win.findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly))
        for (QPropertyAnimation* a : l->findChildren<QPropertyAnimation*>())
          if (a->propertyName() == QByteArray("geometry")) return a->startValue().toRect();
      return QRect();
    };
    const auto flushGhosts = [] {
      QTest::qWait(500);
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };

    // Put the chat in `floating` shape with a message in it, ready to be swapped.
    const auto arm = [&](bool floating, const QString& mark) {
      win.setChatShown(false, false);
      win.chatCompactPopover_ = false;
      dock->setFloating(floating);
      win.actChat_->setChecked(true);
      win.setChatShown(true, false);
      QTRY_VERIFY(dock->isVisible());
      QCOMPARE(dock->isFloating(), floating);
      win.chatDock_->appendUser(mark);
      flushGhosts();
    };
    // Assert the swap: outgoing animates (slide for a dock, flight for a float),
    // the compact float lands, and the transcript came along.
    const auto expectSwap = [&](bool wasFloating, const QString& mark, const char* route) {
      if (wasFloating) {
        const QRect from = flight();
        QVERIFY2(from.isValid(),
                 qPrintable(QString("%1: the outgoing FLOAT did not fly out").arg(route)));
        QVERIFY2(from != QRect(icon->mapTo(&win, QPoint(0, 0)), icon->size()),
                 qPrintable(QString("%1: the flight started at the icon, not the window").arg(route)));
      } else {
        QVERIFY2(win.chatAnim_ != nullptr,
                 qPrintable(QString("%1: the docked panel did not slide out").arg(route)));
        QVERIFY2(!dock->isFloating(),
                 qPrintable(QString("%1: it tore off before the slide played").arg(route)));
      }
      QTRY_VERIFY_WITH_TIMEOUT(win.chatCompactShowing(), 4000);
      bool kept = false;
      for (QLabel* l : dock->findChildren<QLabel*>())
        if (l->property("chatBody").toString() == mark) kept = true;
      QVERIFY2(kept, qPrintable(QString("%1: the swap lost the conversation").arg(route)));
      flushGhosts();
    };

    // ── the four routes ──
    for (const bool floating : {false, true}) {
      const QString shape = floating ? QStringLiteral("float") : QStringLiteral("dock");
      // Right-click on the toolbar icon (the popover gesture).
      {
        const QString mark = shape + " ctx";
        arm(floating, mark);
        QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(4, 4),
                             icon->mapToGlobal(QPoint(4, 4)));
        QApplication::sendEvent(icon, &ev);
        expectSwap(floating, mark, qPrintable(shape + " + right-click"));
      }
      // Alt-hover peek onto the same icon.
      {
        const QString mark = shape + " peek";
        arm(floating, mark);
        win.altPeekOpen(icon, win.actChat_);
        expectSwap(floating, mark, qPrintable(shape + " + alt-peek"));
      }
    }
    // …and the shape the user actually had: a float that IS the compact popover,
    // MOVED away from its anchor. Re-opening it used to teleport the window with
    // no motion at either end — the reported "the chat just vanished".
    {
      QVERIFY(win.chatCompactShowing());
      win.chatDock_->appendUser(QStringLiteral("moved compact"));
      dock->move(dock->pos() + QPoint(160, 120));   // as if dragged
      flushGhosts();
      QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(4, 4),
                           icon->mapToGlobal(QPoint(4, 4)));
      QApplication::sendEvent(icon, &ev);
      expectSwap(/*wasFloating=*/true, QStringLiteral("moved compact"),
                 "moved compact float + right-click");
      // Back at the anchor, the same gesture is idempotent: no flight, no move.
      const QRect settled = dock->geometry();
      QApplication::sendEvent(icon, &ev);
      QCOMPARE(dock->geometry(), settled);
      QVERIFY2(!flight().isValid(), "a re-pin that moves nothing must not animate");
      QVERIFY(win.chatCompactShowing());
    }
    beat();
  }

  // The idle canvas card says "＋ Blank image" on its face; a hover tooltip repeating that
  // is noise, so neither surface carries one any more.
  void blankImageCardHasNoTooltip() {
    MainWindow win;
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.canvas_->clearImage();
    win.refreshActions();
    QTest::qWait(1300);   // past the clear-dust hold, so the card is painted
    // Painting the card is what used to install the tooltip.
    win.canvas_->grab();
    QVERIFY2(win.canvas_->toolTip().isEmpty(),
             qPrintable("the empty canvas still has a tooltip: " + win.canvas_->toolTip()));
  }

  // Clicking the "＋ Blank image" card opens the dialog out of THE CARD, not the toolbar
  // icon the same command flies from when picked there.
  void blankImageDialogFliesFromTheCard() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win;
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.canvas_->clearImage();
    win.refreshActions();
    QTest::qWait(1300);            // past the clear hold, so the card is laid out
    win.canvas_->grab();           // painting is what records the card's rect
    const QRect card = win.canvas_->idleCardGlobalRect();
    QVERIFY2(card.isValid(), "the blank-image card is not on screen");

    QRect start;
    QTimer::singleShot(140, &win, [&] {
      for (QLabel* l : win.findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly))
        for (QPropertyAnimation* a : l->findChildren<QPropertyAnimation*>())
          if (a->propertyName() == QByteArray("geometry")) start = a->startValue().toRect();
      if (QWidget* modal = QApplication::activeModalWidget()) modal->close();
    });
    // The CLOSE flight is captured separately: it starts at the dialog and must END at
    // the card. It used to ignore the anchor rect and shrink into the box above instead.
    QRect closeEnd;
    QTimer::singleShot(260, &win, [&] {
      for (QLabel* l : win.findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly))
        for (QPropertyAnimation* a : l->findChildren<QPropertyAnimation*>())
          if (a->propertyName() == QByteArray("geometry")) closeEnd = a->endValue().toRect();
    });
    emit win.canvas_->blankImageRequested();
    QTest::qWait(400);
    const QRect want(win.mapFromGlobal(card.topLeft()), card.size());
    QCOMPARE(start, want);
    QVERIFY2(closeEnd == want, qPrintable(QString("the close shrinks into %1, the card is at %2")
                                              .arg(QDebug::toString(closeEnd), QDebug::toString(want))));
  }

  // The panel toggles are mouse affordances: taking focus draws the platform's halo
  // around the chevron, which reads as a second, taller pill sitting over the canvas.
  void panelToggleChevronsTakeNoFocusHalo() {
    MainWindow win;
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actPanel_->setChecked(false);
    QTest::qWait(400);
    QVERIFY(win.panelReopenBtn_);
    QCOMPARE(win.panelReopenBtn_->focusPolicy(), Qt::NoFocus);
    QCOMPARE(win.panelReopenBtn_->size(), QSize(kPanelChevronBox, kPanelChevronBox));
    // …and its twin in the panel header, so the pair stays consistent.
    QWidget* bar = nullptr;
    for (QDockWidget* d : win.findChildren<QDockWidget*>())
      if (d->objectName() != QLatin1String("llmChatDock") && d->titleBarWidget()) bar = d->titleBarWidget();
    QVERIFY2(bar, "no selection-panel title bar");
    bool sawChevron = false;
    for (QToolButton* b : bar->findChildren<QToolButton*>()) {
      sawChevron = true;
      QCOMPARE(b->focusPolicy(), Qt::NoFocus);
      QCOMPARE(b->size(), QSize(kPanelChevronBox, kPanelChevronBox));
    }
    QVERIFY2(sawChevron, "the panel header has no collapse chevron");
  }

  void clearProjectActionIsDangerAndGated() {
    MainWindow win;
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QAction* clear = win.actClearProject_;
    QVERIFY(clear);
    // With an image loaded it is live…
    QImage img(24, 24, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());   // the bare-QImage adoption path
    win.refreshActions();
    QVERIFY2(clear->isEnabled(), "Clear Project is dead with an image loaded");
    // …and dead once there is nothing left to clear (an empty canvas, no project).
    win.canvas_->clearImage();
    win.activeProjectId_.clear();
    win.refreshActions();
    QVERIFY2(!clear->isEnabled(), "Clear Project stays live on an empty editor");
    // The ACTION's glyph is the ordinary menu tone, never danger red: menus paint
    // icons muted (browser .ctx-icon) and the red belongs to the filled toolbar
    // button, which dangerToolButtonsAreFilledRed covers.
    const QImage glyph = clear->icon().pixmap(16, 16).toImage();
    QVERIFY(!glyph.isNull());
    const QColor danger = stencil::gui::themePalette(false).danger;
    const QColor dangerDark = stencil::gui::themePalette(true).danger;
    bool tinted = false;
    for (int y = 0; y < glyph.height() && !tinted; ++y)
      for (int x = 0; x < glyph.width(); ++x) {
        const QColor c = glyph.pixelColor(x, y);
        if (c.alpha() < 40) continue;
        const auto near = [&c](const QColor& d) {
          return qAbs(c.red() - d.red()) < 45 && qAbs(c.green() - d.green()) < 45
              && qAbs(c.blue() - d.blue()) < 45;
        };
        if (near(danger) || near(dangerDark)) { tinted = true; break; }
      }
    QVERIFY2(!tinted, "the Clear Project trash is still painted in the danger colour");
  }

  // A panel created LATE must render the conversation that already happened —
  // chat in the dock first, then open the context menu for the first time.
  void chatMenuPanelRendersExistingHistory() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"later reply\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    QVERIFY(!win.chatMenuPanel_);  // never built yet

    auto* chat = win.findChild<QAction*>("actChat");
    chat->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    auto* dockInput = win.chatDock_->findChild<QPlainTextEdit*>("chatInput");
    QVERIFY(dockInput);
    dockInput->setPlainText("said before the menu existed");
    QTest::keyClick(dockInput, Qt::Key_Return);
    QCOMPARE(win.chatHistory_.size(), 2);

    win.ensureChatMenuPanel();  // first time the menu is needed
    QVERIFY(win.chatMenuPanel_);
    QStringList bodies;
    for (QLabel* l : win.chatMenuPanel_->findChildren<QLabel*>())
      if (!l->property("chatRole").toString().isEmpty())
        bodies << l->property("chatBody").toString();
    QCOMPARE(bodies.size(), 2);
    QCOMPARE(bodies.at(0), QString("said before the menu existed"));
    QCOMPARE(bodies.at(1), QString("later reply"));

    win.llmClient_.reset();
    beat();
  }

  // Model output is DATA on every surface that shows it: dock, menu mirror, ask
  // card. Without an explicit format QLabel's Qt::AutoText would let
  // mightBeRichText() render a model's markup per reply; tooltips have no format
  // at all and need escaping instead. A label added without makePlainLabel fails
  // here.
  // The panel is built LAZILY and lives hidden inside a QWidgetAction, so rows
  // mirrored before it is first shown were measured against the default 100px
  // viewport and stayed collapsed to about a tenth of the panel. It re-measures
  // on the way in. Its in-flight row also animates the dock's bouncing dots
  // rather than sitting as a static "…".
  void chatMenuPanelSizesBubblesAndAnimatesPending() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    win.ensureChatMenuPanel();
    QVERIFY(win.chatMenuPanel_);
    // Mirrored while the panel is still HIDDEN — the state that collapsed them.
    const QString longText = QStringLiteral(
        "Loading the image into incognito, converting to black & white and cropping to "
        "portrait 3:4. Once it is done I will report back with the result.");
    for (int i = 0; i < 4; ++i) {
      win.chatMirror(QStringLiteral("You"), longText, false);
      win.chatMirror(QStringLiteral("Assistant"), longText, false);
    }
    win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel_->show();
    QTest::qWait(300);

    auto* scroll = win.chatMenuPanel_->findChild<QScrollArea*>("chatMenuTranscript");
    QVERIFY(scroll);
    const int avail = scroll->viewport()->width();
    QVERIFY2(avail > 100, "the panel transcript never got a real width");
    int checked = 0;
    for (QFrame* card : win.chatMenuPanel_->findChildren<QFrame*>()) {
      if (!card->property("chatMoreBtn").isValid()) continue;   // rows only
      ++checked;
      QVERIFY2(card->width() > avail / 2,
               qPrintable(QString("a mirrored bubble collapsed to %1 of %2 px")
                              .arg(card->width())
                              .arg(avail)));
      QVERIFY2(card->width() <= avail, "a bubble overflowed the transcript");
    }
    QVERIFY2(checked >= 4, "no mirrored rows to measure");

    // …and the pending row animates: the shared dots widget, with its own timer.
    win.chatMirrorPending(true);
    QTest::qWait(120);
    QWidget* dots = win.chatMenuPanel_->findChild<QWidget*>(QStringLiteral("chatTypingDots"));
    QVERIFY2(dots, "the panel's pending row has no typing dots");
    QVERIFY2(dots->isVisible(), "the typing dots are not on screen");
    // It really MOVES: sample the painted frame twice.
    const QImage a = dots->grab().toImage();
    QTest::qWait(160);
    const QImage b = dots->grab().toImage();
    QVERIFY2(a != b, "the typing dots are static");
    // Stopping swaps them for the text, as in the dock.
    win.chatMirrorStopped(QStringLiteral("retry me"));
    QTest::qWait(80);
    QVERIFY2(!win.chatMenuPanel_->findChild<QWidget*>(QStringLiteral("chatTypingDots")),
             "the dots outlived the turn");
    beat();
  }

  // The context-menu Assistant panel renders the DOCK's transcript, not a second
  // ad-hoc one: same message texts, in FULL (the old panel elided them to
  // one-line stubs), whichever surface sent them — and a clear empties both.
  void chatMenuPanelMirrorsTheDockTranscript() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const QString& reply) {
      return QJsonDocument(
                 QJsonObject{{"message",
                              QJsonObject{{"content",
                                           QString("{\"version\":1,\"reply\":\"%1\","
                                                   "\"actions\":[]}")
                                               .arg(reply)}}}})
          .toJson(QJsonDocument::Compact);
    };
    // Long enough that the old one-line elision would have cut it.
    const QString longUser = QStringLiteral(
        "please remove this project and then tell me what happened to the image "
        "I was looking at, in as many words as you can manage");
    const QString longReply = QStringLiteral(
        "Removed the working image and its lines; nothing was saved, so there was "
        "no project file to delete alongside it.");
    mock.response = wrap(longReply);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);

    auto* chat = win.findChild<QAction*>("actChat");
    chat->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    auto* dockInput = win.chatDock_->findChild<QPlainTextEdit*>("chatInput");
    QVERIFY(dockInput);
    dockInput->setPlainText(longUser);
    QTest::keyClick(dockInput, Qt::Key_Return);
    QTRY_COMPARE(win.chatHistory_.size(), 2);

    win.ensureChatMenuPanel();
    QVERIFY(win.chatMenuPanel_);
    // The pending "…" card and cards already handed to deleteLater are excluded
    // the way assistantBubbleTexts does it.
    const auto bodies = [](QWidget* surface) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QStringList out;
      for (QLabel* l : surface->findChildren<QLabel*>()) {
        const QString b = l->property("chatBody").toString();
        if (!b.isEmpty() && b != QStringLiteral("…")) out << b;
      }
      return out;
    };
    QCOMPARE(bodies(win.chatMenuPanel_), bodies(win.chatDock_));
    QVERIFY2(bodies(win.chatMenuPanel_).contains(longReply), "the reply is missing from the panel");

    // Full text on screen, wrapped — not the old "Assistant: Op…" stub.
    bool sawFull = false;
    for (QLabel* l : win.chatMenuPanel_->findChildren<QLabel*>()) {
      if (l->property("chatBody").toString() != longReply) continue;
      sawFull = true;
      QCOMPARE(l->text(), longReply);            // never elided
      QVERIFY2(l->wordWrap(), "a panel row must wrap, not elide");
      QVERIFY2(l->parentWidget()->objectName() == QLatin1String("chatCardAssistant"),
               "the panel row is not the dock's assistant card");
    }
    QVERIFY(sawFull);

    // A message sent from the PANEL lands in both surfaces too.
    mock.response = wrap(QStringLiteral("second reply"));
    auto* menuInput = qobject_cast<QPlainTextEdit*>(win.chatMenuInput_);
    QVERIFY(menuInput);
    menuInput->setPlainText("sent from the menu");
    QTest::keyClick(menuInput, Qt::Key_Return);
    QTRY_COMPARE(win.chatHistory_.size(), 4);
    QTRY_VERIFY(bodies(win.chatDock_).contains(QStringLiteral("sent from the menu")));
    QVERIFY(bodies(win.chatMenuPanel_).contains(QStringLiteral("sent from the menu")));
    QCOMPARE(bodies(win.chatMenuPanel_), bodies(win.chatDock_));

    // Clearing the conversation empties BOTH views.
    win.onChatClear();
    win.chatDock_->clearConversation();
    QTRY_VERIFY(bodies(win.chatMenuPanel_).isEmpty());
    QTRY_VERIFY(bodies(win.chatDock_).isEmpty());

    win.llmClient_.reset();
    beat();
  }

  void chatShowsModelTextLiterally() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";

    // The img is what would reach QTextDocument's resource loader if interpreted.
    const QString reply = QStringLiteral("<b>done</b> <img src=\"/etc/passwd\">");
    const QString question = QStringLiteral("<i>Which</i> one?");
    const QString option = QStringLiteral("<u>the first</u>");

    MockChatTransport mock;
    const QJsonObject plan{
        {"version", 1},
        {"reply", reply},
        // Nothing to run: asking INSTEAD of acting is the §11 case, and the
        // chat-only path must still render the card. Options carry no "actions"
        // either, so it needs no loaded image for previews.
        {"actions", QJsonArray{}},
        {"ask", QJsonObject{{"question", question},
                            {"mode", "single"},
                            {"options", QJsonArray{QJsonObject{{"label", option}},
                                                   QJsonObject{{"label", "the second"}}}}}},
    };
    mock.response =
        QJsonDocument(QJsonObject{
                          {"message",
                           QJsonObject{{"content", QString::fromUtf8(
                                                       QJsonDocument(plan).toJson(QJsonDocument::Compact))}}}})
            .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);

    auto* chat = win.findChild<QAction*>("actChat");
    chat->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    auto* dockInput = win.chatDock_->findChild<QPlainTextEdit*>("chatInput");
    QVERIFY(dockInput);
    dockInput->setPlainText("go");
    QTest::keyClick(dockInput, Qt::Key_Return);
    QCOMPARE(win.chatHistory_.size(), 2);
    win.ensureChatMenuPanel();
    QVERIFY(win.chatMenuPanel_);

    // Every transcript row, on BOTH surfaces, is identified by its property —
    // not by where it sits — so a restyle can't quietly drop this from cover.
    int bodies = 0;
    for (QWidget* surface : {static_cast<QWidget*>(win.chatDock_),
                             static_cast<QWidget*>(win.chatMenuPanel_)}) {
      for (QLabel* l : surface->findChildren<QLabel*>()) {
        if (l->property("chatBody").toString().isEmpty()) continue;
        ++bodies;
        QVERIFY2(l->textFormat() == Qt::PlainText,
                 qPrintable(QStringLiteral("a transcript row renders model text as %1, not PlainText: %2")
                                .arg(int(l->textFormat()))
                                .arg(l->property("chatBody").toString())));
      }
    }
    QVERIFY2(bodies >= 4, "expected the user + assistant row on each of the two surfaces");

    // The assistant row shows the tags themselves. Interpreted markup would
    // leave text() holding the source while the SCREEN showed "done" in bold —
    // so assert the format above AND the round-trip here.
    bool sawReply = false, sawQuestion = false, sawOption = false;
    for (QLabel* l : win.chatDock_->findChildren<QLabel*>()) {
      if (l->text() == reply) { sawReply = true; QCOMPARE(l->textFormat(), Qt::PlainText); }
      if (l->text() == question) { sawQuestion = true; QCOMPARE(l->textFormat(), Qt::PlainText); }
      if (l->text() == option) { sawOption = true; QCOMPARE(l->textFormat(), Qt::PlainText); }
    }
    QVERIFY2(sawReply, "the assistant reply is not on screen as the literal text the model sent");
    QVERIFY2(sawQuestion, "the ask card's question is not on screen as literal text");
    QVERIFY2(sawOption, "the ask card's option label is not on screen as literal text");

    // The menu mirror carries the FULL text in the row itself (the dock's card
    // rendering — no tooltip, no elision), so the round-trip holds there too.
    bool checkedMirror = false;
    for (QLabel* l : win.chatMenuPanel_->findChildren<QLabel*>()) {
      if (l->property("chatBody").toString() != reply) continue;
      checkedMirror = true;
      QCOMPARE(l->text(), reply);
      QCOMPARE(l->textFormat(), Qt::PlainText);
    }
    QVERIFY2(checkedMirror, "no mirrored row carried the assistant reply");

    win.llmClient_.reset();
    beat();
  }

  // Opening a project from the list is gesture-mapped (browser parity):
  //   single click          → confirm, then open in the CURRENT window
  //   double click          → open immediately, NO confirmation
  //   Ctrl/⌘ + single click → confirm, then open in a NEW window
  //   Ctrl/⌘ + double click → new window immediately, NO confirmation
  // The crux is that the single-click open is deferred by doubleClickInterval()
  // and cancelled by the double click, so the confirmation never flashes.
  void projectsListOpenGestures() {
    // Offscreen only (which is how ctest runs this suite). Driving a MODAL
    // dialog with synthetic clicks needs the window server to have activated
    // it; on a real desktop the clicks go nowhere and the flow deadlocks on the
    // still-open modal. Same class of limitation as the fullscreen edge-hover
    // test, which is likewise offscreen-only.
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // A project to click on.
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::magenta);
    const QString id = win.addImageProjectEntry(img, "gesture-target");
    QVERIFY(!id.isEmpty());

    auto mainWindowCount = [] {
      int n = 0;
      for (QWidget* w : QApplication::topLevelWidgets())
        if (qobject_cast<MainWindow*>(w) && w->isVisible()) ++n;
      return n;
    };
    // Watches for a confirmation QMessageBox for the whole flow and answers it.
    struct BoxWatch {
      bool seen = false;
      bool accept = true;
    };
    auto startWatch = [](BoxWatch* w) {
      auto* t = new QTimer;
      t->setInterval(5);
      QObject::connect(t, &QTimer::timeout, t, [w] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) return;
        w->seen = true;
        if (!w->accept) { box->reject(); return; }
        for (QAbstractButton* b : box->buttons())
          if (box->buttonRole(b) == QDialogButtonBox::AcceptRole) { b->click(); return; }
        box->accept();
      });
      t->start();
      return t;
    };
    // Perform a gesture on the first project row inside the (modal) dialog.
    auto gesture = [&win, &id](bool doubleClick, Qt::KeyboardModifiers mods) {
      QTimer::singleShot(0, [doubleClick, mods, id] {
        // Always leave a way out: if anything below fails to accept the modal,
        // reject it so the blocking openProjects() can return.
        const auto bailOut = [] {
          if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
            d->reject();
        };
        QListWidget* list = nullptr;
        for (int i = 0; i < 200 && !list; ++i) {
          if (auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
            list = dlg->findChild<QListWidget*>("projectsList");
          if (!list) QTest::qWait(10);
        }
        if (!list) { bailOut(); return; }
        // OUR project's row specifically — the store may hold hundreds, and a
        // stale one could fail to load and mask the result.
        QListWidgetItem* item = nullptr;
        for (int i = 0; i < list->count() && !item; ++i)
          if (list->item(i)->data(Qt::UserRole).toString() == id) item = list->item(i);
        if (!item) { bailOut(); return; }
        list->scrollToItem(item);
        QTest::qWait(30);
        // Aim right of the icon/kebab strips, at the row's text.
        const QRect r = list->visualItemRect(item);
        const QPoint hit(r.left() + r.width() / 2, r.center().y());
        if (doubleClick) {
          // Synthesised directly rather than via QTest::mouseDClick: that helper
          // waits internally between the events, and the dialog accepting
          // mid-sequence leaves it stuck.
          QWidget* vp = list->viewport();
          const QPointF gp = vp->mapToGlobal(hit);
          const auto send = [&](QEvent::Type t) {
            QMouseEvent e(t, QPointF(hit), gp, Qt::LeftButton,
                          t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                          mods);
            QApplication::sendEvent(vp, &e);
          };
          send(QEvent::MouseButtonPress);
          send(QEvent::MouseButtonRelease);
          send(QEvent::MouseButtonDblClick);
          send(QEvent::MouseButtonRelease);
        } else {
          QTest::mouseClick(list->viewport(), Qt::LeftButton, mods, hit);
        }
        // Outlast the deferred single-click open either way, so the "no dialog"
        // cases are genuinely observed and not just raced past.
        QTest::qWait(QApplication::doubleClickInterval() + 250);
        bailOut();  // gesture did not open anything → don't hang the test
      });
      win.openProjects();
    };

    const int baseWindows = mainWindowCount();

    // ── 1. single click → confirmation, then opens HERE ──
    {
      BoxWatch w;
      QTimer* t = startWatch(&w);
      gesture(false, Qt::NoModifier);
      t->stop();
      delete t;
      QVERIFY2(w.seen, "single click did not ask for confirmation");
      QTRY_COMPARE(mainWindowCount(), baseWindows);  // same window
      QVERIFY(win.canvas_->hasImage());
    }

    // ── 2. single click, confirmation DECLINED → nothing opens ──
    win.canvas_->clearImage();
    {
      BoxWatch w;
      w.accept = false;
      QTimer* t = startWatch(&w);
      gesture(false, Qt::NoModifier);
      t->stop();
      delete t;
      QVERIFY(w.seen);
      QVERIFY2(!win.canvas_->hasImage(), "declining the confirmation still opened it");
      QCOMPARE(mainWindowCount(), baseWindows);
    }

    // ── 3. double click → NO confirmation, opens HERE ──
    {
      BoxWatch w;
      QTimer* t = startWatch(&w);
      gesture(true, Qt::NoModifier);
      t->stop();
      delete t;
      QVERIFY2(!w.seen, "double click still raised a confirmation dialog");
      QTRY_VERIFY(win.canvas_->hasImage());
      QCOMPARE(mainWindowCount(), baseWindows);
    }

    // ── 4. ⌘ + single click → confirmation, then a NEW window ──
    {
      BoxWatch w;
      QTimer* t = startWatch(&w);
      gesture(false, Qt::ControlModifier);
      t->stop();
      delete t;
      QVERIFY2(w.seen, "Ctrl/⌘ + single click did not ask for confirmation");
      QTRY_COMPARE(mainWindowCount(), baseWindows + 1);
    }

    // ── 5. ⌘ + double click → NO confirmation, another NEW window ──
    {
      BoxWatch w;
      QTimer* t = startWatch(&w);
      gesture(true, Qt::ControlModifier);
      t->stop();
      delete t;
      QVERIFY2(!w.seen, "Ctrl/⌘ + double click still raised a confirmation dialog");
      QTRY_COMPARE(mainWindowCount(), baseWindows + 2);
    }

    // Tidy up the windows this test opened.
    for (QWidget* wgt : QApplication::topLevelWidgets())
      if (qobject_cast<MainWindow*>(wgt) && wgt != &win) wgt->close();
    QTRY_COMPARE(mainWindowCount(), baseWindows);
    beat();
  }

  // Removing project rows plays the scatter over an EMPTY slot. The overlay animates a
  // SNAPSHOT, and the list kept painting the ORIGINAL row underneath it — so the removal
  // was never actually seen. Pins the fixed sequence: the real row blanks the instant the
  // removal starts, its slot stays open while the dust falls, and the item leaves the
  // list only once the scatter has played (browser parity: leaveThenRemove +
  // beginRemoval in projectsModal.js). Driven through Clear All — the removal path that
  // keeps the dialog open while the animation runs.
  void projectRemovalBlanksTheRowAndHoldsItsSlot() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.addImageProjectEntry(img, "doomed-row");
    QVERIFY(!id.isEmpty());

    bool sawRow = false, blankedAtOnce = false, slotHeld = false, goneAfter = false;
    QTimer::singleShot(0, [&] {
      const auto bailOut = [] {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) d->reject();
      };
      QDialog* dlg = nullptr;
      QListWidget* list = nullptr;
      for (int i = 0; i < 200 && !list; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dlg) list = dlg->findChild<QListWidget*>("projectsList");
        if (!list) QTest::qWait(10);
      }
      if (!list) { bailOut(); return; }
      QListWidgetItem* item = nullptr;
      for (int i = 0; i < list->count() && !item; ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id) item = list->item(i);
      if (!item) { bailOut(); return; }
      sawRow = true;
      list->scrollToItem(item);
      QTest::qWait(30);
      const QRect r = list->visualItemRect(item);
      const int rowsBefore = list->count();
      const QImage before = list->viewport()->grab(r).toImage();

      QPushButton* clearBtn = nullptr;
      for (QPushButton* b : dlg->findChildren<QPushButton*>())
        if (b->text().startsWith("Clear All")) clearBtn = b;
      if (!clearBtn) { bailOut(); return; }
      // click() is synchronous (like trigger()): dismissModal's 0-timer must first fire
      // INSIDE the confirm's nested loop, not during a QTest::mouseClick event pump —
      // there its qWait poll gets buried under the confirm's loop and deadlocks.
      dismissModal("Yes");   // the in-dialog confirm
      clearBtn->click();

      // The row is still IN the list (slot held open, same height) but paints as blank.
      const QImage after = list->viewport()->grab(r).toImage();
      slotHeld = list->count() == rowsBefore && list->visualItemRect(item).height() == r.height();
      bool uniform = !after.isNull();
      const QRgb base = uniform ? after.pixel(1, 1) : 0;
      for (int y = 0; y < after.height() && uniform; ++y)
        for (int x = 0; x < after.width() && uniform; ++x)
          if (after.pixel(x, y) != base) uniform = false;
      blankedAtOnce = uniform && after != before;

      // …and the item is gone once the scatter has played out.
      const auto rowPresent = [&] {
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->data(Qt::UserRole).toString() == id) return true;
        return false;
      };
      for (int i = 0; i < 300 && rowPresent(); ++i) QTest::qWait(10);
      goneAfter = !rowPresent();
      bailOut();
    });
    win.openProjects();
    QVERIFY2(sawRow, "the seeded project row never appeared in the dialog");
    QVERIFY2(blankedAtOnce, "the original row kept painting under the scatter");
    QVERIFY2(slotHeld, "the row's slot collapsed before the scatter finished");
    QVERIFY2(goneAfter, "the doomed row never left the list");
    beat();
  }

  // Closing the dialog mid-scatter must not bring removed rows back: the close flight
  // photographs the dialog as it hides, and it used to fly the OPEN-time snapshot —
  // rows just cleared reappeared in the shrinking ghost. Pins the fix: on done() the
  // doomed rows are finalized (gone from the list at once, scatters stopped), and the
  // ghost's pixmap shows the row's slot as empty background, not the row.
  void closingProjectsDialogFinalizesRetiredRows() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    // ctest runs this suite with STENCIL_NO_ANIM=1, which turns the reveal/close
    // flights off entirely — but the close flight's ghost IS what this test pins.
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.addImageProjectEntry(img, "doomed-close-row");
    QVERIFY(!id.isEmpty());

    bool sawRow = false, finalized = false, ghostSeen = false, ghostClean = false;
    QTimer::singleShot(0, [&] {
      const auto bailOut = [] {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) d->reject();
      };
      QDialog* dlg = nullptr;
      QListWidget* list = nullptr;
      for (int i = 0; i < 200 && !list; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dlg) list = dlg->findChild<QListWidget*>("projectsList");
        if (!list) QTest::qWait(10);
      }
      if (!list) { bailOut(); return; }
      QListWidgetItem* item = nullptr;
      for (int i = 0; i < list->count() && !item; ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id) item = list->item(i);
      if (!item) { bailOut(); return; }
      sawRow = true;
      list->scrollToItem(item);
      // Let the OPEN flight land and delete its own ghost, so the one found below is
      // unambiguously the close flight's. The reveal's deferred grab has long fired
      // (the dialog-find loop above pumped events); the extra beat is belt and braces.
      // Bounded wait — a loaded machine may need more than the nominal duration.
      QTest::qWait(50);
      for (int i = 0; i < 250 && win.findChild<QLabel*>("stencilModalGhost"); ++i)
        QTest::qWait(10);
      if (win.findChild<QLabel*>("stencilModalGhost")) { bailOut(); return; }
      // Where the row sits, in DIALOG coordinates — the ghost photographs the dialog.
      const QRect rowInDlg =
          QRect(list->viewport()->mapTo(dlg, list->visualItemRect(item).topLeft()),
                list->visualItemRect(item).size()).adjusted(4, 4, -4, -4);

      QPushButton* clearBtn = nullptr;
      QPushButton* closeBtn = nullptr;
      for (QPushButton* b : dlg->findChildren<QPushButton*>()) {
        if (b->text().startsWith("Clear All")) clearBtn = b;
        if (b->text() == "Close") closeBtn = b;
      }
      if (!clearBtn || !closeBtn) { bailOut(); return; }
      dismissModal("Yes");
      clearBtn->click();     // rows doomed, scatter playing
      closeBtn->click();     // …and the dialog closed IMMEDIATELY, mid-scatter

      // Finalized on done(): the doomed row left the list at once, no kMs wait.
      finalized = true;
      for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id) finalized = false;

      // The close flight's ghost must show the slot as bare background — the stale
      // open-time snapshot (or a barely-started scatter) would still paint the row.
      if (auto* ghost = win.findChild<QLabel*>("stencilModalGhost")) {
        ghostSeen = true;
        const QPixmap shot = ghost->pixmap();
        const qreal dpr = shot.devicePixelRatio();
        const QImage gi = shot.toImage();
        const QRect strip(int(rowInDlg.x() * dpr), int(rowInDlg.y() * dpr),
                          int(rowInDlg.width() * dpr), int(rowInDlg.height() * dpr));
        if (gi.rect().contains(strip)) {
          const QRgb base = gi.pixel(strip.center());   // bare list background
          int off = 0;
          for (int y = strip.top(); y <= strip.bottom(); ++y)
            for (int x = strip.left(); x <= strip.right(); ++x)
              if (gi.pixel(x, y) != base) ++off;
          // A hair of frame anti-aliasing may cross the strip; a painted row (icon,
          // text, badges) is orders of magnitude more than 1% of it.
          ghostClean = off < strip.width() * strip.height() / 100;
        }
      }
      bailOut();   // belt and braces — Close already rejected the dialog
    });
    win.openProjects();
    QVERIFY2(sawRow, "the seeded project row never appeared in the dialog");
    QVERIFY2(finalized, "closing mid-scatter left the doomed row in the list");
    QVERIFY2(ghostSeen, "the close flight's ghost was not found");
    QVERIFY2(ghostClean, "the close ghost still painted the removed row");
    beat();
  }

  // Regression: Remove used to CLOSE the projects dialog before its confirm (the box was
  // shown by MainWindow after exec() returned) and left it closed — the user lost their
  // place. Now the ⋯-menu Remove confirms in-dialog (deferred a turn, drag-release safe):
  // No keeps the row and the dialog; Yes removes the project while the dialog stays open.
  void projectsRemoveConfirmsInDialogAndStaysOpen() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkGreen);
    const QString idA = win.addImageProjectEntry(img, "remove-me");
    const QString idB = win.addImageProjectEntry(img, "keep-me");
    QVERIFY(!idA.isEmpty() && !idB.isEmpty());
    const auto hasProject = [&win](const QString& id) {
      for (const auto& p : win.projectList_)
        if (QString::fromStdString(p.meta.id) == id) return true;
      return false;
    };

    bool sawRow = false, openAfterNo = false, keptAfterNo = false;
    bool openAfterYes = false, rowGone = false;
    QTimer::singleShot(0, [&] {
      const auto bailOut = [] {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) d->reject();
      };
      QDialog* dlg = nullptr;
      QListWidget* list = nullptr;
      for (int i = 0; i < 200 && !list; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dlg) list = dlg->findChild<QListWidget*>("projectsList");
        if (!list) QTest::qWait(10);
      }
      if (!list) { bailOut(); return; }
      const auto rowFor = [&list](const QString& id) -> QListWidgetItem* {
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->data(Qt::UserRole).toString() == id) return list->item(i);
        return nullptr;
      };
      QListWidgetItem* item = rowFor(idA);
      if (!item) { bailOut(); return; }
      sawRow = true;
      list->scrollToItem(item);
      // Trigger the ⋯/right-click menu's Remove on the current row: arm a 0-timer to
      // pick it (the menu's exec() blocks), then pop the menu via the real wiring.
      const auto removeViaMenu = [&list](QListWidgetItem* it) {
        list->setCurrentItem(it);
        QTimer::singleShot(0, [] {
          QMenu* menu = nullptr;
          for (int i = 0; i < 200 && !menu; ++i) {
            menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu) QTest::qWait(10);
          }
          if (!menu) return;
          for (QAction* a : menu->actions())
            if (a->text() == QLatin1String("Remove")) { a->trigger(); break; }
          menu->close();
        });
        emit list->customContextMenuRequested(list->visualItemRect(it).center());
      };

      // 1. Answer NO: the row, the project, and the dialog all stay.
      removeViaMenu(item);
      dismissModal("No");            // the deferred in-dialog confirm
      QTest::qWait(400);
      openAfterNo = dlg->isVisible();
      keptAfterNo = rowFor(idA) != nullptr && hasProject(idA);

      // 2. Same remove, answer YES: the project goes, the dialog stays open.
      removeViaMenu(rowFor(idA));
      dismissModal("Yes");
      QTest::qWait(400);
      openAfterYes = dlg->isVisible() && !hasProject(idA);
      // The scattered row leaves the list once the dust lands (setProjects repaint).
      for (int i = 0; i < 300 && rowFor(idA); ++i) QTest::qWait(10);
      rowGone = !rowFor(idA) && rowFor(idB) != nullptr;
      bailOut();
    });
    win.openProjects();
    QVERIFY2(sawRow, "the seeded project row never appeared in the dialog");
    QVERIFY2(openAfterNo, "answering No closed the projects dialog");
    QVERIFY2(keptAfterNo, "answering No still removed the project");
    QVERIFY2(openAfterYes, "answering Yes closed the dialog (or the project survived)");
    QVERIFY2(rowGone, "the removed row never left the still-open list");
    beat();
  }

  // The context menu must open anywhere on the canvas SURFACE, not only on the
  // image. The canvas widget is sized to the image, so the backdrop around a
  // zoomed-out image (and nearly everything when no image is loaded) belongs to
  // the scroll area's viewport — which had no menu at all.
  void contextMenuOpensOnEmptyCanvasArea() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QWidget* viewport = win.findChild<QScrollArea*>()->viewport();
    QVERIFY(viewport);

    // Right-click a corner of the canvas area — clearly outside any image.
    auto rightClickCorner = [&win, viewport](bool* opened, bool* copyEnabled,
                                             bool* copyFound) {
      QTimer::singleShot(0, [&win, opened, copyEnabled, copyFound] {
        QMenu* menu = nullptr;
        for (int i = 0; i < 200 && !menu; ++i) {
          menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
          if (!menu) QTest::qWait(10);
        }
        if (!menu) return;
        *opened = true;
        // The shared QActions are the WINDOW's children (the menu only
        // references them); syncContextActions has just run for this popup.
        for (QAction* a : win.findChildren<QAction*>())
          if (a->text().startsWith("Copy Image")) {
            *copyFound = true;
            *copyEnabled = a->isEnabled();
          }
        menu->close();
      });
      const QPoint corner(6, 6);
      QTest::mouseClick(viewport, Qt::RightButton, {}, corner);
      QTest::qWait(50);
    };

    // ── no image at all: the menu still opens (Fullscreen / Fit / Assistant…)
    // and the image-dependent entries stay disabled.
    QVERIFY(!win.findChild<CanvasWidget*>()->hasImage());
    bool openedEmpty = false, copyEnabledEmpty = true, copyFoundEmpty = false;
    rightClickCorner(&openedEmpty, &copyEnabledEmpty, &copyFoundEmpty);
    QVERIFY2(openedEmpty, "no context menu on the empty canvas with no image");
    QVERIFY(copyFoundEmpty);
    QVERIFY2(!copyEnabledEmpty, "image actions were enabled without an image");

    // ── with an image loaded, clicking OUTSIDE it (the backdrop) ──
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    bool openedOutside = false, copyEnabledOutside = false, copyFoundOutside = false;
    rightClickCorner(&openedOutside, &copyEnabledOutside, &copyFoundOutside);
    QVERIFY2(openedOutside, "no context menu on the backdrop around the image");
    QVERIFY2(copyEnabledOutside, "image actions stayed disabled with an image loaded");
    beat();
  }

  // REGRESSION (hard crash): clicking a CHECKABLE row in the canvas context
  // menu used to overflow the stack. StayOpenMenu re-dispatched mouse events
  // into the hosted chat panel, and QApplication::notify propagates an
  // unaccepted press up the parent chain — straight back into the menu, which
  // re-dispatched it again, forever (crash reports showed ~6600 frames of
  // mousePressEvent → deliverToArea → sendEvent → QMenu::event).
  //
  // Clicks on ordinary and checkable rows must survive, toggle in place, and
  // keep the menu open — with the assistant BOTH on and off, since only the
  // enabled case has an interactive area at all.
  void contextMenuCheckableClickDoesNotRecurse() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    for (const char* provider : {"none", "ollama"}) {
      win.settings_.llmProvider = provider;
      QAction* showPoints = nullptr;
      for (QAction* a : win.findChildren<QAction*>())
        if (a->text() == "Show Points") showPoints = a;
      QVERIFY(showPoints && showPoints->isCheckable());
      const bool before = showPoints->isChecked();

      bool clicked = false, menuAlive = false;
      QTimer::singleShot(0, [&] {
        QMenu* menu = nullptr;
        for (int i = 0; i < 200 && !menu; ++i) {
          menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
          if (!menu) QTest::qWait(10);
        }
        if (!menu) return;
        // The exact repro: a left click on the checkable row.
        QTest::mouseClick(menu, Qt::LeftButton, {},
                          menu->actionGeometry(showPoints).center());
        clicked = true;
        menuAlive = menu->isVisible();  // checkables toggle in place
        menu->close();
      });
      win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));

      QVERIFY2(clicked, "the context menu never opened");
      QVERIFY2(menuAlive, "toggling a checkable row closed the menu");
      QCOMPARE(showPoints->isChecked(), !before);  // it really flipped
      showPoints->setChecked(before);              // restore for the next pass
    }

    // The other half of the same hazard, and the one that actually recursed:
    // a click on a TRANSCRIPT ROW inside the chat panel. A QLabel ignores mouse
    // presses, so the re-dispatched event propagated back up to the menu.
    win.settings_.llmProvider = "ollama";
    win.ensureChatMenuPanel();
    win.chatMirror("You", "hello there", false);
    bool rowClicked = false, subAlive = false, splitterDragged = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      QAction* parent = nullptr;
      for (QAction* a : menu->actions())
        if (a->text() == "Assistant") parent = a;
      if (!parent || !parent->menu()) { menu->close(); return; }
      menu->setActiveAction(parent);
      QTest::keyClick(menu, Qt::Key_Right);
      QMenu* sub = parent->menu();
      for (int i = 0; i < 100 && !sub->isVisible(); ++i) QTest::qWait(10);
      auto* panel = sub->findChild<QWidget*>("chatMenuPanel");
      QLabel* row = nullptr;
      if (panel)
        for (QLabel* l : panel->findChildren<QLabel*>())
          if (l->isVisible() && l->text().contains("hello there")) row = l;
      if (!row) { menu->close(); return; }
      // Would previously recurse until the stack blew up.
      QTest::mouseClick(sub, Qt::LeftButton, {}, row->mapTo(sub, row->rect().center()));
      rowClicked = true;
      subAlive = sub->isVisible() && menu->isVisible();

      // Dragging the composer splitter goes through the SAME re-dispatch (plus
      // the move forwarding a drag needs) — it must not recurse either, and it
      // must actually resize.
      auto* sp = panel->findChild<QSplitter*>("chatMenuSplitter");
      if (sp && sp->count() > 1) {
        QWidget* handle = sp->handle(1);
        const QList<int> before = sp->sizes();
        const QPoint from = handle->mapTo(sub, handle->rect().center());
        QTest::mousePress(sub, Qt::LeftButton, {}, from);
        for (int dy = -8; dy >= -40; dy -= 8)
          QTest::mouseMove(sub, from + QPoint(0, dy));
        QTest::mouseRelease(sub, Qt::LeftButton, {}, from + QPoint(0, -40));
        splitterDragged = sp->sizes().at(1) > before.at(1) && sub->isVisible();
      }
      menu->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));
    QVERIFY2(rowClicked, "could not click a transcript row in the assistant submenu");
    QVERIFY2(subAlive, "clicking a transcript row closed the menu");
    QVERIFY2(splitterDragged,
             "dragging the composer splitter inside the popup did not resize it");
    beat();
  }

  // The assistant submenu's gear opens the assistant-only settings dialog —
  // and closes the context menu FIRST. A modal dialog must never come up under
  // a menu that still holds the popup grab (it would be unfocused and behind
  // it), so the handler dismisses the menu chain and defers the dialog a turn.
  void contextMenuAssistantGearOpensSettings() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";

    bool gearFound = false, menuGoneAfterClick = false, popupGrabGone = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      QAction* parent = nullptr;
      for (QAction* a : menu->actions())
        if (a->text() == "Assistant") parent = a;
      if (!parent || !parent->menu()) { menu->close(); return; }
      menu->setActiveAction(parent);
      QTest::keyClick(menu, Qt::Key_Right);
      QMenu* sub = parent->menu();
      for (int i = 0; i < 100 && !sub->isVisible(); ++i) QTest::qWait(10);
      auto* gear = sub->findChild<QToolButton*>("chatMenuGear");
      gearFound = gear != nullptr;
      if (!gear) { menu->close(); return; }
      // Click it through the menu, the real popup path.
      QTest::mouseClick(sub, Qt::LeftButton, {},
                        gear->mapTo(sub, gear->rect().center()));
      menuGoneAfterClick = !menu->isVisible() && !sub->isVisible();
      popupGrabGone = QApplication::activePopupWidget() == nullptr;
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));
    QVERIFY2(gearFound, "the assistant submenu has no gear button");
    QVERIFY2(menuGoneAfterClick, "the gear left the context menu open");
    QVERIFY2(popupGrabGone, "the popup grab survived the gear click");

    // The dialog opens on the next turn — poll for it, check it is the
    // assistant-only one and genuinely interactive, then dismiss.
    QString dialogName;
    bool dialogLive = false;
    QTimer::singleShot(0, [&] {
      for (int i = 0; i < 200; ++i) {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
          dialogName = d->objectName();
          dialogLive = d->isVisible() && d->isEnabled() &&
                       QApplication::activePopupWidget() == nullptr;
          d->reject();
          return;
        }
        QTest::qWait(10);
      }
    });
    QTest::qWait(800);
    QCOMPARE(dialogName, QString("assistantSettingsDialog"));
    QVERIFY2(dialogLive, "the assistant dialog came up hidden, disabled, or under a popup");
    beat();
  }

  // Drag dock zones (browser parity): while the FLOATING dock is title-dragged
  // the four NON-overlapping bands cover the CENTRAL dockable area (not the
  // chrome) for the whole drag; releasing inside the left/right/bottom bands
  // docks to that edge, releasing mid-area keeps floating, and the overlay
  // hides either way. Tracking is POLL-based (QCursor + mouseButtons), so the
  // drag is simulated via QTest press/release (which update the button state)
  // plus QCursor::setPos — real event delivery is NOT required, exactly like
  // a native macOS drag where moves never reach the widget.
  // The REAL input path (no probes): the title bar consumes press/move/release
  // itself, so a drag works even where Qt would hand the floating window to the
  // window server (macOS) and never deliver the release.
  void chatDockDragViaMouseEvents() {
    MainWindow win;
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);
    QTest::qWait(60);

    const QRect central(win.centralWidget()->mapTo(&win, QPoint(0, 0)),
                        win.centralWidget()->size());
    const auto sendMouse = [&](QEvent::Type t, const QPoint& global) {
      QMouseEvent e(t, title->mapFromGlobal(global), QPointF(global),
                    t == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                    t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                    Qt::NoModifier);
      QApplication::sendEvent(title, &e);
    };
    const QPoint start = title->mapToGlobal(QPoint(30, 8));
    sendMouse(QEvent::MouseButtonPress, start);
    sendMouse(QEvent::MouseMove, start + QPoint(40, 40));   // past the threshold
    QTest::qWait(30);
    QWidget* zones = win.findChild<QWidget*>("chatDockZones");
    QVERIFY2(zones && zones->isVisible(), "zones show for a real (event-driven) drag");

    // Release inside the RIGHT band → docked right, zones gone.
    const QRect zr = zones->geometry();   // bands span the dock region, not `central`
    const QPoint rightBand = win.mapToGlobal(QPoint(zr.right() - 20, zr.center().y()));
    sendMouse(QEvent::MouseMove, rightBand);
    sendMouse(QEvent::MouseButtonRelease, rightBand);
    QTRY_VERIFY(!dock->isFloating());
    QCOMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QTRY_VERIFY(!zones->isVisible());
  }

  // The placement button matching the current state is accent-marked and inert.
  void chatDockPlacementState() {
    MainWindow win;
    win.resize(1000, 720);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);
    const QList<QToolButton*> btns = title->findChildren<QToolButton*>();
    QVERIFY(btns.size() >= 6);   // 4 placements + float + close
    // Docked LEFT by default: exactly one placement button wears the active chip. (It is
    // marked by its stylesheet, not by being disabled — a disabled button would be
    // repainted by QToolButton:disabled as a dead bordered square.)
    const auto activeCount = [&] {
      int n = 0;
      for (QToolButton* b : btns)
        if (b->styleSheet().contains("background:")) ++n;
      return n;
    };
    QTRY_COMPARE(activeCount(), 1);
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QTRY_COMPARE(activeCount(), 1);   // now it's the float button
  }

  void chatDockDragZones() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);

    QTest::qWait(60);  // let the layout reclaim the floated dock's slot
    const QRect central(win.centralWidget()->mapTo(&win, QPoint(0, 0)),
                        win.centralWidget()->size());
    // Offscreen has no movable cursor / synthetic global button state, so the
    // poll reads STUBBED probes — exactly the delivery-free situation of a
    // native macOS drag, where moves/releases never reach the widget.
    QPoint stubCursor = win.mapToGlobal(central.topLeft());  // ≠ the first drag target
    bool stubDown = false;
    win.chatDock_->setDragProbesForTest([&stubCursor] { return stubCursor; },
                                        [&stubDown] { return stubDown; });
    const auto dragTo = [&](const QPoint& globalPos) {
      stubDown = true;
      // Synthesized press on the title bar starts the poll (delivery of the
      // PRESS is all the real flow needs — moves/releases are polled).
      QMouseEvent press(QEvent::MouseButtonPress, QPointF(8, 8), QPointF(8, 8),
                        QPointF(title->mapToGlobal(QPoint(8, 8))), Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(title, &press);
      stubCursor = globalPos;  // observed by the poll loop
    };
    const auto releaseAt = [&](const QPoint& globalPos) {
      stubCursor = globalPos;
      QTest::qWait(40);  // a couple of poll ticks at the drop spot
      stubDown = false;  // "button up" → the poll finishes at stubCursor
      QTest::qWait(40);
    };

    // The overlay appears for the whole drag and spans the DOCK REGION: full
    // window width, below the toolbars, above the status bar — NOT the central
    // widget (which shrinks by whatever is docked, drifting the bands inward).
    dragTo(win.mapToGlobal(central.center()));
    QTest::qWait(50);
    auto* zones = win.findChild<QWidget*>("chatDockZones");
    QVERIFY(zones);
    QTRY_VERIFY(zones->isVisible());
    const QRect zr = zones->geometry();
    QCOMPARE(zr.left(), 0);
    QCOMPARE(zr.width(), win.width());
    QVERIFY2(zr.top() > 0 && zr.top() <= central.top(), "starts below the toolbars");
    QVERIFY2(zr.bottom() >= central.bottom(), "reaches past the central area's bottom");
    // Native docking is locked out for the whole drag: the zones are the ONLY
    // docking mechanism (Qt can't show its placeholder or hover-dock).
    QCOMPARE(dock->allowedAreas(), Qt::NoDockWidgetArea);

    // LEFT band → docks left; areas restored on release.
    releaseAt(win.mapToGlobal(QPoint(zr.left() + 30, zr.center().y())));
    QTRY_VERIFY(!zones->isVisible());
    QTRY_VERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    QCOMPARE(dock->allowedAreas(), Qt::AllDockWidgetAreas);

    // Tear-off-from-DOCKED: the drag starts docked, the poll forces the float
    // past the drag threshold (native docking suppressed throughout), the
    // zones appear, and the RIGHT release band decides.
    QVERIFY(!dock->isFloating());
    dragTo(win.mapToGlobal(central.center()));  // press on the DOCKED title
    QTRY_VERIFY(dock->isFloating());            // forced into the zone flow
    QTRY_VERIFY(zones->isVisible());
    QCOMPARE(dock->allowedAreas(), Qt::NoDockWidgetArea);
    releaseAt(win.mapToGlobal(
        QPoint(zr.right() - 30, zr.center().y())));
    QTRY_VERIFY(!zones->isVisible());
    QTRY_VERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QCOMPARE(dock->allowedAreas(), Qt::AllDockWidgetAreas);

    // BOTTOM band → docks bottom.
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    dragTo(win.mapToGlobal(central.center()));
    QTRY_VERIFY(zones->isVisible());
    releaseAt(win.mapToGlobal(
        QPoint(zr.center().x(), zr.bottom() - 30)));
    QTRY_VERIFY(!zones->isVisible());
    QTRY_VERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::BottomDockWidgetArea);

    // Mid-area release → stays floating; the overlay is gone either way.
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    dragTo(win.mapToGlobal(central.center() + QPoint(40, 0)));
    QTRY_VERIFY(zones->isVisible());
    releaseAt(win.mapToGlobal(central.center()));
    QTRY_VERIFY(!zones->isVisible());
    QVERIFY(dock->isFloating());

    // Restore the default placement for later slots.
    win.addDockWidget(Qt::LeftDockWidgetArea, dock);
    dock->setFloating(false);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

  // The hover shimmer must genuinely ANIMATE: after a hover-enter, the
  // overlay's sweep progress ADVANCES between two samples inside the 325 ms
  // window and clears (-1) on completion — not a band that pops in at a fixed
  // position and sits there (user report on text-entry fields). Exercised on a
  // shimmered text-entry control (toolbar spinbox) when visible, else any
  // shimmered toolbutton.
  void hoverShimmerAnimates() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QWidget* target = nullptr;
    QWidget* overlay = nullptr;
    for (QAbstractSpinBox* s : win.findChildren<QAbstractSpinBox*>()) {
      if (!s->isVisible() || !s->isEnabled()) continue;
      if (QWidget* o = s->findChild<QWidget*>("shimmerOverlay")) {
        target = s;
        overlay = o;
        break;
      }
    }
    if (!target) {
      for (QToolButton* b : win.findChildren<QToolButton*>()) {
        if (!b->isVisible() || !b->isEnabled()) continue;
        if (QWidget* o = b->findChild<QWidget*>("shimmerOverlay")) {
          target = b;
          overlay = o;
          break;
        }
      }
    }
    QVERIFY2(target && overlay, "no visible shimmered control found");
    QCOMPARE(overlay->geometry(), target->rect());  // the band covers the control

    // Two-sample advance, re-triggering the sweep if a slow run let it finish
    // between the samples (the sweep is only 325 ms long).
    bool advanced = false;
    qreal p1 = -1.0, p2 = -1.0;
    for (int attempt = 0; attempt < 3 && !advanced; ++attempt) {
      // Synthesized hover-enter (offscreen QPA has no real cursor motion).
      QEnterEvent enter(QPointF(5, 5), QPointF(5, 5),
                        target->mapToGlobal(QPoint(5, 5)));
      QApplication::sendEvent(target, &enter);
      QTRY_VERIFY(overlay->property("sweepProgress").toReal() >= 0.0);
      p1 = overlay->property("sweepProgress").toReal();
      QTest::qWait(75);
      p2 = overlay->property("sweepProgress").toReal();
      advanced = p2 > p1;
    }
    QVERIFY2(advanced, qPrintable(QString("shimmer sweep did not advance (%1 -> %2)")
                                      .arg(p1)
                                      .arg(p2)));
    // The sweep completes and CLEARS — no lingering band on the control.
    QTRY_COMPARE(overlay->property("sweepProgress").toReal(), -1.0);
    beat();
  }

  // Closing is IMMEDIATE on every path — no "Quit Stencil?" confirmation
  // modal for close() (the ✕ / ⌘Q / app-menu / Dock-Quit / Alt+F4
  // equivalents); the window simply closes.
  void closeHasNoConfirmation() {
    MainWindow win(nullptr, false);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    bool sawDialog = false;
    QTimer::singleShot(300, &win, [&win, &sawDialog] {
      if (auto* box = win.findChild<QMessageBox*>()) {
        sawDialog = true;
        box->reject();  // unblock if a dialog wrongly appeared
      }
    });
    win.close();
    QTRY_VERIFY(!win.isVisible());
    QVERIFY(!sawDialog);
    beat();
  }
  // Pasting a layout over existing lines asks Combine / Replace / Cancel. Each answer
  // carries a glyph, and the two REAL answers carry glyphs that describe them (layers =
  // stack the incoming lines on the existing ones, swap = trade one layout for the other)
  // rather than a generic tick. Browser parity: exportService.js confirmIcon / altIcon.
  // ── Menu-bar coverage: the browser's toolbar sections must all be reachable ──
  // The bug this locks down: Start/Stop Drawing were in the Edit menu but the LINE/RECT
  // mode toggle was not — it existed only on the toolbar and in the canvas context menu, so
  // the menu bar gave no way to switch drawing mode (the browser's Draw section has both).
  // The same held for the line-style set and the image filter. Walking the real menu bar
  // also proves the shared plain QActions did not get moved OUT of the context menu, which
  // is exactly what would happen if a QWidgetAction were reused this way.
  void menuBarExposesTheDrawAndStyleControls() {
    MainWindow win(nullptr, /*restoreLast=*/false);

    // Collect every action title reachable from the menu bar, submenus included.
    QSet<QString> titles;
    std::function<void(QMenu*)> walk = [&](QMenu* m) {
      for (QAction* a : m->actions()) {
        if (a->menu()) walk(a->menu());
        else if (!a->isSeparator()) titles.insert(a->text());
      }
    };
    for (QAction* top : win.menuBar()->actions())
      if (top->menu()) walk(top->menu());

    // The reported gap: drawing mode, beside Start/Stop.
    QVERIFY(titles.contains("Start Drawing"));
    QVERIFY(titles.contains("Stop Drawing"));
    QVERIFY2(titles.contains("Switch to Rectangle Drawing") ||
                 titles.contains("Switch to Line Drawing"),
             "the line/rect mode toggle must be reachable from the menu bar");
    QVERIFY(titles.contains("Draw Rectangle (instant)"));

    // Line style (browser toolbar's Line Style select).
    QVERIFY(titles.contains("Solid"));
    QVERIFY(titles.contains("Dashed"));
    QVERIFY(titles.contains("Dotted"));

    // Image filter (browser toolbar's View section).
    QVERIFY(titles.contains("Cycle Image Filter"));
  }

  void layoutPasteDialogButtonsCarryGlyphs() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(canvas->width() > 0 && canvas->height() > 0);

    // An existing line is what raises the question at all.
    QAction* start = actionByText(&win, "Start Drawing");
    QVERIFY(start);
    start->trigger();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                      QPoint(canvas->width() * 0.3, canvas->height() * 0.3));
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                      QPoint(canvas->width() * 0.6, canvas->height() * 0.5));
    QTRY_VERIFY(!canvas->allLines().empty());

    QApplication::clipboard()->setText(
        QStringLiteral(R"({"lines":[{"points":[{"x":5,"y":5},{"x":9,"y":9}]}]})"));

    // Inspect the modal while it blocks the trigger, then back out of it.
    QMap<QString, bool> hasGlyph;
    QTimer::singleShot(0, [&hasGlyph]() {
      for (int i = 0; i < 200; ++i) {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
          for (QAbstractButton* b : box->buttons())
            hasGlyph.insert(QString(b->text()).remove('&'), !b->icon().isNull());
          for (QAbstractButton* b : box->buttons())
            if (QString(b->text()).remove('&').compare("Cancel", Qt::CaseInsensitive) == 0) {
              b->click();
              return;
            }
          box->reject();
          return;
        }
        QTest::qWait(5);
      }
    });
    QAction* paste = actionByText(&win, "Paste Layout JSON");
    QVERIFY(paste);
    paste->trigger();

    QVERIFY2(hasGlyph.value("Combine", false), "Combine must show a glyph, not a bare word");
    QVERIFY2(hasGlyph.value("Replace", false), "Replace must show a glyph");
    QVERIFY2(hasGlyph.value("Cancel", false), "Cancel must show a glyph");
    // The glyph names themselves must resolve — a renamed one degrades to a null QIcon,
    // which is exactly the "button has no icon" the assertions above would then catch,
    // but this says WHICH name broke.
    QVERIFY2(stencil::gui::hasIcon("layers"), "Combine's glyph");
    QVERIFY2(stencil::gui::hasIcon("swap"), "Replace's glyph");
    beat();
  }

  // Clearing the image scatters it as dust — and the empty-canvas invitation must NOT
  // appear underneath the falling particles, which reads as the clear happening twice.
  // It is held back for the length of the animation, and cannot be clicked while hidden.
  void clearHoldsTheIdleHintUntilTheDustLands() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QVERIFY(!canvas->idleHintHidden());

    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    dismissModal("Yes");
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);

    // Image gone, dust falling, invitation still off screen.
    QVERIFY2(canvas->idleHintHidden(), "the blank-image affordance must wait for the dust");
    QSignalSpy asked(canvas, &CanvasWidget::blankImageRequested);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    QCOMPARE(asked.count(), 0);   // nothing visible to click, so nothing opens

    // …and it comes back once the animation is over, so the affordance is reachable again.
    // NOT clicked here: accepting it opens the blank-image creator, whose modal loop would
    // hold this test until it timed out (which is exactly what it did).
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->idleHintHidden(),
                             stencil::gui::DisintegrateOverlay::kMs + 1500);
    QCOMPARE(asked.count(), 0);
    beat();
  }

  // …and the mirror image: an arriving image ASSEMBLES out of dust (Sweep::Gather) rather
  // than appearing all at once, with the real canvas held back until the motes land.
  // Any fresh image, not just a dropped one — a created blank is covered below.
  void droppedImageAssemblesOutOfDust() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    CanvasWidget* canvas = win.findChild<CanvasWidget*>();
    QVERIFY(canvas);

    // The real OS-open / drop path (synthesising a QDropEvent by hand does not route
    // through Qt's drag session, so the window never sees it). No arming needed: EVERY
    // fresh image assembles now, dropped or not.
    win.openPathFromOS(png_);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    // The dust layer exists and the canvas is behind it (opacity effect at 0), so the
    // picture is the motes, not a canvas that popped in under them.
    auto* fx = win.findChild<QGraphicsOpacityEffect*>();
    QTRY_VERIFY_WITH_TIMEOUT(
        win.findChild<QWidget*>(stencil::gui::DisintegrateOverlay::kObjectName) != nullptr, 3000);
    if (fx) QVERIFY2(fx->opacity() < 0.01, "the real canvas waits behind the motes");

    // Both are gone when it lands, leaving no effect on a canvas that repaints per stroke.
    QTRY_VERIFY_WITH_TIMEOUT(
        win.findChild<QWidget*>(stencil::gui::DisintegrateOverlay::kObjectName) == nullptr,
        stencil::gui::DisintegrateOverlay::kMs + 2000);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->graphicsEffect() == nullptr, 2000);
    beat();
  }

  // A blank image is an image APPEARING, so it assembles like any other — it used to pop
  // into place while a dropped one animated, which is the inconsistency that was reported.
  void createdBlankImageAssemblesToo() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    CanvasWidget* canvas = win.findChild<CanvasWidget*>();
    QVERIFY(canvas);
    const char* kDust = stencil::gui::DisintegrateOverlay::kObjectName;
    QVERIFY(!win.findChild<QWidget*>(kDust));

    win.createBlankImageFromDialog(QColor("#3366cc"), 320, 240);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(kDust) != nullptr, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(kDust) == nullptr,
                             stencil::gui::DisintegrateOverlay::kMs + 2000);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->graphicsEffect() == nullptr, 2000);
    beat();
  }

  // The drop zones paint the SAME glyphs as the browser (upload / incognito) — they used
  // to be the text characters "↑" and "◐", i.e. whatever the system font happened to have.
  void dropZonesPaintTheBrowsersGlyphs() {
    QVERIFY2(stencil::gui::hasIcon("upload"), "the saving half's glyph");
    QVERIFY2(stencil::gui::hasIcon("incognito"), "the incognito half's glyph");

    QWidget host;
    host.resize(700, 460);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    stencil::gui::DropZonesOverlay zones(&host);
    zones.showZones();
    QImage shot(zones.size(), QImage::Format_ARGB32);
    shot.fill(Qt::transparent);
    zones.render(&shot);

    // Something is actually drawn in each zone's glyph band — a mistyped icon name would
    // leave it empty, which is exactly the "no glyph at all" failure to catch.
    const auto bandHasInk = [&shot](int left, int right) {
      const int top = shot.height() / 6, bottom = shot.height() / 2;
      int ink = 0;
      for (int y = top; y < bottom; y += 2)
        for (int x = left; x < right; x += 2)
          if (qAlpha(shot.pixel(x, y)) > 40) ink++;
      return ink;
    };
    QVERIFY2(bandHasInk(20, shot.width() / 2 - 20) > 50, "the upload zone drew its glyph");
    QVERIFY2(bandHasInk(shot.width() / 2 + 20, shot.width() - 20) > 50, "and so did incognito");
    beat();
  }

  // Toasts stack, so a repeated action (flipping the theme a few times) used to build a
  // column of identical messages up the side of the canvas. The stack is capped: a new
  // arrival retires the oldest instead of piling on.
  void toastStackIsCappedAtThree() {
    QWidget host;
    host.resize(600, 420);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    stencil::gui::Notifications toasts(&host);

    // Read the stack TOP-DOWN by geometry. findChildren order is not creation order here:
    // reflow() raise()s each toast, and raise() moves a widget to the end of its parent's
    // child list — the very trap the cap itself had to be written around.
    const auto stackTopDown = [&host] {
      QList<QLabel*> live = host.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly);
      std::sort(live.begin(), live.end(),
                [](QLabel* a, QLabel* b) { return a->y() < b->y(); });
      QStringList out;
      // text() is markup wrapping the level glyph; the plain message rides alongside it.
      for (QLabel* l : live) out << l->property("stencilToastText").toString();
      return out;
    };

    for (int i = 1; i <= 6; ++i) toasts.info(QString("Toast %1").arg(i));
    // The retired ones play their exit first, so wait for the stack to settle rather
    // than asserting on the frame the sixth arrived in.
    QTRY_COMPARE(stackTopDown().size(), stencil::gui::Notifications::kMaxVisible);
    // The OLDEST three went; the newest is lowest, where the next one will appear.
    QCOMPARE(stackTopDown(), QStringList({"Toast 4", "Toast 5", "Toast 6"}));

    // Stacked bottom-left, and none of them ran off the top of the host — which is what
    // an uncapped stack eventually does.
    for (QLabel* l : host.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly)) {
      QCOMPARE(l->x(), 6);   // notifications.cpp kLeftMargin
      QVERIFY(l->y() >= 8);
      QVERIFY(l->geometry().bottom() <= host.height());
    }
    beat();
  }
  // An executor note about SUCCESSFUL work ("Opened X in the editor first…") rides
  // INSIDE the assistant's reply bubble as muted text — one assistant card per turn,
  // never a second card in the red error treatment (browser parity: the note merges
  // into the reply's warnings). Standalone notes (appendNote/appendNotice) render on
  // the muted card style with the reply bubble's paddings; danger stays reserved for
  // actual turn errors.
  void chatExecutorNoteRidesWithReply() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = qobject_cast<stencil::gui::ChatDock*>(
        win.findChild<QDockWidget*>("llmChatDock"));
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY(dock->width() > 200);
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Making it black and white.\",\"actions\":"
                      "[{\"op\":\"filter\",\"mode\":\"bw\"}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);

    // An editing plan arriving with an EMPTY canvas + an attachment adopts the
    // attachment as the working image and says so (the adoption note).
    QVERIFY(!win.canvas_->hasImage());
    QImage att(64, 48, QImage::Format_RGB32);
    att.fill(Qt::darkCyan);
    dock->addAttachmentImage(att, QStringLiteral("cat.png"));
    win.onChatSend("make it b&w");
    QTRY_VERIFY(win.canvas_->hasImage());

    auto* scrollArea = dock->findChild<QScrollArea*>();
    QVERIFY(scrollArea && scrollArea->widget());
    const auto cards = [scrollArea] {
      return scrollArea->widget()->findChildren<QFrame*>(QString(),
                                                         Qt::FindDirectChildrenOnly);
    };
    // ONE assistant bubble for the whole turn: user card + assistant card, and the
    // note is not a card of its own (it used to land as a chatCardError bubble).
    QTRY_COMPARE(cards().size(), 2);
    QFrame* reply = cards().last();
    QCOMPARE(reply->objectName(), QStringLiteral("chatCardAssistant"));
    QLabel* body = nullptr;
    QLabel* note = nullptr;
    for (QLabel* l : reply->findChildren<QLabel*>()) {
      if (!l->property("chatBody").toString().isEmpty()) body = l;
      if (!l->property("chatNote").toString().isEmpty()) note = l;
    }
    QVERIFY(body && note);
    QCOMPARE(body->property("chatBody").toString(),
             QString("Making it black and white."));
    QVERIFY(note->property("chatNote").toString().startsWith("Opened cat.png"));
    // The note is NEUTRAL: the muted stylesheet tone (QSS beats palettes here, so
    // the colour rides the chatNoteLabel rule), never the danger treatment.
    QCOMPARE(note->objectName(), QStringLiteral("chatNoteLabel"));
    QVERIFY(dock->styleSheet().contains("QLabel#chatNoteLabel{color:"));

    // Standalone notes (text-only retry, outline-refine) keep their own card, on
    // the MUTED style — and with exactly the reply bubble's vertical paddings, so
    // the same text renders at the same card height.
    const QString sample =
        QStringLiteral("A note long enough to wrap over a couple of lines in the dock.");
    dock->appendNote(sample);
    dock->appendAssistant(sample);
    QTest::qWait(400);  // let the appear animations land their margins
    const auto after = cards();
    QCOMPARE(after.size(), 4);
    QFrame* noteCard = after.at(2);
    QFrame* bubbleCard = after.at(3);
    QCOMPARE(noteCard->objectName(), QStringLiteral("chatCardMuted"));
    QVERIFY(dock->styleSheet().contains("#chatCardMuted QLabel{color:"));
    QCOMPARE(noteCard->layout()->contentsMargins(),
             bubbleCard->layout()->contentsMargins());
    QCOMPARE(noteCard->height(), bubbleCard->height());

    // The "assistant off" notice is the muted treatment too, never the red row.
    dock->appendNotice("The assistant is turned off.");
    QCOMPARE(cards().last()->objectName(), QStringLiteral("chatCardMuted"));

    if (qEnvironmentVariableIsSet("STENCIL_GUI_SHOTS")) {
      QTest::qWait(50);
      dock->grab().save(QString::fromLocal8Bit(qgetenv("STENCIL_GUI_SHOTS")) +
                        "/dock-executor-note.png");
    }
    beat();
  }
  // §10 new editor rows end-to-end: one mock-transport plan drives the compare
  // view (mode + split), the zoom, and a rename of the active saved project —
  // through the SAME setters the toolbar uses, so the combo/canvas/registry all
  // agree afterwards.
  void chatComparZoomRenamePlanDrivesEditor() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(64, 48, QImage::Format_RGB32);
    img.fill(Qt::darkYellow);
    win.loadImageWithLayout(img, QJsonObject());
    // A saved ACTIVE project, uniquely named per run (renameProjectById
    // validates against the persisted registry).
    const QString base =
        QStringLiteral("chat view src %1").arg(QDateTime::currentMSecsSinceEpoch());
    win.createLocalProject(base, /*announce=*/false);
    QVERIFY(!win.activeProjectId_.isEmpty());

    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const QString renamed = base + QStringLiteral(" renamed");
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      QStringLiteral(
                          "{\"version\":1,\"reply\":\"View set\",\"actions\":["
                          "{\"op\":\"compare\",\"mode\":\"vertical\",\"split\":0.3},"
                          "{\"op\":\"zoom\",\"percent\":150},"
                          "{\"op\":\"renameProject\",\"name\":\"%1\"}]}")
                          .arg(renamed)}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    const QSize sizeBefore = win.canvas_->image().size();
    win.onChatSend("compare it side by side, zoom in, and rename the project");

    // The compare view: canvas mode + divider, and the toolbar combo followed.
    QTRY_COMPARE(win.canvas_->compareMode(), QStringLiteral("vertical"));
    QCOMPARE(win.canvas_->compareSplit(), 0.3);
    QCOMPARE(win.compareCombo_->currentData().toString(), QStringLiteral("vertical"));
    // The zoom landed on the canvas scale (view-only — the image is untouched).
    QVERIFY(std::abs(win.canvas_->scale() - 1.5) < 1e-9);
    QCOMPARE(win.canvas_->image().size(), sizeBefore);
    // The rename went through the real registry path.
    QCOMPARE(win.activeProjectName(), renamed);
    bool inRegistry = false;
    for (const auto& p : win.projectList_)
      if (p.meta.name == renamed.toStdString()) inRegistry = true;
    QVERIFY2(inRegistry, "the renamed project is in the persisted registry");
    // The turn resolved as ONE assistant bubble with the plan's reply.
    auto* dock = qobject_cast<stencil::gui::ChatDock*>(
        win.findChild<QDockWidget*>("llmChatDock"));
    QVERIFY(dock);
    QTRY_VERIFY(assistantBubbleTexts(dock).contains(QStringLiteral("View set")));
    // Leave the shared registry tidy for the other cases.
    const QString id = win.activeProjectId_;
    win.setCompareModeUi(QStringLiteral("none"));
    win.eraseLocalProject(id);
    stencil::gui::fileStore::saveProjects(win.projectList_);
    beat();
  }

  // A follow-up {"op":"compare","mode":"none"} plan must CLEAR the split view:
  // canvas mode off (no divider, not read-only), toolbar combo back to None.
  void chatCompareNonePlanClearsSplit() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(64, 48, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto plan = [](const char* json) {
      return QJsonDocument(QJsonObject{
                 {"message", QJsonObject{{"content", QString::fromUtf8(json)}}}})
          .toJson(QJsonDocument::Compact);
    };
    // Turn 1 mirrors the reported flow: a red blank + a rectangle + the split
    // view in ONE plan (the layout makes the correction/refinement rounds run;
    // their empty responses are harmless keeps).
    mock.queue.append(plan(
        "{\"version\":1,\"reply\":\"split\",\"actions\":["
        "{\"op\":\"blank\",\"color\":\"red\",\"format\":\"a4\"},"
        "{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":100,\"y\":100},"
        "{\"x\":400,\"y\":100},{\"x\":400,\"y\":300},{\"x\":100,\"y\":300},"
        "{\"x\":100,\"y\":100}],\"color\":\"#000000\"}]},"
        "{\"op\":\"compare\",\"mode\":\"vertical\",\"split\":0.4}]}"));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("red album page with a rectangle, compared side by side");
    QTRY_COMPARE(win.canvas_->compareMode(), QStringLiteral("vertical"));
    mock.queue.clear();
    // Models routinely echo the previous divider ("split") back with mode
    // "none" — the §10 bullet shows the two fields side by side. That echo must
    // not cost the plan.
    mock.response = plan("{\"version\":1,\"reply\":\"cleared\",\"actions\":["
                         "{\"op\":\"compare\",\"mode\":\"none\",\"split\":0.4}]}");
    win.onChatSend("turn the comparison off");
    QTRY_COMPARE(win.canvas_->compareMode(), QStringLiteral("none"));
    QVERIFY2(!win.canvas_->compareReadOnly(), "compare 'none' left the canvas read-only");
    QCOMPARE(win.compareCombo_->currentData().toString(), QStringLiteral("none"));
    beat();
  }

  // A split compare of a BLANK page shows the blank's own fill on BOTH halves:
  // the original side is the untouched red page, the edit side the red page
  // with the lines — never a gray/neutral placeholder.
  void compareSplitOfBlankKeepsItsFill() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.applyImageFilter("none");   // the filter persists across sessions/tests
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"done\",\"actions\":["
                      "{\"op\":\"blank\",\"color\":\"red\",\"format\":\"a4\"},"
                      "{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":200,\"y\":300},"
                      "{\"x\":600,\"y\":300},{\"x\":600,\"y\":800},{\"x\":200,\"y\":800},"
                      "{\"x\":200,\"y\":300}],\"color\":\"#000000\"}]},"
                      "{\"op\":\"compare\",\"mode\":\"vertical\",\"split\":0.5}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("red page with a centred rectangle, compared side by side");
    QTRY_COMPARE(win.canvas_->compareMode(), QStringLiteral("vertical"));
    // The arrival effect hides the canvas briefly; grab only once it is gone.
    QTRY_VERIFY(win.canvas_->graphicsEffect() == nullptr);
    const QImage shot = win.canvas_->grab().toImage();
    // Sample well inside each half, away from the rectangle and the divider.
    const QColor left = shot.pixelColor(
        QPoint(int(shot.width() * 0.10), int(shot.height() * 0.5)));
    const QColor right = shot.pixelColor(
        QPoint(int(shot.width() * 0.90), int(shot.height() * 0.5)));
    const auto isRed = [](const QColor& c) {
      return c.red() > 200 && c.green() < 80 && c.blue() < 80;
    };
    QVERIFY2(isRed(right), qPrintable(QStringLiteral("edit half is %1, not the blank fill")
                                          .arg(right.name())));
    QVERIFY2(isRed(left), qPrintable(QStringLiteral("original half is %1, not the blank fill")
                                         .arg(left.name())));
    // With a filter riding (how a model often colours a page: blank + tint/filter),
    // a BLANK's compare still shows the SAME page colour on both halves — its
    // colour IS the page, so only the lines may differ. Before the fix the
    // original half dropped the filter and went red-vs-gray.
    win.applyImageFilter("bw");
    QTest::qWait(30);
    const QImage shotF = win.canvas_->grab().toImage();
    const QColor leftF = shotF.pixelColor(
        QPoint(int(shotF.width() * 0.10), int(shotF.height() * 0.5)));
    const QColor rightF = shotF.pixelColor(
        QPoint(int(shotF.width() * 0.90), int(shotF.height() * 0.5)));
    QCOMPARE(leftF.name(), rightF.name());
    QVERIFY2(!isRed(leftF), "the filtered blank's original half ignored the filter");
    win.applyImageFilter("none");
    beat();
  }

  // A filter left over from the previous image/session must not repaint a FRESH
  // blank: "make a red page" under a riding 'bw' filter rendered flat gray
  // (Rec. 709 luma of pure red = 54) with no red anywhere. Creation resets the
  // filter to none; a filter applied AFTER creation still works (test above).
  void blankCreationResetsRidingFilter() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.applyImageFilter("bw");
    win.createBlankImage(QColor("#ff0000"), 400, 300);
    QCOMPARE(win.settings_.imageFilter, QStringLiteral("none"));
    QTRY_VERIFY(win.canvas_->graphicsEffect() == nullptr);
    const QImage shot = win.canvas_->grab().toImage();
    const QColor mid = shot.pixelColor(shot.width() / 2, shot.height() / 2);
    QVERIFY2(mid.red() > 200 && mid.green() < 80 && mid.blue() < 80,
             qPrintable(QStringLiteral("blank is %1, not red").arg(mid.name())));
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

    // The six controls, in the browser's order.
    QList<QAction*> got;
    for (QToolButton* b : section->findChildren<QToolButton*>())
      if (b->defaultAction()) got << b->defaultAction();
    const QList<QAction*> want{win.actTheme_, win.actFullscreen_, win.actIncognito_,
                               win.actSettings_, win.actAccent_, win.actInfo_};
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
    QToolBar* row = win.findChild<QToolBar*>("pageFormulaToolbar");
    QVERIFY(row);
    const int custom = win.pageSize_->findData(QStringLiteral("custom"));
    QVERIFY(custom >= 0);
    for (const int width : {1950, 1400, 1100, 975}) {
      win.resize(width, 850);
      win.pageSize_->setCurrentIndex(custom);   // the widest state of this row
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

  // The chat's icon controls shimmer on hover like every other button in the app
  // (browser layout.css shimmers every <button>, chat ones included): the sweep
  // starts on Enter, stops on Leave, and never runs on a disabled control.
  // Checked on the dock's composer + title bar, the per-message "…", and the
  // context-menu panel's composer.
  void chatIconButtonsShimmerOnHover() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    win.chatDock_->appendUser(QStringLiteral("shimmer me"));
    win.ensureChatMenuPanel();
    win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel_->show();
    win.chatMirror(QStringLiteral("You"), QStringLiteral("shimmer me too"), false);
    QTest::qWait(200);

    const auto overlayOf = [](QWidget* w) {
      return w ? w->findChild<QWidget*>("shimmerOverlay") : nullptr;
    };
    const auto hoverEnter = [](QWidget* w) {
      QEnterEvent e(QPointF(4, 4), QPointF(4, 4), w->mapToGlobal(QPoint(4, 4)));
      QApplication::sendEvent(w, &e);
    };
    // Every chat icon control carries the overlay, mouse-through and exactly the
    // size of the button, and a hover starts a sweep that a leave cancels.
    // The overlay is there, sized to the button and mouse-through.
    const auto wired = [&](QWidget* b, const char* what) {
      QWidget* fx = overlayOf(b);
      QVERIFY2(fx, qPrintable(QString("%1: no shimmer overlay").arg(what)));
      QCOMPARE(fx->geometry(), b->rect());
      QVERIFY2(fx->testAttribute(Qt::WA_TransparentForMouseEvents),
               qPrintable(QString("%1: the overlay would eat clicks").arg(what)));
    };
    // …and a hover sweeps it, a leave cancels at once.
    const auto sweeps = [&](QWidget* b, const char* what) {
      wired(b, what);
      QWidget* fx = overlayOf(b);
      QVERIFY(fx);
      hoverEnter(b);
      QTRY_VERIFY2(fx->property("sweepProgress").toReal() >= 0.0,
                   qPrintable(QString("%1: hover started no sweep").arg(what)));
      QEvent leave(QEvent::Leave);
      QApplication::sendEvent(b, &leave);
      QCOMPARE(fx->property("sweepProgress").toReal(), -1.0);   // cancelled at once
    };

    // An ENABLED dock control (the composer's send is disabled on an empty box —
    // it is the disabled case below).
    QToolButton* live = nullptr;
    for (QToolButton* b : win.chatDock_->findChildren<QToolButton*>())
      if (b->isVisible() && b->isEnabled() && overlayOf(b)) { live = b; break; }
    QVERIFY2(live, "no shimmered enabled button in the chat dock");
    sweeps(live, "dock composer/header button");

    QFrame* card = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardUser")) card = f;
    QVERIFY(card);
    auto* more = qobject_cast<QToolButton*>(card->property("chatMoreBtn").value<QObject*>());
    QVERIFY2(more, "the card has no \"…\"");
    more->show();   // normally revealed by the card's own hover
    // Presence only: the "…" LIFTS itself 1px on hover, and offscreen QPA (which
    // has no real cursor to keep inside the moved button) answers that move with
    // a synthetic Leave that cancels the sweep. A real pointer stays inside it.
    wired(more, "row-menu \"…\"");

    QToolButton* panelBtn = nullptr;
    for (QToolButton* b : win.chatMenuPanel_->findChildren<QToolButton*>())
      if (b->isEnabled() && overlayOf(b)) { panelBtn = b; break; }
    QVERIFY2(panelBtn, "no shimmered button in the menu panel");
    sweeps(panelBtn, "menu panel composer button");

    // A DISABLED control stays quiet: send, with nothing typed.
    QToolButton* send = win.chatDock_->findChild<QToolButton*>("chatSend");
    QVERIFY(send);
    QVERIFY2(!send->isEnabled(), "the empty composer's send should be disabled");
    QWidget* sendFx = overlayOf(send);
    QVERIFY2(sendFx, "the send button lost its shimmer overlay");
    hoverEnter(send);
    QCOMPARE(sendFx->property("sweepProgress").toReal(), -1.0);
    beat();
  }

  // The card menu pops from a NESTED event loop, so the transcript can change
  // under it: a turn can land and repaint rows, the chat can be mid-close, and
  // the card itself can be deleted while the menu is up. Each of those crashed
  // (SIGSEGV inside QMenu::exec → QCocoaWindow::setVisible) or would use freed
  // memory after exec() returned.
  void chatCardMenuSurvivesTranscriptChurn() {
    MainWindow win(nullptr, false);
    win.resize(1150, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"working on it\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    auto* dock = win.chatDock_;

    const auto cardWithBody = [&](const QString& body) -> QFrame* {
      for (QLabel* l : dock->findChildren<QLabel*>())
        if (l->property("chatBody").toString() == body)
          return qobject_cast<QFrame*>(l->parentWidget());
      return nullptr;
    };
    // Right-click a card's LABEL (the path in the crash report) and, from inside
    // the menu's event loop, run `duringMenu` before closing it.
    const auto popMenu = [&](QWidget* on, std::function<void()> duringMenu) {
      QTimer::singleShot(0, [duringMenu] {
        for (int i = 0; i < 100; ++i) {
          if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            if (duringMenu) duringMenu();
            m->close();
            return;
          }
          QTest::qWait(5);
        }
      });
      const QPoint p = on->rect().center();
      QContextMenuEvent ev(QContextMenuEvent::Mouse, p, on->mapToGlobal(p));
      QApplication::sendEvent(on, &ev);
      QTest::qWait(40);
    };

    // (a) a turn IN FLIGHT, with the transcript repainting under the menu.
    dock->addAttachmentImage(QImage(8, 8, QImage::Format_RGB32), "x.png");
    win.onChatSend(QStringLiteral("crop it, make it b&w, and highlight the edges"));
    QTRY_VERIFY(!dock->isBusy());
    QFrame* userCard = cardWithBody(QStringLiteral("crop it, make it b&w, and highlight the edges"));
    QVERIFY(userCard);
    QLabel* body = nullptr;
    for (QLabel* l : userCard->findChildren<QLabel*>())
      if (!l->property("chatBody").toString().isEmpty()) body = l;
    QVERIFY(body);
    popMenu(body, [&] {
      // …the turn's tail landing while the menu is up.
      dock->appendAssistant(QStringLiteral("late note while the menu is open"));
      dock->appendLateNote(QStringLiteral("layout corrected"));
    });

    // (b) mid-close: the dock is still visible for the length of its slide, but
    // the menu must not pop into a surface that is about to be hidden.
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    win.actChat_->setChecked(false);
    QVERIFY2(dock->isVisible(), "the close should still be animating");
    {
      bool popped = false;
      QTimer::singleShot(0, [&popped] {
        if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
          popped = true;
          m->close();
        }
      });
      const QPoint p = body->rect().center();
      QContextMenuEvent ev(QContextMenuEvent::Mouse, p, body->mapToGlobal(p));
      QApplication::sendEvent(body, &ev);
      QTest::qWait(40);
      QVERIFY2(!popped, "the menu popped out of a chat that was closing");
    }
    if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim);
    QTRY_VERIFY(!dock->isVisible());
    win.actChat_->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    QTest::qWait(400);

    // (c) the card is DELETED while its own menu is up — nothing may touch it
    // after exec() returns.
    dock->appendUser(QStringLiteral("doomed row"));
    QTest::qWait(250);   // let it lay out, or it is not visible enough to pop on
    QFrame* doomed = cardWithBody(QStringLiteral("doomed row"));
    QVERIFY(doomed);
    QTRY_VERIFY(doomed->isVisible());
    QLabel* doomedBody = nullptr;
    for (QLabel* l : doomed->findChildren<QLabel*>())
      if (!l->property("chatBody").toString().isEmpty()) doomedBody = l;
    QVERIFY(doomedBody);
    QPointer<QFrame> gone(doomed);
    popMenu(doomedBody, [&] {
      delete gone.data();   // the transcript settling mid-menu, at its worst
    });
    QVERIFY2(!gone, "the card should be gone");
    QTest::qWait(100);
    QVERIFY2(!dock->isBusy(), "the dock survived the churn");

    // …and a right-click on the now-dangling label's siblings still does nothing bad.
    QFrame* survivor = cardWithBody(QStringLiteral("late note while the menu is open"));
    if (survivor) popMenu(survivor, nullptr);
    win.llmClient_.reset();
    beat();
  }

  // A row the transcript is CLIPPING still has a reachable "…": the button is
  // parked against the intersection of the card and the viewport, not against the
  // card's own bottom (which is off screen for a half-shown row — the reported
  // bug: a reply cut off mid-sentence with no menu anywhere). Checked for a row
  // clipped at the TOP and one clipped at the BOTTOM, in the docked shape, the
  // floating one, and the context-menu panel.
  void chatRowMenuStaysInsideTheViewport() {
    MainWindow win(nullptr, false);
    win.resize(1100, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    // Enough long messages that the transcript really scrolls.
    for (int i = 0; i < 8; ++i) {
      win.chatDock_->appendUser(
          QStringLiteral("Loading the image into incognito, converting to black & white "
                         "and cropping to portrait 3:4. Once it's done I will report "
                         "back with the result (%1).").arg(i));
      win.chatDock_->appendAssistant(
          QStringLiteral("Working on it — this reply is deliberately long so the row is "
                         "taller than a line and gets clipped by the viewport edge "
                         "while scrolling (%1).").arg(i));
    }
    QTest::qWait(300);

    const auto globalRect = [](QWidget* w) {
      return QRect(w->mapToGlobal(QPoint(0, 0)), w->size());
    };
    const auto moreOf = [](QFrame* card) {
      return qobject_cast<QToolButton*>(card->property("chatMoreBtn").value<QObject*>());
    };
    // Hover the card the way the user does, then read where its "…" landed.
    const auto hover = [](QFrame* card) {
      QEvent enter(QEvent::Enter);
      QApplication::sendEvent(card, &enter);
    };
    // Every surface is checked the same way: park the scroll somewhere in the
    // middle, then take a row clipped at each edge.
    const auto checkSurface = [&](QWidget* host, QScrollArea* scroll, const char* what) {
      QVERIFY2(scroll, what);
      QScrollBar* bar = scroll->verticalScrollBar();
      QVERIFY2(bar->maximum() > 0, qPrintable(QString("%1: the transcript does not scroll")
                                                  .arg(what)));
      bar->setValue(bar->maximum() / 2);
      QTest::qWait(120);
      const QRect vp = globalRect(scroll->viewport());
      QFrame* clippedTop = nullptr;
      QFrame* clippedBottom = nullptr;
      // A slice tall enough to CARRY the pill (a shorter one deliberately hides
      // it — that rule has its own checks below).
      const int room = 21 + 8;
      for (QFrame* card : host->findChildren<QFrame*>()) {
        if (!card->property("chatMoreBtn").isValid()) continue;
        const QRect g = globalRect(card);
        const QRect vis = g.intersected(vp);
        if (vis.height() < room) continue;
        if (g.top() < vp.top()) clippedTop = card;
        if (g.bottom() > vp.bottom()) clippedBottom = card;
      }
      QVERIFY2(clippedTop, qPrintable(QString("%1: no row clipped at the top").arg(what)));
      QVERIFY2(clippedBottom, qPrintable(QString("%1: no row clipped at the bottom").arg(what)));
      for (QFrame* card : {clippedTop, clippedBottom}) {
        hover(card);
        QToolButton* more = moreOf(card);
        QVERIFY2(more, qPrintable(QString("%1: a clipped row has no \"…\"").arg(what)));
        QVERIFY2(more->isVisible(),
                 qPrintable(QString("%1: the clipped row's \"…\" never showed").arg(what)));
        QVERIFY2(vp.contains(globalRect(more)),
                 qPrintable(QString("%1: the \"…\" sits outside the viewport (%2 vs %3)")
                                .arg(what)
                                .arg(QDebug::toString(globalRect(more)))
                                .arg(QDebug::toString(vp))));
      }
      // …and it tracks the view: scrolling must not leave it behind.
      hover(clippedBottom);
      bar->setValue(bar->value() + 40);
      QTest::qWait(80);
      QToolButton* more = moreOf(clippedBottom);
      if (more->isVisible())
        QVERIFY2(globalRect(scroll->viewport()).contains(globalRect(more)),
                 qPrintable(QString("%1: the \"…\" fell out of the viewport on scroll").arg(what)));
    };

    // NARROW transcript: the bubbles reach the edge, which is where the "…"
    // (it hangs OUTSIDE the bubble) was landing half over the boundary.
    const auto checkNarrow = [&](QWidget* host, QScrollArea* scroll, const char* what) {
      const QRect vp = globalRect(scroll->viewport());
      QFrame* left = nullptr;   // a user row: its "…" hangs off the LEFT
      QFrame* right = nullptr;  // an assistant row: off the RIGHT
      for (QFrame* card : host->findChildren<QFrame*>()) {
        if (!card->property("chatMoreBtn").isValid()) continue;
        if (globalRect(card).intersected(vp).height() < 21 + 8) continue;
        if (card->objectName() == QLatin1String("chatCardUser")) left = card;
        else right = card;
      }
      QVERIFY2(left && right, qPrintable(QString("%1: need a row hanging each way").arg(what)));
      for (QFrame* card : {left, right}) {
        hover(card);
        QToolButton* more = moreOf(card);
        QVERIFY(more && more->isVisible());
        const QRect r = globalRect(more);
        QVERIFY2(vp.contains(r),
                 qPrintable(QString("%1 (%2): the \"…\" is clipped by the edge (%3 vs %4)")
                                .arg(what, card->objectName(),
                                     QDebug::toString(r), QDebug::toString(vp))));
        QVERIFY2(r.left() >= vp.left() + 4 && r.right() <= vp.right() - 4,
                 qPrintable(QString("%1 (%2): no padding at the edge (%3 in %4)")
                                .arg(what, card->objectName(),
                                     QDebug::toString(r), QDebug::toString(vp))));
        // …and inside the widget that actually CLIPS it (the scrolled content),
        // not merely inside the viewport.
        QVERIFY2(globalRect(scroll->widget()).contains(r),
                 qPrintable(QString("%1 (%2): the pill hangs outside its clipping parent")
                                .arg(what, card->objectName())));
        // It is CHROME, not a transcript row: the edge-reveal must not dissolve
        // it (it is pinned at the edge by design) nor replace its accent glow.
        QVERIFY2(qobject_cast<QGraphicsDropShadowEffect*>(more->graphicsEffect()),
                 qPrintable(QString("%1 (%2): the \"…\" lost its glow to the edge reveal")
                                .arg(what, card->objectName())));
      }
    };

    // A row whose visible SLICE is too short to hold the pill does not show one:
    // the clamp would park it across the neighbouring card, which reads as a bug.
    // A fully visible row always shows it.
    const auto checkSliver = [&](QWidget* host, QScrollArea* scroll, const char* what) {
      QScrollBar* bar = scroll->verticalScrollBar();
      const QRect vp = globalRect(scroll->viewport());
      // Walk the scroll until some row is only a sliver at the viewport's edge.
      QFrame* sliver = nullptr;
      QFrame* whole = nullptr;
      for (int v = 0; v <= bar->maximum() && !sliver; v += 7) {
        bar->setValue(v);
        QTest::qWait(20);
        for (QFrame* card : host->findChildren<QFrame*>()) {
          if (!card->property("chatMoreBtn").isValid()) continue;
          const QRect g = globalRect(card);
          const int slice = g.intersected(vp).height();
          if (slice > 2 && slice < 16 && g.height() > 40) sliver = card;
          if (vp.contains(g)) whole = card;
        }
      }
      QVERIFY2(sliver, qPrintable(QString("%1: no row ended up a sliver").arg(what)));
      hover(sliver);
      QToolButton* more = moreOf(sliver);
      QVERIFY(more);
      QVERIFY2(!more->isVisible(),
               qPrintable(QString("%1: a sliver of a row still shows its \"…\"").arg(what)));
      // …and it must not be straddling anything if it somehow shows later.
      if (whole) {
        hover(whole);
        QToolButton* m2 = moreOf(whole);
        QVERIFY2(m2 && m2->isVisible(),
                 qPrintable(QString("%1: a fully visible row lost its \"…\"").arg(what)));
        const QRect r = globalRect(m2);
        for (QFrame* card : host->findChildren<QFrame*>()) {
          if (card == whole || !card->property("chatMoreBtn").isValid()) continue;
          if (!card->isVisible()) continue;
          QVERIFY2(!globalRect(card).intersects(r),
                   qPrintable(QString("%1: the \"…\" overlaps a neighbouring card").arg(what)));
        }
      }
    };

    QScrollArea* dockScroll = nullptr;
    for (QScrollArea* a : win.chatDock_->findChildren<QScrollArea*>()) dockScroll = a;
    checkSurface(win.chatDock_, dockScroll, "docked");
    checkSliver(win.chatDock_, dockScroll, "docked");
    win.chatDock_->setMinimumWidth(0);
    win.resizeDocks({win.chatDock_}, {230}, Qt::Horizontal);   // squeeze it
    QTest::qWait(300);
    checkNarrow(win.chatDock_, dockScroll, "docked narrow");

    // The floating/compact shape uses the same transcript widget.
    win.chatDock_->setFloating(true);
    win.chatDock_->resize(360, 460);
    QTest::qWait(300);
    checkSurface(win.chatDock_, dockScroll, "floating");
    win.chatDock_->resize(240, 460);   // narrow float: bubbles at both edges
    QTest::qWait(300);
    checkNarrow(win.chatDock_, dockScroll, "floating narrow");
    win.chatDock_->setFloating(false);
    QTest::qWait(200);

    // …and so does the context menu's panel.
    win.ensureChatMenuPanel();
    QVERIFY(win.chatMenuPanel_);
    // It normally lives inside the menu's QWidgetAction; show it in place so it
    // lays out (a hidden scroll area has no range to scroll).
    win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel_->show();
    // The panel is built lazily and mirrors the SHARED history, which these
    // direct dock appends never touched — mirror the same volume into it.
    for (int i = 0; i < 8; ++i) {
      win.chatMirror(QStringLiteral("You"),
                     QStringLiteral("Loading the image into incognito, converting to "
                                    "black & white and cropping to portrait 3:4 (%1).").arg(i),
                     false);
      win.chatMirror(QStringLiteral("Assistant"),
                     QStringLiteral("Working on it — this reply is deliberately long so "
                                    "the row is taller than a line and gets clipped by "
                                    "the viewport edge while scrolling (%1).").arg(i),
                     false);
    }
    QTest::qWait(200);
    checkSurface(win.chatMenuPanel_,
                 win.chatMenuPanel_->findChild<QScrollArea*>("chatMenuTranscript"), "menu panel");
    beat();
  }

  // The transcript's jump pills float in the bottom-right corner — exactly where
  // an assistant row's "…" lands when that row is the one being clipped. Two
  // round controls stacked are unclickable, so the pills yield to the button and
  // come straight back when it moves on.
  void chatJumpPillsYieldToTheRowMenu() {
    MainWindow win(nullptr, false);
    win.resize(1100, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    for (int i = 0; i < 10; ++i)
      win.chatDock_->appendAssistant(
          QStringLiteral("Row %1 — long enough that the transcript scrolls and a row "
                         "gets clipped at the bottom edge of the viewport.").arg(i));
    // A NARROW dock: the bubbles then reach the width cap, so an assistant row's
    // "…" (it hangs off the right) lands in the pills' corner.
    win.chatDock_->setMinimumWidth(0);
    win.resizeDocks({win.chatDock_}, {250}, Qt::Horizontal);
    QScrollArea* scroll = nullptr;
    for (QScrollArea* a : win.chatDock_->findChildren<QScrollArea*>()) scroll = a;
    QVERIFY(scroll);
    QScrollBar* bar = scroll->verticalScrollBar();
    QTRY_VERIFY(bar->maximum() > 24);
    bar->setValue(bar->maximum() / 2);   // mid-log: both pills want to show
    QTest::qWait(200);
    const auto jumps = win.chatDock_->findChildren<QToolButton*>(QStringLiteral("chatJumpBtn"));
    QCOMPARE(jumps.size(), 2);
    // Precondition: no row menu on screen, so the pills' own rule lets them show
    // (a scroll can leave one from an earlier hover, and they yield to it). The
    // clear + nudge is re-run each poll, since only a scroll re-evaluates it.
    const auto pillsUp = [&] {
      for (QToolButton* m : win.chatDock_->findChildren<QToolButton*>("chatCardMore"))
        m->hide();
      // Re-centre each poll: the bubble-width pass keeps changing the range while
      // the transcript settles, and an end position legitimately hides one pill.
      bar->setValue(bar->maximum() / 2);
      bar->setValue(bar->value() + 1);
      bar->setValue(bar->value() - 1);
      return jumps[0]->isVisible() && jumps[1]->isVisible();
    };
    QTRY_VERIFY(pillsUp());
    const QRect pills = QRect(jumps[0]->mapToGlobal(QPoint(0, 0)), jumps[0]->size())
                            .united(QRect(jumps[1]->mapToGlobal(QPoint(0, 0)), jumps[1]->size()));

    // Show one row's "…" and park it ON the pills — which is where a wide bubble
    // puts it in a real conversation (the button hangs off the bubble's right
    // edge, and the pills float in that same corner).
    QFrame* card = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>())
      if (f->property("chatMoreBtn").isValid() &&
          QRect(f->mapToGlobal(QPoint(0, 0)), f->size())
              .intersects(QRect(scroll->viewport()->mapToGlobal(QPoint(0, 0)),
                                scroll->viewport()->size())))
        card = f;
    QVERIFY(card);
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(card, &enter);
    auto* more = qobject_cast<QToolButton*>(card->property("chatMoreBtn").value<QObject*>());
    QVERIFY(more && more->isVisible());
    QVERIFY2(jumps[0]->isVisible() && jumps[1]->isVisible(),
             "the pills should still be up before the button reaches them");
    more->move(more->parentWidget()->mapFromGlobal(pills.topLeft()));
    QVERIFY2(QRect(more->mapToGlobal(QPoint(0, 0)), more->size()).intersects(pills),
             "the test failed to park the button on the pills");
    QVERIFY2(!jumps[0]->isVisible() && !jumps[1]->isVisible(),
             "the jump pills stayed under the row menu button");
    // …and they come back the moment it goes.
    more->hide();
    QTRY_VERIFY2(jumps[1]->isVisible(), "the jump pills never came back");
    beat();
  }

  // Error and stopped cards are settled rows, so they carry the SAME affordances
  // the browser gives them: the hover "…", the right-click menu (Copy message /
  // Insert into prompt) and the neutral Resend control. Desktop
  // offered none of it — the stopped card was built outside appendCard entirely.
  void chatErrorCardsCarryTheRowMenu() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    win.chatHistory_.append({QStringLiteral("user"), QStringLiteral("remove this project"), {}});
    // The panel exists BEFORE the failure, as it does in use — errors are
    // mirrored live (they never enter the replayed history).
    win.ensureChatMenuPanel();
    win.chatError(QStringLiteral("Could not read the assistant's plan: \"clear\" is an "
                                 "editor-settings op"),
                  QString());
    // …and a stopped turn, which is built through the PENDING card on both
    // surfaces, not through the ordinary append path.
    win.chatDock_->showPending();
    win.chatMirrorPending(true);
    win.chatDock_->markPendingStopped(QStringLiteral("remove this project"));
    win.chatMirrorStopped(QStringLiteral("remove this project"));
    QTest::qWait(200);

    const auto menuItems = [](QWidget* w) {
      QStringList names;
      QTimer::singleShot(0, [&names] {
        for (int i = 0; i < 100; ++i) {
          if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            for (QAction* a : m->actions()) names << a->text();
            m->close();
            return;
          }
          QTest::qWait(5);
        }
      });
      const QPoint pos = w->rect().center();
      QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, w->mapToGlobal(pos));
      QApplication::sendEvent(w, &ev);
      QTest::qWait(20);
      return names;
    };

    QList<QFrame*> errorCards = win.chatDock_->findChildren<QFrame*>("chatCardError");
    QVERIFY2(errorCards.size() >= 2, "expected the error card AND the stopped card");
    for (QFrame* card : errorCards) {
      const QString what = card->findChildren<QLabel*>().isEmpty()
                               ? QString()
                               : card->findChildren<QLabel*>().first()->text().left(20);
      QVERIFY2(card->property("chatMoreBtn").value<QObject*>(),
               qPrintable(QString("%1: no \"…\" on an error card").arg(what)));
      QCOMPARE(card->contextMenuPolicy(), Qt::CustomContextMenu);
      const QStringList items = menuItems(card);
      QVERIFY2(items.contains(QStringLiteral("Copy message")), qPrintable(what + ": no Copy"));
      QVERIFY2(items.contains(QStringLiteral("Insert into prompt")),
               qPrintable(what + ": no Insert into prompt"));
      // Resend is a USER-bubble item (browser parity); the error row's own
      // retry control is the neutral refresh button instead.
      QVERIFY2(!items.contains(QStringLiteral("Resend")),
               qPrintable(what + ": Resend leaked onto a non-user row"));
      QVERIFY2(!items.contains(QStringLiteral("Select all")),
               qPrintable(what + ": Select all is still offered"));
      QVERIFY2(card->findChild<QToolButton*>("chatRetry"),
               qPrintable(what + ": the error card has no Resend control"));
    }

    // The mirrored panel gets the same treatment (the browser's flyout does).
    // The panel's cards are only on screen while its menu is open — show it, or
    // the row menu rightly refuses to pop into an invisible surface.
    win.chatMenuPanel_->setGeometry(20, 20, 340, 620);
    win.chatMenuPanel_->show();
    QTest::qWait(150);
    QList<QFrame*> panelErrors = win.chatMenuPanel_->findChildren<QFrame*>("chatCardError");
    QVERIFY2(!panelErrors.isEmpty(), "the panel mirrored no error card");
    bool sawRetry = false;
    for (QFrame* card : panelErrors) {
      QVERIFY2(card->property("chatMoreBtn").value<QObject*>(),
               "no \"…\" on the panel's error card");
      const QStringList items = menuItems(card);
      QVERIFY(items.contains(QStringLiteral("Copy message")));
      QVERIFY(items.contains(QStringLiteral("Insert into prompt")));
      if (card->findChild<QToolButton*>("chatRetry")) sawRetry = true;
    }
    QVERIFY2(sawRetry, "the panel's error card offers no Resend");
    beat();
  }

  // Right-click on a transcript bubble → Copy message / Insert into prompt on
  // every card (user, assistant, note), plus Resend on user bubbles —
  // the same turn again, original attachments included. Also pins that a mouse
  // selection inside a bubble can be copied with the Copy shortcut.
  void chatBubbleContextMenu() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"hi there\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    win.actChat_->setChecked(true);   // the menu only pops on a VISIBLE surface
    QTRY_VERIFY(dock->isVisible());
    QTest::qWait(400);
    QImage att(24, 24, QImage::Format_RGB32);
    att.fill(Qt::green);
    dock->addAttachmentImage(att, "cat.png");
    win.onChatSend("highlight the cat");
    QTRY_VERIFY(!dock->isBusy());
    const int firstImages = mock.body.value("messages").toArray().last().toObject()
                                .value("images").toArray().size();
    QVERIFY2(firstImages >= 1, "the first turn did not carry the attachment");
    const int postsBefore = mock.allBodies.size();

    // Open the card's custom context menu and activate the item named `text`
    // (keyboard activation, so the blocking exec() returns that action).
    const auto pickMenuItem = [](QWidget* w, const QString& text) {
      bool found = false;
      QTimer::singleShot(0, [text, &found] {
        for (int i = 0; i < 100; ++i) {
          if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            for (QAction* a : m->actions())
              if (a->text() == text) {
                found = true;
                m->setActiveAction(a);
                QTest::keyClick(m, Qt::Key_Return);
                return;
              }
            m->close();
            return;
          }
          QTest::qWait(5);
        }
      });
      const QPoint pos = w->rect().center();
      QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, w->mapToGlobal(pos));
      QApplication::sendEvent(w, &ev);
      QTest::qWait(20);
      return found;
    };

    QFrame* userCard = nullptr;
    for (QFrame* f : dock->findChildren<QFrame*>("chatCardUser")) userCard = f;
    QVERIFY2(userCard, "no user bubble in the transcript");
    QFrame* assistantCard = nullptr;
    QLabel* userBody = nullptr;
    for (QLabel* l : dock->findChildren<QLabel*>()) {
      if (l->property("chatBody").toString() == QLatin1String("hi there"))
        assistantCard = qobject_cast<QFrame*>(l->parentWidget());
      if (l->property("chatBody").toString() == QLatin1String("highlight the cat"))
        userBody = l;
    }
    QVERIFY2(assistantCard, "no assistant bubble in the transcript");
    QVERIFY2(userBody, "no body label on the user bubble");

    // Copy message: the plain, role-stripped text lands on the clipboard.
    QGuiApplication::clipboard()->setText(QString());
    QVERIFY(pickMenuItem(userCard, QStringLiteral("Copy message")));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("highlight the cat"));
    QVERIFY(pickMenuItem(assistantCard, QStringLiteral("Copy message")));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hi there"));
    // …and a note card gets the same menu.
    dock->appendNote(QStringLiteral("just a note"));
    QFrame* noteCard = nullptr;
    for (QFrame* f : dock->findChildren<QFrame*>("chatCardMuted")) noteCard = f;
    QVERIFY2(noteCard, "no note card in the transcript");
    QVERIFY(pickMenuItem(noteCard, QStringLiteral("Copy message")));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("just a note"));
    // No Resend anywhere but on the user's own bubbles.
    QVERIFY(!pickMenuItem(assistantCard, QStringLiteral("Resend")));

    // "Select all" is NOT offered (dropped from both surfaces — a drag selects
    // what you actually want, and Copy message already takes the whole row).
    QVERIFY2(!pickMenuItem(userCard, QStringLiteral("Select all")),
             "Select all is still in the row menu");
    // What it stood in for still works: a selection inside the bubble copies
    // with the shortcut, because the label takes focus on click.
    userBody->setSelection(0, static_cast<int>(userBody->text().size()));
    userBody->setFocus(Qt::OtherFocusReason);
    QCOMPARE(userBody->selectedText(), QStringLiteral("highlight the cat"));
    // The offscreen platform leaves no window active after the popup closes, so
    // assert the WINDOW's recorded focus widget (what activation restores).
    QTRY_COMPARE(userBody->window()->focusWidget(), static_cast<QWidget*>(userBody));
    QGuiApplication::clipboard()->setText(QString());
    QTest::keyClick(userBody, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("highlight the cat"));

    // Insert into prompt: the text lands in the composer, which takes focus.
    auto* input = dock->findChild<QPlainTextEdit*>();
    QVERIFY(input);
    input->clear();
    QVERIFY(pickMenuItem(assistantCard, QStringLiteral("Insert into prompt")));
    QCOMPARE(input->toPlainText(), QStringLiteral("hi there"));
    input->clear();

    // Resend: a SECOND request with the same text AND the original turn's
    // attachment back in the payload (the tray drained on the first send).
    QVERIFY(pickMenuItem(userCard, QStringLiteral("Resend")));
    QTRY_VERIFY(!dock->isBusy());
    QCOMPARE(mock.allBodies.size(), postsBefore + 1);
    const QJsonObject last = mock.body.value("messages").toArray().last().toObject();
    QCOMPARE(last.value("content").toString(), QStringLiteral("highlight the cat"));
    QCOMPARE(last.value("images").toArray().size(), firstImages);
    QCOMPARE(dock->attachedImages().size(), 0);   // the resend drained the tray too
    beat();
  }

  // Escape closes the chat card menu natively: exec() returns no action, the
  // popup grab is released, nothing runs — guarded so the menu reveal
  // animation can never break it.
  void chatCardMenuEscapeCloses() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    win.actChat_->setChecked(true);   // the menu only pops on a VISIBLE surface
    QTRY_VERIFY(dock->isVisible());
    QTest::qWait(400);
    dock->appendNote(QStringLiteral("escape me"));
    QFrame* noteCard = nullptr;
    for (QFrame* f : dock->findChildren<QFrame*>("chatCardMuted")) noteCard = f;
    QVERIFY2(noteCard, "no note card in the transcript");

    bool sawMenu = false;
    QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));
    QTimer::singleShot(0, [&sawMenu] {
      if (!QTest::qWaitFor(
              [] { return QApplication::activePopupWidget() != nullptr; }, 500))
        return;
      auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!m) return;
      sawMenu = true;
      QTest::keyClick(m, Qt::Key_Escape);
    });
    const QPoint pos = noteCard->rect().center();
    QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, noteCard->mapToGlobal(pos));
    QApplication::sendEvent(noteCard, &ev);   // blocks in exec() until Escape lands
    QTest::qWait(20);
    QVERIFY2(sawMenu, "the card menu never opened");
    QTRY_VERIFY(QApplication::activePopupWidget() == nullptr);   // grab released
    // exec() returned nullptr: no action ran, the sentinel clipboard survives.
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("sentinel"));
    beat();
  }

  // Hovering a bubble reveals the ghost "⋯" in its bottom corner — LEFT on the
  // right-aligned user bubbles, RIGHT on assistant cards — and clicking it opens
  // the same menu as a right-click. Leave hides it again.
  void chatCardHoverMenuButton() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"hi there\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    dock->show();   // visibility checks below need visible ancestors
    win.onChatSend("highlight the cat");
    QTRY_VERIFY(!dock->isBusy());
    QTest::qWait(20);   // let the transcript layout activate (shows the cards)

    QFrame* userCard = nullptr;
    for (QFrame* f : dock->findChildren<QFrame*>("chatCardUser")) userCard = f;
    QVERIFY2(userCard, "no user bubble in the transcript");
    QFrame* assistantCard = nullptr;
    for (QLabel* l : dock->findChildren<QLabel*>())
      if (l->property("chatBody").toString() == QLatin1String("hi there"))
        assistantCard = qobject_cast<QFrame*>(l->parentWidget());
    QVERIFY2(assistantCard, "no assistant bubble in the transcript");

    // Offscreen has no real cursor, so hover is a synthetic Enter event.
    const auto hover = [](QWidget* w) {
      QEnterEvent ev(QPointF(2, 2), QPointF(2, 2), w->mapToGlobal(QPoint(2, 2)));
      QApplication::sendEvent(w, &ev);
    };
    // The button reparents beside its card on the first place — resolve it via
    // the property link, not parentage.
    auto* userMore = qobject_cast<QToolButton*>(
        userCard->property("chatMoreBtn").value<QObject*>());
    auto* asstMore = qobject_cast<QToolButton*>(
        assistantCard->property("chatMoreBtn").value<QObject*>());
    QVERIFY2(userMore && asstMore, "cards are missing the hover menu button");
    QVERIFY(userMore->toolTip().isEmpty());   // no "Message actions" tooltip
    QVERIFY(!userMore->isVisible());   // hidden at rest
    hover(userCard);
    QVERIFY(userMore->isVisible());
    // BESIDE the user bubble on its left — never overlapping it (parent coords
    // after placeCardMore reparents the button next to the card).
    QVERIFY(userMore->geometry().right() < userCard->geometry().left());
    QVERIFY(qAbs(userMore->geometry().bottom() - userCard->geometry().bottom()) <= 2);
    hover(assistantCard);
    QVERIFY(asstMore->isVisible());
    // …and beside the assistant bubble on its right.
    QVERIFY(asstMore->geometry().left() > assistantCard->geometry().right());
    QVERIFY(qAbs(asstMore->geometry().bottom() - assistantCard->geometry().bottom()) <= 2);
    // Leave hides after a short grace (the button sits across a gap) — park the
    // offscreen cursor away from both widgets first so the check can pass.
    QCursor::setPos(win.mapToGlobal(QPoint(5, 5)));
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(assistantCard, &leave);
    QTRY_VERIFY(!asstMore->isVisible());   // gone when the cursor moves off

    // Clicking it opens the SAME card menu (timer-driven pick, as above).
    bool sawMenu = false;
    QGuiApplication::clipboard()->setText(QString());
    QTimer::singleShot(0, [&sawMenu] {
      // Bounded wait for the blocking exec()'s popup, then activate the item.
      if (!QTest::qWaitFor(
              [] { return QApplication::activePopupWidget() != nullptr; }, 500))
        return;
      auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!m) return;
      for (QAction* a : m->actions())
        if (a->text() == QLatin1String("Copy message")) {
          sawMenu = true;
          m->setActiveAction(a);
          QTest::keyClick(m, Qt::Key_Return);
          return;
        }
      m->close();
    });
    userMore->click();   // blocking until the menu picks/closes
    QTest::qWait(20);
    QVERIFY2(sawMenu, "the hover button did not open the card menu");
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("highlight the cat"));
    // The menu is gone, so a Leave hides the button again (after the grace).
    QApplication::sendEvent(userCard, &leave);
    QTRY_VERIFY(!userMore->isVisible());
    beat();
  }

  // Retry resends the failed turn WITH its attachments: the tray drained on the
  // first send, so the retry handler re-queues them — thumbnails on the resent
  // bubble, images back in the wire payload.
  void chatRetryResendsAttachments() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.status = 0;
    mock.netError = "network down";
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QImage att(24, 24, QImage::Format_RGB32);
    att.fill(Qt::green);
    dock->addAttachmentImage(att, "cat.png");
    win.onChatSend("highlight the cat");
    QTRY_VERIFY(!dock->isBusy());
    QCOMPARE(dock->attachedImages().size(), 0);   // the send drained the tray
    const int firstImages = mock.body.value("messages").toArray().last().toObject()
                                .value("images").toArray().size();
    QVERIFY2(firstImages >= 1, "the failed turn carried the attachment");
    // The network heals; the error card's retry resends the whole turn.
    mock.status = 200;
    mock.netError.clear();
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"ok\",\"actions\":[]}"}}}})
                       .toJson(QJsonDocument::Compact);
    QToolButton* retry = nullptr;
    for (QToolButton* b : dock->findChildren<QToolButton*>("chatRetry")) retry = b;
    QVERIFY2(retry, "no retry button on the failed turn");
    retry->click();
    QTRY_VERIFY(!dock->isBusy());
    const auto msgs = mock.body.value("messages").toArray();
    const int retryImages =
        msgs.last().toObject().value("images").toArray().size();
    QCOMPARE(retryImages, firstImages);   // the resend carries the image again
    QCOMPARE(dock->attachedImages().size(), 0);   // and drained normally after
    beat();
  }
  // A follow-up turn with NO new attachments (an ask-card answer) keeps the prior
  // turn's attachments: the editing plan it triggers still adopts that image on an
  // empty canvas (browser parity — turnAttachments only resets when new ones queue).
  void chatFollowUpAdoptsPriorAttachment() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    // Turn 1: attachment queued, but the model only asks back — nothing to run,
    // so nothing is adopted and the canvas stays empty.
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"Which photo first?\",\"actions\":[]}"}}}})
                       .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QImage att(64, 48, QImage::Format_RGB32);
    att.fill(Qt::darkMagenta);
    dock->addAttachmentImage(att, "cat.png");
    win.onChatSend("edit these photos");
    QTRY_VERIFY(!dock->isBusy());
    QVERIFY(!win.canvas_->hasImage());
    QCOMPARE(dock->attachedImages().size(), 0);   // the send drained the tray
    // Turn 2: the attachment-less answer triggers an editing plan — it must still
    // adopt turn 1's image instead of failing on the empty canvas.
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Making it black and white.\",\"actions\":"
                      "[{\"op\":\"filter\",\"mode\":\"bw\"}]}"}}}})
                       .toJson(QJsonDocument::Compact);
    win.onChatSend("the first one");
    QTRY_VERIFY(win.canvas_->hasImage());
    beat();
  }

  // §2.1 multi-image plans: one turn edits SEVERAL attached images — each
  // `image` op switches the working image to that attachment, and each `save`
  // persists the result as its own LOCAL project (fresh id per save, named
  // after the image it was working on, suffixed when the name is taken).
  void chatMultiImagePlanSavesOneProjectPerImage() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const char* plan) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", plan}}}})
          .toJson(QJsonDocument::Compact);
    };
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Both done.\",\"actions\":["
        "{\"op\":\"image\",\"index\":1},{\"op\":\"filter\",\"mode\":\"bw\"},{\"op\":\"save\"},"
        "{\"op\":\"image\",\"index\":2},{\"op\":\"filter\",\"mode\":\"sepia\"},"
        "{\"op\":\"save\",\"name\":\"second\"}]}"));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QImage shore(64, 48, QImage::Format_RGB32);
    shore.fill(Qt::darkCyan);
    QImage dunes(40, 30, QImage::Format_RGB32);
    dunes.fill(Qt::darkMagenta);
    dock->addAttachmentImage(shore, "shore.jpg");
    dock->addAttachmentImage(dunes, "dunes.png");
    const int before = int(win.projectList_.size());
    win.onChatSend("make the first b&w and the second sepia, then save both");
    QTRY_VERIFY(!dock->isBusy());
    // One project per image, in plan order: the unnamed save took the name of
    // the attachment it was working on, the named one kept its own.
    QTRY_COMPARE(int(win.projectList_.size()), before + 2);
    QCOMPARE(QString::fromStdString(win.projectList_.at(before).meta.name),
             QStringLiteral("shore"));
    QCOMPARE(QString::fromStdString(win.projectList_.at(before + 1).meta.name),
             QStringLiteral("second"));
    QVERIFY2(win.projectList_.at(before).meta.id != win.projectList_.at(before + 1).meta.id,
             "each save must promote to a FRESH project, never overwrite the last");
    // …and each one holds ITS image, not the last one processed.
    QImage firstSaved, secondSaved;
    QVERIFY(firstSaved.load(win.projectList_.at(before).imagePath));
    QVERIFY(secondSaved.load(win.projectList_.at(before + 1).imagePath));
    QCOMPARE(firstSaved.size(), shore.size());
    QCOMPARE(secondSaved.size(), dunes.size());
    // The LAST processed image stays in the editor, with its own filter.
    QVERIFY(win.canvas_->hasImage());
    QCOMPARE(win.settings_.imageFilter, QStringLiteral("sepia"));
    QCOMPARE(win.activeProjectName(), QStringLiteral("second"));

    // A follow-up turn saving image 1 again cannot reuse the taken name: it
    // suffixes instead of losing the save.
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Saved again.\",\"actions\":["
        "{\"op\":\"image\",\"index\":1},{\"op\":\"save\"}]}"));
    win.onChatSend("save the first one again");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_COMPARE(int(win.projectList_.size()), before + 3);
    QCOMPARE(QString::fromStdString(win.projectList_.at(before + 2).meta.name),
             QStringLiteral("shore 2"));
    beat();
  }

  // §3.0: a multi-image plan that also draws is still ONE round — and it must not
  // apologise for a pass that no longer exists (this used to append a "layout
  // correction skipped" note).
  void chatMultiImageLayoutStillOneRound() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Outlined.\",\"actions\":["
                      "{\"op\":\"image\",\"index\":1},{\"op\":\"layout\",\"lines\":["
                      "{\"points\":[{\"x\":4,\"y\":4},{\"x\":20,\"y\":4},"
                      "{\"x\":20,\"y\":20},{\"x\":4,\"y\":4}]}]}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QImage att(64, 48, QImage::Format_RGB32);
    att.fill(Qt::darkYellow);
    dock->addAttachmentImage(att, "cat.jpg");
    win.onChatSend("outline both");
    QTRY_VERIFY(!dock->isBusy());
    QCOMPARE(mock.allBodies.size(), 1);   // nothing went out behind the turn
    QVERIFY2(!chatTranscriptHas(dock, "correction"),
             "no self-check note may reach the transcript");
    beat();
  }

  // §2.1: an image index the turn cannot satisfy costs that ACTION a warning,
  // never the rest of the plan; a save with nothing on the canvas is skipped
  // the same way instead of failing the turn.
  // A restored session must keep its PROJECT identity: without the binding, a
  // relaunch showed the project's pixels while activeProjectId_ was empty, so
  // deleting that project later left its image orphaned on the canvas.
  void sessionRoundTripsTheActiveProjectBinding() {
    MainWindow win(nullptr, false);
    win.resize(1200, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(24, 24, QImage::Format_RGB32);
    img.fill(Qt::darkMagenta);
    win.loadImageWithLayout(img, QJsonObject());
    win.createLocalProject(QStringLiteral("session-bind"), /*announce=*/false);
    QVERIFY(!win.activeProjectId_.isEmpty());
    win.saveSessionNow();
    const auto sess = stencil::gui::fileStore::loadSession();
    QVERIFY(sess.has_value());
    QCOMPARE(sess->activeProjectId, win.activeProjectId_);
    // …and the restore path re-binds it (project still exists in the list).
    MainWindow win2(nullptr, true);
    QCOMPARE(win2.activeProjectId_, win.activeProjectId_);
  }

  void chatMultiImageOutOfRangeAndEmptySaveWarn() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const char* plan) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", plan}}}})
          .toJson(QJsonDocument::Compact);
    };
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"The fourth one.\",\"actions\":["
        "{\"op\":\"image\",\"index\":4},{\"op\":\"filter\",\"mode\":\"bw\"}]}"));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QImage att(64, 48, QImage::Format_RGB32);
    att.fill(Qt::darkGreen);
    dock->addAttachmentImage(att, "cat.jpg");
    win.onChatSend("do the fourth one");
    QTRY_VERIFY(!dock->isBusy());
    QVERIFY2(chatTranscriptHas(dock, "attached image 4"),
             "an unsatisfiable index must warn, naming it");
    QCOMPARE(win.settings_.imageFilter, QStringLiteral("bw"));   // the rest still ran

    // Nothing on the canvas: the save is skipped with a warning, no project.
    win.resetToBlankEditor();   // the trash button's body, minus its confirmation
    QTRY_VERIFY(!win.canvas_->hasImage());
    const int before = int(win.projectList_.size());
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Saving.\",\"actions\":[{\"op\":\"save\",\"name\":\"x\"}]}"));
    win.onChatSend("save it");
    QTRY_VERIFY(!dock->isBusy());
    QVERIFY2(chatTranscriptHas(dock, "no working image"), "an empty save must warn");
    QCOMPARE(int(win.projectList_.size()), before);
    beat();
  }

  // §10 project management from chat: a removeProject plan runs the projects
  // dialog's Delete flow — confirm included (auto-accepted here) — removing
  // exactly the named project; a DECLINED clearProjects confirm lands as a
  // "clear canceled" note with every project still in place.
  void chatRemoveProjectConfirmsAndClearDeclineNotes() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const char* plan) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", plan}}}})
          .toJson(QJsonDocument::Compact);
    };
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    // Seed two local projects to pick between.
    QImage img(24, 24, QImage::Format_RGB32);
    img.fill(Qt::darkBlue);
    const QString goneId = win.addImageProjectEntry(img, "chat del target");
    const QString keptId = win.addImageProjectEntry(img, "chat del keeper");
    QVERIFY(!goneId.isEmpty() && !keptId.isEmpty());
    const int before = int(win.projectList_.size());

    // Remove one by name; the blocking QMessageBox confirm is answered "Yes".
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Removed.\",\"actions\":["
        "{\"op\":\"removeProject\",\"name\":\"chat del target\"}]}"));
    dismissModal("Yes");
    win.onChatSend("delete the chat del target project");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_COMPARE(int(win.projectList_.size()), before - 1);
    QVERIFY2(!win.findProject(goneId.toStdString()), "the named project must be gone");
    QVERIFY2(win.findProject(keptId.toStdString()), "the other project must remain");

    // clearProjects, confirm DECLINED: nothing removed, the note says so.
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Clearing.\",\"actions\":[{\"op\":\"clearProjects\"}]}"));
    dismissModal("No");
    win.onChatSend("clear all my projects");
    QTRY_VERIFY(!dock->isBusy());
    QCOMPARE(int(win.projectList_.size()), before - 1);
    QVERIFY2(chatTranscriptHas(dock, "clear canceled"),
             "a declined clear must land as a note");
    beat();
  }

  // §10 removeProject{current:true} with NOTHING saved but an image open (the
  // unsaved / incognito editor the user was looking at): answering "no saved
  // project is open" is a refusal on a technicality, so it falls back to the
  // `clear` flow behind the SAME confirm. Declined keeps the picture; accepted
  // takes the image and its lines. With a saved project open, the old path runs.
  void chatRemoveCurrentFallsBackToClearWhenNothingIsSaved() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const char* plan) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", plan}}}})
          .toJson(QJsonDocument::Compact);
    };
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    const char* removeCurrent =
        "{\"version\":1,\"reply\":\"Removing.\",\"actions\":["
        "{\"op\":\"removeProject\",\"current\":true}]}";

    // The reported case: an incognito editor holding an edited image — nothing
    // saved (incognito blocks the project promotion a normal load would do).
    CanvasWidget* canvas = win.canvas_;
    QVERIFY(canvas);
    win.actIncognito_->setChecked(true);
    QImage shot(120, 90, QImage::Format_RGB32);
    shot.fill(Qt::darkCyan);
    canvas->loadFromImage(shot);
    QTRY_VERIFY(canvas->hasImage());
    QVERIFY(win.incognito_ && win.activeProjectId_.isEmpty());
    stencil::core::Line line;
    line.points = {{10, 10}, {80, 40}};
    canvas->setLines({line});
    QCOMPARE(static_cast<int>(canvas->lines().size()), 1);

    // Declined: the confirm ran, nothing went.
    mock.queue.append(wrap(removeCurrent));
    dismissModal("No");
    win.onChatSend("remove this project");
    QTRY_VERIFY(!dock->isBusy());
    QVERIFY2(canvas->hasImage(), "a declined confirm must keep the image");
    QCOMPARE(static_cast<int>(canvas->lines().size()), 1);
    QVERIFY2(chatTranscriptHas(dock, "removal canceled"),
             "a declined confirm is a note, never a failed plan");
    QVERIFY2(!chatTranscriptHas(dock, "no saved project is open"),
             "the technicality refusal must be gone");

    // Accepted: the §10 clear flow — image and lines go, the editor is empty.
    mock.queue.append(wrap(removeCurrent));
    dismissModal("Yes");
    win.onChatSend("remove this project");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_VERIFY2(!canvas->hasImage(), "the accepted fallback must clear the image");
    QCOMPARE(static_cast<int>(canvas->lines().size()), 0);

    // With a SAVED project open the old path is unchanged: the project itself goes.
    win.actIncognito_->setChecked(false);
    QImage img(24, 24, QImage::Format_RGB32);
    img.fill(Qt::darkBlue);
    const QString id = win.addImageProjectEntry(img, "chat current target");
    QVERIFY(!id.isEmpty());
    win.activeProjectId_ = id;
    const int before = int(win.projectList_.size());
    mock.queue.append(wrap(removeCurrent));
    dismissModal("Yes");
    win.onChatSend("remove this project");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_COMPARE(int(win.projectList_.size()), before - 1);
    QVERIFY2(!win.findProject(id.toStdString()), "the saved project must be the one removed");
    beat();
  }

  // §10 clearChat from chat: DEFERRED (the plan's other action runs first even
  // when clearChat is listed first) and always confirmed. Declined: a "clear
  // canceled" note, everything kept. Accepted: dock transcript, chatHistory_,
  // the §12 persisted copy AND the §7 text-only latch all clear.
  void chatClearChatDefersConfirmsAndClears() {
    using stencil::gui::Project;
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    win.settings_.saveChatsWithProject = true;
    MockChatTransport mock;
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    // A local project to file the persisted copy under (§12).
    win.adoptCanvasAsLocalProject();
    QVERIFY(!win.activeProjectId_.isEmpty());
    const QString projectId = win.activeProjectId_;
    const auto wrap = [](const char* plan) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", plan}}}})
          .toJson(QJsonDocument::Compact);
    };

    // Declined round: clearChat FIRST, units second — the units op still runs
    // (deferral), and the decline lands as a note with everything kept.
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Inches it is — clearing next.\",\"actions\":["
        "{\"op\":\"clearChat\"},{\"op\":\"units\",\"value\":\"in\"}]}"));
    // The deferred confirm is QUEUED at turn end, so arm the dismissal AFTER
    // the send: its poll then runs inside the modal's own event loop.
    win.onChatSend("switch to inches, then clear the chat");
    dismissModal("No");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_VERIFY2(chatTranscriptHas(dock, "clear canceled"),
                 "a declined confirm must land as a note");
    QCOMPARE(win.settings_.units, QString("in"));  // ran despite being listed second
    win.applyUnits("cm");                          // tidy the persisted setting
    QCOMPARE(win.chatHistory_.size(), 2);          // user + assistant kept
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY2(pr && !pr->chat.isEmpty(), "the persisted copy must survive a decline");
    }

    // Accepted round: transcript + history + persisted copy go, latch re-arms.
    win.chatTextOnlyKey_ = QStringLiteral("some|other|model");
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Clearing.\",\"actions\":[{\"op\":\"clearChat\"}]}"));
    win.onChatSend("clear the chat");
    dismissModal("Yes");   // after the send — the confirm is queued (see above)
    QTRY_VERIFY(!dock->isBusy());
    QTRY_VERIFY2(win.chatHistory_.isEmpty(), "the replay history must clear");
    QTRY_VERIFY2(assistantBubbleTexts(dock).isEmpty(), "the transcript must clear");
    QVERIFY2(win.chatTextOnlyKey_.isEmpty(), "the §7 text-only latch must re-arm");
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY2(pr && pr->chat.isEmpty(), "the §12 persisted copy must clear (§12.2)");
    }

    // Tidy the dev state dir: drop the project this test created.
    dismissModal("Yes");
    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);
    beat();
  }

  // §7 edge map: a turn that carries the working snapshot carries a SECOND
  // image — the contour render — directly after it, with the exact suffix
  // sentence riding along, and the edge map is never replayed on later turns.
  // §3.0: a layout answer ends the turn, so ONE request goes out — the edge map
  // belongs to the main turn and to nothing else.
  void chatEdgeMapRidesAlong() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.canvas_->hasImage());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Outlined.\",\"actions\":[{\"op\":\"layout\","
                      "\"lines\":[{\"points\":[{\"x\":40,\"y\":40},{\"x\":80,\"y\":40},"
                      "{\"x\":80,\"y\":80},{\"x\":40,\"y\":40}]}]}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("outline the box");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QCOMPARE(mock.allBodies.size(), 1);   // the turn, and nothing behind it

    const QString sentence =
        "The second attached image is an edge-map render of the working image at the "
        "same pixel coordinates: use it to place outline points on real edges.";
    const QJsonArray msgs = mock.allBodies.first().value("messages").toArray();
    const QJsonArray images = msgs.last().toObject().value("images").toArray();
    QCOMPARE(images.size(), 2);   // snapshot first, edge map second
    QVERIFY2(images.at(0).toString() != images.at(1).toString(),
             "the edge map must be a distinct (contoured) render");
    const QString sys = msgs.at(0).toObject().value("content").toString();
    QVERIFY2(sys.endsWith(sentence), "suffix must end with the exact edge-map sentence");
    QCOMPARE(sys.count(sentence), qsizetype(1));

    // The next turn replays the PRIOR turn's snapshot — never its edge map.
    mock.allBodies.clear();
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"ok\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.onChatSend("thanks");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    const QJsonArray msgs2 = mock.allBodies.first().value("messages").toArray();
    // system, user1, assistant1, user2: the replayed user1 keeps exactly its
    // snapshot; the fresh turn carries snapshot + edge map again.
    QCOMPARE(msgs2.at(1).toObject().value("role").toString(), QString("user"));
    const QJsonArray prior = msgs2.at(1).toObject().value("images").toArray();
    QCOMPARE(prior.size(), 1);
    QCOMPARE(prior.at(0).toString(), images.at(0).toString());   // the snapshot
    QCOMPARE(msgs2.last().toObject().value("images").toArray().size(), 2);
    beat();
  }

  // §7 auto-continuation: the re-sent round carries the NEW working snapshot
  // plus its edge map (browser parity), with the exact suffix sentence — while
  // the imageless first round carried neither image nor sentence.
  void chatEdgeMapOnContinuation() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(!win.canvas_->hasImage());   // empty editor: turn 1 has no snapshot
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    // A load-only plan (blank) triggers the single §7 continuation round. A
    // COLOURED blank: the edge map contours to flat white, so a white blank's
    // snapshot would coincide with it byte-for-byte and void the ≠ check below.
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Blank page.\",\"actions\":"
                      "[{\"op\":\"blank\",\"color\":\"#3366cc\",\"format\":\"a6\"}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("give me a blank a6 page");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QVERIFY(win.canvas_->hasImage());
    QCOMPARE(mock.allBodies.size(), 2);   // the turn + exactly one continuation

    const QString sentence =
        "The second attached image is an edge-map render of the working image at the "
        "same pixel coordinates: use it to place outline points on real edges.";
    const QJsonArray msgs1 = mock.allBodies.first().value("messages").toArray();
    QVERIFY(!msgs1.last().toObject().contains("images"));   // nothing to snapshot yet
    QVERIFY(!msgs1.at(0).toObject().value("content").toString().contains(sentence));

    const QJsonArray msgs2 = mock.allBodies.at(1).value("messages").toArray();
    const QJsonArray images = msgs2.last().toObject().value("images").toArray();
    QCOMPARE(images.size(), 2);   // fresh snapshot + its edge map
    QVERIFY(images.at(0).toString() != images.at(1).toString());
    QVERIFY2(msgs2.at(0).toObject().value("content").toString().endsWith(sentence),
             "continuation suffix must end with the exact edge-map sentence");
    beat();
  }

  // §10 openUrl awaits the load + amended §7: a plan [openUrl, filter] must land
  // the filter on the fetched picture (no "no working image" race with the async
  // MediaLoader), then continue ONCE — it contains a load op and drew no layout.
  void chatOpenUrlAwaitsLoadThenContinues() {
    // A tiny local HTTP server serving one PNG, so MediaLoader has a real
    // download to await — fully offline.
    QImage src(20, 14, QImage::Format_RGB32);
    src.fill(QColor("#3366cc"));
    QByteArray png;
    QBuffer buf(&png);
    QVERIFY(buf.open(QIODevice::WriteOnly));
    QVERIFY(src.save(&buf, "PNG"));
    QTcpServer http;
    QVERIFY(http.listen(QHostAddress::LocalHost, 0));
    connect(&http, &QTcpServer::newConnection, this, [&http, &png] {
      QTcpSocket* s = http.nextPendingConnection();
      connect(s, &QTcpSocket::readyRead, s, [s, &png] {
        s->readAll();
        s->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: " +
                 QByteArray::number(png.size()) + "\r\nConnection: close\r\n\r\n" + png);
        s->disconnectFromHost();
      });
      connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    });
    const QString url = QStringLiteral("http://127.0.0.1:%1/cat.png").arg(http.serverPort());

    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(!win.canvas_->hasImage());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    // The unknown op yields a round-1 WARNING, which must survive into the
    // single final bubble (§7 one-reply parity below).
    const QString plan = QStringLiteral(
        "{\"version\":1,\"reply\":\"Loading and filtering.\",\"actions\":["
        "{\"op\":\"sparkle\"},"
        "{\"op\":\"openUrl\",\"url\":\"%1\"},{\"op\":\"filter\",\"mode\":\"bw\"}]}").arg(url);
    mock.queue.append(QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content", plan}}}}).toJson(QJsonDocument::Compact));
    // The continuation round answers chat-only.
    mock.queue.append(QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content", "All done."}}}}).toJson(QJsonDocument::Compact));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);

    win.onChatSend(QStringLiteral("open %1, then make it b&w").arg(url));
    QTRY_VERIFY(!win.chatDock_->isBusy());
    // openUrl waited for the download: the fetched picture IS the working image…
    QVERIFY(win.canvas_->hasImage());
    QCOMPARE(win.canvas_->effectiveOriginalImage().size(), QSize(20, 14));
    // …and the filter landed on it (a grayscale pixel, not the blue source).
    const QImage out = win.canvas_->renderToImage(false);
    const QRgb px = out.pixel(out.width() / 2, out.height() / 2);
    QVERIFY2(qRed(px) == qGreen(px) && qGreen(px) == qBlue(px),
             "the b&w filter did not land on the loaded image");
    // Amended §7: the mixed load+edit plan (no layout) continued exactly once,
    // with the fresh snapshot attached.
    QCOMPARE(mock.allBodies.size(), 2);
    const QJsonArray contMsgs = mock.allBodies.at(1).value("messages").toArray();
    QVERIFY2(!contMsgs.last().toObject().value("images").toArray().isEmpty(),
             "the continuation round must attach the fresh snapshot");
    // ONE final reply (browser parity): round 1's bubble was held, its warnings
    // folded into the continuation's bubble; the intermediate reply never rendered.
    const QStringList bubbles = assistantBubbleTexts(win.chatDock_);
    QCOMPARE(bubbles.size(), 1);
    QVERIFY2(bubbles.first().contains(QStringLiteral("All done.")),
             "the single bubble must carry the FINAL round's reply");
    QVERIFY2(bubbles.first().contains(QStringLiteral("Skipped unknown op \"sparkle\".")),
             "round 1's warnings must ride in the final bubble");
    QVERIFY2(!chatTranscriptHas(win.chatDock_, QStringLiteral("Loading and filtering.")),
             "round 1's reply must not render as its own bubble");
    // …while the model-side history still keeps round 1's own answer per round.
    QCOMPARE(win.chatHistory_.at(1).text, QStringLiteral("Loading and filtering."));
    beat();
  }

  // §7 one-reply companion: a load-shaped plan HOLDS its bubble, but when the
  // continuation cannot launch (here: a text-only model, nothing to continue
  // WITH) the held reply posts right then — one bubble, nothing lost, no round 2.
  void chatHeldReplyPostsWhenContinuationSkipped() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    const stencil::llm::LlmSettings cfg = win.currentLlmSettings();
    win.chatTextOnlyKey_ =
        QStringList{cfg.provider, cfg.baseUrl, cfg.model, cfg.serverUrl}.join(QLatin1Char('|'));
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Here is your page.\",\"actions\":"
                      "[{\"op\":\"blank\",\"color\":\"#ffffff\",\"format\":\"a6\"}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("blank a6 page");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QVERIFY(win.canvas_->hasImage());
    QCOMPARE(mock.allBodies.size(), 1);   // no continuation round launched
    const QStringList bubbles = assistantBubbleTexts(win.chatDock_);
    QCOMPARE(bubbles.size(), 1);
    QVERIFY2(bubbles.first().contains(QStringLiteral("Here is your page.")),
             "the held round-1 reply must flush when no continuation fires");
    beat();
  }

  // §7 regression: a ZERO-line layout op validates but draws nothing, so a
  // [blank, layout{lines:[]}] plan must still continue — counting it as "drew"
  // suppressed the very round meant to draw ("blank page — drawing now" ended
  // as an empty page and a promise).
  void chatEmptyLayoutOpStillContinues() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(!win.canvas_->hasImage());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.queue.append(QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Blank made — drawing now.\",\"actions\":"
                      "[{\"op\":\"blank\",\"color\":\"#3366cc\",\"format\":\"a6\"},"
                      "{\"op\":\"layout\",\"lines\":[]}]}"}}}})
                          .toJson(QJsonDocument::Compact));
    mock.queue.append(QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content", "All done drawing."}}}})
                          .toJson(QJsonDocument::Compact));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("blank 20x20 page with a smiley");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QVERIFY(win.canvas_->hasImage());
    QCOMPARE(mock.allBodies.size(), 2);   // the turn + exactly one continuation
    const QJsonArray msgs2 = mock.allBodies.at(1).value("messages").toArray();
    QVERIFY2(msgs2.last().toObject().value("content").toString().contains(
                 QStringLiteral("continue with it")),
             "round 2 must be the §7 continuation, carrying its note");
    // One final bubble (browser parity): round 1's reply was held and folded in.
    const QStringList bubbles = assistantBubbleTexts(win.chatDock_);
    QCOMPARE(bubbles.size(), 1);
    QVERIFY(bubbles.first().contains(QStringLiteral("All done drawing.")));
    beat();
  }

  // §7 companion: a layout op with REAL lines committed to its coordinates —
  // the same [blank, layout] shape must NOT continue, and (§3.0) must not send
  // anything else either: one round, done.
  void chatDrawnLayoutSuppressesContinuation() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(!win.canvas_->hasImage());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    // Full-frame line (huge coords clamp to the blank's bounds): drawn for real.
    mock.queue.append(QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Outlined.\",\"actions\":"
                      "[{\"op\":\"blank\",\"color\":\"#3366cc\",\"format\":\"a6\"},"
                      "{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":0,\"y\":0},"
                      "{\"x\":99999,\"y\":0},{\"x\":99999,\"y\":99999},{\"x\":0,\"y\":0}]}]}]}"}}}})
                          .toJson(QJsonDocument::Compact));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("blank page with a box drawn on it");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QVERIFY(win.canvas_->hasImage());
    QCOMPARE(int(win.canvas_->allLines().size()), 1);   // the line really drew
    QCOMPARE(mock.allBodies.size(), 1);   // the turn, and nothing behind it
    for (const QJsonObject& b : mock.allBodies)
      for (const QJsonValue& m : b.value("messages").toArray())
        QVERIFY2(!m.toObject().value("content").toString().contains(
                     QStringLiteral("continue with it")),
                 "a plan that placed real lines must never continue");
    beat();
  }

  // A reply bubble must HUG its text: the reserved label height is measured at
  // the width the layout actually gives it. Measuring a freshly appended card
  // at its default 100×30 child geometry reserved several times the needed
  // height and left ~50 px dead bands above and below the centered text.
  void chatBubbleHugsItsText() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.chatDock_->show();
    QTest::qWait(30);
    // Fill the transcript until it scrolls FIRST: appending into an already
    // scrollable transcript triggers no scrollbar toggle, so no healing
    // viewport-resize re-measure follows — exactly the intermittent case users
    // hit, where the first (wrong-width) reservation was the one that stuck.
    for (int i = 0; i < 10; ++i)
      win.chatDock_->appendAssistant(QStringLiteral("Filler bubble %1.").arg(i));
    auto* scroll = win.chatDock_->findChild<QScrollArea*>();
    QVERIFY(scroll);
    QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
    QTest::qWait(50);
    const QString text = QStringLiteral(
        "This reply is deliberately long enough to wrap across several transcript lines, "
        "so a height reserved at the wrong measurement width visibly disagrees with the "
        "height the rendered text actually needs — the regression this test guards.");
    win.chatDock_->appendAssistant(text);
    QTest::qWait(120);   // entrance animation + deferred layout settle
    QLabel* body = nullptr;
    for (QLabel* l : win.chatDock_->findChildren<QLabel*>())
      if (l->property("chatBody").toString() == text) body = l;
    QVERIFY(body);
    QVERIFY(body->width() > 100);   // really laid out at bubble width
    // What the text NEEDS at the rendered width — measured with the reservation
    // lifted, because QLabel::heightForWidth reports no less than minimumHeight.
    const int reserved = body->minimumHeight();
    body->setMinimumHeight(0);
    const int needed = body->heightForWidth(body->width());
    body->setMinimumHeight(reserved);
    QVERIFY2(reserved <= needed + 2,
             qPrintable(QStringLiteral("reserved %1 px for text needing %2 px")
                            .arg(reserved)
                            .arg(needed)));
    const auto* card = body->parentWidget();
    QVERIFY2(card->height() <= needed + 24,   // text + the card's 6px paddings, no dead band
             qPrintable(QStringLiteral("bubble %1 px tall around %2 px of text")
                            .arg(card->height())
                            .arg(needed)));
    beat();
  }

  // Transcript follow (chat stickiness): a send scrolls fully down to the
  // pending "…"; a reply follows only while the view already sits at the
  // bottom — never yanking a user who scrolled up to read history.
  void chatTranscriptFollowsSendsAndPinnedReplies() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.chatDock_->show();
    QTest::qWait(30);
    auto* scroll = win.chatDock_->findChild<QScrollArea*>();
    QVERIFY(scroll);
    auto* bar = scroll->verticalScrollBar();
    const QString filler = QStringLiteral(
        "Filler turn %1 — long enough to wrap across the bubble and give the "
        "transcript real scrollable height for the follow assertions below.");
    for (int i = 0; i < 8; ++i) {
      win.chatDock_->appendUser(filler.arg(i));
      win.chatDock_->appendAssistant(filler.arg(i + 100));
    }
    QTRY_VERIFY(bar->maximum() > 0);
    // A send lands the view at the very bottom, where the "…" card sits.
    bar->setValue(0);
    win.chatDock_->appendUser(QStringLiteral("newest question"));
    win.chatDock_->showPending();
    QTRY_VERIFY2(bar->maximum() > 0 && bar->value() == bar->maximum(),
                 "sending must scroll to the pending indicator at the bottom");
    QTest::qWait(50);   // drain the deferred scroll timers before scrolling away
    // Reading history releases the pin: a landing reply must not yank the view.
    bar->setValue(0);
    win.chatDock_->clearPending();
    win.chatDock_->appendAssistant(QStringLiteral("a reply landing mid-history"));
    QTest::qWait(80);
    QVERIFY2(bar->value() < bar->maximum() / 2,
             "a reply must not yank a reader back down from history");
    // Back at the bottom the pin re-arms: the next reply is followed.
    bar->setValue(bar->maximum());
    win.chatDock_->appendAssistant(QStringLiteral("and one the reader follows"));
    QTRY_VERIFY(bar->maximum() > 0 && bar->value() == bar->maximum());
    beat();
  }

  // The transcript's jump pills rest translucent (they float OVER bubbles and
  // fully covered a short message) and return to full opacity under the cursor.
  void chatJumpArrowsRestTranslucent() {   // …opaque now; the NAME is historical
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.chatDock_->show();
    QTest::qWait(30);
    auto* scroll = win.chatDock_->findChild<QScrollArea*>();
    QVERIFY(scroll);
    for (int i = 0; i < 10; ++i)
      win.chatDock_->appendAssistant(QStringLiteral("Row %1 padding the transcript out "
                                                    "until it scrolls.").arg(i));
    auto* bar = scroll->verticalScrollBar();
    QTRY_VERIFY(bar->maximum() > 24);
    bar->setValue(bar->maximum() / 2);   // mid-log: both pills visible
    const auto jumps =
        win.chatDock_->findChildren<QToolButton*>(QStringLiteral("chatJumpBtn"));
    QCOMPARE(jumps.size(), 2);
    // The browser's .chat-jump-btn is FULLY OPAQUE at rest (a 0.45 rest opacity
    // was a desktop invention, and it is what made these circles read as washed
    // out); hover brightens the glyph from --text-muted to --text-main.
    // …in whichever theme this window actually resolved to.
    const bool dark = stencil::gui::resolveDark(win.settings_.themeMode);
    const QColor muted = stencil::gui::themePalette(dark, win.settings_.accentColor).textMuted;
    const QColor main = stencil::gui::themePalette(dark, win.settings_.accentColor).textMain;
    const auto glyphIs = [](QToolButton* b, const QColor& want) {
      const QImage im = b->icon().pixmap(14, 14).toImage();
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x) {
          const QColor c = im.pixelColor(x, y);
          if (c.alpha() < 120) continue;
          if (qAbs(c.red() - want.red()) < 40 && qAbs(c.green() - want.green()) < 40 &&
              qAbs(c.blue() - want.blue()) < 40)
            return true;
        }
      return false;
    };
    for (QToolButton* b : jumps) {
      QTRY_VERIFY(b->isVisible());
      auto* fx = qobject_cast<QGraphicsOpacityEffect*>(b->graphicsEffect());
      QVERIFY2(fx, "jump pill must carry the rest-opacity effect");
      QCOMPARE(fx->opacity(), 1.0);
      QVERIFY2(glyphIs(b, muted), "the pill's rest glyph is not --text-muted");
      QEvent enter(QEvent::Enter);
      QCoreApplication::sendEvent(b, &enter);
      QCOMPARE(fx->opacity(), 1.0);
      QVERIFY2(glyphIs(b, main), "hover must brighten the glyph to --text-main");
      QEvent leave(QEvent::Leave);
      QCoreApplication::sendEvent(b, &leave);
      QCOMPARE(fx->opacity(), 1.0);
      QVERIFY2(glyphIs(b, muted), "leaving must drop the glyph back to --text-muted");
    }
    beat();
  }

  // §3.0: a turn ends when its plan has executed and its reply is shown — one model
  // round, nothing after it. This used to fan out one re-trace request PER LINE plus
  // up to three whole-layout self-check rounds, so a 17-line trace spent minutes
  // working (and failing) under a reply that was already on screen.
  void chatLayoutTurnIssuesExactlyOneRound() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.canvas_->hasImage());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";

    // 17 small boxes — every one of them would have earned its own refinement
    // request, and the spread would have earned suspect rounds on top.
    QStringList lines;
    for (int i = 0; i < 17; ++i) {
      const int x = 5 + (i % 6) * 30, y = 5 + (i / 6) * 30;
      lines << QStringLiteral("{\"points\":[{\"x\":%1,\"y\":%2},{\"x\":%3,\"y\":%2},"
                              "{\"x\":%3,\"y\":%4},{\"x\":%1,\"y\":%2}]}")
                   .arg(x).arg(y).arg(x + 20).arg(y + 20);
    }
    MockChatTransport mock;
    mock.response =
        QJsonDocument(
            QJsonObject{{"message",
                         QJsonObject{{"content",
                                      QStringLiteral("{\"version\":1,\"reply\":\"Outlined.\","
                                                     "\"actions\":[{\"op\":\"layout\",\"lines\":[%1]}]}")
                                          .arg(lines.join(QLatin1Char(',')))}}}})
            .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);

    QElapsedTimer clock;
    clock.start();
    win.onChatSend("outline every box");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    const qint64 settledMs = clock.elapsed();

    QCOMPARE(mock.allBodies.size(), 1);   // ONE round for the whole turn
    QCOMPARE(int(win.canvas_->allLines().size()), 17);   // …and it drew all of them
    // Nothing may be queued behind the reply either: the turn is over.
    QTest::qWait(200);
    QCOMPARE(mock.allBodies.size(), 1);
    QVERIFY2(settledMs < 2000, "the turn must settle with its reply, not minutes later");

    // No self-check / sharpening vocabulary may reach the user, in the transcript
    // or in the toast.
    const QString shown = dockText(win.chatDock_) +
                          (win.chatToast_ ? dockText(win.chatToast_) : QString());
    for (const char* word : {"sharpen", "self-check", "re-checked", "correction", "refine"})
      QVERIFY2(!shown.contains(QLatin1String(word), Qt::CaseInsensitive),
               qPrintable(QString("the reply still mentions \"%1\": %2")
                              .arg(QLatin1String(word), shown.simplified())));
    beat();
  }

  // The same rule seen from the wire: with the transport HOLDING the answer, exactly
  // one request is ever made — answering it settles the turn and parks nothing new.
  void chatLayoutTurnParksNothingAfterTheAnswer() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.canvas_->hasImage());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";

    DeferredChatTransport deferred;
    deferred.response =
        QJsonDocument(QJsonObject{
            {"message",
             QJsonObject{{"content",
                          "{\"version\":1,\"reply\":\"Outlined.\",\"actions\":[{\"op\":\"layout\","
                          "\"lines\":[{\"points\":[{\"x\":20,\"y\":20},{\"x\":50,\"y\":20},"
                          "{\"x\":50,\"y\":50},{\"x\":20,\"y\":20}]}]}]}"}}}})
            .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&deferred);
    win.onChatSend("outline the box");
    QCOMPARE(deferred.parked.size(), 1);   // the turn's own request, waiting
    QVERIFY(win.chatDock_->isBusy());

    deferred.answerNext();                 // …the answer lands
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QCOMPARE(int(win.canvas_->allLines().size()), 1);
    QTest::qWait(200);
    QVERIFY2(deferred.parked.isEmpty(), "a follow-up round was sent behind the reply");
    QCOMPARE(deferred.started, 1);
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "mainWindow.gui.moc"
