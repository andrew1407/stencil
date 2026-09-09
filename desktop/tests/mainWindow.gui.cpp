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
#include "../src/support/dockGrip.hpp"   // DockEdgeOverlay: the chat dock's resize-edge tint
#include "../src/canvas/dropZonesOverlay.hpp"
#include "../src/canvas/canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "chatDock.hpp"
#include "../src/app/dataExportController.hpp"
#include "pillSplitter.hpp"
#include "../src/app/selectionPanel.hpp"
#include "../src/app/selectedLineBar.hpp"
#include "../src/app/scrollReveal.hpp"
#include "fileStore.hpp"
#include "connectDialog.hpp"
#include "../src/support/appTooltip.hpp"
#include "serverClient.hpp"
#include "llmSettingsForm.hpp"
#include "../src/llm/chatPlanTarget.hpp"   // §10 chatPanel: the plan target that places the dock
#include "../src/llm/opPlan.hpp"
#include "../src/llm/planExecutor.hpp"
#include "mediaLoader.hpp"
#include "popover.hpp"
#include "iconSet.hpp"
#include "faceSwap.hpp"
#include "controlSwap.hpp"
#include "iconMotion.hpp"
#include "guiHelpers.hpp"
#include <QStyleFactory>
#include "theme.hpp"
#include "modalReveal.hpp"
#include "settingsDialog.hpp"
#include "../src/app/mainWindowHelpers.hpp"   // kNameChipBox
#include "../src/support/searchCombo.hpp"
#include <QScopeGuard>
#include <QtTest>
#include <QStyleOptionSlider>
#include <QButtonGroup>
#include <QRadioButton>
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
#include <algorithm>
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
  // ── Reading a surface flight ────────────────────────────────────────────────
  // A window, a popup menu and the tooltip form from motes streaming out of the control
  // that owns them and come apart into motes pouring back in (support/modalReveal.cpp,
  // menuReveal.cpp, appTooltip.hpp — the browser's js/ui/motion.js surfaceIn/surfaceOut).
  // The flight is a DisintegrateOverlay child of the window, and it carries the point it
  // is aimed at: that point IS "where the window comes out of", which is what these tests
  // are about. Q_OBJECT-free, so it is found by object name and cast statically.
  stencil::gui::DisintegrateOverlay* surfaceFlight(const QWidget* host) {
    stencil::gui::DisintegrateOverlay* found = nullptr;
    for (QWidget* w : host->findChildren<QWidget*>(
             QString::fromLatin1(stencil::gui::DisintegrateOverlay::kObjectName))) {
      auto* fx = static_cast<stencil::gui::DisintegrateOverlay*>(w);
      if (fx->surfacePicture().isValid()) found = fx;   // the newest one wins
    }
    return found;
  }

  // Where the live flight is aimed, in `host` coordinates; an invalid point = nothing
  // is flying.
  QPoint surfaceFlightTarget(const QWidget* host) {
    auto* fx = surfaceFlight(host);
    return fx ? fx->surfaceTarget() : QPoint(-1, -1);
  }

  // The GHOST twin of the above: the fallback for whatever the dust engine declines
  // outright (an unmeasurable box, a snapshot that failed to grab) — every dialog,
  // including the big `.app-modal` ones, dusts first (support/modalReveal.cpp
  // kDialogDustMaxCells). Found the same way, by its own object name (makeGhost).
  QLabel* modalGhost(const QWidget* host) {
    QLabel* found = nullptr;
    for (QLabel* g : host->findChildren<QLabel*>(QStringLiteral("stencilModalGhost")))
      if (g->isVisible()) found = g;   // the newest one wins
    return found;
  }

  // Where either flight mechanism (dust or ghost) STARTS, caught on its QEvent::Show
  // rather than polled later — a ghost's geometry is an active tween, so reading it even
  // 50ms in is already off. Only the FIRST match sticks, so an immediate accept/close's
  // CLOSE flight (same object name, starts at the dialog box, not the icon) can't clobber it.
  struct RevealOriginWatcher : QObject {
    QPoint origin{-1, -1};
    bool captured = false;
    void reset() { origin = QPoint(-1, -1); captured = false; }
    bool eventFilter(QObject* o, QEvent* e) override {
      if (captured || e->type() != QEvent::Show) return false;
      auto* w = qobject_cast<QWidget*>(o);
      if (!w) return false;
      if (w->objectName() == QLatin1String(stencil::gui::DisintegrateOverlay::kObjectName)) {
        auto* fx = static_cast<stencil::gui::DisintegrateOverlay*>(w);
        if (fx->surfacePicture().isValid()) { origin = fx->surfaceTarget(); captured = true; }
      } else if (w->objectName() == QLatin1String("stencilModalGhost")) {
        origin = w->geometry().center();
        captured = true;
      }
      return false;
    }
  };

  // A control's centre in the window's coordinates — what a flight out of it aims at.
  QPoint flightPointOf(const QWidget* control, const QWidget* host) {
    return control->mapTo(host, control->rect().center());
  }

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

  // A confirmation is a modal dialog that blocks the triggering call (Quit, Delete Project
  // File, …) — a QMessageBox, or the chrome-styled confirmModal (modalChrome.cpp) the
  // projects flows use. Arm this BEFORE triggering the action: it polls until a modal
  // CARRYING the named button appears (an unrelated modal — e.g. the projects dialog the
  // question will sit over — is skipped, not clicked) and clicks it, letting the
  // otherwise-blocked trigger() return with that answer.
  void dismissModal(const QString& buttonText) {
    QTimer::singleShot(0, [buttonText]() {
      for (int i = 0; i < 200; ++i) {
        if (auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
          for (QAbstractButton* b : dlg->findChildren<QAbstractButton*>())
            if (QString(b->text()).remove('&').compare(buttonText, Qt::CaseInsensitive) == 0) {
              b->click();
              return;
            }
        }
        QTest::qWait(5);
      }
    });
  }
}

class MainWindowGuiTest : public QObject {
  Q_OBJECT
  QString png_;

  // The suite runs with STENCIL_NO_ANIM=1; a case that drives real motion turns it off for
  // its own scope. Hold the returned guard for as long as the animation must run.
  // …and its counterpart, for a test that PAINTS rather than watches: the suite runs with
  // STENCIL_NO_ANIM=1, but a case that turns motion on can leave it on for whatever runs
  // next, and then a fixed wait races a flight. Pins it off for the case's own lifetime.
  [[nodiscard]] auto withoutMotion() {
    const QByteArray had = qgetenv("STENCIL_NO_ANIM");
    qputenv("STENCIL_NO_ANIM", "1");
    return qScopeGuard([had] {
      if (had.isEmpty()) qunsetenv("STENCIL_NO_ANIM"); else qputenv("STENCIL_NO_ANIM", had);
    });
  }

  [[nodiscard]] auto withMotion() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    return qScopeGuard([noAnim] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
  }

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
    png_ = QDir::temp().filePath("stencil_e2e_input.png");
    QVERIFY(img.save(png_, "PNG"));
  }






  // The chat dock's resize edge (browser .chat-resizer). The strip is QMainWindow chrome
  // with no widget of its own, so a mouse-transparent band is painted over it, in whichever
  // area the dock sits and only where Qt would actually start the resize.
  void chatResizeEdgeFollowsTheDockInEveryArea() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.chatDock_);
    win.chatDock_->show();
    QTest::qWait(120);
    auto* edge = win.chatEdge_;
    QVERIFY(edge);
    const struct { Qt::DockWidgetArea area; Qt::Orientation split; const char* name; } kAreas[] = {
        {Qt::LeftDockWidgetArea, Qt::Horizontal, "left"},
        {Qt::RightDockWidgetArea, Qt::Horizontal, "right"},
        {Qt::TopDockWidgetArea, Qt::Vertical, "top"},
        {Qt::BottomDockWidgetArea, Qt::Vertical, "bottom"},
    };
    for (const auto& a : kAreas) {
      win.addDockWidget(a.area, win.chatDock_, a.split);
      QTest::qWait(120);
      const QRect dock = win.chatDock_->geometry();
      const QRect hit = win.chatEdgeHit_;
      const QRect band = edge->geometry();
      QVERIFY2(edge->isVisible(), a.name);
      QVERIFY2(!hit.isEmpty(), a.name);
      // The strip sits OUTSIDE the panel, against the edge it is docked by.
      QVERIFY2(!hit.intersects(dock), a.name);
      if (a.area == Qt::LeftDockWidgetArea) QCOMPARE(hit.left(), dock.right() + 1);
      if (a.area == Qt::RightDockWidgetArea) QCOMPARE(hit.right(), dock.left() - 1);
      if (a.area == Qt::TopDockWidgetArea) QCOMPARE(hit.top(), dock.bottom() + 1);
      if (a.area == Qt::BottomDockWidgetArea) QCOMPARE(hit.bottom(), dock.top() - 1);
      // …and the band is drawn AROUND that strip, never thinner than an affordance can
      // be seen at (the horizontal separators are a hairline by design, theme.cpp).
      QVERIFY2(band.contains(hit), a.name);
      QVERIFY2(qMin(band.width(), band.height()) >= stencil::gui::DockEdgeOverlay::kMinThickness, a.name);
    }
    // Nothing to grab while it floats — the window frame owns that resize.
    win.chatDock_->setFloating(true);
    QTest::qWait(120);
    QVERIFY(!edge->isVisible());
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
    QTest::mouseClick(win.projectNameEdit_, Qt::LeftButton);
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

    // The browser's markup for one control id, and one attribute of it (HTML entities
    // back to their characters).
    auto browserTag = [&js](const QString& id) {
      const QRegularExpression tag("<[a-zA-Z]+[^>]*\\bid=\"" + id + "\"[^>]*>");
      return tag.match(js).captured(0);
    };
    auto attrOf = [](const QString& tag, const QString& attr) {
      QString v = QRegularExpression(attr + "=\"([^\"]*)\"").match(tag).captured(1);
      return v.replace("&amp;", "&").replace("&#10;", "\n");
    };
    // The browser's hover text: data-title when it has one (the rich tooltip), else the
    // plain title.
    auto browserTip = [&](const QString& id) {
      const QString t = browserTag(id);
      const QString v = attrOf(t, "data-title");
      return v.isEmpty() ? attrOf(t, "\\stitle") : v;
    };
    // A desktop tooltip is "<text> (<shortcut>)" plus a "— reason" line while the control
    // is disabled (browser composeControlTitle) — the shortcut is drawn as a keycap and the
    // reason is checked on its own below, so only the text takes part in the comparison.
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
        {win.actOpen_, nullptr, "load-image-btn"},
        {win.actOpenAnother_, nullptr, "open-image-btn"},
        {win.actSaveImage_, nullptr, "save-image"},
        {win.actCopyImage_, nullptr, "copy-image"},
        {win.actOpenIn_, nullptr, "open-in-btn"},
        {win.actProjects_, nullptr, "projects-btn"},
        {win.actOpenProjectFile_, nullptr, "open-project-btn"},
        {win.actStencilLiveSync_, nullptr, "live-sync-btn"},
        {win.actDeleteProjectFile_, nullptr, "delete-project-btn"},
        {win.actConnect_, nullptr, "connect-btn"},
        {win.actLinks_, nullptr, "links-btn"},
        {win.actDescription_, nullptr, "description-btn"},
        {win.actKeywords_, nullptr, "keywords-btn"},
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
        {nullptr, win.compareCombo_, "compare-mode"},
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
      // Both sides go through textOf: the browser writes its key into the data-title
      // ("Save name (Enter)"), the desktop hands the same key to the rich tooltip as a
      // keycap — the WORDS are what has to match.
      const QString want = textOf(browserTip(QString::fromLatin1(p.browserId)));
      QVERIFY2(!want.isEmpty(), qPrintable(QString("no browser control #%1").arg(p.browserId)));
      QVERIFY2(p.act || p.widget, p.browserId);
      // The plain text a widget's tooltip was composed from (the app renders it rich in
      // place; this suite's plain QApplication does not).
      QObject* target = p.act ? static_cast<QObject*>(p.act) : p.widget;
      const QString plain = p.act ? p.act->toolTip()
                            : target->property(stencil::gui::kPlainTipProperty).isValid()
                                ? target->property(stencil::gui::kPlainTipProperty).toString()
                                : p.widget->toolTip();
      const QString got = textOf(plain);
      QVERIFY2(got == want,
               qPrintable(QString("#%1: desktop says \"%2\", the browser says \"%3\"")
                              .arg(p.browserId, got, want)));
      // …and why it is greyed out, verbatim (data-disabled-reason): carried by every control
      // the browser gives one to, and on the tooltip exactly while the control is disabled.
      const QString tag = browserTag(QString::fromLatin1(p.browserId));
      const QString wantReason = attrOf(tag, "data-disabled-reason");
      const QString gotReason = target->property(stencil::gui::kTipReasonProperty).toString();
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
            p.widget->property(stencil::gui::kTipHotkeyProperty).value<QObject*>());
        QVERIFY2(bound, qPrintable(QString("#%1 wears no chord for %2").arg(p.browserId, hk)));
        QCOMPARE(bound->shortcut(), QKeySequence(win.hotkey(hk, QString())));
        QVERIFY2(plain.contains("(" + bound->shortcut().toString(QKeySequence::NativeText) + ")"),
                 qPrintable(QString("#%1: no chord on \"%2\"").arg(p.browserId, plain)));
      }
    }
    // …and the shortcut the shared registry defines for a control really is on it, or the
    // tooltip has no keycap to draw and the chord does nothing.
    QCOMPARE(win.actStencilLiveSync_->shortcut(), QKeySequence(win.hotkey("toggleLiveSync", "Ctrl+Shift+Y")));
    QCOMPARE(win.actDeleteProjectFile_->shortcut(), QKeySequence(win.hotkey("deleteProject", "Ctrl+Shift+Backspace")));
  }

  // ── the fading control tooltip (support/appTooltip.hpp) ──
  // A shown, enabled, tooltip-carrying toolbar button, plus the app's live tooltip panel.
  QToolButton* tipCarrier(MainWindow& win) {
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->isVisible() && b->isEnabled() && !b->toolTip().isEmpty()) return b;
    return nullptr;
  }
  static void sendToolTipTo(QWidget* w) {
    const QPoint local(4, 4);
    QHelpEvent ev(QEvent::ToolTip, local, w->mapToGlobal(local));
    QApplication::sendEvent(w, &ev);
  }

  // Tooltips FADE in and out (browser #app-tooltip: 90 ms) instead of snapping. Qt's own
  // QTipLabel cannot be animated, so QEvent::ToolTip is taken over — and the wake-up delay,
  // the content and the placement all have to survive that swap.
  void tooltipFadesInAndOut() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    win.raise();
    win.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* btn = tipCarrier(win);
    QVERIFY2(btn, "no shown toolbar button with a tooltip");
    stencil::gui::AppTooltip* tip = stencil::gui::appTooltip();
    QVERIFY2(tip, "the app tooltip was never installed");
    QVERIFY(!tip->isVisible());

    // Park the pointer ON the control: the panel's anti-stranding heartbeat retires a
    // tooltip whose control the pointer has left, and here it must not.
    QCursor::setPos(btn->mapToGlobal(btn->rect().center()));
    sendToolTipTo(btn);
    QVERIFY2(tip->isVisible(), "the tooltip did not take over QEvent::ToolTip");
    QCOMPARE(tip->owner(), static_cast<QWidget*>(btn));
    QVERIFY2(tip->windowOpacity() < 0.99, "it snapped in at full opacity");
    QTRY_COMPARE_WITH_TIMEOUT(tip->windowOpacity(), 1.0, 1500);   // …and rose to solid
    QTest::qWait(300);
    QVERIFY2(tip->isVisible(), "it retired while the pointer was still on its control");
    // Placed off the cursor and kept on screen.
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    QVERIFY2(screen.intersects(tip->geometry()), "the tooltip was placed off screen");
    QLabel* body = tip->findChild<QLabel*>();
    QVERIFY(body && !body->text().isEmpty());

    // Leaving the control fades it OUT — still visible while it goes, gone at the end.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(btn, &leave);
    QVERIFY2(tip->fadingOut(), "it vanished instead of fading");
    QTRY_VERIFY_WITH_TIMEOUT(!tip->isVisible(), 1500);

    // Reduced motion: shown solid at once, hidden at once — the same end states.
    qputenv("STENCIL_NO_ANIM", "1");
    sendToolTipTo(btn);
    QVERIFY(tip->isVisible());
    QCOMPARE(tip->windowOpacity(), 1.0);
    QApplication::sendEvent(btn, &leave);
    QVERIFY2(!tip->isVisible(), "reduced motion still played the fade-out");
    qunsetenv("STENCIL_NO_ANIM");
    beat();
  }

  // A fast pointer sweep must never STRAND a tooltip: whatever happened to the control it
  // described — hidden, disabled, or simply left behind without a Leave we saw — the panel
  // goes on its own.
  void fastPointerSweepStrandsNoTooltip() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    win.raise();
    win.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* btn = tipCarrier(win);
    QVERIFY(btn);
    stencil::gui::AppTooltip* tip = stencil::gui::appTooltip();
    QVERIFY(tip);

    // Shown for a control the pointer is nowhere near — the sweep already moved on, and no
    // Leave was ever delivered for it.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));
    sendToolTipTo(btn);
    QVERIFY(tip->isVisible());
    // The cursor is nowhere near it (offscreen QPA parks it at the origin, and no Leave is
    // synthesised) — the heartbeat is the only thing that can clear this.
    QTRY_VERIFY_WITH_TIMEOUT(!tip->isVisible(), 3000);
    QVERIFY(!tip->owner());
    beat();
  }

  // A shown, enabled toolbar button whose rendered tooltip does (`want`) or does not carry
  // keycaps — the shake fires on the content, not on the control.
  QToolButton* capCarrier(MainWindow& win, bool want) {
    for (QToolButton* b : win.findChildren<QToolButton*>()) {
      if (!b->isVisible() || !b->isEnabled() || b->toolTip().isEmpty()) continue;
      const QString rich = b->toolTip().trimmed().startsWith('<')
                               ? b->toolTip()
                               : stencil::gui::enrichedToolTip(b->toolTip());
      if (stencil::gui::hasKeycaps(rich) == want) return b;
    }
    return nullptr;
  }

  // The KEYCAPS shake as their tooltip appears — a brief, non-repeating flick that says
  // "and here is the shortcut" while you read the tip (browser/extension:
  // .tip-key.key-shake). No key press is involved: showing the tooltip is the trigger, only
  // a tooltip that actually draws caps reacts, and the PANEL never moves — the caps painted
  // inside it do, and they settle back on exactly the picture they started from.
  void showingATooltipShakesItsKeycaps() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    win.raise();
    win.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* btn = capCarrier(win, true);
    QVERIFY2(btn, "no shown toolbar button whose tooltip carries keycaps");
    stencil::gui::AppTooltip* tip = stencil::gui::appTooltip();
    QVERIFY(tip);

    // Park the pointer ON the control so the anti-stranding heartbeat leaves it up. The
    // offscreen screen is smaller than this window, so a control out past its edge can
    // never take the cursor at all: walk the WINDOW towards wherever the cursor actually
    // landed until the two agree. It matters more than it used to — the keycap shake now
    // waits out the tip's own arrival, well past the heartbeat's first look.
    const auto onControl = [&] {
      return btn->rect().contains(btn->mapFromGlobal(QCursor::pos()));
    };
    for (int i = 0; i < 4 && !onControl(); ++i) {
      QCursor::setPos(btn->mapToGlobal(btn->rect().center()));
      if (onControl()) break;
      win.move(win.pos() + (QCursor::pos() - btn->mapToGlobal(btn->rect().center())));
      QTest::qWait(40);
    }
    sendToolTipTo(btn);
    QVERIFY2(tip->isVisible(), "the tooltip did not appear");
    // The caps HOLD STILL while the tip is still assembling out of its own motes — a
    // nudge nobody can see is the one thing this must never be — and are queued for the
    // moment it lands. Without dust (an unmeasurable host) there is nothing to wait for
    // and the shake is immediate instead.
    QVERIFY2(tip->shaking() || tip->shakePending(),
             "the keycaps were neither shaken nor queued to shake");
    if (tip->shakePending())
      QVERIFY2(!tip->shaking(), "the caps moved while the tip was still forming");
    QTRY_VERIFY_WITH_TIMEOUT(tip->shaking(), stencil::gui::AppTooltip::kDustInMs + 2000);
    QVERIFY2(tip->keycapsShown() > 0, "the shake found no caps to move");
    QLabel* body = tip->findChild<QLabel*>();
    QVERIFY(body);
    const QPoint home = tip->pos();
    // The CAPS really move — pixels, not a counter — while the panel around them holds
    // still, and it is one pass: they settle back on the picture they started from.
    // (No "starts at 0" check here any more: the wait above is what lets the queued
    // shake begin, so by this line it is already a frame or two in.)
    QImage midShake;
    for (int i = 0; i < 40 && midShake.isNull(); ++i) {
      QTest::qWait(8);
      QCOMPARE(tip->pos(), home);                     // the panel itself must not swing
      if (tip->shakeOffset() != 0) midShake = body->grab().toImage();
    }
    QVERIFY2(!midShake.isNull(), "the caps never left their resting slot");
    QTRY_VERIFY_WITH_TIMEOUT(!tip->shaking(), stencil::gui::AppTooltip::kShakeMs + 2000);
    QCOMPARE(tip->pos(), home);
    QCOMPARE(tip->shakeOffset(), 0);
    const QImage settled = body->grab().toImage();
    QVERIFY2(midShake != settled, "the shake redrew the tooltip exactly as it sits at rest");
    QVERIFY2(tip->isVisible(), "the shake must not retire the tooltip");
    // A re-sent ToolTip for the SAME control is not a new appearance (Qt keeps re-arming
    // its wake-up while the pointer wanders inside one control): no second shake.
    sendToolTipTo(btn);
    QVERIFY2(!tip->shaking() && !tip->shakePending(),
             "the same tooltip shook again under a wandering pointer");

    // ANY key retires it — Escape included. The shake announces the shortcut; it is not a
    // way to pin the tooltip open.
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(win.canvas_, &esc);
    QTRY_VERIFY_WITH_TIMEOUT(!tip->isVisible(), 1500);

    // A tooltip with NO shortcut draws no caps, so it has nothing to announce.
    QToolButton* plain = capCarrier(win, false);
    if (!plain) {  // every live control happens to carry a chord — give one a bare tooltip
      for (QToolButton* b : win.findChildren<QToolButton*>())
        if (b->isVisible() && b->isEnabled() && b != btn) { plain = b; break; }
      QVERIFY(plain);
      plain->setToolTip(QStringLiteral("Bare hover text, nothing bound"));
    }
    QCursor::setPos(plain->mapToGlobal(plain->rect().center()));
    sendToolTipTo(plain);
    QVERIFY(tip->isVisible());
    QVERIFY2(!stencil::gui::hasKeycaps(body->text()), "the control drew keycaps after all");
    QVERIFY2(!tip->shaking() && !tip->shakePending(), "a tooltip with no keycaps still shook");
    QCOMPARE(tip->shakeOffset(), 0);
    QCOMPARE(tip->keycapsShown(), 0);
    const QPoint bareHome = tip->pos();
    const QImage bare = body->grab().toImage();
    QTest::qWait(120);
    QCOMPARE(tip->pos(), bareHome);                    // …and nothing moved while we watched
    QCOMPARE(body->grab().toImage(), bare);

    // A fast sweep re-points the tooltip mid-shake, over and over: the shakes must not
    // stack, and every cap must end up back on its own slot.
    for (int i = 0; i < 8; ++i) {
      sendToolTipTo(i % 2 ? plain : btn);
      QTest::qWait(20);
    }
    QCursor::setPos(btn->mapToGlobal(btn->rect().center()));
    sendToolTipTo(btn);
    QTRY_VERIFY_WITH_TIMEOUT(!tip->shaking(), stencil::gui::AppTooltip::kShakeMs + 2000);
    QCOMPARE(tip->shakeOffset(), 0);
    // Settled back EXACTLY: the same picture as the first appearance left behind.
    QCOMPARE(body->grab().toImage(), settled);
    // …and nothing is stranded: the pointer is on neither control any more.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));
    QTRY_VERIFY_WITH_TIMEOUT(!tip->isVisible(), 3000);
    QCOMPARE(tip->shakeOffset(), 0);

    // Reduced motion: shown, correct, and not a pixel of shake — panel or caps.
    qputenv("STENCIL_NO_ANIM", "1");
    QCursor::setPos(btn->mapToGlobal(btn->rect().center()));
    sendToolTipTo(btn);
    QVERIFY(tip->isVisible());
    QVERIFY(stencil::gui::hasKeycaps(body->text()));
    const QPoint restingPos = tip->pos();
    QVERIFY2(!tip->shaking(), "reduced motion still shook the keycaps");
    QCOMPARE(tip->shakeOffset(), 0);
    QTest::qWait(120);
    QCOMPARE(tip->pos(), restingPos);
    QCOMPARE(tip->shakeOffset(), 0);
    QCOMPARE(body->grab().toImage(), settled);         // the caps drawn exactly where they live
    qunsetenv("STENCIL_NO_ANIM");
    beat();
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
    QTest::qWait(30);   // let the toolbar's own deferred layout pass settle before measuring it
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
    // The reveal is ANIMATED, so every width here is settled with a QTRY — a plain
    // compare beside the first one reads the pair mid-slide on a slow machine.
    QTRY_COMPARE(win.formulaX_->width(), 180);
    QTRY_COMPARE(win.formulaY_->width(), 180);
    // …side by side on the browser's 6px gap, not spread out over the row's leftover width.
    QTRY_COMPARE(win.formulaY_->x() - (win.formulaX_->x() + win.formulaX_->width()), 6);
    // A row with no slack squeezes them; the cluster itself stays out on the toolbar.
    win.resize(920, 800);
    QTRY_VERIFY2(win.formulaX_->width() < 180 && win.formulaX_->width() >= 72,
                 qPrintable(QString("squeezed to %1px").arg(win.formulaX_->width())));
    QVERIFY2(win.formulaX_->isVisible(), "the formula fields hid instead of shrinking");
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
    // The pill starts wherever the LAST run left it (settings.json persists
    // allowFormulas), and its reveal/hide is animated — so every wait here is a
    // QTRY: a fixed one passed only when the pill happened to start unchecked.
    if (pill->isChecked()) pill->setChecked(false);
    QTRY_VERIFY(!fx->isVisible());
    QTest::mouseClick(pill, Qt::LeftButton, Qt::NoModifier, pill->rect().center());
    QTRY_VERIFY2(fx->isVisible(), "formula inputs should appear when f(x,y) is enabled");
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
    const auto motion = withMotion();   // the loop honours motionReduced(), which is on here
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* logo = win.logoBtn_;
    QVERIFY(logo);
    QWidget* fx = win.findChild<QWidget*>("logoHoverFx");
    QVERIFY2(fx, "logo hover fx overlay not installed");
    QTest::qWait(20);   // the overlay shows its resting mark on a deferred tick after show
    auto iconBlank = [logo] {
      const QImage im = logo->icon().pixmap(logo->iconSize()).toImage();
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x)
          if (qAlpha(im.pixel(x, y)) != 0) return false;
      return true;
    };
    // The overlay now paints the mark at ALL times (a QToolButton draws its icon half
    // size on Retina), so at rest it is VISIBLE but NOT animating, and the button icon is
    // blanked — the overlay owns the pixels. Only the pulse/rays are hover-gated.
    QVERIFY2(fx->isVisible(), "the fx paints the resting mark");
    QVERIFY(!fx->property("fxActive").toBool());
    QVERIFY2(iconBlank(), "the overlay owns the mark at rest (button icon blanked)");

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
    QVERIFY2(fx->isVisible(), "leave keeps the resting mark shown (only the pulse stops)");
    QVERIFY(!fx->property("fxActive").toBool());
    for (QVariantAnimation* a : fx->findChildren<QVariantAnimation*>())
      QVERIFY2(a->state() != QAbstractAnimation::Running,
               "an fx animation kept running after hover-leave");
    QVERIFY2(iconBlank(), "the overlay keeps the mark after leave (button icon stays blanked)");
  }

  // Browser parity: the accent popover the logo opens is part of the logo's hover —
  // the shine holds while the cursor crosses the anchor gap onto it and while it rests
  // there, starts from a hover that begins on the popover, and stops only once the
  // cursor has left both.
  void logoHoverFxHoldsOverAccentPopover() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* logo = win.logoBtn_;
    QWidget* fx = win.findChild<QWidget*>("logoHoverFx");
    QVERIFY(logo && fx);
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();   // typingFocus gate off
    const QPoint c = logo->rect().center();
    const QPoint logoGlobal = logo->mapToGlobal(c);
    const QPoint awayGlobal = win.mapToGlobal(QPoint(win.width() / 2, win.height() - 40));
    const auto enter = [](QWidget* w, const QPoint& global) {
      const QPointF local(w->mapFromGlobal(global));
      QEnterEvent e(local, local, QPointF(global));
      QApplication::sendEvent(w, &e);
    };
    const auto leave = [](QWidget* w) {
      QEvent e(QEvent::Leave);
      QApplication::sendEvent(w, &e);
    };
    QCursor::setPos(logoGlobal);
    enter(logo, logoGlobal);
    QVERIFY(fx->property("fxActive").toBool());

    bool opened = false, heldOnCrossing = false, heldOnBox = false;
    bool stoppedOffBoth = false, startedOnBox = false;
    QTimer::singleShot(600, &win, [&] {   // past the popover's open flight
      QWidget* box = win.popoverOverlay_.data();
      opened = box && box->isVisible() && win.activePopover_ &&
               win.activePopover_->objectName() == QLatin1String("accentPopover");
      if (!opened) { if (win.activePopover_) win.activePopover_->reject(); return; }
      // The cursor crosses the anchor gap onto the box: the logo's Leave alone must not
      // stop the loop (the browser's hover bridge), and resting on the box holds it.
      const QPoint boxGlobal = box->mapToGlobal(box->rect().center());
      QCursor::setPos(boxGlobal);
      leave(logo);
      QTest::qWait(60);   // inside the grace
      heldOnCrossing = fx->property("fxActive").toBool();
      enter(box, boxGlobal);
      QTest::qWait(300);  // well past the grace
      heldOnBox = fx->property("fxActive").toBool() && fx->isVisible();
      // Off both (onto the canvas): the loop stops once the grace runs out.
      QCursor::setPos(awayGlobal);
      leave(box);
      QTest::qWait(300);
      stoppedOffBoth = !fx->property("fxActive").toBool();
      // A hover that BEGINS on the popover lights the logo too.
      QCursor::setPos(boxGlobal);
      enter(box, boxGlobal);
      startedOnBox = fx->property("fxActive").toBool();
      QCursor::setPos(awayGlobal);
      leave(box);
      win.activePopover_->reject();
    });
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logoGlobal);
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's loop until the timer acts
    QTRY_VERIFY(!win.activePopover_);
    QVERIFY2(opened, "right-click did not open the accent popover");
    QVERIFY2(heldOnCrossing, "leaving the logo for the open popover stopped the shine");
    QVERIFY2(heldOnBox, "hovering the open popover did not hold the shine");
    QVERIFY2(stoppedOffBoth, "the shine kept running with the cursor off logo and popover");
    QVERIFY2(startedOnBox, "hovering the popover did not start the shine");
    // The popover has gone and the cursor is on neither: the loop is down and idle.
    QTRY_VERIFY(!fx->property("fxActive").toBool());
    for (QVariantAnimation* a : fx->findChildren<QVariantAnimation*>())
      QVERIFY(a->state() != QAbstractAnimation::Running);
  }

  // Shift+F10 (shared hotkeysConfig contextMenu) opens the canvas context menu from the
  // keyboard: under the pointer while it rests over the canvas viewport, else at the
  // viewport's centre — the browser's placement for the same chord.
  void contextMenuOpensOnShiftF10() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QVERIFY(win.actContextMenu_);
    QCOMPARE(win.actContextMenu_->shortcut(), QKeySequence("Shift+F10"));
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();
    win.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&win));   // a WindowShortcut needs the active window
    QWidget* vp = win.scroll_->viewport();
    const QRect vpGlobal(vp->mapToGlobal(QPoint(0, 0)), vp->size());
    // The menu exec()s: a poll (armed BEFORE the press — the platform key path flushes
    // pending events, so a one-shot would fire too early) records where it opened and closes it.
    const auto armCloser = [&win](bool& opened, QPoint& at) {
      auto* poll = new QTimer(&win);
      poll->setInterval(10);
      int ticks = 0;
      QObject::connect(poll, &QTimer::timeout, &win, [poll, &opened, &at, ticks]() mutable {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
          opened = true;
          at = menu->pos();
          menu->close();
          poll->stop();
          poll->deleteLater();
        } else if (++ticks > 300) {
          poll->stop();
          poll->deleteLater();
        }
      });
      poll->start();
    };
    // Pointer resting on the canvas: the menu grows from right there — via the chord
    // itself, through the platform window so the press walks the real shortcut map.
    const QPoint onCanvas = vpGlobal.topLeft() + QPoint(40, 40);
    QCursor::setPos(onCanvas);
    QPoint at1(-1, -1);
    bool opened1 = false;
    armCloser(opened1, at1);
    QTest::keyClick(win.windowHandle(), Qt::Key_F10, Qt::ShiftModifier);
    QTRY_VERIFY2_WITH_TIMEOUT(opened1, "Shift+F10 did not open the canvas context menu", 4000);
    // x is the pointer's; y may be pulled up to keep the menu on the (short) offscreen screen.
    QCOMPARE(at1.x(), onCanvas.x());
    QVERIFY(at1.y() <= onCanvas.y());
    QTRY_VERIFY(!QApplication::activePopupWidget());
    // Pointer off the canvas (on the toolbar): the menu lands at the viewport's centre.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 8, 8)));
    QPoint at2(-1, -1);
    bool opened2 = false;
    armCloser(opened2, at2);
    win.actContextMenu_->trigger();
    QTRY_VERIFY2_WITH_TIMEOUT(opened2, "the context-menu action did not open the menu", 4000);
    // x lands on the centre exactly; y may be pulled up to keep the menu on screen.
    QCOMPARE(at2.x(), vpGlobal.center().x());
    QVERIFY(at2.y() <= vpGlobal.center().y());
    QTRY_VERIFY(!QApplication::activePopupWidget());
  }

  // The open flight photographs the dialog before its scroll area has decided its
  // scrollbar, so the picture that flew was a scrollbar too wide (user report).
  // settleLayout brings the scrollbar in before the shot.
  void revealSnapshotWaitsForTheScrollbar() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::gui::SettingsDialog dlg(win.settings_, &win);
    dlg.resize(dlg.width(), 320);   // short enough that the body must scroll
    dlg.show();
    auto* scroll = dlg.findChild<QScrollArea*>();
    QVERIFY(scroll);
    // Straight after show() the vertical bar may not be decided yet — the frame the old
    // snapshot was taken on. After the settle the viewport is the real row width.
    stencil::support::settleLayout(dlg);
    QVERIFY2(scroll->verticalScrollBar()->isVisible(), "the settled shell shows its scrollbar");
    const int settledViewport = scroll->viewport()->width();
    QTest::qWait(80);   // …and nothing moves once the event loop has had its say
    QCOMPARE(scroll->viewport()->width(), settledViewport);
    QVERIFY(scroll->viewport()->width() < scroll->width());
    dlg.reject();
  }

  // The Interface-animation combo's rows wear ONE glyph each (support/motionIcons.hpp),
  // handed to the style AS the row's icon — never painted beside its own (that drew two).
  // Rendered offscreen: the glyph column lights no wider than one 16px icon.
  // A press outside a modal dismisses it, the way a press on the browser's modal overlay
  // does (ui/base.js).
  //
  // WHAT THIS COVERS: the decision — outside dismisses, inside and the dialog's own popup
  // do not, and an opted-out dialog never does. NOT the delivery: QTest::mouseClick hands
  // the widget a QMouseEvent directly, while a REAL press on a window a modal blocks never
  // becomes a QEvent at all (QtGui drops it in processMouseEvent). That half is
  // modalDismissMac.mm reading the NSEvent, and no offscreen test can reach it — this
  // passing does not mean a real click works.
  void clickOutsideAModalDismissesIt() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::support::installModalDismiss();   // the app installs it at startup; explicit here

    QDialog dlg(&win);
    dlg.setObjectName(QStringLiteral("probeModal"));
    dlg.resize(200, 150);
    dlg.setModal(true);
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));

    // A press INSIDE the box changes nothing…
    QTest::mouseClick(&dlg, Qt::LeftButton, {}, QPoint(60, 60));
    QTest::qWait(40);
    QVERIFY2(dlg.isVisible(), "a press inside the dialog closed it");

    // …and one on a widget the dialog RAISED (a popup keeps it as its parent) neither.
    QWidget popup(&dlg, Qt::Popup);
    popup.resize(40, 40);
    QTest::mouseClick(&popup, Qt::LeftButton, {}, QPoint(5, 5));
    QTest::qWait(40);
    QVERIFY2(dlg.isVisible(), "a press in the dialog's own popup closed it");

    // A press on the blocked main window dismisses it — posted the way the PLATFORM does,
    // not with QTest::mouseClick: that hands the widget a QMouseEvent directly and skips
    // the window-system layer, which is exactly where Qt drops a blocked window's clicks.
    QTest::mouseClick(&win, Qt::LeftButton, {}, QPoint(60, 400));
    QTRY_VERIFY_WITH_TIMEOUT(!dlg.isVisible(), 1500);
    QCOMPARE(dlg.result(), int(QDialog::Rejected));

    // …unless the dialog says it must be answered.
    QDialog must(&win);
    must.setProperty(stencil::support::kNoOutsideDismissProperty, true);
    must.resize(200, 150);
    must.setModal(true);
    must.show();
    QVERIFY(QTest::qWaitForWindowExposed(&must));
    QTest::mouseClick(&win, Qt::LeftButton, {}, QPoint(60, 400));
    QTest::qWait(80);
    QVERIFY2(must.isVisible(), "an opted-out dialog was clicked away");
    must.close();
  }

  void motionComboRowsWearOneGlyph() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::gui::SettingsDialog dlg(win.settings_, &win);
    dlg.show();
    QTest::qWait(30);
    auto* combo = static_cast<stencil::gui::SearchComboBox*>(   // no Q_OBJECT on the combo: found as its base
        dlg.findChild<QComboBox*>(QStringLiteral("motionModeCombo")));
    QVERIFY(combo);
    QCOMPARE(combo->count(), 5);
    QVERIFY2(combo->toolTip().isEmpty(), "the browser's dropdown carries no tooltip");
    for (int i = 0; i < combo->count(); ++i) QVERIFY(!combo->itemIcon(i).isNull());
    combo->showPopup();
    QTest::qWait(60);
    QListView* list = combo->popupList();
    QVERIFY(list && list->isVisible());
    // Measured in LOGICAL pixels: the grab is at the screen's ratio (2x offscreen here).
    const QPixmap shot = list->grab();
    const int dpr = qMax(1, qRound(shot.devicePixelRatio()));
    const QImage img = shot.toImage().convertToFormat(QImage::Format_ARGB32)
                           .scaled(shot.width() / dpr, shot.height() / dpr, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // Per row: the ICON slot's lit columns (the label starts past 36px), as runs. One
    // glyph is one run no wider than a 16px icon; a second icon would be a second run.
    const int rowH = img.height() / 5;
    QVERIFY(rowH > 10);
    for (int r = 0; r < 5; ++r) {
      QString cols;
      // Ink is whatever differs from the ROW's own background (a selected row wears the
      // accent wash), sampled just inside the popup's border.
      const QColor bg = img.pixelColor(5, r * rowH + rowH / 2);
      // Alpha counts: the list's viewport grabs transparent, so black ink on a clear row
      // differs from it in alpha alone.
      const auto far = [&](const QColor& c) {
        return qAbs(c.red() - bg.red()) + qAbs(c.green() - bg.green()) + qAbs(c.blue() - bg.blue())
               + qAbs(c.alpha() - bg.alpha()) > 90;
      };
      for (int x = 4; x < qMin(36, img.width()); ++x) {   // past the popup's own border
        bool lit = false;
        for (int y = r * rowH + 4; y < (r + 1) * rowH - 4 && !lit; ++y) lit = far(img.pixelColor(x, y));
        cols += lit ? QLatin1Char('#') : QLatin1Char('.');
      }
      qDebug("row %d icon columns: %s", r, qPrintable(cols));
      const QStringList runs = cols.split(QLatin1Char('.'), Qt::SkipEmptyParts);
      QVERIFY2(runs.size() >= 1, qPrintable(QStringLiteral("row %1 shows no glyph (%2)").arg(r).arg(cols)));
      int widest = 0, gaps = 0;
      for (const QString& run : runs) widest = qMax(widest, int(run.size()));
      // Runs parted by a gap of three or more columns are separate glyphs.
      for (int i = 3; i < cols.size(); ++i)
        if (cols.mid(i - 3, 3) == QLatin1String("...") && cols[i] == QLatin1Char('#') && cols.left(i - 3).contains(QLatin1Char('#'))) ++gaps;
      QVERIFY2(gaps == 0, qPrintable(QStringLiteral("row %1: two glyphs (%2)").arg(r).arg(cols)));
      QVERIFY2(widest <= 20, qPrintable(QStringLiteral("row %1: glyph ink %2px wide — more than one icon (%3)").arg(r).arg(widest).arg(cols)));
    }
    // …and a pick from the popup reports itself as a user pick — activated(), the signal
    // the dialog applies live from — BEFORE the list leaves. setCurrentIndex alone never
    // emits it, which is why a popup pick used to apply only on OK.
    QSignalSpy picked(combo, &QComboBox::activated);
    list->setCurrentIndex(list->model()->index(2, 0));   // Fire
    emit list->clicked(list->currentIndex());
    QCOMPARE(picked.count(), 1);
    QCOMPARE(picked.at(0).at(0).toInt(), 2);
    QCOMPARE(combo->currentData().toString(), QStringLiteral("fire"));
    QVERIFY(!list->isVisible());
    combo->hidePopup();
  }

  // The ✓ in an OPEN accent popover follows the accent wherever it moved from — the
  // logo's click-cycle (applySettings), not only the popover's own row picks (user
  // report). The browser's logo menu re-marks off stencil:accent-changed the same way.
  void accentPopoverTickFollowsAnOutsideAccentChange() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(30);
    QToolButton* logo = win.logoBtn_;
    QVERIFY(logo);
    const QString original = win.settings_.accentColor;
    const auto& presets = stencil::gui::accentPresets();
    QVERIFY(presets.size() >= 2);
    QString other;
    for (const auto& a : presets) if (a.key != original) { other = a.key; break; }
    bool opened = false, markedBefore = false, markedAfter = false, oneMark = true;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.activePopover_.data();
      opened = pop && pop->objectName() == QLatin1String("accentPopover") && pop->isVisible();
      if (pop) {
        auto* was = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + original);
        markedBefore = was && was->property("currentAccent").toBool();
        // The accent moves OUTSIDE the popover — the logo click-cycle's own path.
        auto next = win.settings_;
        next.accentColor = other;
        win.applySettings(next, true);
        QTest::qWait(30);
        int marks = 0;
        for (const auto& a : presets) {
          auto* r = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + a.key);
          if (r && r->property("currentAccent").toBool()) ++marks;
        }
        auto* now = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + other);
        markedAfter = now && now->property("currentAccent").toBool();
        oneMark = marks == 1;
        pop->reject();
      }
    });
    const QPoint c = logo->rect().center();
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's exec until the timer acts
    QVERIFY2(opened, "right-click did not open the accent popover");
    QVERIFY2(markedBefore, "the current accent's row starts marked");
    QVERIFY2(markedAfter, "an accent applied from outside the popover must move its tick");
    QVERIFY2(oneMark, "exactly one row carries the tick");
    auto restore = win.settings_;
    restore.accentColor = original;
    win.applySettings(restore, true);
  }

  // Alt held with the cursor resting ON the open popover must not glide onto an icon the
  // box is COVERING: the glide's cursor-rect fallback is pure geometry, so it read that as
  // hovering the buttons under the box and opened their window beneath it.
  void altGlideIgnoresIconsUnderTheOpenPopover() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(30);
    QToolButton* logo = win.logoBtn_;
    QVERIFY(logo);
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();   // typingFocus gate off
    const QPoint cursorWas = QCursor::pos();
    bool opened = false, covered = false, stayed = false, armed = true;
    QTimer::singleShot(700, &win, [&] {   // past the popover's open flight
      QWidget* box = win.popoverOverlay_.data();
      opened = box && box->isVisible() && win.activePopover_ &&
               win.activePopover_->objectName() == QLatin1String("accentPopover");
      if (opened) {
        const QRect boxGlobal(box->mapToGlobal(QPoint(0, 0)), box->size());
        QPoint on;   // a point on the box AND on a popover icon it covers
        for (auto it = win.popoverButtons_.cbegin(); it != win.popoverButtons_.cend(); ++it) {
          auto* b = static_cast<QToolButton*>(it.key());
          if (b == logo || !b->isVisible() || !it.value()->isEnabled()) continue;
          const QRect hit = QRect(b->mapToGlobal(QPoint(0, 0)), b->size()).intersected(boxGlobal);
          if (hit.isEmpty()) continue;
          covered = true;
          on = hit.center();
          break;
        }
        if (covered) {
          win.altHeldForTest_ = true;   // the glide poll's stand-in for a held Alt
          QCursor::setPos(on);
          QTest::qWait(300);            // several glide ticks (80ms)
          stayed = win.activePopover_ &&
                   win.activePopover_->objectName() == QLatin1String("accentPopover");
          armed = !win.altPeekNextAction_.isNull();
          win.altHeldForTest_ = false;
          // Never leave a peek queued: it would open (and block) after this unwinds.
          win.altPeekNextAction_.clear();
          win.altPeekNextButton_.clear();
        }
      }
      if (win.activePopover_) win.activePopover_->reject();
    });
    const QPoint c = logo->rect().center();
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's loop until the timer acts
    QCursor::setPos(cursorWas);
    QVERIFY2(opened, "right-click did not open the accent popover");
    QVERIFY2(covered, "the accent box covers no popover icon — nothing to guard here");
    QVERIFY2(stayed, "resting on the box glided onto an icon underneath it");
    QVERIFY2(!armed, "resting on the box armed a covered icon's peek");
  }

  // A row under the pointer eases a couple of pixels RIGHT — the browser's
  // `.accent-dd-opt:hover { transform: translateX(2px) }` — and SURVIVES the preview's own
  // flood, which re-polishes every stylesheet and re-lays the popover out.
  void accentRowSlidesUnderThePointer() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(30);
    QToolButton* logo = win.logoBtn_;
    QVERIFY(logo);
    const auto& presets = stencil::gui::accentPresets();
    QVERIFY(presets.size() >= 2);
    const QString original = win.settings_.accentColor;
    QString other;
    for (const auto& a : presets) if (a.key != original) { other = a.key; break; }
    bool opened = false, foundRow = false;
    int restX = 0, hoverX = 0, floodedX = 0, backX = 0;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.activePopover_.data();
      opened = pop && pop->objectName() == QLatin1String("accentPopover") && pop->isVisible();
      if (pop) {
        auto* row = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + presets.front().key);
        foundRow = row != nullptr;
        if (row) {
          restX = row->x();
          const QPointF p(4, 4);
          QEnterEvent enter(p, p, row->mapToGlobal(QPoint(4, 4)));
          QApplication::sendEvent(row, &enter);
          QTest::qWait(200);            // the 120ms slide, with room to spare
          hoverX = row->x();
          // The accent flood the preview plays, straight through applySettings.
          auto next = win.settings_;
          next.accentColor = other;
          win.applySettings(next, true);
          QTest::qWait(150);
          floodedX = row->x();
          QEvent leave(QEvent::Leave);
          QApplication::sendEvent(row, &leave);
          QTest::qWait(200);
          backX = row->x();
        }
        pop->reject();
      }
    });
    const QPoint c = logo->rect().center();
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's exec until the timer acts
    QVERIFY2(opened, "right-click did not open the accent popover");
    QVERIFY2(foundRow, "the popover must carry the preset rows");
    QCOMPARE(hoverX, restX + 2);
    QCOMPARE(floodedX, restX + 2);
    QCOMPARE(backX, restX);
    auto restore = win.settings_;
    restore.accentColor = original;
    win.applySettings(restore, true);
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
    // A toolbar icon that is not the logo — and NOT one the open popover's box covers: a
    // press on a covered icon is a press ON the window (the app's own onOpenBox rule), so
    // it is not the "outside press" this case is about. The SETTINGS cluster's ℹ sits at
    // the far end of the last row, clear of a box anchored to the logo.
    QToolButton* other = nullptr;
    for (auto it = win.popoverButtons_.cbegin(); it != win.popoverButtons_.cend(); ++it)
      if (it.value() == win.actInfo_ && static_cast<QWidget*>(it.key())->isVisible())
        other = static_cast<QToolButton*>(it.key());
    QVERIFY2(other, "no visible Help button to press outside on");

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
    // The logo itself is NOT outside: a left click on it with a STICKY popover up keeps
    // the list open and cycles the accent under it, the ✓ following (browser parity; user
    // report). The peek's own no-op rule is checked in logoAccentPopoverPicksDirectly.
    {
      const QString accentBefore = win.settings_.accentColor;
      const auto& presets = stencil::gui::accentPresets();
      bool opened = false, stayedOpen = false, cycleArmed = false, cycled = false, stillOpen = false, ticked = false;
      QTimer::singleShot(120, &win, [&] {
        QDialog* pop = win.activePopover_.data();
        opened = pop && pop->objectName() == QLatin1String("accentPopover") && pop->isVisible();
        const QPoint local = logo->rect().center();
        const QPoint at = logo->mapToGlobal(local);
        QMouseEvent press(QEvent::MouseButtonPress, local, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(logo, &press);
        QMouseEvent rel(QEvent::MouseButtonRelease, local, at, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(logo, &rel);
        stayedOpen = win.activePopover_ && !win.activePopover_->isHidden();
        cycleArmed = win.logoClickTimer_->isActive();
        QTest::qWait(320);   // past the deferred click: the cycle lands
        cycled = win.settings_.accentColor != accentBefore &&
                 std::any_of(presets.begin(), presets.end(),
                             [&](const auto& a) { return a.key == win.settings_.accentColor; });
        stillOpen = win.activePopover_ && !win.activePopover_->isHidden();
        if (pop) {
          auto* r = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + win.settings_.accentColor);
          ticked = r && r->property("currentAccent").toBool();
        }
        if (win.activePopover_ && !win.activePopover_->isHidden()) win.activePopover_->reject();
      });
      QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
      QApplication::sendEvent(logo, &ctx);      // blocks in exec until the timer acts
      QVERIFY2(opened, "sticky + logo click: the popover never opened");
      QVERIFY2(stayedOpen, "a left press on the logo must not close its sticky popover");
      QVERIFY2(cycleArmed, "the logo click must still arm the accent cycle");
      QVERIFY2(cycled, "the deferred click must cycle the accent under the open popover");
      QVERIFY2(stillOpen, "the popover must survive the accent change");
      QVERIFY2(ticked, "the popover's tick must follow the cycled accent");
      QVERIFY(!win.activePopover_);
      auto restore = win.settings_;
      restore.accentColor = accentBefore;
      win.applySettings(restore, true);
    }

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
    // The lifecycle as it happens, in order: the popover's own show/hide, the particle
    // dust flights execMaybePopover plays over the hosting overlay, and any ghost the
    // reveal machinery might fly instead — there must be none.
    struct Trace : QObject {
      QStringList seq;
      QSet<QObject*> dialogs;
      QElapsedTimer clock;
      qint64 pressedAt = -1, hidAt = -1;
      // DisintegrateOverlay flights actually launched, before/after the outside press.
      int dustOpening = 0, dustClosing = 0;
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
        if (w && w->objectName() == QLatin1String(stencil::gui::DisintegrateOverlay::kObjectName)
            && e->type() == QEvent::Show) {
          if (dismissed) ++dustClosing; else ++dustOpening;
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

    // The logo is not an outside target any more (the block above), so the lifecycle is
    // traced on the canvas and a toolbar icon.
    for (QWidget* target : {static_cast<QWidget*>(win.canvas_), static_cast<QWidget*>(other)}) {
      trace.seq.clear();
      trace.dialogs.clear();
      trace.pressedAt = trace.hidAt = -1;
      trace.dustOpening = trace.dustClosing = 0;
      trace.dismissed = false;
      bool wasTopLevel = true, hadOverlay = false;
      QTimer::singleShot(400, &win, [&] {   // …once the open animation has landed
        const QPoint local = target->rect().center();
        const QPoint at = target->mapToGlobal(local);
        trace.pressedAt = trace.clock.elapsed();
        // THE property: no window of its own. That is what made three animated closes
        // invisible, and what the in-window overlay fixes.
        if (win.activePopover_) wasTopLevel = win.activePopover_->isWindow();
        hadOverlay = win.popoverOverlay_ && win.popoverOverlay_->isVisible();
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
      // Both edges actually flew the particle dust — the popover no longer grows/shrinks
      // its own box.
      QVERIFY2(trace.dustOpening > 0,
               qPrintable(QString("%1: no dust played on open").arg(what)));
      QVERIFY2(trace.dustClosing > 0,
               qPrintable(QString("%1: no dust played on close").arg(what)));
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
    QTest::qWait(30);   // let the toolbar's own deferred layout pass settle before measuring it
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
      // Picking a colour APPLIES it and CLOSES the popover (user decision — hovering
      // already previews live, so a click is a commit). Browser twin: the logo menu
      // closes on a pick.
      if (pop) {
        const QString key = presets[size_t(pick)].key;
        auto* row = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + key);
        if (!row) { picksOk = false; }
        else {
          row->click();
          QTest::qWait(50);
          picksOk = win.settings_.accentColor == key;                     // applied
          escapeClosedAfterPicks = !win.activePopover_ || win.activePopover_->isHidden();  // closed
          lastPick = key;
        }
      }
      if (win.activePopover_ && !win.activePopover_->isHidden()) win.activePopover_->reject();
    });
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's exec until the timer acts
    QVERIFY2(stickyOpened, "right-click did not open the accent popover");
    QVERIFY2(rowsOk, "popover rows must be the preset list with one ✓-marked current row");
    QVERIFY2(stickySurvivedAlt, "the sticky popover must survive an Alt press/release");
    QVERIFY2(picksOk, "a colour pick must apply the accent");
    QVERIFY2(escapeClosedAfterPicks, "a colour pick must close the popover");
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
    const auto motion = withMotion();   // the wipe is motion: reduced motion just restyles
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
    const auto motion = withMotion();
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
    const auto motion = withMotion();
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
    QVERIFY(win.hotkeyActions_.contains(QStringLiteral("openAssistantSettings")));
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

  // The assistant's settings (the chat's … ▸ Settings) have a chord of their own, from the
  // shared registry: it opens the assistant-only dialog with no chat surface up at all, and
  // pressed again inside that dialog it closes it — the toolbar windows' toggle rule.
  void assistantSettingsShortcutOpensAndClosesTheDialog() {
    MainWindow win(nullptr, false);
    openLoaded(win);
    QVERIFY2(!win.actAssistantSettings_->shortcut().isEmpty(), "the dialog has a chord");
    QCOMPARE(win.hotkeyLabels_.value(QStringLiteral("openAssistantSettings")),
             QStringLiteral("AI Assistant Settings"));   // the Shortcuts window lists it
    QVERIFY(!win.chatDock_->isVisible());

    QString dialogName;
    bool sawOwn = false, closedByOwn = false;
    QTimer::singleShot(0, &win, [&] {
      QDialog* dlg = nullptr;
      for (int i = 0; i < 200 && !dlg; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) QTest::qWait(10);
      }
      if (!dlg) return;
      dialogName = dlg->objectName();
      for (QShortcut* sc : dlg->findChildren<QShortcut*>()) {
        if (sc->key() == win.actAssistantSettings_->shortcut()) { sawOwn = true; emit sc->activated(); }
      }
      closedByOwn = !dlg->isVisible();
      if (!closedByOwn) dlg->reject();
    });
    win.actAssistantSettings_->trigger();   // blocks in exec() until the timer closes it

    QCOMPARE(dialogName, QString("assistantSettingsDialog"));
    QVERIFY2(sawOwn, "the dialog carried its own opener's chord");
    QVERIFY2(closedByOwn, "its own shortcut closed the window");
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
    // The invariant is that exactly ONE of the two readouts is up — polled for, not timed:
    // the size line only reads as gone once the fold's finish step hides the rows
    // (QToolBarLayout re-shows its action widgets on every relayout, and the slide is one
    // per frame), and the fold's duration is not this test's business.
    QTRY_VERIFY_WITH_TIMEOUT(hint->isVisible() && !win.imageSizeInfo_->isVisible(), 3000);
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
  // the on-accent white while a session is live. The bug this locks down: it carried the
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

      // ── Drawing: filled, with the on-accent white the app's other filled accent
      // controls use (chatDock's send/attach/gear).
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
      QTRY_VERIFY2_WITH_TIMEOUT(near(glyph(), QColor(Qt::white), 40),
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
      return stencil::gui::themedIcon(name, QColor(Qt::black), 32, false, 1.0)
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
    // reuse it) asks first — the browser's styled confirm (drawingApp.js
    // clearAllLines) — and on Confirm wipes every committed and in-progress point.
    QAction* clear = actionByText(&win, "Clear All Lines");
    QVERIFY(clear && clear->isEnabled());
    dismissModal("OK");
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

    dismissModal("OK");      // blocks on the confirm until the timer answers it
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
    const QString path = QDir::temp().filePath("stencil_e2e_project.stencil");
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
    const QString path = QDir::temp().filePath("stencil_livesync.stencil");
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
    const QString path = QDir::temp().filePath("stencil_delete.stencil");
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
    QCOMPARE(items, (QStringList{"Add image", "Clear history", "Swap message sides", "Settings"}));
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
      // Near the BOTTOM, not the exact vertical center: the top bars (toolbars,
      // image-info strip) are a few rows tall and grow/shrink with theme/content
      // changes — a center sample can drift onto one of them by coincidence. The
      // bottom stays safely inside the canvas/page area regardless.
      const QColor canvasBg =
          centralImg.pixelColor(centralImg.width() / 2, centralImg.height() - 10);
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

  // Browser .chat-msg-user::before/::after parity: every settled bubble grows a
  // painted tail at the corner facing the panel centre — user right, assistant/
  // error left — and "Swap message sides" (the "…" menu item between Clear
  // history and Settings) flips BOTH the alignment and the tail side of every
  // card ALREADY on screen, not just future ones, and persists.
  void chatSwapSidesReskinsRetroactively() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // Whatever this machine's settings.json already holds — restored at the end,
    // like every other test here that touches real persisted settings.
    const bool wasSwapped = win.settings_.chatSwapSides;
    if (wasSwapped) win.chatDock_->setChatSwapSides(false);   // start from a known state
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    win.chatDock_->appendUser(QStringLiteral("hi"), {});
    win.chatDock_->appendAssistant(QStringLiteral("hello"));
    QTest::qWait(250);   // past kAppearMs (140ms) — the entrance slide must have finished
    // A short bubble's width can still settle over a couple of extra layout
    // passes after the wait above (viewport/scrollbar interplay in
    // applyChatBubbleWidths) — one more explicit re-sync makes the geometry
    // checks below deterministic instead of racing the last pixel or two of it.
    win.chatDock_->applyBubbleWidths();

    QFrame* userCard = nullptr;
    QFrame* asstCard = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardUser")) userCard = f;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardAssistant")) asstCard = f;
    QVERIFY(userCard && asstCard);
    auto* transcriptLayout = qobject_cast<QVBoxLayout*>(userCard->parentWidget()->layout());
    QVERIFY(transcriptLayout);
    // QBoxLayout carries a widget's alignment on the LayoutItem, not as a
    // queryable per-widget property — find it by index (setAlignment(widget,…)
    // is the only setter Qt offers, so this is the matching getter path).
    const auto alignmentOf = [](QVBoxLayout* lay, QWidget* w) {
      return lay->itemAt(lay->indexOf(w))->alignment();
    };
    QCOMPARE(alignmentOf(transcriptLayout, userCard), Qt::AlignRight);
    QCOMPARE(alignmentOf(transcriptLayout, asstCard), Qt::AlignLeft);

    // Every role bubble carries a tail — a real widget, positioned OUTSIDE the
    // card's own box on the side its alignment implies.
    const auto tailOf = [](QFrame* card) {
      return qobject_cast<QWidget*>(card->property("chatTail").value<QObject*>());
    };
    auto* userTail = tailOf(userCard);
    auto* asstTail = tailOf(asstCard);
    QVERIFY2(userTail && userTail->isVisible(), "the user bubble has no tail");
    QVERIFY2(asstTail && asstTail->isVisible(), "the assistant bubble has no tail");
    // The tail hangs from the bubble's BOTTOM edge, flush with it, and pokes out
    // past the corner facing the panel centre — checked as an invariant (pokes
    // out on the right side, sits near the bottom), not exact pixel offsets,
    // which are the placement formula's own implementation detail.
    QVERIFY2(userTail->geometry().right() > userCard->geometry().right(),
             "the user bubble's tail must poke out past its RIGHT edge");
    QVERIFY2(qAbs(userTail->geometry().bottom() - userCard->geometry().bottom()) <= 2,
             "the tail must hang flush with the bubble's bottom edge");
    QVERIFY2(asstTail->geometry().left() < asstCard->geometry().left(),
             "the assistant bubble's tail must poke out past its LEFT edge");
    if (qEnvironmentVariableIsSet("STENCIL_GUI_SHOTS")) {
      QTest::qWait(50);
      win.chatDock_->grab().save(QString::fromLocal8Bit(qgetenv("STENCIL_GUI_SHOTS")) + "/dock-tails.png");
    }

    // Flip it — through the REAL menu action, not the setter directly, so the
    // persistence signal is exercised too.
    bool signaled = false;
    bool signaledValue = false;
    connect(win.chatDock_, &stencil::gui::ChatDock::chatSwapSidesChanged, &win,
            [&](bool on) { signaled = true; signaledValue = on; });
    auto* moreBtn = win.chatDock_->findChild<QToolButton*>("chatMore");
    QVERIFY(moreBtn && moreBtn->menu());
    QAction* swapAction = nullptr;
    for (QAction* a : moreBtn->menu()->actions())
      if (a->text() == QLatin1String("Swap message sides")) swapAction = a;
    QVERIFY2(swapAction, "no \"Swap message sides\" action in the … menu");
    swapAction->trigger();

    QVERIFY2(signaled && signaledValue, "the dock must emit chatSwapSidesChanged(true)");
    QVERIFY2(win.settings_.chatSwapSides, "MainWindow must persist the flip into settings_");
    QVERIFY(win.chatDock_->chatSwapSides());

    // The EXISTING cards moved — this is the whole point (a browser/extension
    // parity CSS class would do this for free; Qt has to re-skin by hand).
    QCOMPARE(alignmentOf(transcriptLayout, userCard), Qt::AlignLeft);
    QCOMPARE(alignmentOf(transcriptLayout, asstCard), Qt::AlignRight);
    QVERIFY2(userTail->geometry().left() < userCard->geometry().left(),
             "swapped: the user bubble's tail must have moved to poke out past its LEFT edge");
    QVERIFY2(asstTail->geometry().right() > asstCard->geometry().right(),
             "swapped: the assistant bubble's tail must have moved to poke out past its RIGHT edge");

    // A round trip through the SAME file persistence every other setting uses.
    const stencil::gui::Settings loaded = stencil::gui::fileStore::loadSettings();
    QVERIFY2(loaded.chatSwapSides, "the flip must survive a settings.json round trip");

    // …and a NEWLY opened context-menu mirror panel picks up the SAME preference,
    // not the pre-flip default.
    win.ensureChatMenuPanel();
    win.chatMirror(QStringLiteral("Assistant"), QStringLiteral("mirrored"), false);
    QTest::qWait(20);
    QFrame* mirroredAsst = nullptr;
    for (QFrame* f : win.chatMenuPanel_->findChildren<QFrame*>("chatCardAssistant"))
      mirroredAsst = f;
    QVERIFY2(mirroredAsst, "the mirror panel never rendered the appended row");
    auto* mirrorLayout = qobject_cast<QVBoxLayout*>(mirroredAsst->parentWidget()->layout());
    QVERIFY(mirrorLayout);
    QCOMPARE(alignmentOf(mirrorLayout, mirroredAsst), Qt::AlignRight);

    // Restore whatever this machine's settings.json held before the test, exactly
    // (an even number of clicks isn't enough if it started true).
    win.chatDock_->setChatSwapSides(wasSwapped);
    win.settings_.chatSwapSides = wasSwapped;
    stencil::gui::fileStore::saveSettings(win.settings_);
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
    QCOMPARE(send->toolTip(), QString());
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

  // FEATURE (user report): Cmd+C / the toolbar Copy button's plain click default to
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

  // FEATURE (user report): "Filter Only" would render byte-identical to "Original" with
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

  // FEATURE (user report): "Current"'s OWN row (actCopyImageCurrentRow_/
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

  // REGRESSION (user report, screenshot): MenuHotkeyChips only ever placed/hid a row's
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

  // FEATURE (user report): "With Compare" is a SEPARATE action (actCopyImageSplit_/
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

    // REGRESSION (user report): MenuHotkeyChips never deleted a row's chip widget on
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

    // Appear: a fresh card is claimed by its own opacity effect and ends fully visible.
    // Overlapping appends each own their animation, so all of them land at 1.0 and at
    // their resting margins. This suite runs REDUCED (STENCIL_NO_ANIM), where the card is
    // simply THERE — the arrival's veil-then-dust is chatCardsArriveOutOfDust's business,
    // and a card left hidden here is what reducedMotionChatCardArrivesAtOnce pins.
    dock->appendUser("again");
    dock->appendAssistant("sure");
    dock->appendError("nope");
    const auto cards =
        transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly);
    QCOMPARE(cards.size(), 3);
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
    dismissModal("OK");
    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);
    beat();
  }

  // Pan/zoom persistence (browser parity: storage.js's own debounced scroll/zoom save +
  // "Saved" toast). A zoom change and a scrollbar drag each debounce into ONE save, the
  // active project's record picks up the new view, and reopening that project restores it
  // instead of the plain fit-to-window.
  void panZoomPersistsPerProjectWithSavedToast() {
    using stencil::gui::Project;
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    win.adoptCanvasAsLocalProject();
    QVERIFY(!win.activeProjectId_.isEmpty());
    const QString projectId = win.activeProjectId_;

    // Zoom in past the viewport so the canvas actually grows a scrollable range —
    // otherwise the scrollbars have nowhere to move and the pan half of this test proves
    // nothing.
    win.setZoom(5.0);
    win.scroll_->horizontalScrollBar()->setValue(30);
    win.scroll_->verticalScrollBar()->setValue(20);
    QTest::qWait(600);   // past the 400ms debounce

    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY(pr);
      QCOMPARE(pr->zoomScale, 5.0);
      QCOMPARE(pr->scrollLeft, 30);
      QCOMPARE(pr->scrollTop, 20);
    }
    // The debounced save flashes the same toast every other save does.
    {
      bool sawSaved = false;
      for (QLabel* l : win.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly))
        if (l->property("stencilToastText").toString() == "Saved") sawSaved = true;
      QVERIFY2(sawSaved, "no \"Saved\" toast after the debounced pan/zoom save");
    }

    // Knock the live canvas to a different zoom WITHOUT going through setZoom (which would
    // reschedule — and overwrite — the very save just verified above), simulating "the
    // editor is sitting somewhere else" right before this project reopens.
    canvas->setScale(1.0);
    QVERIFY(win.loadProjectIntoCanvas(projectId, /*animate=*/false));
    QTest::qWait(50);   // the scroll half restores a turn later (QTimer::singleShot(0, …))
    QCOMPARE(canvas->scale(), 5.0);
    QCOMPARE(win.scroll_->horizontalScrollBar()->value(), 30);
    QCOMPARE(win.scroll_->verticalScrollBar()->value(), 20);

    // Tidy the dev state dir: drop the project this test created.
    dismissModal("OK");
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
      // startsWith, not == : a submenu-opener's own hint text is native-formatted off
      // the action it names (mainWindow.cpp hintTab — "Image Filter\t⌥B" on macOS,
      // "Image Filter\tAlt+B" elsewhere), so a literal platform-specific suffix isn't
      // reliable to match here. "Style" etc. carry no hint at all, so the prefix IS
      // the whole label — startsWith is exact for them too.
      for (QAction* a : menu->actions())
        if (a->text().startsWith(title)) parent = a;
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
      for (QAction* a : menu->actions())    // startsWith — see openSub's comment above
        if (a->text().startsWith(title)) parent = a;
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
      filterOpenedOff = openSubByKey(menu, "Image Filter") != nullptr;
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
      filterOpened = openSub(menu, "Image Filter") != nullptr;

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

  // A context submenu hover-opens, SubmenuCloseGuard closes it again on a hover-away, and
  // on a real display it dusts on every open — including the second open of the same QMenu
  // instance, which is what MenuReveal's re-arming fixed. The dust half cannot run here:
  // dustMotionOk() refuses on the `offscreen` platform, which nothing can lift, so only the
  // open/close half is asserted and the case reports itself SKIPPED.
  void ctxSubmenuDustReplayProbe() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);

    const auto moveTo = [](QMenu* m, const QPoint& p) {
      QMouseEvent e(QEvent::MouseMove, QPointF(p), QPointF(m->mapToGlobal(p)),
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(m, &e);
    };
    const auto hoverPath = [&](QMenu* m, const QPoint& from, const QPoint& to) {
      for (int i = 1; i <= 8; ++i) { moveTo(m, from + (to - from) * i / 8); QTest::qWait(15); }
    };
    const auto dustSeen = [&win] {
      for (QWidget* w : win.findChildren<QWidget*>(
               QString::fromLatin1(stencil::gui::DisintegrateOverlay::kObjectName))) {
        auto* fx = static_cast<stencil::gui::DisintegrateOverlay*>(w);
        if (fx->surfacePicture().isValid()) return true;
      }
      return false;
    };

    bool dustOnFirstOpen = false, dustOnClose = false, closed = false, dustOnSecondOpen = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      QAction* parent = nullptr;
      for (QAction* a : menu->actions())
        if (a->text().startsWith("Image / Layout")) parent = a;
      if (!parent || !parent->menu()) return;
      QAction* plainRow = nullptr;
      for (QAction* a : menu->actions()) {
        if (a->isSeparator() || a->menu() || !a->isEnabled()) continue;
        plainRow = a; break;
      }
      if (!plainRow) return;
      const QPoint plainCenter = menu->actionGeometry(plainRow).center();
      const QPoint parentCenter = menu->actionGeometry(parent).center();
      QMenu* sub = parent->menu();

      // Open #1.
      for (int attempt = 0; attempt < 4 && !sub->isVisible(); ++attempt) {
        moveTo(menu, plainCenter); QTest::qWait(30);
        moveTo(menu, parentCenter);
        for (int i = 0; i < 40 && !sub->isVisible(); ++i) QTest::qWait(10);
      }
      if (!sub->isVisible()) { menu->close(); return; }
      dustOnFirstOpen = dustSeen();

      // Hover away — our own SubmenuCloseGuard should hide it AND dust it.
      hoverPath(menu, parentCenter, plainCenter);
      for (int i = 0; i < 60 && sub->isVisible(); ++i) { QTest::qWait(10); if (dustSeen()) dustOnClose = true; }
      // The flight is spawned by the hide itself (menuReveal.cpp dustMenuOut off
      // aboutToHide), so it is only there to see once the popup has gone.
      if (dustSeen()) dustOnClose = true;
      closed = !sub->isVisible();

      // Open #2 — the SAME QMenu instance, reopened.
      hoverPath(menu, plainCenter, parentCenter);
      for (int i = 0; i < 60 && !sub->isVisible(); ++i) QTest::qWait(10);
      dustOnSecondOpen = dustSeen();

      menu->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));

    // Correctness first, and it holds on every platform: our guard really does close a
    // hovered-away submenu (Qt itself leaves it up).
    QVERIFY2(closed, "the submenu never closed");
    if (!stencil::support::dustMotionOk())
      QSKIP("dust is gated off on the offscreen platform (dustMotionOk) — "
            "run this binary on a real display to exercise the flights");
    QVERIFY2(dustOnFirstOpen, "no dust on the first open");
    QVERIFY2(dustOnClose, "no dust while our own guard closed the submenu");
    QVERIFY2(dustOnSecondOpen, "no dust replayed on the second open of the same submenu");
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
    win.openPathFromOS(png_);   // the canvas menu opens for an image, and only then
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
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
      const QIcon ic = stencil::gui::themedIcon("download", QColor("#e0e0e0"), 18, false, dpr);
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
      // on the light theme's pale disabled chip (user report).
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

  // The dialog reveal must START at the icon that opened it. This checks the flight
  // itself: support::revealDialog dusts a snapshot of the dialog across the window, every
  // mote streaming out of that icon — so the flight's target point IS the origin the user
  // sees the window come out of.
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
    // The cloud is already in flight by the time we can look, so read the point it aims
    // at — that is where the user sees the window come out of.
    const auto flightOrigin = [&](QWidget* anchor) {
      QDialog dlg(&win);
      dlg.resize(300, 200);
      stencil::support::revealDialog(dlg, anchor, QRect());
      dlg.show();
      QTest::qWait(50);          // past the 0-timer that builds the cloud, inside the flight
      const QPoint from = surfaceFlightTarget(&win);
      dlg.close();
      return from;
    };
    QToolButton* icon = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->isVisible() && b->property("toolSection").isValid()) { icon = b; break; }
    QVERIFY2(icon, "no visible toolbar icon to fly out of");
    const QPoint origin = flightOrigin(icon);
    QVERIFY2(origin != QPoint(-1, -1), "no reveal flight was created");
    const QPoint want = flightPointOf(icon, &win);
    QVERIFY2(origin == want, qPrintable(QString("the flight starts at %1, the icon is at %2")
                                            .arg(QDebug::toString(origin), QDebug::toString(want))));
  }

  // The chat composer's "…" was the last popup in the app that still hard-cut on both
  // edges, and the Assistant window it raises grew out of nothing — its Settings item is
  // gone by the time the window opens, so the anchor measured 0x0. Both now belong to the
  // "…" TRIGGER, which is also what makes the window fall from above when the dock is
  // shut: a hidden anchor is no anchor (modalReveal originRect).
  void chatOverflowAndItsWindowFlyToTheDotsTrigger() {
    MainWindow win;
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.chatDock_->setVisible(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    QTest::qWait(150);
    // ctest runs with STENCIL_NO_ANIM=1 and every flight is a no-op under it; this test
    // is about the flight itself, so turn it back on for the duration.
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });

    auto* moreBtn = win.chatDock_->moreButton();
    QVERIFY(moreBtn && moreBtn->isVisible());
    QMenu* menu = moreBtn->menu();
    QVERIFY(menu);
    const QPoint want = flightPointOf(moreBtn, &win);

    // The menu's own dust is skipped offscreen by design (menuReveal revealPopup — the
    // gui suite picks items the instant the popup lands), so what is checkable here is
    // that BOTH edges are wired, and wired to the trigger. It is a repeat-show menu, so
    // the one-shot MenuReveal would have been wrong; revealMenuFrom is what it gets.
    auto* flight = menu->findChild<QObject*>(QStringLiteral("stencilMenuFlight"),
                                             Qt::FindDirectChildrenOnly);
    QVERIFY2(flight, "the … menu has no flight wired at all");

    // It is four short labelled icons, NOT a menu-bar menu: the theme's gutters (24px
    // left check reserve + 26px right shortcut slack) plus the shortcut column Qt
    // reserves anyway left a visible gap after each glyph and a band of dead space down
    // the right edge. compactIconMenu hugs the longest label instead.
    menu->popup(moreBtn->mapToGlobal(moreBtn->rect().bottomLeft()));
    QVERIFY(QTest::qWaitForWindowExposed(menu));
    QTest::qWait(30);
    int widest = 0;
    for (QAction* a : menu->actions())
      widest = std::max(widest, menu->fontMetrics().horizontalAdvance(a->text()));
    QVERIFY2(widest > 0, "no labels to measure");
    const int slack = menu->width() - widest;
    // Icon + paddings only. The untamed hint ran ~95px past the label on this font.
    QVERIFY2(slack > 0 && slack <= 64,
             qPrintable(QString("menu is %1 wide for a %2 label — %3px of slack")
                            .arg(menu->width()).arg(widest).arg(slack)));
    menu->hide();
    QTest::qWait(30);
    // Popping it twice must not stack a second filter — nor go quiet on the second show.
    menu->popup(moreBtn->mapToGlobal(moreBtn->rect().bottomLeft()));
    QVERIFY(QTest::qWaitForWindowExposed(menu));
    menu->hide();
    QTest::qWait(30);
    menu->popup(moreBtn->mapToGlobal(moreBtn->rect().bottomLeft()));
    QTest::qWait(30);
    menu->hide();
    QTest::qWait(30);
    QCOMPARE(menu->findChildren<QObject*>(QStringLiteral("stencilMenuFlight"),
                                          Qt::FindDirectChildrenOnly).size(), 1);

    // …and the window that Settings raises rides the trigger's point — this half DOES
    // fly offscreen, so it is asserted for real.
    QDialog dlg(&win);
    dlg.resize(320, 240);
    stencil::support::revealDialog(dlg, moreBtn);
    dlg.show();
    QTest::qWait(50);
    QCOMPARE(surfaceFlightTarget(&win), want);
    dlg.close();
    QTest::qWait(50);

    // A shut dock leaves nothing on screen to own the window: it falls from above
    // instead of out of the trigger's stale last position.
    win.chatDock_->setVisible(false);
    QTRY_VERIFY(!moreBtn->isVisible());
    QDialog orphan(&win);
    orphan.resize(320, 240);
    stencil::support::revealDialog(orphan, moreBtn);
    orphan.show();
    QTest::qWait(50);
    const QPoint above = surfaceFlightTarget(&win);
    if (above != QPoint(-1, -1))
      QVERIFY2(above != want, "a hidden trigger must not keep claiming the flight");
    orphan.close();
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
    // exec() blocks, so a 0-timer drives the modal: the watcher catches the flight's
    // origin the instant it starts (QColorDialog is comfortably past the dust size
    // ceiling, so this is the ghost — reading it late would already be mid-tween).
    RevealOriginWatcher watcher;
    qApp->installEventFilter(&watcher);
    const auto removeWatcher = qScopeGuard([&] { qApp->removeEventFilter(&watcher); });
    QTimer::singleShot(0, [&] {
      for (int i = 0; i < 200; ++i) {
        if (auto* dlg = qobject_cast<QColorDialog*>(QApplication::activeModalWidget())) {
          // revealDialog's own 0-timer (registered after this one) needs a turn to
          // fire and start the OPEN flight before accept() closes the dialog and
          // starts the close flight instead — same watcher, same object name.
          QTest::qWait(50);
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
    const QPoint want = flightPointOf(icon, &win);
    QVERIFY2(watcher.origin == want,
             qPrintable(QString("the picker's flight starts at %1, the anchor icon is at %2")
                            .arg(QDebug::toString(watcher.origin), QDebug::toString(want))));
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

    // The watcher catches the flight's origin the instant it starts, whichever
    // mechanism plays it — a ghost's geometry is what a QPropertyAnimation is
    // actively tweening, so reading it any time after Show would already be
    // mid-flight, not the origin.
    RevealOriginWatcher watcher;
    qApp->installEventFilter(&watcher);
    const auto removeWatcher = qScopeGuard([&] { qApp->removeEventFilter(&watcher); });
    // Let the dialog and its flight fully appear, then close it so trigger() returns.
    const auto closeSoon = [&] {
      QTimer::singleShot(140, &win, [&] {
        if (QWidget* modal = QApplication::activeModalWidget()) modal->close();
      });
    };

    // ── an icon-backed dialog ──
    QWidget* icon = win.buttonForAction(win.actProjects_);
    QVERIFY2(icon && icon->isVisible(), "the Projects icon is not on the toolbar");
    watcher.reset();
    closeSoon();
    win.actProjects_->trigger();
    QTest::qWait(50);
    {
      const QPoint want = flightPointOf(icon, &win);
      QVERIFY2(watcher.origin == want,
               qPrintable(QString("icon case: flight starts at %1, icon at %2")
                              .arg(QDebug::toString(watcher.origin), QDebug::toString(want))));
    }

    // ── a menu-only dialog: no icon, so the clicked ROW is the origin ──
    // Synthetic action: every dialog-opening action now has a toolbar icon
    // (actShortcuts_ used to be the exception this borrowed — fixed to carry the
    // browser's gear icon like its siblings), so nothing icon-less is left to
    // borrow. Built the same way a real one would be: bound, then wired to open
    // a plain dialog through the same execMaybePopover path.
    QMenu* help = nullptr;
    for (QMenu* m : win.menuBar()->findChildren<QMenu*>())
      if (m->actions().contains(win.actInfo_)) { help = m; break; }
    QVERIFY2(help, "the Help menu was not found");
    auto* act = new QAction("Test Menu-Only Dialog", &win);
    help->addAction(act);
    win.bindRevealAnchor(act);
    connect(act, &QAction::triggered, &win, [&win, act] {
      QDialog dlg(&win);
      dlg.resize(300, 200);
      win.execMaybePopover(dlg, act);
    });
    QVERIFY2(!win.buttonForAction(act), "the synthetic action must have no toolbar icon");
    help->popup(win.mapToGlobal(QPoint(60, 40)));
    QVERIFY(QTest::qWaitForWindowExposed(help));
    const QRect row = help->actionGeometry(act);
    QTest::mouseMove(help, row.center());
    QTest::qWait(30);
    help->close();
    watcher.reset();
    closeSoon();
    act->trigger();
    QTest::qWait(50);
    const QRect rowInWin(win.mapFromGlobal(help->mapToGlobal(row.topLeft())), row.size());
    QVERIFY2(watcher.origin == rowInWin.center(),
             qPrintable(QString("menu case: flight starts at %1, row at %2")
                            .arg(QDebug::toString(watcher.origin), QDebug::toString(rowInWin))));
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
    // A pixel test owns its palette: this suite shares the real settings file, and on the
    // LIGHT theme the card's dashed border is paler than its accent fill, so the
    // brightest-pixel search below locks onto the border and never sees the band move.
    // Not persisted (applySettings' `persist` is false) — the app's own theme is left be.
    {
      Settings s = win.settings_;
      s.themeMode = QStringLiteral("dark");
      win.applySettings(s, /*persist=*/false);
      QTest::qWait(60);
    }
    win.canvas_->clearImage();
    win.refreshActions();
    QTest::qWait(1800);   // past the clear-dust hold that hides the card (kMs + a beat)
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
    // The palette IS the browser's, byte for byte, on every platform: encoding into
    // Display P3 on macOS was a second conversion on an already colour-managed surface and
    // made the whole app read duller. The values below are exactly the ones in
    // browser/css/theme.css and js/config/constants.json.
    QCOMPARE(stencil::gui::accentPrimary("violet").name(), QStringLiteral("#7c3aed"));
    QCOMPARE(stencil::gui::themePalette(true).bgPage.name(), QStringLiteral("#1a1a1a"));
    QCOMPARE(stencil::gui::themePalette(false).bgPage.name(), QStringLiteral("#f0f0f0"));
    QCOMPARE(stencil::gui::themePalette(false).danger.name(), QStringLiteral("#d6293e"));
    QCOMPARE(stencil::gui::themePalette(true).danger.name(), QStringLiteral("#f0697a"));
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

  // Docking the chat onto the SAME side as the points panel used to let the two fight
  // over width: the panel would balloon or collapse mid-slide (Qt's dock layout freely
  // redistributing space between two flexible siblings), and that bad width then got
  // captured as the "restore" size, reappearing very wide on the next reopen. Browser
  // parity (layout.css .main-content flex row): the chat overlay only ever eats into the
  // canvas column — the fixed-width panel beside it never moves. Regression for that bug.
  void chatSharingPanelSideKeepsPanelWidthStable() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.selPanel_ && win.chatDock_);
    QVERIFY(win.dockWidgetArea(win.selPanel_) == Qt::RightDockWidgetArea);
    QTRY_VERIFY(!win.selPanel_->isHidden());

    // Chat starts docked LEFT by default — open it (an UNRELATED area, so this alone
    // settles QMainWindow's dock layout onto the panel's real natural width, not
    // whatever incidental size it had straight out of construction) and let it settle.
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible() && !win.chatDock_->isFloating());
    QTest::qWait(400);
    const int panelBefore = win.selPanel_->width();
    QVERIFY2(panelBefore > 120, "the points panel never reached its natural width");

    // Now place the chat RIGHT, alongside the points panel, and watch the panel's
    // width through the whole flight.
    emit static_cast<stencil::gui::ChatDock*>(win.chatDock_)->dockRequested(Qt::RightDockWidgetArea);
    int maxSeen = 0, minSeen = win.width();
    for (int i = 0; i < 60; ++i) {
      QTest::qWait(15);
      if (win.selPanel_->isHidden()) continue;
      const int w = win.selPanel_->width();
      maxSeen = std::max(maxSeen, w);
      minSeen = std::min(minSeen, w);
    }
    QTRY_COMPARE(win.dockWidgetArea(win.chatDock_), Qt::RightDockWidgetArea);
    QTest::qWait(200);
    // They must land SIDE BY SIDE (same row, chat to the right of the panel) —
    // never stacked vertically (Qt's plain, unsplit addDockWidget default).
    QCOMPARE(win.selPanel_->mapTo(&win, QPoint(0, 0)).y(), win.chatDock_->mapTo(&win, QPoint(0, 0)).y());
    QVERIFY2(win.chatDock_->mapTo(&win, QPoint(0, 0)).x() > win.selPanel_->mapTo(&win, QPoint(0, 0)).x(),
             "chat did not land to the right of the points panel");
    // The panel must never balloon past its pre-share width, nor get squeezed away —
    // the chat's own slide is what should move, not the panel sitting beside it.
    QVERIFY2(maxSeen <= panelBefore + 8,
             qPrintable(QString("points panel widened to %1 (was %2)").arg(maxSeen).arg(panelBefore)));
    QVERIFY2(minSeen >= 100,
             qPrintable(QString("points panel collapsed to %1 mid-slide").arg(minSeen)));

    // Close the chat: the panel should hand its width right back...
    win.actChat_->setChecked(false);
    QTRY_VERIFY(!win.chatDock_->isVisible());
    QTest::qWait(300);
    QVERIFY2(win.selPanel_->width() >= panelBefore - 8,
             qPrintable(QString("panel stayed narrow after chat closed: %1 (was %2)")
                            .arg(win.selPanel_->width()).arg(panelBefore)));

    // ...and reopening the chat (sharing again) must not have baked a bad "restore"
    // width into the panel from the earlier fight — it settles back near its own size,
    // never "very wide".
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    QTest::qWait(400);
    QVERIFY2(win.selPanel_->width() <= panelBefore + 8,
             qPrintable(QString("panel reopened very wide: %1 (was %2)")
                            .arg(win.selPanel_->width()).arg(panelBefore)));
  }

  // The points panel can be HIDDEN (no image loaded, or collapsed by the user) at the
  // moment the chat gets placed onto its side — dockChatTo used to gate its split on the
  // panel being visible right then, so the two were left plain-stacked (Qt's unsplit
  // addDockWidget default: one squashed row above the other). Showing the panel again
  // later never re-split them — it just reappeared squashed under the chat. Regression
  // for that; ensurePanelChatSplit must repair it wherever either dock's visibility flips.
  void chatPlacedWhilePanelHiddenStillSplitsSideBySide() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.selPanel_ && win.chatDock_ && win.actPanel_ && win.actChat_);
    QVERIFY(win.dockWidgetArea(win.selPanel_) == Qt::RightDockWidgetArea);
    QTRY_VERIFY(!win.selPanel_->isHidden());

    // Hide the points panel FIRST...
    win.actPanel_->setChecked(false);
    QTRY_VERIFY(win.selPanel_->isHidden());

    // ...then place the (unrelated-side) chat onto the panel's side while it's hidden.
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible() && !win.chatDock_->isFloating());
    emit static_cast<stencil::gui::ChatDock*>(win.chatDock_)->dockRequested(Qt::RightDockWidgetArea);
    QTRY_COMPARE(win.dockWidgetArea(win.chatDock_), Qt::RightDockWidgetArea);
    QTest::qWait(400);

    // Now reveal the panel again — it must come back BESIDE the chat, not squashed
    // underneath it.
    win.actPanel_->setChecked(true);
    QTRY_VERIFY(!win.selPanel_->isHidden());
    QTest::qWait(400);
    QCOMPARE(win.selPanel_->mapTo(&win, QPoint(0, 0)).y(), win.chatDock_->mapTo(&win, QPoint(0, 0)).y());
    QVERIFY2(win.chatDock_->mapTo(&win, QPoint(0, 0)).x() > win.selPanel_->mapTo(&win, QPoint(0, 0)).x(),
             "the panel reappeared stacked under the chat instead of beside it");
    // Squashed means SHARING a vertical row with the chat — not "shorter than half
    // the window": the stacked toolbars and the top info dock can leave the whole
    // dock row well under half of it. Side by side, both fill that row.
    QCOMPARE(win.selPanel_->height(), win.chatDock_->height());
    QVERIFY(win.centralWidget());
    QVERIFY2(win.selPanel_->height() == win.centralWidget()->height(),
             "the panel came back with a squashed, shared-row height");
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
    const QPoint iconPoint = flightPointOf(icon, &win);
    // The flight is a cloud of the dock's own pixels inside the MAIN window, aimed at
    // that icon: gathering out of it on the way in, scattering back into it on the way
    // out. Both are the icon — the DIRECTION is what tells the two apart.
    win.actChat_->setChecked(false);          // close: the window comes apart into the icon
    QTest::qWait(60);
    auto* closing = surfaceFlight(&win);
    QVERIFY2(closing, "closing a floating chat did not animate");
    QCOMPARE(closing->surfaceTarget(), iconPoint);
    QVERIFY2(!closing->gathering(), "the close flight scatters INTO the icon, it does not gather");
    QTest::qWait(400);
    win.actChat_->setChecked(true);           // open: it forms out of the icon
    QTest::qWait(60);
    auto* opening = surfaceFlight(&win);
    QVERIFY2(opening, "opening a floating chat did not animate");
    QCOMPARE(opening->surfaceTarget(), iconPoint);
    QVERIFY2(opening->gathering(), "the open flight gathers OUT of the icon");
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
    // …while the attachment cap is a TOAST (browser parity: notify(…, 'info')), not a
    // transcript card: neither surface grows a row, the window's stack shows the line in
    // the accent (never the danger red), and a batch's repeated hits fold into one toast.
    const int dockRows = cardsOf(win.chatDock_).size();
    win.chatDock_->warnAttachmentCap();
    win.chatDock_->warnAttachmentCap();
    same("cap toast");
    QCOMPARE(cardsOf(win.chatDock_).size(), dockRows);
    int capToasts = 0;
    for (QLabel* l : win.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly)) {
      if (!l->property("stencilToastText").toString().startsWith("Up to 3 images per message")) continue;
      ++capToasts;
      const auto pal = stencil::gui::themePalette(stencil::gui::resolveDark(win.settings_.themeMode), win.settings_.accentColor);
      QVERIFY2(!l->styleSheet().contains(pal.danger.name(), Qt::CaseInsensitive), "the cap toast is red");
    }
    QCOMPARE(capToasts, 1);

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
    // Nothing may mark the icon at all now; the lambda stays to prove it.
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
    QVERIFY2(!unreadShown(), "the toast is the whole notice — nothing is left on the icon");

    // Opening clears the mark, and nothing toasts while the chat is up.
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    QVERIFY2(!unreadShown(), "opening must leave the icon unmarked too");
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
    win.chatTurnSettled();
    QVERIFY2(!toast(), "the turn terminal must be silent — nothing runs after the reply");
    QVERIFY2(!unreadShown(), "…and it must not mark the icon either");
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

    // Hover an image-space spot; report what the tooltip says (empty = hidden). The
    // reveal now waits out the same delay as the toolbar tooltip (mainWindow.cpp
    // scheduleHoverShow, 200 ms) before it actually shows, so this outwaits it.
    const auto hoverText = [&](double ix, double iy) {
      const QPoint p(qRound(ix * s), qRound(iy * s));
      QMouseEvent move(QEvent::MouseMove, QPointF(p), canvas->mapToGlobal(p), Qt::NoButton,
                       Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(canvas, &move);
      QTest::qWait(240);
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

  // The tooltip must not show — or stay stuck showing — while the mouse is down doing
  // something else (Alt-dragging a point, drag-creating a rect/zoom box, panning);
  // hoverLeft() retracts one already up when the drag starts.
  void noTooltipWhileTheMouseIsDownDrawingOrDragging() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTest::qWait(150);
    win.settings_.tooltipEnabled = true;    // independent of the machine's saved settings
    win.settings_.tooltipShowScreen = true;

    stencil::core::Line line;
    line.points = {{40, 40}, {160, 120}};
    canvas->setLines({line});
    const double s = canvas->scale();

    auto sendMouse = [&](QEvent::Type t, const QPointF& pos, Qt::MouseButton btn,
                         Qt::MouseButtons btns, Qt::KeyboardModifiers mods) {
      QMouseEvent ev(t, pos, canvas->mapToGlobal(pos.toPoint()), btn, btns, mods);
      QCoreApplication::sendEvent(canvas, &ev);
    };
    const auto moveTo = [&](double ix, double iy) {
      sendMouse(QEvent::MouseMove, QPointF(ix * s, iy * s), Qt::NoButton, Qt::NoButton,
               Qt::NoModifier);
    };

    // Baseline: hovering the first point (no modifiers, nothing else going on) shows the
    // tooltip after the reveal delay — proves the setup actually can show one at all.
    moveTo(40, 40);
    QTRY_VERIFY_WITH_TIMEOUT(win.tooltip_->isVisible(), 1000);

    // Alt-press ON that same point starts a point drag without moving first — the
    // stranding case: the very next move must retract the tooltip that was already up,
    // not just skip showing a new one. (The drag itself relocates the point to wherever
    // it's released — setLines() below puts it back for the next section.)
    sendMouse(QEvent::MouseButtonPress, QPointF(40 * s, 40 * s), Qt::LeftButton,
             Qt::LeftButton, Qt::AltModifier);
    sendMouse(QEvent::MouseMove, QPointF(70 * s, 60 * s), Qt::NoButton, Qt::LeftButton,
             Qt::AltModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!win.tooltip_->isVisible(), 1000);
    // Dragging further — even back over the SECOND point — never re-shows it either.
    sendMouse(QEvent::MouseMove, QPointF(160 * s, 120 * s), Qt::NoButton, Qt::LeftButton,
             Qt::AltModifier);
    QTest::qWait(260);   // outwait the reveal delay — it must still be hidden
    QVERIFY2(!win.tooltip_->isVisible(), "a point drag popped a tooltip mid-drag");
    sendMouse(QEvent::MouseButtonRelease, QPointF(160 * s, 120 * s), Qt::LeftButton,
             Qt::NoButton, Qt::AltModifier);
    beat();

    // Rect-draw mode, dragging out a box over the first point: no tooltip either.
    canvas->setLines({line});   // undo the point drag above — point 1 back at (40, 40)
    canvas->setDrawMode(CanvasWidget::DrawMode::Rect);
    moveTo(40, 40);
    QTRY_VERIFY_WITH_TIMEOUT(win.tooltip_->isVisible(), 1000);
    sendMouse(QEvent::MouseButtonPress, QPointF(40 * s, 40 * s), Qt::LeftButton,
             Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, QPointF(90 * s, 90 * s), Qt::NoButton, Qt::LeftButton,
             Qt::NoModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!win.tooltip_->isVisible(), 1000);
    QTest::qWait(260);
    QVERIFY2(!win.tooltip_->isVisible(), "a rect-draw drag popped a tooltip mid-drag");
    sendMouse(QEvent::MouseButtonRelease, QPointF(90 * s, 90 * s), Qt::LeftButton,
             Qt::NoButton, Qt::NoModifier);
    canvas->setDrawMode(CanvasWidget::DrawMode::Line);
    beat();

    // Shift-drag (zoom rect) over a point: still nothing.
    canvas->setLines({line});   // drop the rect-draw commit above, back to the plain line
    moveTo(40, 40);
    QTRY_VERIFY_WITH_TIMEOUT(win.tooltip_->isVisible(), 1000);
    sendMouse(QEvent::MouseButtonPress, QPointF(40 * s, 40 * s), Qt::LeftButton,
             Qt::LeftButton, Qt::ShiftModifier);
    // Kept under the 4-image-px commit threshold (mouseReleaseEvent) so releasing does
    // NOT actually zoom — this section only cares about the tooltip during the drag.
    sendMouse(QEvent::MouseMove, QPointF(42 * s, 41 * s), Qt::NoButton, Qt::LeftButton,
             Qt::ShiftModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!win.tooltip_->isVisible(), 1000);
    QTest::qWait(260);
    QVERIFY2(!win.tooltip_->isVisible(), "a zoom-rect drag popped a tooltip mid-drag");
    sendMouse(QEvent::MouseButtonRelease, QPointF(42 * s, 41 * s), Qt::LeftButton,
             Qt::NoButton, Qt::ShiftModifier);
    beat();

    // Back to a plain hover afterwards: the tooltip is not stuck off either.
    moveTo(40, 40);
    QTRY_VERIFY_WITH_TIMEOUT(win.tooltip_->isVisible(), 1000);
    beat();
  }

  // imageSizeInfo_ needs real top/bottom breathing room via contentsMargins, not
  // stylesheet `padding` — QSS padding on this QLabel had no effect on paint or sizeHint().
  void imageSizeInfoHasRealVerticalPadding() {
    MainWindow win(nullptr, false);
    openLoaded(win);
    QVERIFY(win.imageSizeInfo_);
    // 10px left/right (browser parity: css/layout.css .info padding: 10px), 11px top/bottom
    // so the readout reads as its own band between the toolbars and the canvas.
    QCOMPARE(win.imageSizeInfo_->contentsMargins(), QMargins(10, 11, 10, 11));
    // Not just set — actually taken into account: the reserved fixed height must exceed
    // the bare font height by at least the vertical margins.
    win.reserveImageInfoHeight();
    const int fontH = QFontMetrics(win.imageSizeInfo_->font()).height();
    QVERIFY2(win.imageSizeInfo_->height() >= fontH + 22,
             "the reserved height leaves no room for 11px top + 11px bottom");
  }

  // The way back OUT of a closed area. "Unchain" sits in the bar's area-only group, so it
  // is offered exactly when a line is an area, and clicking it puts the line back to an
  // open polyline (canvas/chainEdit.hpp; Alt+Ctrl+drag is the gesture route).
  void unchainButtonIsOfferedOnlyForAreas() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    auto* unchain = win.selectedLineBar_->findChild<QPushButton*>("selectedLineUnchain");
    QVERIFY2(unchain, "the bar has no Unchain button");

    // An OPEN line: the area controls, Unchain among them, stay away.
    stencil::core::Line open;
    open.points = {{20, 20}, {80, 80}, {40, 90}};
    canvas->setLines({open});
    canvas->selectLineByIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(win.selectedLineDock_->isVisible(), 2000);
    // The group SLIDES away now (controlReveal), so visibility settles on the event loop
    // rather than on the same tick — wait it out instead of reading it mid-flight.
    QTRY_VERIFY2(!unchain->isVisible(), "an open line was offered Unchain");

    // REGRESSION: the fill swatch showed a washed-out salmon for a translucent green — a
    // CSS `#rrggbbaa` handed to QColor, whose 8-digit form is #AARRGGBB, alpha first. It
    // must go through cssColor(), like every other stored colour.
    {
      stencil::core::Line tinted;
      tinted.points = {{10, 10}, {90, 10}, {90, 70}, {10, 70}};
      tinted.locked = true;
      tinted.fillColor = "#00aa4440";        // green at alpha 0x40
      canvas->setLines({tinted});
      canvas->selectLineByIndex(0);
      beat();
      auto* swatch = win.selectedLineBar_->findChild<QPushButton*>("selectedLineFillSwatch");
      QVERIFY2(swatch, "the bar has no fill swatch");
      // The colour lives in the well's CHIP now (a 32x16 pixmap inside the input frame),
      // so read it there: its channels must be the CSS ones, alpha included.
      const QImage chip = swatch->icon().pixmap(32, 16).toImage();
      const QColor mid = chip.pixelColor(chip.width() / 2, chip.height() / 2);
      // ±2 per channel: the chip is drawn into a premultiplied pixmap, so the readback
      // rounds by a unit. What matters is that it is THIS green at THIS alpha, and not
      // the washed-out salmon a #AARRGGBB misread produced (170, 68, 64).
      const auto near8 = [](int got, int want) { return std::abs(got - want) <= 2; };
      QVERIFY2(near8(mid.red(), 0) && near8(mid.green(), 170) && near8(mid.blue(), 68) &&
                   near8(mid.alpha(), 64),
               qPrintable("fill chip reads " + mid.name(QColor::HexArgb)));

      // REGRESSION: the bar's colours must be the browser's own hex, painted flat.
      // Encoding into Display P3 on macOS was a second conversion on an already
      // colour-managed surface and made the whole app read duller.
      win.resize(1900, 900);
      QTest::qWait(300);
      const QImage bar = win.selectedLineBar_->grab().toImage();
      auto* ds = win.selectedLineBar_->findChild<QWidget*>("selectedLineDeselect");
      QVERIFY(ds);
      const QColor got = bar.pixelColor(ds->mapTo(win.selectedLineBar_, QPoint(5, ds->height() / 2)));
      // Deselect wears the bar's own amber, the same token its siblings use — one
      // palette, no orange outlier (browser .deselect-btn -> var(--bg-sel-btn)). For the
      // theme the window is actually in: another case may have left the app in light.
      const stencil::gui::Palette live = stencil::gui::themePalette(
          stencil::gui::resolveDark(win.settings_.themeMode), win.settings_.accentColor);
      QCOMPARE(got.name(), live.bgSelBtn.name());
      QCOMPARE(stencil::gui::themePalette(true, "violet").danger.name(), QStringLiteral("#f0697a"));
    }

    // The two glyphs in this group are sized like the browser's: the clear-fill cross is
    // small (11px box, not the style's 16 scaling an 11px pixmap up), and Unchain carries
    // the same icon-plus-label pairing #sel-unchain has.
    {
      auto* clear = win.selectedLineBar_->findChild<QPushButton*>("selectedLineFillClear");
      QVERIFY2(clear, "no clear-fill button");

      // …and the same boxes the browser's controls have: a 23x19 cross, 28px-tall buttons
      // and 46x34 colour wells beside 34px fields. Clear-fill is a full-height control,
      // not a small cross sitting low in the row.
      QVERIFY2(clear->height() >= 26, qPrintable(QString("clear is %1px tall").arg(clear->height())));
      QCOMPARE(clear->iconSize(), QSize(13, 13));
      // ONE colour well everywhere: 46x24, the size the browser and extension now use too.
      auto* swatch2 = win.selectedLineBar_->findChild<QPushButton*>("selectedLineFillSwatch");
      QVERIFY(swatch2);
      QCOMPARE(swatch2->size(), QSize(46, 26));
      QVERIFY2(!swatch2->icon().isNull(),
               "the well should draw a colour CHIP inside its frame, like the toolbar's");
      // …in the theme's own input chrome, exactly as the toolbar's wells are. Read from
      // the widget's palette instead, the frame resolved to the LIGHT theme's #dddddd and
      // the wells sat in the dark bar ringed in near-white (user report).
      const stencil::gui::Palette chrome = stencil::gui::themePalette(
          stencil::gui::resolveDark(win.settings_.themeMode), win.settings_.accentColor);
      QVERIFY2(swatch2->styleSheet().contains(chrome.borderMain.name()),
               qPrintable("well frame reads: " + swatch2->styleSheet()));
      QVERIFY2(swatch2->styleSheet().contains(chrome.inputBg.name()),
               "the well should sit on the theme's input ground");
      // …and four hairlines part the bar, as the browser's do: header | colours |
      // geometry | fill | actions. The fill's own comes and goes WITH the group, or
      // unchaining leaves two side by side with nothing between. Measured while the group
      // is still there, at a width narrow enough that it costs a second row.
      win.resize(1100, 900);
      QTest::qWait(300);
      const int barHeightWithFill = win.selectedLineBar_->height();
      const auto visibleSeps = [&] {
        int n = 0;
        for (QFrame* f : win.selectedLineBar_->findChildren<QFrame*>("selectedLineSep"))
          if (f->isVisible()) ++n;
        return n;
      };
      QCOMPARE(visibleSeps(), 4);
      canvas->unchainSelectedLine();
      beat();
      QTRY_COMPARE(visibleSeps(), 3);
      QTRY_VERIFY2(!swatch2->isVisible(), "the fill group should be gone with it");
      // …and the bar SHRINKS with it. Losing the fill group can cost the flow layout a
      // whole row, and nothing re-asked for the height. refitHeight() runs on every
      // content change now.
      const int tallWithFill = barHeightWithFill;
      QTRY_VERIFY2(win.selectedLineBar_->height() < tallWithFill,
                   qPrintable(QString("bar stayed %1px tall after the fill group left (was %2)")
                                  .arg(win.selectedLineBar_->height()).arg(tallWithFill)));
      for (QComboBox* cb : win.selectedLineBar_->findChildren<QComboBox*>()) {
        QVERIFY2(cb->height() >= 32 && cb->height() <= 36,
                 qPrintable(QString("style combo is %1px tall, the browser's is 34").arg(cb->height())));
        break;
      }
      QVERIFY2(!clear->toolTip().isEmpty(), "the clear-fill button has no tooltip");
      QVERIFY2(!unchain->icon().isNull(), "Unchain has no icon — the browser's has one");
      QCOMPARE(unchain->iconSize(), QSize(13, 13));
      QVERIFY2(!unchain->text().isEmpty(), "…and it keeps its label beside it");
    }

    // A rect (locked, four corners, no closing duplicate) — the button appears…
    stencil::core::Line rect;
    rect.points = {{10, 10}, {90, 10}, {90, 70}, {10, 70}};
    rect.locked = true;
    canvas->setLines({rect});
    canvas->selectLineByIndex(0);
    beat();
    QTRY_VERIFY2(unchain->isVisible(), "an area was not offered Unchain");

    // …and pressing it opens the area, keeping every corner. click() rather than a
    // synthetic press at coordinates: the group is mid-slide when it first becomes visible
    // (controlReveal), so a positional click can land beside a still-growing button.
    unchain->click();
    beat();
    QVERIFY2(!canvas->lines()[0].locked, "the click did not unchain the area");
    QCOMPARE(canvas->lines()[0].points.size(), std::size_t(4));
    QTRY_VERIFY2(!unchain->isVisible(), "Unchain is still offered on a line that is now open");
  }

  // The "Selected Line:" bar appears/disappears through dustSelectedLineBarIn/Out
  // (mainWindow.cpp) rather than a plain instant show/hide. Like the other docked
  // surface flights, it declines under the offscreen QPA platform this suite runs
  // under, so this asserts the end state rather than a live flight.
  void selectedLineBarAppearsAndDisappearsWithDust() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QVERIFY2(!win.selectedLineDock_->isVisible(), "nothing selected yet");

    stencil::core::Line line;
    line.points = {{20, 20}, {80, 80}};
    canvas->setLines({line});

    // Select it: the bar comes up (and, off this platform, dust would gather into it).
    canvas->selectLineByIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(win.selectedLineDock_->isVisible(), 2000);
    beat();

    // selectedLineBarDustPoint() is plain geometry, so it runs fine offscreen even
    // though the flight it feeds does not.
    QVERIFY2(win.imageInfoBar_ && win.imageInfoBar_->isVisible(), "no image-info row to anchor to");
    const QRect barPicture(win.selectedLineBar_->mapTo(&win, QPoint(0, 0)),
                           win.selectedLineBar_->size());
    const QRect infoRectNow(win.imageInfoBar_->mapTo(&win, QPoint(0, 0)), win.imageInfoBar_->size());
    const int dockTop = win.selectedLineDock_->mapTo(&win, QPoint(0, 0)).y();

    // The x is the BAR's own centre — both bars span the full window width (each its own
    // Qt::TopDockWidgetArea dock), so this already IS the window's centre.
    QVERIFY2(std::abs(barPicture.center().x() - win.width() / 2) < 4,
             "the bar itself is not spanning the full window width — the premise of this test");
    const QPoint openPt = win.selectedLineBarDustPoint(barPicture, /*closing=*/false);
    QCOMPARE(openPt.x(), barPicture.center().x());
    QCOMPARE(openPt.y(), infoRectNow.bottom());   // reflow already ran — read it as-is

    // Closing predicts the row's post-close position (the dock's current top + the row's
    // height) rather than using its live, still-stale bottom — which would overshoot.
    const QPoint closePt = win.selectedLineBarDustPoint(barPicture, /*closing=*/true);
    QCOMPARE(closePt.x(), barPicture.center().x());
    QCOMPARE(closePt.y(), dockTop + infoRectNow.height());
    QVERIFY2(closePt.y() <= barPicture.top() + infoRectNow.height(),
             "the dock's own top must be at or above the bar's content-widget top");
    QVERIFY2(closePt.y() < infoRectNow.bottom(),
             "the closing point used the stale (pre-close) position instead of predicting it");

    // Reselecting a DIFFERENT line while the bar is already open must not disturb it —
    // it just repopulates in place (same as the browser's wasHidden gate).
    stencil::core::Line line2;
    line2.points = {{100, 20}, {160, 80}};
    canvas->setLines({line, line2});
    canvas->selectLineByIndex(1);
    QTest::qWait(150);
    QVERIFY2(win.selectedLineDock_->isVisible(), "the bar stays up across a re-selection");
    beat();

    // Deselect: the bar goes away (and, off this platform, dust would scatter out of it).
    canvas->deselect();
    QTRY_VERIFY_WITH_TIMEOUT(!win.selectedLineDock_->isVisible(), 2000);
    beat();

    // Re-selecting after a full hide brings it straight back — nothing latched stuck.
    canvas->selectLineByIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(win.selectedLineDock_->isVisible(), 2000);
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
    // The float's exit is a cloud of its own pixels flown inside the main window, every
    // mote pouring back into the icon.
    const auto flight = [&win] { return surfaceFlight(&win); };
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
    auto* from = flight();
    QVERIFY2(from, "floating: the X closed with no flight");
    QWidget* icon = win.buttonForAction(win.actChat_);
    QVERIFY(icon);
    QVERIFY2(!from->gathering(), "floating: the X must scatter the window INTO the icon");
    QCOMPARE(from->surfaceTarget(), flightPointOf(icon, &win));
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

    // The outgoing FLOAT's exit is a cloud of its own pixels flown inside the main
    // window: it SCATTERS, every mote pouring back into the icon.
    const auto flight = [&win] { return surfaceFlight(&win); };
    // Past the whole surface flight, so a cloud from the LAST swap can never be mistaken
    // for the next one's (the gather is the longer of the two clocks).
    const auto flushGhosts = [] {
      QTest::qWait(stencil::gui::DisintegrateOverlay::kSurfaceInMs + 300);
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
        auto* from = flight();
        QVERIFY2(from, qPrintable(QString("%1: the outgoing FLOAT did not fly out").arg(route)));
        QVERIFY2(!from->gathering(),
                 qPrintable(QString("%1: the outgoing float must come APART, not form").arg(route)));
        QCOMPARE(from->surfaceTarget(), flightPointOf(icon, &win));
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
      QVERIFY2(!flight(), "a re-pin that moves nothing must not animate");
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
    QTest::qWait(1800);   // past the clear-dust hold, so the card is painted
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
    QTest::qWait(1800);            // past the clear hold, so the card is laid out
    win.canvas_->grab();           // painting is what records the card's rect
    const QRect card = win.canvas_->idleCardGlobalRect();
    QVERIFY2(card.isValid(), "the blank-image card is not on screen");

    QPoint start(-1, -1);
    QTimer::singleShot(140, &win, [&] {
      start = surfaceFlightTarget(&win);
      if (QWidget* modal = QApplication::activeModalWidget()) modal->close();
    });
    // The CLOSE flight is captured separately: its motes must pour back into the CARD.
    // It used to ignore the anchor rect and shrink into the box above instead.
    QPoint closeEnd(-1, -1);
    bool closeScatters = false;
    QTimer::singleShot(300, &win, [&] {
      if (auto* fx = surfaceFlight(&win)) {
        closeEnd = fx->surfaceTarget();
        closeScatters = !fx->gathering();
      }
    });
    emit win.canvas_->blankImageRequested();
    QTest::qWait(500);
    const QPoint want = win.mapFromGlobal(card.center());
    QCOMPARE(start, want);
    QVERIFY2(closeScatters, "the close must come APART into the card, not form out of it");
    QVERIFY2(closeEnd == want, qPrintable(QString("the close pours into %1, the card is at %2")
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
      if (d->objectName() != QLatin1String("llmChatDock") &&
          d->objectName() != QLatin1String("selectedLineDock") &&
          d->objectName() != QLatin1String("imageInfoDock") && d->titleBarWidget())
        bar = d->titleBarWidget();
    QVERIFY2(bar, "no selection-panel title bar");
    // By NAME, not "every QToolButton in the header": the header also carries the
    // Points | Lines strip, and a QTabBar owns two internal scroll arrows that are
    // QToolButtons of its own sizing.
    const auto chevrons = bar->findChildren<QToolButton*>(QStringLiteral("panelCollapseBtn"));
    QVERIFY2(!chevrons.isEmpty(), "the panel header has no collapse chevron");
    for (QToolButton* b : chevrons) {
      QCOMPARE(b->focusPolicy(), Qt::NoFocus);
      QCOMPARE(b->size(), QSize(kPanelChevronBox, kPanelChevronBox));
    }
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
    // Watches for the styled in-dialog open-confirm (modalChrome confirmModal —
    // it sits OVER the still-open projects dialog now) and answers it. Matched by
    // objectName: the projects dialog itself is a modal QDialog too.
    struct BoxWatch {
      bool seen = false;
      bool accept = true;
    };
    auto startWatch = [](BoxWatch* w) {
      auto* t = new QTimer;
      t->setInterval(5);
      QObject::connect(t, &QTimer::timeout, t, [w] {
        QWidget* m = QApplication::activeModalWidget();
        if (!m || m->objectName() != QLatin1String("stencilConfirmModal")) return;
        w->seen = true;
        const QLatin1String want = w->accept ? QLatin1String("Open") : QLatin1String("Cancel");
        for (QPushButton* b : m->findChildren<QPushButton*>())
          if (b->text() == want) { b->click(); return; }
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
      dismissModal("OK");   // the in-dialog styled confirm
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

  // The Start/Stop and Line/Rect faces read as WORDS: a size up from the toolbar's dense
  // default, with the glyph+label pair CENTRED in the button. Qt anchors a text-beside-icon
  // label at the left of the content rect and keeps its own slack on the right, so equal
  // padding drew the pair off-centre in its box (user report, with a picture) — the theme
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

  // The ✎/🎨 are hover-revealed over the name group, and a pointer that lands anywhere else
  // has left it — even when the group's own Leave never arrives (crossing straight onto
  // another row's icon left the pair lit three clusters away: user report, with a picture).
  void nameAffordancesGoWhenThePointerLeavesTheGroup() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1400, 860);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.addImageProjectEntry(img, "hover-out");
    QVERIFY(!id.isEmpty());
    QVERIFY(win.loadProjectIntoCanvas(id, false));
    QTest::qWait(300);
    const auto paintedOut = [](QWidget* w) { return w->property("stencilPaintedOut").toBool(); };

    QCursor::setPos(win.nameGroup_->mapToGlobal(win.nameGroup_->rect().center()));
    win.updateNameHover();
    QTRY_VERIFY(!paintedOut(win.projectNameEdit_));
    QVERIFY(!paintedOut(win.projectColorBtn_));

    // Onto another control, and the pair goes — driven by the same recompute the app runs
    // when a pointer enters anything else (here: called directly, as the poll would).
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 60, 200)));
    win.updateNameHover();
    QTRY_VERIFY_WITH_TIMEOUT(paintedOut(win.projectNameEdit_), 2000);
    QVERIFY(paintedOut(win.projectColorBtn_));
    // …and they keep their slots either way: painting out must never move the row.
    QVERIFY(win.projectNameEdit_->isVisible() && win.projectColorBtn_->isVisible());
    beat();
  }

  // Edit mode SWAPS the name affordances in place: ✎/🎨 out, ✓/✗ in, and back again. The
  // pair returning while the marks were still flying out put all four in the row at once —
  // it widened, and the ✎/🎨 appeared BESIDE the leaving marks instead of in their place
  // (user report, with a picture). Never more than two hold a slot at any moment.
  void nameChipsSwapInPlaceWithoutWideningTheRow() {
    const auto motion = withMotion();   // the flights below ARE the thing under test
    MainWindow win(nullptr, false);
    win.resize(1400, 860);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.addImageProjectEntry(img, "swap-row");
    QVERIFY(!id.isEmpty());
    QVERIFY(win.loadProjectIntoCanvas(id, false));
    QTest::qWait(300);
    win.nameHover_ = true;   // ✎/🎨 are hover-revealed; pin them on for the swap
    const auto held = [&win] {
      int n = 0;
      for (QToolButton* b : { win.projectNameEdit_, win.projectColorBtn_,
                              win.projectNameAccept_, win.projectNameCancel_ })
        if (b && b->isVisible()) ++n;
      return n;
    };

    win.enterNameEdit();
    for (int i = 0; i < 10; ++i) {   // through the whole in-flight
      QTest::qWait(50);
      QVERIFY2(held() <= 2, qPrintable(QString("entering: %1 chips held a slot").arg(held())));
    }
    QVERIFY(win.projectNameAccept_->isVisible() && win.projectNameCancel_->isVisible());

    win.cancelProjectName();
    for (int i = 0; i < 10; ++i) {   // …and the whole way back
      QTest::qWait(50);
      QVERIFY2(held() <= 2, qPrintable(QString("leaving: %1 chips held a slot").arg(held())));
    }
    QTRY_VERIFY(win.projectNameEdit_->isVisible() && win.projectColorBtn_->isVisible());
    QVERIFY(!win.projectNameAccept_->isVisible() && !win.projectNameCancel_->isVisible());
    beat();
  }

  // The project name at the top is a TITLE at rest — no box — that RINGS in the accent
  // under the pointer, as the browser's read-only #project-name-input does. The ring has
  // to live in that field's OWN stylesheet (applyProjectNameStyle): a per-widget sheet
  // outranks the themed one for every property it names, so the rule in theme.cpp was
  // simply ignored and the title stayed inert (user report, three times over). Watched in
  // PIXELS for that reason — a stylesheet that exists is not a ring that paints.
  void projectNameTitleRingsOnHoverOnly() {
    MainWindow win(nullptr, false);
    win.resize(1400, 860);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.addImageProjectEntry(img, "name-row");
    QVERIFY(!id.isEmpty());
    QVERIFY(win.loadProjectIntoCanvas(id, false));
    QTest::qWait(300);
    QVERIFY(win.projectName_ && win.projectNameEdit_ && win.projectColorBtn_);

    // The chips: the browser's box, and its 4px gaps either side.
    QCOMPARE(win.projectNameEdit_->size(), QSize(stencil::gui::kNameChipBox,
                                                 stencil::gui::kNameChipBox));
    QCOMPARE(win.projectColorBtn_->size(), win.projectNameEdit_->size());
    const QRect f(win.projectName_->mapTo(&win, QPoint(0, 0)), win.projectName_->size());
    const QRect e(win.projectNameEdit_->mapTo(&win, QPoint(0, 0)), win.projectNameEdit_->size());
    const QRect c(win.projectColorBtn_->mapTo(&win, QPoint(0, 0)), win.projectColorBtn_->size());
    // 8px of air either side — at 4 the chips sat right against the field's edge (user
    // report, with a picture). Browser twin: .project-name-field's `gap`.
    QCOMPARE(e.left() - f.right() - 1, 8);
    QCOMPARE(c.left() - e.right() - 1, 8);

    // …and the ring itself, top edge of the field: nothing at rest, the accent on hover.
    const auto edge = [&] {
      const QImage im = win.grab(f).toImage();
      return im.pixelColor(im.width() / 2, 1);
    };
    const QColor accent = stencil::gui::accentPrimary(win.settings_.accentColor);
    const QColor rest = edge();
    // The ring is the accent at the shared 45% (the browser's two stacked layers come to
    // the same on screen), so the edge lands between the ground and the accent — never the
    // flat accent, which read far brighter than the browser's (user report).
    const auto near = [](const QColor& a, const QColor& b, int tol) {
      return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue()) < tol;
    };
    const QColor blend(qRound(0.45 * accent.red() + 0.55 * rest.red()),
                       qRound(0.45 * accent.green() + 0.55 * rest.green()),
                       qRound(0.45 * accent.blue() + 0.55 * rest.blue()));
    QVERIFY2(!near(rest, accent, 60), "the title wears the ring at rest");
    win.projectName_->setAttribute(Qt::WA_UnderMouse, true);
    QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), win.projectName_->mapToGlobal(QPointF(5, 5)));
    QApplication::sendEvent(win.projectName_, &enter);
    win.projectName_->update();
    QTest::qWait(150);
    const QColor hovered = edge();
    QVERIFY2(!near(hovered, rest, 24), "no ring appeared under the pointer");
    QVERIFY2(near(hovered, blend, 40),
             qPrintable(QString("the ring is not the shared 45%% accent: %1 (wanted ~%2)")
                            .arg(hovered.name(), blend.name())));
    win.projectName_->setAttribute(Qt::WA_UnderMouse, false);
    beat();
  }

  // Fullscreen pulls every toolbar out from under whatever they had in the air, and takes
  // the logo's own overlay with it: a cloud started by a toolbar control was left flying
  // over the bare canvas, and the logo's resting mark sat on over the label that took its
  // place (user report, with pictures of both).
  void fullscreenLeavesNothingBehindIt() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("fullscreen gestures need the offscreen platform");
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    QTest::qWait(200);
    QVERIFY(win.logoBtn_);
    QWidget* fx = win.logoFx_;
    QVERIFY(fx);
    // The mark is up (the overlay paints the logo, blanked on the button itself).
    QTRY_VERIFY_WITH_TIMEOUT(fx->isVisible(), 2000);

    // Something is in the air when the switch happens — a control's own cloud.
    stencil::gui::DisintegrateOverlay::over(win.logoBtn_, &win,
                                           stencil::gui::DisintegrateOverlay::Sweep::Fall);
    const auto cloudsUp = [&win] {
      int n = 0;
      for (const char* name : {stencil::gui::DisintegrateOverlay::kObjectName,
                               "stencilControlReveal", "stencilFilterDust"})
        for (QWidget* w : win.findChildren<QWidget*>(QString::fromLatin1(name)))
          if (w->isVisible()) ++n;
      return n;
    };
    QVERIFY2(cloudsUp() > 0, "the test's own cloud never started");

    win.toggleFullscreen();
    QTest::qWait(120);
    QVERIFY2(cloudsUp() == 0, "a cloud was left flying over the fullscreen canvas");
    QVERIFY2(!win.logoBtn_->isVisible(), "fullscreen kept the header row");
    QVERIFY2(!fx->isVisible(), "the logo's mark stayed up with its button gone");

    win.toggleFullscreen();   // …and back, with the header row and its mark restored
    QTest::qWait(200);
    QTRY_VERIFY_WITH_TIMEOUT(win.logoBtn_->isVisible(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(fx->isVisible(), 2000);
    beat();
  }

  // A select popup's rows hover like every other item in the app: the glass sweep, and the
  // 2px ease right the browser's `.accent-dd-opt:hover { transform: translateX(2px) }`
  // plays. The desktop's popups had NEITHER — a page-size row lit up and that was all
  // (user report). The slide WRAPS whatever delegate the popup already has, so a list with
  // its own painter (the motion modes' animated glyphs) keeps it.
  void selectPopupRowsSweepAndSlideOnHover() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("popup gestures need the offscreen platform");
    const auto motion = withMotion();   // the slide below IS the thing under test
    MainWindow win(nullptr, false);
    win.resize(1500, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    QTest::qWait(200);
    stencil::gui::SearchComboBox* combo = nullptr;
    for (QComboBox* c : win.findChildren<QComboBox*>())
      if (c->isVisible() && c->count() > 2) {
        combo = dynamic_cast<stencil::gui::SearchComboBox*>(c);
        if (combo) break;
      }
    QVERIFY2(combo, "no themed select on the toolbar");
    combo->showPopup();
    QTest::qWait(250);
    QListView* list = combo->popupList();
    QVERIFY(list);
    QCOMPARE(list->itemDelegate()->objectName(), QStringLiteral("stencilSlidingRows"));
    // The sweep lives on the viewport, as the projects list's does.
    QVERIFY2(!list->viewport()->findChildren<QWidget*>().isEmpty(),
             "no shimmer overlay over the popup's rows");

    const QRect row = list->visualRect(list->model()->index(1, 0));
    const auto pointAt = [&](const QPoint& at) {
      QMouseEvent mv(QEvent::MouseMove, QPointF(at),
                     list->viewport()->mapToGlobal(QPointF(at)),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(list->viewport(), &mv);
    };
    QCOMPARE(list->property("rowSlidePx").toInt(), 0);   // nothing hovered yet
    pointAt(row.center());
    QTRY_COMPARE_WITH_TIMEOUT(list->property("rowSlidePx").toInt(), 2, 1500);

    // …and it settles back the moment the pointer is off the rows.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(list->viewport(), &leave);
    QTRY_COMPARE_WITH_TIMEOUT(list->property("rowSlidePx").toInt(), 0, 1500);
    combo->hidePopup();
    beat();
  }

  // Nothing open here, so the pinned "Temporary (unsaved)" row is listed above the saved
  // projects — and the batch bar (it hosts Select all) is up because there are rows to
  // select. Removing every project takes both away, and the row underneath must GLIDE up
  // into the space, not be dropped into it: the strip used to lose its height the frame
  // its last control was hidden, and the layout's own spacing went in one more frame
  // after that (user report — "it should smoothly move"). Pins the whole close as a
  // continuous slide: no single frame moves the row more than a few pixels.
  void closingTheBatchBarGlidesTheRowsUp() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    const auto motion = withMotion();   // the slide below IS the thing under test
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    QVERIFY(!win.addImageProjectEntry(img, "one").isEmpty());
    QVERIFY(!win.addImageProjectEntry(img, "two").isEmpty());

    bool sawPinned = false, barWasUp = false;
    int biggestStep = 0, travelled = 0;
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
      QWidget* selectAll = dlg->findChild<QPushButton*>("projectsSelectAll");
      QWidget* bar = selectAll ? selectAll->parentWidget() : nullptr;
      QPushButton* clearBtn = nullptr;
      for (QPushButton* b : dlg->findChildren<QPushButton*>())
        if (b->text().startsWith("Clear All")) clearBtn = b;
      if (!bar || !clearBtn) { bailOut(); return; }
      // The pinned row's top edge, in screen coordinates — what the eye follows.
      const auto pinnedTop = [&] {
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->data(Qt::UserRole + 11).toBool())
            return list->viewport()->mapToGlobal(list->visualItemRect(list->item(i)).topLeft()).y();
        return -1;
      };
      sawPinned = pinnedTop() >= 0;
      barWasUp = bar->isVisible() && bar->height() > 0;
      if (!sawPinned || !barWasUp) { bailOut(); return; }
      const int from = pinnedTop();
      dismissModal("OK");
      clearBtn->click();
      int last = from;
      for (int i = 0; i < 70 && bar->isVisible(); ++i) {
        QTest::qWait(16);
        const int now = pinnedTop();
        if (now < 0) continue;   // mid-rebuild
        biggestStep = std::max(biggestStep, std::abs(now - last));
        last = now;
      }
      travelled = from - last;
      bailOut();
    });
    win.openProjects();
    QVERIFY2(sawPinned, "the pinned row was not listed with the saved projects");
    QVERIFY2(barWasUp, "the batch bar was not up over the selectable rows");
    QVERIFY2(travelled > 20, QString("the rows never moved up (%1px)").arg(travelled).toLatin1());
    QVERIFY2(biggestStep <= 20,
             QString("the bar's close dropped the rows %1px in one frame — not a glide")
                 .arg(biggestStep).toLatin1());
    beat();
  }

  // Removing the OPEN project empties the list — and what stands there then is the pinned
  // "Temporary (unsaved)" row, never "No projects yet": the removal reset this window to a
  // blank unsaved editor (eraseLocalProject → resetToBlankEditor), exactly the state the
  // browser's list pins that row for. The window was asked once, at open time, so the row
  // never came and the emptied list read "No projects yet" (user report). Driven through
  // Clear All, the removal path that keeps the dialog up.
  void removingTheOpenProjectPinsTheTemporaryRow() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    const auto motion = withMotion();   // the arrival below IS the thing under test
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkMagenta);
    const QString id = win.addImageProjectEntry(img, "the-only-one");
    QVERIFY(!id.isEmpty());
    QVERIFY(win.loadProjectIntoCanvas(id, false));   // …and it is this window's OPEN project

    bool sawRow = false, tempPinned = false, noPlaceholder = true, landedWhereItArrived = false;
    bool arrivedVeiled = false, cloudInFlight = false, landedWhole = false;
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
      for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id) sawRow = true;
      if (!sawRow) { bailOut(); return; }

      QPushButton* clearBtn = nullptr;
      for (QPushButton* b : dlg->findChildren<QPushButton*>())
        if (b->text().startsWith("Clear All")) clearBtn = b;
      if (!clearBtn) { bailOut(); return; }
      dismissModal("OK");   // the in-dialog styled confirm (see the note above)
      clearBtn->click();

      // Once the dust has landed the list holds the pinned row and nothing else.
      const auto pinned = [&] {
        return list->count() == 1 && list->item(0)->data(Qt::UserRole + 11).toBool();
      };
      for (int i = 0; i < 300 && !pinned(); ++i) QTest::qWait(10);
      tempPinned = pinned() && list->item(0)->text() == QStringLiteral("Temporary (unsaved)");
      for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->text() == QStringLiteral("No projects yet")) noPlaceholder = false;
      // …and it ARRIVES: veiled behind its own motes (the sand IS the row forming) with
      // the filter's light cloud in flight, never the removal's scatter. The rebuild that
      // answers a removal finds the list EMPTY — the doomed row left the view when its
      // scatter ended — and reading that as the dialog's opening build skipped the
      // arrival outright: the row simply appeared (user report).
      arrivedVeiled = list->item(0)->data(Qt::UserRole + 43).toDouble() == 0.0;
      cloudInFlight = !dlg->findChildren<QWidget*>("stencilFilterDust").isEmpty();
      for (int i = 0; i < 200 && list->item(0)->data(Qt::UserRole + 43).toDouble() < 1.0; ++i)
        QTest::qWait(10);
      landedWhole = list->item(0)->data(Qt::UserRole + 43).toDouble() >= 1.0;

      // …and it arrives WHERE IT BELONGS. The list's own top moves with the batch bar
      // above it, and answering the removal in two repaints showed that bar again for the
      // stale row: the pinned row appeared a bar's height too low and jumped up a beat
      // later (user report). Its screen position at arrival must be its final one.
      if (tempPinned) {
        const auto rowTop = [&] {
          return list->viewport()->mapToGlobal(list->visualItemRect(list->item(0)).topLeft()).y();
        };
        const int atArrival = rowTop();
        for (int i = 0; i < 60; ++i) QTest::qWait(10);   // past the bar's out-flight
        landedWhereItArrived = rowTop() == atArrival;
      }
      bailOut();
    });
    win.openProjects();
    QVERIFY2(sawRow, "the seeded project row never appeared in the dialog");
    QVERIFY2(tempPinned, "the emptied list never pinned the window's \"Temporary (unsaved)\" row");
    QVERIFY2(noPlaceholder, "the emptied list still read \"No projects yet\"");
    QVERIFY2(arrivedVeiled, "the pinned row was simply there — not veiled behind its own motes");
    QVERIFY2(cloudInFlight, "no arrival cloud played for the row the removal revealed");
    QVERIFY2(landedWhole, "the arriving row never came out from behind its veil");
    QVERIFY2(landedWhereItArrived, "the pinned row appeared off its final place and jumped");
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
      // Let the OPEN flight (dust or ghost) land and delete itself, so the one found
      // below is unambiguously the close flight's. Bounded wait for a loaded machine.
      QTest::qWait(50);
      const auto openFlightLive = [&] { return surfaceFlight(&win) || modalGhost(&win); };
      for (int i = 0; i < 250 && openFlightLive(); ++i) QTest::qWait(10);
      if (openFlightLive()) { bailOut(); return; }
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
      dismissModal("OK");
      clearBtn->click();     // rows doomed, scatter playing
      closeBtn->click();     // …and the dialog closed IMMEDIATELY, mid-scatter

      // Finalized on done(): the doomed row left the list at once, no kMs wait.
      finalized = true;
      for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id) finalized = false;

      // The close flight's SNAPSHOT must show the slot as bare background — the stale
      // open-time picture (or a barely-started scatter) would still paint the row.
      // Checked either way (dust or ghost), same as the reveal tests.
      QPixmap shot;
      if (auto* fx = surfaceFlight(&win)) { ghostSeen = true; shot = fx->snapshot(); }
      else if (auto* g = modalGhost(&win)) { ghostSeen = true; shot = g->pixmap(); }
      if (!shot.isNull()) {
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

      // 1. Answer Cancel: the row, the project, and the dialog all stay.
      removeViaMenu(item);
      dismissModal("Cancel");        // the deferred in-dialog styled confirm
      QTest::qWait(400);
      openAfterNo = dlg->isVisible();
      keptAfterNo = rowFor(idA) != nullptr && hasProject(idA);

      // 2. Same remove, answer Confirm: the project goes, the dialog stays open.
      removeViaMenu(rowFor(idA));
      dismissModal("OK");
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
  // zoomed-out image belongs to the scroll area's viewport — which had no menu at
  // all. With NO image there is no menu anywhere: every entry acts on an image
  // (browser contextMenu.js parity).
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
        // The "Current" copy-image variant action — syncContextActions has just
        // run for this popup. actCopyImage_ is a fixed pointer (not text-matched):
        // its own text no longer starts with "Copy Image" now that it is nested
        // under a "Copy Image ▸" submenu parent (which is a DIFFERENT, always-
        // enabled QAction — the submenu opener, not the image-dependent copy itself).
        *copyFound = win.actCopyImage_ != nullptr;
        *copyEnabled = win.actCopyImage_ && win.actCopyImage_->isEnabled();
        menu->close();
      });
      const QPoint corner(6, 6);
      QTest::mouseClick(viewport, Qt::RightButton, {}, corner);
      QTest::qWait(50);
    };

    // ── no image at all: NO menu — a popup of dead rows is worse than none. Clicked
    // directly (not through rightClickCorner): its poll would spin for two seconds
    // waiting for a menu that never comes, and still be running for the next case.
    QVERIFY(!win.findChild<CanvasWidget*>()->hasImage());
    QTest::mouseClick(viewport, Qt::RightButton, {}, QPoint(6, 6));
    QTest::qWait(50);
    QVERIFY2(!QApplication::activePopupWidget(), "the context menu opened with no image");
    // …and the keyboard route (Shift+F10) goes through the same gate.
    win.showContextMenuFromKeyboard();
    QTest::qWait(30);
    QVERIFY2(!QApplication::activePopupWidget(), "Shift+F10 opened a menu with no image");

    // ── with an image loaded, clicking OUTSIDE it (the backdrop) ──
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    bool openedOutside = false, copyEnabledOutside = false, copyFoundOutside = false;
    rightClickCorner(&openedOutside, &copyEnabledOutside, &copyFoundOutside);
    QVERIFY2(openedOutside, "no context menu on the backdrop around the image");
    QVERIFY2(copyEnabledOutside, "image actions stayed disabled with an image loaded");
    beat();
  }

  // The copy/download-image toolbar buttons open a small variant-options popup on
  // right-click instead of re-running the plain action (browser parity:
  // js/ui/exportOptionsMenu.js) — verifies wireExportOptionsPopups(). The copy button's
  // plain click is also exercised (safe: no blocking dialog, unlike Save's file picker).
  void toolbarImageButtonsOpenExportOptionsOnRightClick() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    QWidget* saveBtn = win.buttonForAction(win.actSaveImage_);
    QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
    QVERIFY(saveBtn);
    QVERIFY(copyBtn);
    QVERIFY(win.saveImageOptionsMenu_);
    QVERIFY(win.copyImageOptionsMenu_);
    QVERIFY(win.saveImageOptionsMenu_->actions().contains(win.actSaveImageCurrentRow_));
    QVERIFY(win.saveImageOptionsMenu_->actions().contains(win.actSaveImageOriginal_));
    QVERIFY(win.saveImageOptionsMenu_->actions().contains(win.actSaveImageTint_));
    QVERIFY(win.copyImageOptionsMenu_->actions().contains(win.actCopyImageCurrentRow_));
    QVERIFY(win.copyImageOptionsMenu_->actions().contains(win.actCopyImageOriginal_));
    QVERIFY(win.copyImageOptionsMenu_->actions().contains(win.actCopyImageTint_));

    // Right-click the Download button: the popup opens, the plain action does NOT fire
    // (a real download would pop a blocking file dialog — this must never happen here).
    int saveTriggers = 0;
    connect(win.actSaveImage_, &QAction::triggered, &win, [&] { ++saveTriggers; });
    QContextMenuEvent saveCtx(QContextMenuEvent::Mouse, saveBtn->rect().center(),
                              saveBtn->mapToGlobal(saveBtn->rect().center()));
    QApplication::sendEvent(saveBtn, &saveCtx);
    QVERIFY2(QApplication::activePopupWidget() == win.saveImageOptionsMenu_,
             "right-click on the download-image button opened no popup, or the wrong one");
    QCOMPARE(saveTriggers, 0);
    win.saveImageOptionsMenu_->close();

    // Same gesture on the Copy button.
    QContextMenuEvent copyCtx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                              copyBtn->mapToGlobal(copyBtn->rect().center()));
    QApplication::sendEvent(copyBtn, &copyCtx);
    QVERIFY2(QApplication::activePopupWidget() == win.copyImageOptionsMenu_,
             "right-click on the copy-image button opened no popup, or the wrong one");
    win.copyImageOptionsMenu_->close();

    // A plain single click on Copy still runs the default ("current") variant — deferred
    // briefly (so a following dblclick could still cancel it, though none comes here).
    int copyTriggers = 0;
    connect(win.actCopyImage_, &QAction::triggered, &win, [&] { ++copyTriggers; });
    QTest::mouseClick(copyBtn, Qt::LeftButton);
    QTRY_COMPARE(copyTriggers, 1);
    beat();
  }

  // REGRESSION: holding Alt over an export-variant row (to peek its live preview,
  // exportPreview.cpp) used to close the menu instantly instead of showing the
  // preview. The preview's own dust flight span an ESCAPING top-level window while
  // the menu still held the platform pointer/keyboard grab, which killed that grab
  // (menuReveal.cpp's dustMenuIn/dustMenuOut hit and solved the identical problem
  // for a menu's own reveal/dismiss dust — exportPreview.cpp now follows suit).
  // The escaping window only exists on a REAL platform (disintegrateOverlay.hpp
  // skips it under offscreen, where the gui suite normally runs), so this only
  // actually exercises the bug outside of `QT_QPA_PLATFORM=offscreen`; it still
  // documents and checks the expected behavior either way.
  void altHoldOverExportRowDoesNotCloseTheMenu() {
    // The offscreen QPA plugin doesn't honor Qt::ToolTip's real-platform contract
    // of coexisting with an open popup's grab — showing exportPreview.cpp's own
    // preview tooltip closes the menu there regardless of this fix, which is about
    // a REAL platform (verified: reverting it reproduces the exact same failure
    // for real, but this offscreen quirk persists even with the fix in place and
    // even with the tooltip's own dust flight removed entirely). Nothing to check
    // here without a real windowing platform.
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
      QSKIP("Alt-hover's preview tooltip needs a real platform's popup-grab handling");
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // Away from every icon before touching Alt at all — see the sibling test below
    // for why a stray popoverButtons_ match here would be a real, hang-the-suite bug.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    // "Current"'s own row (actCopyImageCurrentRow_) only shows once something is
    // drawn — see currentRowHiddenWithNoLinesButToolbarButtonStays.
    {
      stencil::core::Line line;
      line.points.push_back({4.0, 20.0});
      line.points.push_back({36.0, 20.0});
      win.canvas_->setLines({line});
      win.refreshActions();
    }

    QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
    QVERIFY(copyBtn);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                          copyBtn->mapToGlobal(copyBtn->rect().center()));
    QApplication::sendEvent(copyBtn, &ctx);
    QMenu* menu = win.copyImageOptionsMenu_;
    QVERIFY2(menu && menu->isVisible(), "the copy-image options popup never opened");

    // Hover the first row (QMenu::hovered is what wireExportPreviewHover listens
    // on) so AltPreviewFilter has an activeAction() to render a preview for. A
    // synthetic mouseMove doesn't reliably drive QMenu's own hover tracking on a
    // real platform popup, so set it directly — exactly what QMenu does internally
    // on a real hover.
    QAction* row = win.actCopyImageCurrentRow_;
    menu->setActiveAction(row);
    QCOMPARE(menu->activeAction(), row);

    QTest::keyPress(menu, Qt::Key_Alt);
    QVERIFY2(menu->isVisible(), "holding Alt over an export row closed the menu");
    QTest::keyRelease(menu, Qt::Key_Alt);
    QVERIFY2(menu->isVisible(), "releasing Alt closed the menu");
    menu->close();
    // Let the preview's dust-out flight (kDustOutMs, exportPreview.cpp) actually
    // finish and its DisintegrateOverlay (parented to this popup) get cleaned up
    // before `win` — and the popup with it — is destroyed underneath it.
    QTest::qWait(260);
  }

  // REGRESSION: same bug class as altHoldOverExportRowDoesNotCloseTheMenu, but through
  // the CANVAS CONTEXT MENU's doubly-nested Copy Image submenu (Image/Layout ▸ Copy
  // Image ▸ Current/Original/Tint) rather than the toolbar's single-level options
  // popup. A submenu opened by hovering never grabs its own keyboard — the ROOT of the
  // chain keeps holding it — so a raw Alt keypress can land there instead of on the
  // leaf; AltPreviewFilter used to watch only the leaf and threw such a keypress away.
  // Sent to `root` here, not the leaf, to exercise exactly that routing (mainWindowActions.cpp).
  void altHoldOverNestedCtxMenuRowDoesNotCloseTheMenu() {
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
      QSKIP("Alt-hover's preview tooltip needs a real platform's popup-grab handling");
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    bool reached = false, survived = false, previewShown = false;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      QAction* layoutAct = nullptr;
      for (QAction* a : root->actions()) if (a->text() == "Image / Layout") layoutAct = a;
      if (!layoutAct || !layoutAct->menu()) { root->close(); return; }
      root->setActiveAction(layoutAct);
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* layoutMenu = layoutAct->menu();
      for (int i = 0; i < 100 && !layoutMenu->isVisible(); ++i) QTest::qWait(10);
      QAction* copyAct = nullptr;
      for (QAction* a : layoutMenu->actions()) if (a->text().startsWith("Copy Image")) copyAct = a;
      if (!copyAct || !copyAct->menu()) { root->close(); return; }
      layoutMenu->setActiveAction(copyAct);
      QTest::keyClick(layoutMenu, Qt::Key_Right);
      QMenu* copyMenu = copyAct->menu();
      for (int i = 0; i < 100 && !copyMenu->isVisible(); ++i) QTest::qWait(10);
      if (!copyMenu->isVisible()) { root->close(); return; }

      // actCopyImageOriginal_, not actCopyImage_ itself: the latter is no longer a row
      // in this submenu at all (actCopyImageCurrentRow_ is — hidden with nothing
      // drawn), while Original is always there — this test's own point is Alt-key
      // ROUTING, not which specific row it lands on.
      copyMenu->setActiveAction(win.actCopyImageOriginal_);
      reached = true;
      // The real bug: a bare Alt landing on the ROOT of the chain (not the leaf) —
      // with only the leaf watched, AltPreviewFilter threw this away entirely and the
      // preview never fired. Checked directly (not just "did the menu survive" — a
      // filter that does nothing at all would trivially pass that half too).
      QTest::keyPress(root, Qt::Key_Alt);
      previewShown = false;
      for (QWidget* w : QApplication::topLevelWidgets())
        if (w->objectName() == QLatin1String("exportPreviewTip") && w->isVisible()) previewShown = true;
      survived = copyMenu->isVisible() && layoutMenu->isVisible() && root->isVisible();
      QTest::keyRelease(root, Qt::Key_Alt);
      root->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(500, 400)));
    QVERIFY2(reached, "never reached the nested Copy Image submenu");
    QVERIFY2(previewShown, "Alt delivered to the chain's ROOT never reached the leaf's preview at all");
    QVERIFY2(survived, "holding Alt (delivered to the chain's ROOT) over a nested export row closed the menu");
    QTest::qWait(260);
  }

  // REGRESSION: still closed the menu even after the fix above, because a DIFFERENT
  // mechanism was doing it. The nested Copy/Download Image flyout paints right OVER
  // the toolbar it grew from, so the REAL cursor sits, in plain screen coordinates, on
  // top of whatever toolbar button happens to be underneath — and mainWindowEvents.cpp's
  // qApp-wide Alt-KeyPress filter (altPeekExportMenu_/popoverButtons_, entirely
  // unrelated to exportPreview's own row preview) read that as "Alt held over an icon"
  // and popped ITS OWN popover open on top, stealing the platform grab the context menu
  // chain depended on. Fixed by skipping that whole block outright whenever a QMenu
  // popup is already active.
  void altHoldOverNestedRowAboveAToolbarButtonDoesNotHijackTheMenu() {
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
      QSKIP("needs a real platform's popup-grab handling");
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
    QVERIFY(copyBtn);

    bool reached = false, survived = false, hijacked = false;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      QAction* layoutAct = nullptr;
      for (QAction* a : root->actions()) if (a->text() == "Image / Layout") layoutAct = a;
      if (!layoutAct || !layoutAct->menu()) { root->close(); return; }
      root->setActiveAction(layoutAct);
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* layoutMenu = layoutAct->menu();
      for (int i = 0; i < 100 && !layoutMenu->isVisible(); ++i) QTest::qWait(10);
      QAction* copyAct = nullptr;
      for (QAction* a : layoutMenu->actions()) if (a->text().startsWith("Copy Image")) copyAct = a;
      if (!copyAct || !copyAct->menu()) { root->close(); return; }
      layoutMenu->setActiveAction(copyAct);
      QTest::keyClick(layoutMenu, Qt::Key_Right);
      QMenu* copyMenu = copyAct->menu();
      for (int i = 0; i < 100 && !copyMenu->isVisible(); ++i) QTest::qWait(10);
      if (!copyMenu->isVisible()) { root->close(); return; }
      // actCopyImageOriginal_, not actCopyImage_ itself: the latter is no longer a row
      // in this submenu at all (actCopyImageCurrentRow_ is — hidden with nothing
      // drawn), while Original is always there — this test's own point is Alt-key
      // ROUTING, not which specific row it lands on.
      copyMenu->setActiveAction(win.actCopyImageOriginal_);
      reached = true;

      // The exact repro: the cursor sits over the toolbar's own Copy button — right
      // where the flyout is actually painted on screen — while Alt is pressed.
      // underMouse() backs up the cursor-position check in mainWindowEvents.cpp (same
      // answer for a real resting pointer); it's also what an offscreen-adjacent test
      // can reliably mock — a real QCursor::setPos warp is not guaranteed to land in time.
      copyBtn->setAttribute(Qt::WA_UnderMouse, true);
      QTest::keyPress(root, Qt::Key_Alt);
      QTest::qWait(30);
      hijacked = win.copyImageOptionsMenu_ && win.copyImageOptionsMenu_->isVisible();
      survived = copyMenu->isVisible() && layoutMenu->isVisible() && root->isVisible();
      QTest::keyRelease(root, Qt::Key_Alt);
      copyBtn->setAttribute(Qt::WA_UnderMouse, false);
      root->close();
      if (win.copyImageOptionsMenu_) win.copyImageOptionsMenu_->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(500, 400)));
    QVERIFY2(reached, "never reached the nested Copy Image submenu");
    QVERIFY2(!hijacked, "Alt over the row opened the toolbar button's OWN options popup on top");
    QVERIFY2(survived, "holding Alt with the cursor over a toolbar button closed the context menu chain");
    QTest::qWait(260);
  }

  // Alt+hover over the copy/download-image toolbar buttons themselves opens their
  // export-options popup, the SAME hold-to-peek gesture every other popover icon
  // gets (mainWindowEvents.cpp's altPeekExportMenu_) — not just right-click/dblclick.
  // Releasing Alt closes it again unless the cursor moved inside it first (engaged).
  void altHoldOverExportButtonOpensItsOptionsPopup() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // QCursor::pos() is one process-wide value that outlives any one test/window —
    // a stray Alt keypress otherwise risks landing on WHATEVER popover button a
    // PRIOR test last left the (fake, offscreen) cursor sitting over, opening a
    // modal dialog that then blocks forever in execMaybePopover's QEventLoop::exec()
    // with nothing left to close it (regression: hung the whole suite, 300s
    // watchdog abort). Away from every icon before this test touches Alt at all.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
    QVERIFY(copyBtn);
    QMenu* menu = win.copyImageOptionsMenu_;
    QVERIFY(menu && !menu->isVisible());

    // underMouse() backs up the real cursor-position check (mainWindowEvents.cpp) —
    // same state a real resting pointer leaves, and what an offscreen test can mock.
    copyBtn->setAttribute(Qt::WA_UnderMouse, true);
    QTest::keyPress(&win, Qt::Key_Alt);
    QVERIFY2(menu->isVisible(), "Alt-hover over the copy button never opened its options popup");

    // NOT engaged (cursor stayed on the button, never moved into the popup): the
    // release closes it, same as any other hold-to-peek icon.
    QTest::keyRelease(&win, Qt::Key_Alt);
    QVERIFY2(!menu->isVisible(), "releasing Alt over the button did not close the peeked popup");
    copyBtn->setAttribute(Qt::WA_UnderMouse, false);

    // ENGAGED: move the cursor onto the popup itself before releasing Alt — it
    // must survive, exactly like every other peeked popover.
    copyBtn->setAttribute(Qt::WA_UnderMouse, true);
    QTest::keyPress(&win, Qt::Key_Alt);
    QVERIFY(menu->isVisible());
    QCursor::setPos(menu->mapToGlobal(menu->rect().center()));
    QTest::qWait(20);
    copyBtn->setAttribute(Qt::WA_UnderMouse, false);
    QTest::keyRelease(&win, Qt::Key_Alt);
    QVERIFY2(menu->isVisible(), "an ENGAGED peek (cursor moved into the popup) must survive Alt release");
    menu->close();
    QTest::qWait(260);   // let the row-preview's own dust settle before `win` dies (see above)
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));   // leave it parked for whatever runs next
  }

  // REGRESSION: under Fusion (main.cpp forces it app-wide) a chipped row's icon-to-
  // label gap blew out to ~3x normal. Root cause: MenuHotkeyChips pads the action's
  // TEXT with its own "\t"+spaces to blank the native shortcut column for the chip
  // widget to paint over, but never touched the action's real shortcut() — with
  // AA_DontShowShortcutsInContextMenus off (main.cpp), QMenuPrivate/Fusion then
  // double up the reserved shortcut width (already-tabbed text + a still-live
  // native shortcut), regardless of how much padding follows the tab. Fixed by
  // silencing shortcut() for the duration of the chip (the row still SHOWS it —
  // that's what the chip paints) and restoring it when the chip is torn down.
  void hotkeyChipDoesNotWidenTheIconGapUnderFusion() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    // actCopyImageOriginal_, not actCopyImage_: the latter is no longer a row in this
    // popup at all (actCopyImageCurrentRow_ is, and it carries no real shortcut of its
    // own by design — see mainWindow.hpp), but Original's Ctrl+Shift+C is exactly as
    // real and exactly as much MenuHotkeyChips' job to silence while chipped.
    const QKeySequence realShortcut = win.actCopyImageOriginal_->shortcut();
    QVERIFY2(!realShortcut.isEmpty(), "actCopyImageOriginal_ should carry a real shortcut to chip");

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
    // has the full story — this test used to assert first, and the FALSE this
    // regression exposed took the whole process down with it, SIGSEGV, reported).
    bool silencedWhileChipped = false;
    QString cachedCombo;
    if (opened) {
      // While chipped: the native shortcut is silenced (that's the actual fix)...
      silencedWhileChipped = win.actCopyImageOriginal_->shortcut().isEmpty();
      // ...but the row still knows the real combo (property-cache, menuHotkeys.hpp).
      cachedCombo = win.actCopyImageOriginal_->property("stencilHotkeyCombo").toString();
      menu->close();
      QTest::qWait(50);
    }
    QVERIFY2(opened, "the copy-image options popup never opened");
    QVERIFY2(silencedWhileChipped,
             "the action's native shortcut must be cleared while its row is chipped");
    QCOMPARE(cachedCombo, realShortcut.toString(QKeySequence::NativeText));
    QCOMPARE(win.actCopyImageOriginal_->shortcut(), realShortcut);   // restored once the chip is torn down
  }

  // A chipped row's keycaps shake once on hover (browser: .ctx-item:hover .tip-key /
  // keycapShake) — verified via capOffset(), "what the tests watch" per its own comment
  // (appTooltip.hpp), and driven with setActiveAction() rather than QTest::mouseMove:
  // the latter does not reliably reach a shown popup's own hover tracking (confirmed —
  // it left QMenu::hovered's own spy at 0 — so it isn't a usable probe for this or any
  // other hover-driven popup behaviour), exactly the same limitation
  // contextMenuRowShimmersOnHover already worked around for the sibling shimmer sweep.
  void hotkeyChipShakesOnHover() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    // "Current"'s own row (actCopyImageCurrentRow_) only shows once something is
    // drawn — see currentRowHiddenWithNoLinesButToolbarButtonStays.
    {
      stencil::core::Line line;
      line.points.push_back({4.0, 20.0});
      line.points.push_back({36.0, 20.0});
      win.canvas_->setLines({line});
      win.refreshActions();
    }

    QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
    QVERIFY(copyBtn);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                          copyBtn->mapToGlobal(copyBtn->rect().center()));
    QApplication::sendEvent(copyBtn, &ctx);
    QMenu* menu = win.copyImageOptionsMenu_;
    QVERIFY2(menu && menu->isVisible(), "the copy-image options popup never opened");

    QAction* row = win.actCopyImageCurrentRow_;
    QAction* other = nullptr;
    // Skip invisible rows too (actCopyImageSplit_ leads this same menu but stays
    // hidden outside compare mode) — setActiveAction on a row with no real geometry
    // wouldn't make the later move onto `row` a genuine transition.
    for (QAction* a : menu->actions()) if (a != row && !a->isSeparator() && a->isVisible()) { other = a; break; }
    QVERIFY(other);
    const QRect r = menu->actionGeometry(row);

    // NOT c->isHidden(): an action that's currently invisible (e.g. "Filter Only" with
    // no filter applied) still has its OWN chip widget parked wherever it was last valid
    // — geometry().intersects() alone can't tell a genuinely-showing chip from a hidden
    // one sitting in the same spot (menuHotkeys.hpp's place() hides, never destroys them).
    stencil::gui::TipBody* chip = nullptr;
    for (QLabel* l : menu->findChildren<QLabel*>())
      if (auto* c = dynamic_cast<stencil::gui::TipBody*>(l))
        if (!c->isHidden() && c->geometry().intersects(r)) chip = c;
    QVERIFY2(chip, "no chip found for the Current row");
    // NOT chip->capCount() here — calling it is what LAZILY hunts the keycap regions
    // (appTooltip.hpp's own findCaps()), and doing so from the test would prime the
    // exact state menuHotkeys.hpp's wire() must prime ITSELF, silently passing even if
    // production never does (the actual regression: nothing in menuHotkeys.hpp ever
    // called capCount(), so caps_ stayed empty forever and paintEvent()'s own "nothing
    // to shake" guard ate every shake in every real run of the app — capOffset() alone
    // still read correctly since dx_ itself was never in question, only whether
    // anything ever painted it; user report). The REST snapshot below is taken first,
    // grab()ing the chip exactly as wire() left it — untouched by this test.
    const QImage rest = chip->grab().toImage();

    bool sawNonZero = false;
    QImage midShake;
    // Land on a KNOWN different row first, so the move onto `row` is a genuine
    // transition (a freshly-opened QMenu can already be hovering its first row).
    menu->setActiveAction(other);
    menu->setActiveAction(row);
    for (int i = 0; i < 40 && !sawNonZero; ++i) {
      QTest::qWait(10);
      if (chip->capOffset() != 0) { sawNonZero = true; midShake = chip->grab().toImage(); }
    }
    menu->close();
    QVERIFY2(sawNonZero, "the chip's keycaps never moved during the shake window");
    QVERIFY2(!midShake.isNull() && midShake != rest,
             "the shake changed capOffset() but never actually painted anything different "
             "— the caps were never hunted, so paintEvent() had nothing to draw the shake with");
    QTest::qWait(50);
    menu->close();
  }

  // REGRESSION (user report): QMenu::hovered(QAction*) re-fires for the action ALREADY
  // being hovered — confirmed here via setActiveAction() on an already-active row,
  // which genuinely re-emits the signal, exactly what a repaint or plain mouse jitter
  // within the same row's bounds does for real — and shakeRow() restarted the
  // animation from frame one on every single re-fire, reading as the caps
  // continuously shaking on any mouse movement rather than once per hover.
  // REGRESSION: the re-fire guard above only advances on a genuinely NEW hovered(QAction*)
  // — but the mouse leaving a row WITHOUT landing on another one first (out past the menu
  // edge, then back onto the SAME row) never fires hovered() again either, so the guard
  // stayed stuck on that row and ate the second, perfectly legitimate hover (user report:
  // "plays only once, and don't [play] again on another hover"). Fixed the way
  // menuShimmer.hpp's RowOverlay already had to: reset the guard on QEvent::Leave.
  void hotkeyChipShakeReplaysAfterTheMouseLeavesAndComesBackToTheSameRow() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    {
      stencil::core::Line line;
      line.points.push_back({4.0, 20.0});
      line.points.push_back({36.0, 20.0});
      win.canvas_->setLines({line});
      win.refreshActions();
    }

    QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
    QVERIFY(copyBtn);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                          copyBtn->mapToGlobal(copyBtn->rect().center()));
    QApplication::sendEvent(copyBtn, &ctx);
    QMenu* menu = win.copyImageOptionsMenu_;
    QVERIFY2(menu && menu->isVisible(), "the copy-image options popup never opened");

    QAction* row = win.actCopyImageCurrentRow_;
    const QRect r = menu->actionGeometry(row);
    stencil::gui::TipBody* chip = nullptr;
    for (QLabel* l : menu->findChildren<QLabel*>())
      if (auto* c = dynamic_cast<stencil::gui::TipBody*>(l))
        if (!c->isHidden() && c->geometry().intersects(r)) chip = c;
    QVERIFY2(chip, "no chip found for the Current row");

    menu->setActiveAction(row);   // first hover: starts the shake
    QTRY_VERIFY2(chip->capOffset() != 0, "the shake should have started");
    // A plain wait long past the cycle's own length, not QTRY on ==0: the curve crosses
    // zero mid-cycle (hotkeyChipShakeDoesNotRestartOnAReFireForTheSameRow's own comment),
    // so QTRY would happily accept a passing zero-crossing as "settled" while the shake
    // is still actually running underneath it.
    QTest::qWait(stencil::gui::AppTooltip::kShakeMs + 300);
    QCOMPARE(chip->capOffset(), 0);

    // The mouse leaves the row WITHOUT ever landing on another one — no second
    // hovered(QAction*) fires for that, only a real Leave.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(menu, &leave);

    menu->setActiveAction(row);   // back onto the SAME row — must shake again
    QTRY_VERIFY2(chip->capOffset() != 0, "the shake should replay after the mouse came back");
    menu->close();
  }

  void hotkeyChipShakeDoesNotRestartOnAReFireForTheSameRow() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    {
      stencil::core::Line line;
      line.points.push_back({4.0, 20.0});
      line.points.push_back({36.0, 20.0});
      win.canvas_->setLines({line});
      win.refreshActions();
    }

    QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
    QVERIFY(copyBtn);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                          copyBtn->mapToGlobal(copyBtn->rect().center()));
    QApplication::sendEvent(copyBtn, &ctx);
    QMenu* menu = win.copyImageOptionsMenu_;
    QVERIFY2(menu && menu->isVisible(), "the copy-image options popup never opened");

    QAction* row = win.actCopyImageCurrentRow_;
    QAction* other = nullptr;
    for (QAction* a : menu->actions()) if (a != row && !a->isSeparator() && a->isVisible()) { other = a; break; }
    QVERIFY(other);
    const QRect r = menu->actionGeometry(row);
    stencil::gui::TipBody* chip = nullptr;
    for (QLabel* l : menu->findChildren<QLabel*>())
      if (auto* c = dynamic_cast<stencil::gui::TipBody*>(l))
        if (!c->isHidden() && c->geometry().intersects(r)) chip = c;
    QVERIFY2(chip, "no chip found for the Current row");

    // The shake curve crosses zero mid-cycle (it's a wiggle, not a one-way ramp), so a
    // single fixed-instant sample can land on a crossing and misread a live shake as
    // settled. Use QTRY to catch it on the way up instead of a single qWait+assert, and
    // time the re-fire and the settle check off a real clock rather than guessed delays.
    QElapsedTimer timer;
    menu->setActiveAction(other);
    timer.start();
    menu->setActiveAction(row);   // first hover: starts the shake
    QTRY_VERIFY2(chip->capOffset() != 0, "the shake should have started");
    while (timer.elapsed() < 120) QTest::qWait(10);   // well clear of the start
    menu->setActiveAction(row);   // the re-fire — must NOT restart it
    // Wait to (a hair past) the ORIGINAL shake's own finish line, measured from when it
    // actually started. A wrongly-restarted shake would still be running here (its own
    // clock reset at the re-fire, well under kShakeMs old by this checkpoint); the
    // correctly-unbothered one has already settled back to rest.
    const int remaining = int(stencil::gui::AppTooltip::kShakeMs + 60 - timer.elapsed());
    if (remaining > 0) QTest::qWait(remaining);
    // Read the chip BEFORE closing: menu->close() tears down MenuHotkeyChips, which
    // deletes the chip widgets outright — reading through the pointer after that is a
    // use-after-free (previously the source of this test's own flakiness).
    const int settledOffset = chip->capOffset();
    menu->close();
    QCOMPARE(settledOffset, 0);
  }

  // REGRESSION: the copy/download-image variant popups (and their canvas-context-menu
  // and top-Data-menu counterparts) used to size themselves off theme.cpp's generic
  // QMenu::item padding (24px left / 26px right) — sized for the menu BAR's own wider
  // checkable/submenu column — leaving a visible gap after the icon and a dead band
  // past the hotkey chip on these short rows (reported: roughly a third of the row's
  // width sitting empty on both sides of the chip). Fixed by MenuHotkeyChips' own
  // `compact` mode: a tighter local stylesheet plus an ACCURATE "\t"+spaces run sized
  // to the chip's own real width (no setFixedWidth — that only clips the outer widget
  // frame, not QMenuPrivate's own sizeHint-driven row layout, which stayed at the OLD
  // wider size and clipped every combo's last keycap when tried — menuHotkeys.hpp's
  // own comment has the full story). This test only bounds the SLACK; the per-chip
  // "does it actually fit" check lives in downloadPopupChipsAreNotClipped.
  void exportOptionsPopupIsNotWiderThanItsContent() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));   // main.cpp forces this app-wide
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
    QVERIFY(copyBtn);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                          copyBtn->mapToGlobal(copyBtn->rect().center()));
    QApplication::sendEvent(copyBtn, &ctx);
    QMenu* menu = win.copyImageOptionsMenu_;
    QVERIFY2(menu && menu->isVisible(), "the copy-image options popup never opened");

    int widestLabel = 0;
    for (QAction* a : menu->actions()) {
      if (!a->isVisible()) continue;   // e.g. "Filter Only" with no filter applied
      const QString label = a->text().left(a->text().indexOf('\t'));
      widestLabel = std::max(widestLabel, menu->fontMetrics().horizontalAdvance(label));
    }
    int widestChip = 0;
    // Skip HIDDEN chips ("Filter Only" with no filter applied, "With Compare" outside
    // compare) — menuHotkeys.hpp's place() hides rather than destroys them, so one can
    // still be sitting there with a nonzero width that never actually shows on screen.
    for (QLabel* l : menu->findChildren<QLabel*>())
      if (auto* chip = dynamic_cast<stencil::gui::TipBody*>(l))
        if (!chip->isHidden()) widestChip = std::max(widestChip, chip->width());
    QVERIFY2(widestChip > 0, "no hotkey chips found on the copy-image popup");

    // Icon + paddings + the gap between label and chip + the menu's own frame. A
    // generous ceiling (not an exact match) — it only has to catch the row coming out
    // FAR wider than its content, the actual regression.
    const int slack = menu->width() - (widestLabel + widestChip);
    // Closed BEFORE asserting, not after — an early QVERIFY2 return must never leave the
    // menu open, or it outlives `win` and crashes on teardown (downloadPopupChipsAreNotClipped's
    // own comment has the full story; this test used to assert first, so a failing slack
    // check here left the popup open and took the whole process down with it — SIGSEGV,
    // reported).
    menu->close();
    QVERIFY2(slack > 0 && slack <= 80,
             qPrintable(QString("menu is %1 wide for a %2px label + %3px chip — %4px of slack")
                            .arg(menu->width()).arg(widestLabel).arg(widestChip).arg(slack)));
  }

  // REGRESSION: the download popup's own combos are the widest (3 modifiers + D) — an
  // explicit per-chip "does it actually fit inside the menu" check, not just the
  // aggregate slack bound exportOptionsPopupIsNotWiderThanItsContent already checks.
  // A setFixedWidth()-based fix tried here first clipped every combo's last keycap: it
  // only clips the outer WIDGET frame, not QMenuPrivate's own (independently sizeHint-
  // driven) row layout, so the row — and this class's chip, positioned from that SAME
  // row rect — kept the wider natural size while the frame around it shrank underneath
  // (menuHotkeys.hpp's own comment has the full story). Menu closed BEFORE asserting,
  // not after — an early QVERIFY2 return must never leave it open, or a QMenu that
  // outlives `win` crashes on teardown (styleDangerToolButtons fires off a QAction
  // signal from MenuHotkeyChips' destructor mid `~MainWindow`, reported).
  void downloadPopupChipsAreNotClipped() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    QWidget* saveBtn = win.buttonForAction(win.actSaveImage_);
    QVERIFY(saveBtn);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, saveBtn->rect().center(),
                          saveBtn->mapToGlobal(saveBtn->rect().center()));
    QApplication::sendEvent(saveBtn, &ctx);
    QMenu* menu = win.saveImageOptionsMenu_;
    const bool opened = menu && menu->isVisible();
    bool anyOverflow = false;
    if (opened) {
      QTest::qWait(60);
      for (QLabel* l : menu->findChildren<QLabel*>()) {
        if (auto* chip = dynamic_cast<stencil::gui::TipBody*>(l))
          if (!chip->isHidden() && chip->geometry().right() > menu->width()) anyOverflow = true;
      }
      menu->close();
      QTest::qWait(50);
    }
    QVERIFY2(opened, "the download-image options popup never opened");
    QVERIFY2(!anyOverflow, "a chip's right edge overflows the menu's own width");
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
    win.openPathFromOS(png_);   // the canvas menu opens for an image, and only then
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

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
    win.openPathFromOS(png_);   // the canvas menu opens for an image, and only then
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
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

  // §10 chatPanel: the assistant panel's OWN placement, driven by a plan — "put the
  // chat on the right and open it" is a thing users ask for out loud, hands-free
  // (browser opPlan.js chatPanel parity). The plan runs through the real parser and
  // executor, so this pins the whole path, not the target method alone.
  void chatPanelOpDocksAndOpensThePanel() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());

    const auto run = [&win](const char* json) {
      const stencil::llm::OpPlanResult r = stencil::llm::parseOpPlan(QString::fromUtf8(json));
      QVERIFY2(r.ok, qPrintable(r.error));
      stencil::gui::ChatPlanTarget target(win);
      const stencil::llm::ExecResult res = stencil::llm::executePlan(r.plan, target);
      QVERIFY2(res.ok, qPrintable(res.error));
    };

    // A dock with no "open" moves it AND shows it — placing a panel nobody can see is
    // not what was asked for.
    run(R"({"reply":"ok","actions":[{"op":"chatPanel","dock":"right"}]})");
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(chat->isChecked());
    // The open and the side switch both FLY (chatSurfaceFlight) — the area is what it
    // settles at, not what it holds mid-flight.
    QTRY_VERIFY(!win.chatAnim_);
    QVERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);

    // …the other sides go through the same path as the title bar's own buttons.
    run(R"({"reply":"ok","actions":[{"op":"chatPanel","dock":"bottom"}]})");
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::BottomDockWidgetArea);
    // The side switch flies (chatSurfaceFlight) and its finish SHOWS the dock again —
    // let it land before asking for a close, exactly as a user's second sentence would.
    QTRY_VERIFY(!win.chatAnim_);

    // "open": false closes it and leaves the placement alone.
    run(R"({"reply":"ok","actions":[{"op":"chatPanel","open":false}]})");
    QTRY_VERIFY(!dock->isVisible());
    QVERIFY(!chat->isChecked());

    // …and "float" lifts it off the edges.
    run(R"({"reply":"ok","actions":[{"op":"chatPanel","open":true,"dock":"float"}]})");
    QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY(dock->isFloating());

    // A field-less chatPanel says nothing and is rejected by the PARSER, so no plan
    // reaches the editor at all.
    const auto bad = stencil::llm::parseOpPlan(
        QStringLiteral(R"({"reply":"ok","actions":[{"op":"chatPanel"}]})"));
    QVERIFY2(!bad.ok, "a chatPanel with neither open nor dock must not parse");
    beat();
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
    const auto motion = withMotion();   // the sweep honours motionReduced(), which is on here
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

  // Every icon button mimes its OWN action on hover (support/iconMotion.hpp, the port of
  // browser/js/config/iconMotion.json): the trash lid lifts, plus grows, minus shrinks.
  // Driven here on a REAL toolbar button, for the three things the app-wide contract is
  // made of — reduced motion wins, the glyph really is repainted, and NOTHING reflows
  // (only the icon's own pixels change, so a hovered control cannot shove the row).
  void iconMotionRunsOnToolbarButtonsWithoutReflow() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(30);   // let the toolbar's own deferred layout pass settle before measuring it

    // A shown, enabled toolbar button carrying a SETTLE design — it comes back to rest on
    // its own, so convergence can be asserted without a leave.
    QToolButton* btn = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>()) {
      if (!b->isVisible() || !b->isEnabled()
          || b->property(stencil::gui::kNoIconMotionProperty).toBool())
        continue;
      stencil::gui::IconRequest r;
      if (!stencil::gui::iconRequestForKey(b->icon().cacheKey(), &r)) continue;
      const stencil::gui::IconMotionSpec* spec = stencil::gui::iconMotionFor(r.name);
      if (!spec || spec->hold) continue;
      btn = b;
      break;
    }
    QVERIFY2(btn, "no shown toolbar button with a settle icon motion");

    const auto enter = [](QWidget* w) {
      QEnterEvent e(QPointF(3, 3), QPointF(3, 3), w->mapToGlobal(QPoint(3, 3)));
      QApplication::sendEvent(w, &e);
    };
    const auto leave = [](QWidget* w) {
      QEvent e(QEvent::Leave);
      QApplication::sendEvent(w, &e);
    };
    // Every sibling's box, so a reflow anywhere in the row is caught, not just the
    // hovered button's own.
    QWidget* row = btn->parentWidget();
    QList<QRect> before;
    for (QWidget* w : row->findChildren<QWidget*>()) before << w->geometry();

    // Reduced motion is this suite's default, and the preference wins outright.
    const qint64 rest = btn->icon().cacheKey();
    enter(btn);
    QTest::qWait(80);
    QCOMPARE(btn->icon().cacheKey(), rest);
    leave(btn);

    // …and with motion allowed, the hover repaints the glyph and the play lands back on it.
    const auto motion = withMotion();
    enter(btn);
    QTRY_VERIFY2(btn->icon().cacheKey() != rest, "a hover did not move the glyph");
    QList<QRect> during;
    for (QWidget* w : row->findChildren<QWidget*>()) during << w->geometry();
    QCOMPARE(during, before);   // no layout shift, anywhere in the row
    QTRY_COMPARE(btn->icon().cacheKey(), rest);
    leave(btn);
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
  // The bug this locks down: Start/Stop Drawing were in the Edit menu but the instant
  // line/rect items were not (only the rect one existed, and only on the toolbar and in
  // the canvas context menu) — the menu bar gave no way to draw a shape instantly (the
  // browser's Draw section has both). The same held for the line-style set and the image
  // filter. Walking the real menu bar also proves the shared plain QActions did not get
  // moved OUT of the context menu, which is exactly what would happen if a QWidgetAction
  // were reused this way.
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

    // The reported gap: instant line/rect, beside Start/Stop.
    QVERIFY(titles.contains("Start Drawing"));
    QVERIFY(titles.contains("Stop Drawing"));
    QVERIFY(titles.contains("Draw Line"));
    QVERIFY(titles.contains("Draw Rectangle"));

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

    // Inspect the modal while it blocks the trigger, then back out of it. The prompt
    // is the chrome-styled askAlt (modalChrome confirmModalChoice), not a QMessageBox.
    QMap<QString, bool> hasGlyph;
    QTimer::singleShot(0, [&hasGlyph]() {
      for (int i = 0; i < 200; ++i) {
        QWidget* m = QApplication::activeModalWidget();
        if (m && m->objectName() == QLatin1String("stencilConfirmModal")) {
          for (QPushButton* b : m->findChildren<QPushButton*>())
            hasGlyph.insert(QString(b->text()).remove('&'), !b->icon().isNull());
          for (QPushButton* b : m->findChildren<QPushButton*>())
            if (QString(b->text()).remove('&').compare("Cancel", Qt::CaseInsensitive) == 0) {
              b->click();
              return;
            }
          if (auto* d = qobject_cast<QDialog*>(m)) d->reject();
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
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QVERIFY(!canvas->idleHintHidden());

    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    dismissModal("OK");
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

  // A chat card arrives the way a toast does: its dust gathers into place out of a point
  // off the side it sits against, while the bubble is held back behind the motes (which
  // side is chatCardDustArrivesFromTheCardsOwnSide's job — this is that it flies at all).
  // Browser twin: motion.js chatIn.
  void chatCardsArriveOutOfDust() {
    const auto motion = withMotion();   // the suite runs with STENCIL_NO_ANIM on
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    const char* kDust = stencil::gui::DisintegrateOverlay::kObjectName;
    QTRY_VERIFY_WITH_TIMEOUT(!win.findChild<QWidget*>(kDust),
                             stencil::gui::DisintegrateOverlay::kMs + 2000);

    // The message you send, the "…" holding the turn, the reply, and a failure — every
    // one of them is a card appearing, so every one of them gathers.
    for (const auto& append : QVector<std::function<void()>>{
             [&win] { win.chatDock_->appendUser(QStringLiteral("crop it square"), {}); },
             [&win] { win.chatDock_->showPending(); },
             [&win] { win.chatDock_->appendAssistant(QStringLiteral("Cropped.")); },
             [&win] { win.chatDock_->appendError(QStringLiteral("Couldn't reach Ollama at localhost:11434 (fetch failed)"),
                                                 QStringLiteral("crop it square")); }}) {
      append();
      // The grab is deferred (appendTranscriptCard hands the caller an EMPTY card — the
      // dust has to be a photograph of the FINISHED bubble, laid out at its real width),
      // so the cloud shows up a beat later, not in this tick.
      QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(kDust) != nullptr, 3000);
      QFrame* card = nullptr;
      for (QFrame* f : win.chatDock_->findChildren<QFrame*>())
        if (f->objectName().startsWith(QLatin1String("chatCard"))) card = f;
      QVERIFY(card);
      // Held FULLY hidden while the motes fly — they ARE the bubble forming. Fading it up
      // underneath them drew the finished card first and played the animation over the
      // top of it, which is the one thing an arrival must not do (the reported bug).
      auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect());
      QVERIFY2(fx && fx->opacity() == 0.0, "the card is invisible until its motes land");
      QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(kDust) == nullptr,
                               stencil::gui::DisintegrateOverlay::kMs + 2000);
      win.chatDock_->clearPending();   // the "…" must not outlive its own case
    }

    // …and every card lands on its resting state: full opacity, resting margins, and the
    // effect handed back to the scroll-edge reveal (kEnteringProperty dropped).
    int settled = 0;
    for (QFrame* card : win.chatDock_->findChildren<QFrame*>()) {
      if (!card->objectName().startsWith(QLatin1String("chatCard"))) continue;
      ++settled;
      if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect()))
        QTRY_COMPARE(fx->opacity(), 1.0);
      QTRY_COMPARE(card->property(stencil::gui::ScrollReveal::kEnteringProperty).toBool(), false);
      QTRY_COMPARE(card->layout()->contentsMargins().top(), 6);
    }
    QVERIFY2(settled > 0, "no card was found — the checks above would be vacuous");
    beat();
  }

  // …and so does a row in the context menu's assistant panel — the third chat surface on
  // this front-end. It mirrors the dock's transcript, so an arrival there is an arrival
  // too (ChatMenuPanel::gatherRow, the dock's animateCardIn in miniature).
  void chatMenuPanelRowsArriveOutOfDust() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.ensureChatMenuPanel();
    QVERIFY(win.chatMenuPanel_);
    win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel_->show();
    QTest::qWait(300);   // the panel's own width has to arrive before the grab can read it
    const char* kDust = stencil::gui::DisintegrateOverlay::kObjectName;
    QTRY_VERIFY_WITH_TIMEOUT(!win.findChild<QWidget*>(kDust),
                             stencil::gui::DisintegrateOverlay::kMs + 2000);

    win.chatMirror(QStringLiteral("You"), QStringLiteral("crop it square"), false);
    QTRY_VERIFY_WITH_TIMEOUT(win.chatMenuPanel_->findChild<QWidget*>(kDust) != nullptr, 3000);
    // …with the row itself held back behind them, never faded up underneath.
    for (QFrame* row : win.chatMenuPanel_->findChildren<QFrame*>()) {
      if (!row->property("chatMoreBtn").isValid()) continue;
      if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(row->graphicsEffect()))
        QVERIFY2(fx->opacity() == 0.0, "the row is invisible until its motes land");
    }
    QTRY_VERIFY_WITH_TIMEOUT(win.chatMenuPanel_->findChild<QWidget*>(kDust) == nullptr,
                             stencil::gui::DisintegrateOverlay::kMs + 2000);
    // The row lands visible — held back behind the motes, never left behind them.
    int settled = 0;
    for (QFrame* row : win.chatMenuPanel_->findChildren<QFrame*>()) {
      if (!row->property("chatMoreBtn").isValid()) continue;
      ++settled;
      if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(row->graphicsEffect()))
        QTRY_COMPARE(fx->opacity(), 1.0);
    }
    QVERIFY2(settled > 0, "no mirrored row was found — the check would be vacuous");
    beat();
  }

  // REGRESSION: on a transcript long enough to scroll, the cloud was photographed before
  // the scroll landed, so the motes flew at the card's pre-scroll box and rained over the
  // composer. The entrance now waits a frame for scrollToBottom() and refuses to fly for a
  // card not wholly inside the viewport.
  void chatCardDustArrivesFromTheCardsOwnSide() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1000, 620);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    const char* kDust = stencil::gui::DisintegrateOverlay::kObjectName;
    QTRY_VERIFY_WITH_TIMEOUT(!win.findChild<QWidget*>(kDust),
                             stencil::gui::DisintegrateOverlay::kMs + 4000);

    // `side` is +1 when the cloud must come from the right of the card, -1 from the left.
    const auto arrivesFrom = [&](const char* cardName, int side, const char* what) {
      QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(kDust) != nullptr, 3000);
      // No Q_OBJECT on the overlay (it needs no MOC), so its unique object name IS the
      // type check — qobject_cast will not compile for it.
      auto* dust = static_cast<stencil::gui::DisintegrateOverlay*>(win.findChild<QWidget*>(kDust));
      QVERIFY2(dust, what);
      QVERIFY2(dust->gathering(), what);   // an arrival, not a leave
      QFrame* card = nullptr;
      for (QFrame* f : win.chatDock_->findChildren<QFrame*>(QString::fromLatin1(cardName))) card = f;
      QVERIFY2(card, what);
      const QRect box(card->mapTo(&win, QPoint(0, 0)), card->size());
      const int dx = dust->surfaceTarget().x() - box.center().x();
      QVERIFY2(side * dx > 0, what);
      // …and clear of the card itself, so the motes visibly travel in over its edge.
      QVERIFY2(qAbs(dx) > box.width() / 2, what);
      QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(kDust) == nullptr,
                               stencil::gui::DisintegrateOverlay::kMs + 2000);
    };
    win.chatDock_->appendUser(QStringLiteral("mine, on the right"), {});
    arrivesFrom("chatCardUser", +1, "a user message must gather from the RIGHT");
    win.chatDock_->appendAssistant(QStringLiteral("and the reply, on the left"));
    arrivesFrom("chatCardAssistant", -1, "an assistant message must gather from the LEFT");
  }

  void chatCardDustNeverEscapesTheScrolledTranscript() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1000, 620);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    const char* kDust = stencil::gui::DisintegrateOverlay::kObjectName;

    // Fill it well past one viewport, so every further append really does scroll.
    for (int i = 0; i < 10; ++i) {
      win.chatDock_->appendUser(QStringLiteral("question %1 long enough to wrap onto a second line").arg(i), {});
      win.chatDock_->appendAssistant(QStringLiteral("reply %1, also long enough to take real height in the column").arg(i));
    }
    QTRY_VERIFY_WITH_TIMEOUT(!win.findChild<QWidget*>(kDust),
                             stencil::gui::DisintegrateOverlay::kMs + 4000);
    auto* scroll = win.chatDock_->findChild<QScrollArea*>();
    QVERIFY(scroll);
    QVERIFY2(scroll->verticalScrollBar()->maximum() > 0, "the transcript never became scrollable");

    win.chatDock_->appendUser(QStringLiteral("one more, which has to scroll into view"), {});
    // The cloud that belongs to THIS card. The fill above can still have arrivals in
    // flight (gatherChatCardIn waits the layout out in hops), and grabbing whichever
    // overlay happened to exist measured one card's cloud against another's box.
    QFrame* card = nullptr;
    stencil::gui::DisintegrateOverlay* fx = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
      card = nullptr;
      for (QFrame* f : win.chatDock_->findChildren<QFrame*>(QStringLiteral("chatCardUser"))) card = f;
      if (!card) return false;
      const QRect box(card->mapTo(&win, QPoint(0, 0)), card->size());
      for (QWidget* w : win.findChildren<QWidget*>(QString::fromLatin1(kDust))) {
        auto* o = static_cast<stencil::gui::DisintegrateOverlay*>(w);
        if (o->surfacePicture() == box) { fx = o; return true; }
      }
      return false;
    }()), 4000);
    QVERIFY(fx);
    QVERIFY(card);
    // The card the cloud stands in for is inside the viewport — so the motes cannot be
    // flying anywhere near the composer.
    const QRect viewGlobal(scroll->viewport()->mapToGlobal(QPoint(0, 0)), scroll->viewport()->size());
    const QRect cardGlobal(card->mapToGlobal(QPoint(0, 0)), card->size());
    // VERTICALLY inside — the axis the scroll moves, and the one the composer is on. A
    // bubble's own furniture (the "…" trigger) deliberately hangs outside it sideways.
    QVERIFY2(cardGlobal.top() >= viewGlobal.top() && cardGlobal.bottom() <= viewGlobal.bottom(),
             "the dusted card is not wholly in the viewport");
    // An arrival is a surface cloud, so the layer is the whole window and carries the
    // card's box inside it; what matters is where it may PAINT. The clip is the
    // transcript's viewport, so no mote reaches the composer however far it flies.
    QCOMPARE(fx->paintClip(),
             QRect(scroll->viewport()->mapTo(&win, QPoint(0, 0)), scroll->viewport()->size()));
    QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(kDust) == nullptr,
                             stencil::gui::DisintegrateOverlay::kMs + 2000);
    if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect()))
      QTRY_COMPARE(fx->opacity(), 1.0);
    beat();
  }

  // REGRESSION: a card's dust is a SNAPSHOT placed once, but the transcript keeps moving
  // under it — appending the next card scrolls the view, a wrapped label re-reserves its
  // height, the dock is resized. Left where it launched, that snapshot was drawn over a
  // NEIGHBOURING bubble, which reads as one message overlapping the one below it
  // (reported). The overlay now follows its own card, and is dropped outright the moment
  // the card moves out of view or changes size.
  void chatCardDustFollowsItsOwnCard() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    const char* kDust = stencil::gui::DisintegrateOverlay::kObjectName;

    // Enough traffic that every further append really does scroll the transcript.
    for (int i = 0; i < 8; ++i) {
      win.chatDock_->appendUser(QStringLiteral("Give me 3 variants: rotated, tinted, cropped %1").arg(i), {});
      win.chatDock_->appendAssistant(QStringLiteral("reply %1, long enough to take real height").arg(i));
    }
    QTRY_VERIFY_WITH_TIMEOUT(!win.findChild<QWidget*>(kDust),
                             stencil::gui::DisintegrateOverlay::kMs + 4000);

    // A turn's two cards land back to back — the second one's append is what scrolls the
    // first one's snapshot off its subject.
    win.chatDock_->appendUser(QStringLiteral("Give me 3 variants: rotated, tinted, cropped"), {});
    QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(kDust) != nullptr, 3000);
    win.chatDock_->appendError(QStringLiteral("not connected to http://localhost:8090 (no token)"),
                               QStringLiteral("retry me"));

    // Watch the whole flight: every live overlay must sit on a card, never between two.
    int strandedFrames = 0, sampled = 0;
    for (int f = 0; f < 90; ++f) {
      QTest::qWait(16);
      for (QWidget* d : win.findChildren<QWidget*>(kDust)) {
        if (!d->isVisible()) continue;
        ++sampled;
        const QPoint dTL = d->mapToGlobal(QPoint(0, 0));
        bool onACard = false;
        for (QFrame* c : win.chatDock_->findChildren<QFrame*>()) {
          if (!c->objectName().startsWith(QLatin1String("chatCard"))) continue;
          const QPoint cTL = c->mapToGlobal(QPoint(0, 0));
          // The layer is padded around its picture (kSurfacePadPx-ish slack), so it is
          // the OFFSET that must match, not the box.
          constexpr int kSlack = 70;
          if (qAbs(dTL.x() - cTL.x()) <= kSlack && qAbs(dTL.y() - cTL.y()) <= kSlack) {
            onACard = true;
            break;
          }
        }
        if (!onACard) ++strandedFrames;
      }
    }
    QVERIFY2(sampled > 0, "no overlay was ever sampled — the check would be vacuous");
    QCOMPARE(strandedFrames, 0);

    // …and when the tracker DOES drop a stale snapshot, the card it was standing in for
    // takes over in that same moment. The veil is otherwise lifted only at the end of the
    // full flight, so a cancel that just killed the motes left the message invisible with
    // nothing in its place (the browser twin had exactly this, found by resizing a live
    // entry mid-flight). Resizing the dock changes every card's width — the drop path.
    QTRY_VERIFY_WITH_TIMEOUT(!win.findChild<QWidget*>(kDust),
                             stencil::gui::DisintegrateOverlay::kMs + 3000);
    win.chatDock_->appendUser(QStringLiteral("resized mid-flight"), {});
    QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(kDust) != nullptr, 3000);
    win.chatDock_->resize(win.chatDock_->width() - 90, win.chatDock_->height());
    QTest::qWait(120);
    QFrame* resized = nullptr;
    for (QFrame* c : win.chatDock_->findChildren<QFrame*>("chatCardUser")) resized = c;
    QVERIFY(resized);
    if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(resized->graphicsEffect()))
      QTRY_VERIFY2_WITH_TIMEOUT(fx->opacity() == 1.0,
                                "a card whose snapshot was dropped must not stay invisible",
                                1500);

    // …and nothing is left behind: every card lands visible, at its resting margins.
    QTRY_VERIFY_WITH_TIMEOUT(!win.findChild<QWidget*>(kDust),
                             stencil::gui::DisintegrateOverlay::kMs + 3000);
    for (QFrame* c : win.chatDock_->findChildren<QFrame*>()) {
      if (!c->objectName().startsWith(QLatin1String("chatCard"))) continue;
      if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(c->graphicsEffect()))
        QTRY_COMPARE(fx->opacity(), 1.0);
    }
    beat();
  }

  // REGRESSION: the empty-state chips rendered as sharp RECTANGLES (reported). Qt draws a
  // square box — silently — when border-radius exceeds half the widget's height, and these
  // chips settle at 28px while the sheet asked for the browser's 16. Reading the height at
  // style time does not save it either: the flow layout compresses the chip from 32 to 28
  // afterwards, so the radius has to be pinned to the floor the app-wide sheet guarantees.
  void suggestionChipsAreRoundedPills() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    QTest::qWait(300);
    QWidget* chips = win.chatDock_->findChild<QWidget*>(QStringLiteral("chatSuggest"));
    QVERIFY(chips);
    const auto btns = chips->findChildren<QPushButton*>(QStringLiteral("chatSuggestChip"));
    QCOMPARE(btns.size(), 4);

    for (QPushButton* chip : btns) {
      // The radius the sheet asks for must be one this chip can actually carry.
      const QRegularExpression re(QStringLiteral("border-radius:(\\d+)px"));
      const QRegularExpressionMatch m = re.match(chip->styleSheet());
      QVERIFY2(m.hasMatch(), "the chip carries no border-radius at all");
      const int radius = m.captured(1).toInt();
      QVERIFY2(radius * 2 <= chip->height(),
               qPrintable(QStringLiteral("radius %1 exceeds half of the chip's %2px height — "
                                         "Qt renders that as a rectangle")
                              .arg(radius).arg(chip->height())));
      QVERIFY2(radius >= 8, "…and it still has to read as a pill, not a soft rectangle");
    }

    // …and it really PAINTS rounded: rendered onto white, the corners must show white
    // through. grab() alone cannot tell — outside a rounded corner it leaves transparent
    // pixels, which over this dark theme look exactly like the chip's own fill.
    QPushButton* chip = btns.first();
    QPixmap shot(chip->size());
    shot.fill(Qt::white);
    chip->render(&shot, QPoint(), QRegion(), QWidget::DrawChildren);
    const QImage img = shot.toImage();
    QVERIFY2(img.pixelColor(0, 0) == QColor(Qt::white),
             "the top-left corner is filled — the chip is a rectangle");
    QVERIFY2(img.pixelColor(img.width() - 1, img.height() - 1) == QColor(Qt::white),
             "the bottom-right corner is filled — the chip is a rectangle");
    // …while its middle is of course painted.
    QVERIFY2(img.pixelColor(img.width() / 2, img.height() / 2) != QColor(Qt::white),
             "the chip did not paint at all — the corner check would be vacuous");
    beat();
  }

  // Reduced motion: the card is simply THERE on the short fade — no cloud, and above all
  // no card left sitting at opacity 0 for a flight that never ran.
  void reducedMotionChatCardArrivesAtOnce() {
    qputenv("STENCIL_NO_ANIM", "1");   // the suite's own default; set explicitly for the reader
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    win.chatDock_->appendUser(QStringLiteral("hi"), {});
    QTest::qWait(120);
    QVERIFY2(!win.findChild<QWidget*>(stencil::gui::DisintegrateOverlay::kObjectName),
             "no dust under reduced motion");
    QFrame* card = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardUser")) card = f;
    QVERIFY(card);
    if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect()))
      QTRY_COMPARE(fx->opacity(), 1.0);
    beat();
  }

  // …and the mirror image: an arriving image ASSEMBLES out of dust (Sweep::Gather) rather
  // than appearing all at once, with the real canvas held back until the motes land.
  // Any fresh image, not just a dropped one — a created blank is covered below.
  void droppedImageAssemblesOutOfDust() {
    const auto motion = withMotion();
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
    const auto motion = withMotion();
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

  // REGRESSION: REOPENING a saved project put its picture on screen with no arrival at all —
  // the everyday way an image appears, and the one path that never played. Every user-facing
  // "a picture lands on the canvas" now goes through playImageArrival.
  void reopenedProjectAssemblesLikeAFreshImage() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    const char* kDust = stencil::gui::DisintegrateOverlay::kObjectName;
    // Let the OPEN's own arrival finish, so what we see next belongs to the reopen.
    QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(kDust) == nullptr,
                             stencil::gui::DisintegrateOverlay::kMs + 2000);
    const QString id = win.activeProjectId_;
    QVERIFY2(!id.isEmpty(), "the loaded image was adopted as a local project");

    QVERIFY(win.loadProjectIntoCanvas(id));
    QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(kDust) != nullptr, 3000);
    if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(canvas->graphicsEffect()))
      QVERIFY2(fx->opacity() < 0.01, "the real canvas waits behind the motes");
    QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(kDust) == nullptr,
                             stencil::gui::DisintegrateOverlay::kMs + 2000);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->graphicsEffect() == nullptr, 2000);
    beat();
  }

  // …but a REBIND is not an arrival: the same picture is already on screen (a move-to-local
  // relinks the open editor), so it must not flourish.
  void rebindingTheOpenProjectDoesNotReplayTheArrival() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    const char* kDust = stencil::gui::DisintegrateOverlay::kObjectName;
    QTRY_VERIFY_WITH_TIMEOUT(win.findChild<QWidget*>(kDust) == nullptr,
                             stencil::gui::DisintegrateOverlay::kMs + 2000);

    QVERIFY(win.loadProjectIntoCanvas(win.activeProjectId_, /*animate=*/false));
    QTest::qWait(150);
    QVERIFY2(!win.findChild<QWidget*>(kDust), "a rebind is not an image appearing");
    QVERIFY2(!canvas->graphicsEffect(), "…and it must never hide the canvas");
    beat();
  }

  // Reduced motion: the image is simply THERE. The bug this pins is not the missing dust —
  // it is the opacity effect, which used to stay on at 0 and leave the canvas blank for the
  // whole 900 ms flight, i.e. "nothing plays and then it pops".
  void reducedMotionShowsTheImageAtOnce() {
    qputenv("STENCIL_NO_ANIM", "1");   // the suite's own default; set explicitly for the reader
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTest::qWait(120);
    QVERIFY2(!win.findChild<QWidget*>(stencil::gui::DisintegrateOverlay::kObjectName),
             "no dust under reduced motion");
    QVERIFY2(!canvas->graphicsEffect(), "the end state, immediately: a visible canvas");

    // The clear counterpart lands on its end state too — no scatter, and the empty-canvas
    // invitation is back at once instead of waiting out an animation that never ran.
    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    dismissModal("OK");
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);
    QVERIFY2(!win.findChild<QWidget*>(stencil::gui::DisintegrateOverlay::kObjectName),
             "no dust on the clear either");
    QVERIFY2(!canvas->idleHintHidden(), "the invitation is not held back by a missing animation");
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
    // The follow-up carries the mode alone: an echoed "split" beside "none" is a
    // parse failure since the registry's onlyWith rule (fixture 160).
    mock.response = plan("{\"version\":1,\"reply\":\"cleared\",\"actions\":["
                         "{\"op\":\"compare\",\"mode\":\"none\"}]}");
    win.onChatSend("turn the comparison off");
    QTRY_COMPARE(win.canvas_->compareMode(), QStringLiteral("none"));
    QVERIFY2(!win.canvas_->compareReadOnly(), "compare 'none' left the canvas read-only");
    QCOMPARE(win.compareCombo_->currentData().toString(), QStringLiteral("none"));
    beat();
  }

  // REGRESSION: the compare combo's connect() lived in buildStyleToolbar(), which
  // buildToolbar() calls BEFORE buildDrawViewToolbar() — the function that actually
  // constructs compareCombo_. Every click there wired to a still-null combo (Qt drops
  // a connect() with a null sender, warning "invalid nullptr parameter"), so every row
  // in the popup looked selectable but never touched the canvas (reported: "none of
  // the options work"). Fixed by moving the connect() into buildDrawViewToolbar,
  // right after compareCombo_ is built. A REAL click through the combo's own themed
  // popup, not win.setCompareModeUi() called directly — that bypasses the exact wiring
  // that was broken.
  void compareComboClickActuallyChangesTheCanvas() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(64, 48, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());

    QVERIFY(win.compareCombo_);
    QCOMPARE(win.compareCombo_->currentData().toString(), QStringLiteral("none"));
    win.compareCombo_->showPopup();
    QTest::qWait(60);
    QWidget* popup = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets())
      if (w->isVisible() && w->findChild<QWidget*>("searchComboPopup")) popup = w;
    QVERIFY2(popup, "the compare combo's themed popup never appeared");
    auto* list = popup->findChild<QListView*>("searchComboList");
    QVERIFY(list);
    // Row 2 = "Split ↔" (vertical) — see the addItem() order in buildDrawViewToolbar.
    const QModelIndex idx = list->model()->index(2, 0);
    QCOMPARE(idx.data(Qt::DisplayRole).toString(), QString::fromUtf8("Split ↔"));
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, list->visualRect(idx).center());
    QTest::qWait(30);
    QCOMPARE(win.compareCombo_->currentData().toString(), QStringLiteral("vertical"));
    QCOMPARE(win.canvas_->compareMode(), QStringLiteral("vertical"));
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

  // The bug this locks down: recoloring a blank project's background regenerates the
  // SAME dimensions in place (applyBlankColor → loadFromImage(img, keepZoom=true)) —
  // there is nothing to refit, so the zoom the user had set must survive it (browser
  // parity: drawingApp.js loadImageFromFile's opts.keepZoom / drawingApp-launch tests).
  void recoloringABlankKeepsTheZoom() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.createBlankImage(QColor("#ffffff"), 400, 300);
    win.canvas_->setScale(2.5);
    QCOMPARE(win.canvas_->scale(), 2.5);
    win.applyBlankColor(QColor("#0000ff"));
    QCOMPARE(win.canvas_->scale(), 2.5);
    beat();
  }

  // The bug this locks down: a context-menu row whose action isn't available (no
  // image, no lines) used to show up greyed out with nothing to explain why — now it
  // is simply not in the menu, the desktop's version of the browser's hide-not-disable
  // (contextMenu.js syncState). The persistent menu bar keeps the conventional greyed
  // rows instead (menuBarExposesTheDrawAndStyleControls covers that one).
  void contextMenuHidesUnavailableActionsInsteadOfGreyingThem() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // An image but NO lines — the menu needs an image to open at all, so the
    // line-dependent rows are what "unavailable" means here. Real right-click on the
    // scroll area's viewport (contextMenuOpensOnEmptyCanvasArea's own way in).
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    QWidget* viewport = win.findChild<QScrollArea*>()->viewport();
    QVERIFY(viewport);

    auto openSubByKey = [](QMenu* menu, const QString& title) -> QMenu* {
      QAction* parent = nullptr;
      for (QAction* a : menu->actions())
        if (a->text().startsWith(title)) parent = a;
      if (!parent || !parent->menu()) return nullptr;
      menu->setActiveAction(parent);
      QTest::keyClick(menu, Qt::Key_Right);
      for (int i = 0; i < 100 && !parent->menu()->isVisible(); ++i) QTest::qWait(10);
      return parent->menu()->isVisible() ? parent->menu() : nullptr;
    };

    QSet<QString> rootTitles, layoutTitles;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      for (QAction* a : menu->actions())
        if (!a->isSeparator()) rootTitles.insert(a->text());
      if (QMenu* layout = openSubByKey(menu, "Image / Layout"))
        for (QAction* a : layout->actions())
          if (!a->isSeparator()) layoutTitles.insert(a->text());
      menu->close();
    });
    QTest::mouseClick(viewport, Qt::RightButton, {}, QPoint(6, 6));
    QTest::qWait(50);

    // A row with a shortcut carries it appended as "\t<combo>" (native-rendered, so a
    // literal platform-specific suffix isn't reliable to match) — startsWith throughout,
    // exactly like openSubByKey above.
    auto has = [](const QSet<QString>& set, const QString& prefix) {
      for (const QString& t : set) if (t.startsWith(prefix)) return true;
      return false;
    };

    QVERIFY2(has(rootTitles, "Fit to Window"), "the context menu never opened");
    QVERIFY2(!has(rootTitles, "Clear All Lines"), "Clear All Lines showed with no lines to clear");

    QVERIFY2(!layoutTitles.isEmpty(), "the Image / Layout submenu never opened");
    // Line-dependent rows are the ones missing here — nothing is drawn yet.
    QVERIFY2(!has(layoutTitles, "Copy Layout JSON"), "Copy Layout showed with no lines to copy");
    QVERIFY2(!has(layoutTitles, "Export Layout JSON"), "Download Layout showed with no lines to download");
    // "Copy Image"/"Download Image" are the SUBMENU-OPENER titles (subMenuIn's own
    // arg) — a different, always-enabled QAction than actCopyImage_/actSaveImage_
    // itself, whose OWN text is the "Current (Tint + Lines/Points)" row nested
    // inside (contextMenuOpensOnEmptyCanvasArea's comment explains the same split).
    // They ride on the image, which this menu proves is there by existing at all.
    QVERIFY2(has(layoutTitles, "Copy Image"), "Copy Image hid with an image loaded");
    QVERIFY2(has(layoutTitles, "Download Image"), "Download Image hid with an image loaded");
    QVERIFY2(has(layoutTitles, "Paste Layout JSON"), "Paste Layout hid with an image loaded");
    // These two need neither an image nor lines — they always show.
    QVERIFY2(has(layoutTitles, "Paste (Image or Layout)"), "Paste Image needs no existing image");
    QVERIFY2(has(layoutTitles, "Import Layout JSON"), "Upload Layout needs no existing lines");
    beat();
  }

  // Browser parity: css/layout.css's ui-shimmer now covers .ctx-item too (support/
  // menuShimmer.hpp is the desktop port) — the same left→right sweep every other
  // shimmered control gets (hoverShimmerAnimates), played on a context-menu ROW.
  void contextMenuRowShimmersOnHover() {
    const auto motion = withMotion();   // the sweep honours motionReduced(), which is on here
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);   // the canvas menu opens for an image, and only then
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    QWidget* viewport = win.findChild<QScrollArea*>()->viewport();
    QVERIFY(viewport);

    bool overlayFound = false, advanced = false;
    qreal p1 = -1.0, p2 = -1.0;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      QWidget* overlay = menu->findChild<QWidget*>("menuShimmerOverlay");
      if (!overlay) { menu->close(); return; }
      overlayFound = true;
      QCOMPARE(overlay->geometry(), menu->rect());  // the band covers the whole menu

      QAction* fit = nullptr;
      QAction* other = nullptr;
      for (QAction* a : menu->actions()) {
        if (a->isSeparator()) continue;
        if (a->text().startsWith("Fit to Window")) fit = a;
        else if (!other) other = a;
      }
      if (!fit || !other) { menu->close(); return; }
      // A freshly-opened QMenu can already be hovering its first row on its own —
      // land on a KNOWN different row first, so the move onto "fit" is a genuine
      // transition (sweep()'s own re-fire guard would no-op a same-row "hover").
      menu->setActiveAction(other);
      menu->setActiveAction(fit);
      p1 = overlay->property("sweepProgress").toReal();
      // Poll rather than a single timed sample: a QVariantAnimation ticks off
      // QMenu::exec()'s own event loop, which paces timers coarser than a normal
      // window's, so the SAME 325ms sweep can take a good deal longer, wall-clock,
      // to visibly move here than it does outside a popup (hoverShimmerAnimates).
      for (int i = 0; i < 400 && !advanced; ++i) {
        QTest::qWait(15);
        p2 = overlay->property("sweepProgress").toReal();
        advanced = p2 > p1 && p1 >= 0.0;
      }
      menu->close();
    });
    QTest::mouseClick(viewport, Qt::RightButton, {}, QPoint(6, 6));
    QTest::qWait(50);

    QVERIFY2(overlayFound, "no shimmer overlay on the context menu");
    QVERIFY2(advanced, qPrintable(QString("row shimmer did not advance (%1 -> %2)")
                                      .arg(p1)
                                      .arg(p2)));
    beat();
  }

  // REGRESSION (user report): MenuHotkeyChips shares ONE rows_ list across the WHOLE
  // recursive wire() tree (root menu + every submenu level), but installPlacer() gives
  // each level its OWN aboutToShow/live-poll calling place() with THAT level's `menu`.
  // Without a "does this row actually belong to `menu`" check, opening ANY submenu
  // (e.g. Style, which carries no hotkey rows of its own) called place(styleMenu),
  // which asked styleMenu->actionGeometry() for a ROOT-level row like "Fit to Window"
  // — got an invalid rect back (that action isn't IN Style's list) — and hid the
  // root's own chip out from under it, even though the root menu was still showing
  // behind it. With several submenu levels each polling on their own timer, no
  // chip anywhere stayed shown long enough to shake, preview, or shimmer.
  void openingASubmenuDoesNotHideTheRootMenusOwnChips() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);   // the canvas menu opens for an image, and only then
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    QWidget* viewport = win.findChild<QScrollArea*>()->viewport();
    QVERIFY(viewport);

    auto openSubByKey = [](QMenu* menu, const QString& title) -> QMenu* {
      QAction* parent = nullptr;
      for (QAction* a : menu->actions())
        if (a->text().startsWith(title)) parent = a;
      if (!parent || !parent->menu()) return nullptr;
      menu->setActiveAction(parent);
      QTest::keyClick(menu, Qt::Key_Right);
      for (int i = 0; i < 100 && !parent->menu()->isVisible(); ++i) QTest::qWait(10);
      return parent->menu()->isVisible() ? parent->menu() : nullptr;
    };
    // Direct children only: findChildren() recurses into the SUBMENUS, whose own
    // chips sit at their y=0 and so intersect the root menu's first row.
    auto fitChip = [](QMenu* menu, QAction* fit) -> stencil::gui::TipBody* {
      const QRect r = menu->actionGeometry(fit);
      for (QLabel* l : menu->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly))
        if (auto* c = dynamic_cast<stencil::gui::TipBody*>(l))
          if (c->geometry().intersects(r)) return c;
      return nullptr;
    };

    bool chippedBeforeSubmenu = false, styleOpened = false, chippedAfterSubmenu = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      QAction* fit = nullptr;
      for (QAction* a : menu->actions())
        if (a->text().startsWith("Fit to Window")) fit = a;
      if (!fit) { menu->close(); return; }
      auto* chip = fitChip(menu, fit);
      chippedBeforeSubmenu = chip && !chip->isHidden();

      QMenu* style = openSubByKey(menu, "Style");
      styleOpened = style != nullptr;
      QTest::qWait(200);   // past a couple of Style's own 120ms live-poll ticks

      chip = fitChip(menu, fit);
      chippedAfterSubmenu = chip && !chip->isHidden();
      if (style) style->close();
      menu->close();
    });
    QTest::mouseClick(viewport, Qt::RightButton, {}, QPoint(6, 6));
    QTest::qWait(50);

    QVERIFY2(chippedBeforeSubmenu, "Fit to Window never carried a chip to begin with");
    QVERIFY2(styleOpened, "the Style submenu never opened");
    QVERIFY2(chippedAfterSubmenu, "Fit to Window's chip was hidden by the Style submenu's own live-poll");
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
    for (QAction* a : want) QVERIFY(win.popoverDialogActions_.contains(a));

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

  // The chat's icon controls shimmer on hover like every other button in the app
  // (browser layout.css shimmers every <button>, chat ones included): the sweep
  // starts on Enter, stops on Leave, and never runs on a disabled control.
  // Checked on the dock's composer + title bar, the per-message "…", and the
  // context-menu panel's composer.
  void chatIconButtonsShimmerOnHover() {
    const auto motion = withMotion();   // the sweep honours motionReduced(), which is on here
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
    // Enough long messages that the transcript really scrolls. Bubbles now
    // stretch to the full cap width once they need to wrap (browser
    // shrink-to-fit parity, applyChatBubbleWidths) rather than Qt's narrower
    // "balanced" wrap, so each one is shorter than it used to be — more
    // turns are needed to still leave a row clipped past the viewport edge.
    for (int i = 0; i < 14; ++i) {
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
    // The jump pills' current box. A row whose "…" would land under them hides it
    // instead (placeChatCardMore's shift-else-hide — the pills are the higher-priority
    // control, checked by chatJumpPillsYieldToTheRowMenu), so the checks below that
    // want a SHOWN "…" must not pick a row sitting in that corner.
    const auto pillsBox = [&] {
      return static_cast<stencil::gui::ChatDock*>(win.chatDock_)->jumpPillsGlobalRect();
    };
    const auto crowdedByPills = [&](const QRect& cardGlobal) {
      const QRect p = pillsBox();
      return p.isValid() && cardGlobal.intersects(p.adjusted(-8, -8, 8, 8));
    };
    // Every surface is checked the same way: park the scroll somewhere in the
    // middle, then take a row clipped at each edge.
    const auto checkSurface = [&](QWidget* host, QScrollArea* scroll, const char* what) {
      QVERIFY2(scroll, what);
      QScrollBar* bar = scroll->verticalScrollBar();
      QVERIFY2(bar->maximum() > 0, qPrintable(QString("%1: the transcript does not scroll")
                                                  .arg(what)));
      const QRect vp = globalRect(scroll->viewport());
      QFrame* clippedTop = nullptr;
      QFrame* clippedBottom = nullptr;
      // A slice tall enough to CARRY the pill (a shorter one deliberately hides
      // it — that rule has its own checks below).
      const int room = 21 + 8;
      // Bubbles that all wrap to the same line count (browser shrink-to-fit
      // parity: every long turn here stretches to the same cap width) land in
      // exact lockstep — a card pitch that divides the viewport height evenly
      // leaves the middle sitting BETWEEN two cards instead of straddling one,
      // or leaves a clipped candidate's slice barely at `room` — just enough to
      // pass the clip check, but too tight to also clear the jump pills sharing
      // that same bottom-right corner (its own, correct, hide rule). Walk scroll
      // positions out from the middle until BOTH clipped rows actually show
      // their "…", not merely until each looks clipped.
      for (int v = bar->maximum() / 2; v <= bar->maximum(); v += 12) {
        bar->setValue(v);
        QTest::qWait(30);
        clippedTop = clippedBottom = nullptr;
        for (QFrame* card : host->findChildren<QFrame*>()) {
          if (!card->property("chatMoreBtn").isValid()) continue;
          const QRect g = globalRect(card);
          const QRect vis = g.intersected(vp);
          if (vis.height() < room) continue;
          if (g.top() < vp.top()) clippedTop = card;
          if (g.bottom() > vp.bottom()) clippedBottom = card;
        }
        if (!clippedTop || !clippedBottom) continue;
        bool bothShow = true;
        for (QFrame* card : {clippedTop, clippedBottom}) {
          hover(card);
          QToolButton* more = moreOf(card);
          if (!more || !more->isVisible()) { bothShow = false; break; }
        }
        if (bothShow) break;
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
      QScrollBar* bar = scroll->verticalScrollBar();
      // This test is about the HORIZONTAL edge (a narrow column's pill hanging off
      // the bubble's side), not vertical clipping — so the row must be FULLY on
      // screen AND clear of the jump pills, each of which legitimately hides the
      // "…" under its own rule, checked elsewhere. A narrow column fits about one
      // row at a time, so each kind is hunted — and checked — at its own scroll
      // position rather than whichever rows happen to share the current one.
      for (const char* kind : {"chatCardUser",         // its "…" hangs off the LEFT
                               "chatCardAssistant"}) { // …and this one's off the RIGHT
        QFrame* card = nullptr;
        for (int v = 0; v <= bar->maximum() && !card; v += 12) {
          bar->setValue(v);
          QTest::qWait(20);
          const QRect seen = globalRect(scroll->viewport());
          for (QFrame* c : host->findChildren<QFrame*>()) {
            if (!c->property("chatMoreBtn").isValid()) continue;
            if (c->objectName() != QLatin1String(kind)) continue;
            const QRect g = globalRect(c);
            if (!seen.contains(g) || crowdedByPills(g)) continue;
            card = c;
            break;
          }
        }
        QVERIFY2(card, qPrintable(QString("%1: no fully visible %2 row to hang a \"…\" off")
                                      .arg(what, kind)));
        const QRect vp = globalRect(scroll->viewport());
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
    // A fully visible row clear of the jump pills always shows it.
    const auto checkSliver = [&](QWidget* host, QScrollArea* scroll, const char* what) {
      QScrollBar* bar = scroll->verticalScrollBar();
      const QRect vp = globalRect(scroll->viewport());
      // Walk the scroll until some row is only a sliver at the viewport's edge.
      QFrame* sliver = nullptr;
      QFrame* whole = nullptr;
      for (int v = 0; v <= bar->maximum() && !sliver; v += 7) {
        bar->setValue(v);
        QTest::qWait(20);
        whole = nullptr;   // only a row fully visible at THIS position counts
        for (QFrame* card : host->findChildren<QFrame*>()) {
          if (!card->property("chatMoreBtn").isValid()) continue;
          const QRect g = globalRect(card);
          const int slice = g.intersected(vp).height();
          if (slice > 2 && slice < 16 && g.height() > 40) sliver = card;
          if (vp.contains(g) && !crowdedByPills(g)) whole = card;
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
    for (int i = 0; i < 14; ++i) {
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
  // The pills are the higher-priority control (user report: they used to vanish
  // under a row's "…", which read as broken scrolling) — they stay up regardless,
  // and the TRIGGER gets out of their way: it shifts clear, or hides if a bubble
  // too short leaves nowhere to shift it to (browser/extension parity).
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
    const auto pillsRect = [&] {
      return QRect(jumps[0]->mapToGlobal(QPoint(0, 0)), jumps[0]->size())
          .united(QRect(jumps[1]->mapToGlobal(QPoint(0, 0)), jumps[1]->size()));
    };

    // Hover the row whose "…" lands in the pills' corner — same real Enter path a
    // cursor takes, so placeChatCardMore runs its actual shift/hide logic.
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
    QVERIFY(more);
    // The pills never stand down for this any more — up before AND after the hover.
    QVERIFY2(jumps[0]->isVisible() && jumps[1]->isVisible(),
             "the pills should still be up before the button reaches them");
    QVERIFY2(jumps[0]->isVisible() && jumps[1]->isVisible(),
             "the jump pills must stay up — the row's trigger yields, not them");
    // The trigger itself either shifted clear of the pills, or — nowhere left in
    // this row's own visible slice to shift it to — hid instead. Either way it
    // must never simply sit ON them (unclickable, two round controls stacked).
    if (more->isVisible()) {
      QVERIFY2(!QRect(more->mapToGlobal(QPoint(0, 0)), more->size()).intersects(pillsRect()),
               "the trigger sat under the pills instead of shifting clear of them");
    }
    // …and forcing it directly onto the pills (bypassing the real placement path,
    // the way a stale position from before a resize might) is corrected on the next
    // real placement pass, never by the pills hiding.
    if (more->isVisible()) {
      more->move(more->parentWidget()->mapFromGlobal(pillsRect().topLeft()));
      win.chatDock_->revalidateMoreButtons();
      QVERIFY2(jumps[0]->isVisible() && jumps[1]->isVisible(), "the pills stayed up");
      if (more->isVisible())
        QVERIFY2(!QRect(more->mapToGlobal(QPoint(0, 0)), more->size()).intersects(pillsRect()),
                 "revalidateMoreButtons must pull the trigger back off the pills");
    }
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

  // Deliberate NON-round-trip: the image filter/tint (and the compare split view,
  // which was never persisted to begin with) must NOT carry over into a freshly
  // reopened desktop app (user report) — unlike everything else a session restores
  // (image, lines, page size, scale, crop, rotation, draw mode), which still does.
  void sessionRestoreDoesNotCarryOverTheFilterOrTint() {
    MainWindow win(nullptr, false);
    win.resize(1200, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);   // a REAL file path — restoreSession needs one to reload from
    QTRY_VERIFY(win.canvas_->hasImage());
    win.applyImageFilter(QStringLiteral("custom"));
    win.applyTintColor(QColor(200, 40, 40));
    QCOMPARE(win.settings_.imageFilter, QStringLiteral("custom"));
    win.saveSessionNow();

    MainWindow win2(nullptr, true);
    QVERIFY(win2.canvas_ && win2.canvas_->hasImage());   // the rest of the session DID restore
    QCOMPARE(win2.settings_.imageFilter, QStringLiteral("none"));
    QCOMPARE(win2.canvas_->imageFilter(), QStringLiteral("none"));
    if (win2.imageFilter_) QCOMPARE(win2.imageFilter_->currentData().toString(), QStringLiteral("none"));
    if (win2.filterColorBtn_) QVERIFY(!win2.filterColorBtn_->isVisible());
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
    dismissModal("OK");
    win.onChatSend("delete the chat del target project");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_COMPARE(int(win.projectList_.size()), before - 1);
    QVERIFY2(!win.findProject(goneId.toStdString()), "the named project must be gone");
    QVERIFY2(win.findProject(keptId.toStdString()), "the other project must remain");

    // clearProjects, confirm DECLINED: nothing removed, the note says so.
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Clearing.\",\"actions\":[{\"op\":\"clearProjects\"}]}"));
    dismissModal("Cancel");
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
    dismissModal("Cancel");
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
    dismissModal("OK");
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
    dismissModal("OK");
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
    dismissModal("Cancel");
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
    dismissModal("OK");   // after the send — the confirm is queued (see above)
    QTRY_VERIFY(!dock->isBusy());
    QTRY_VERIFY2(win.chatHistory_.isEmpty(), "the replay history must clear");
    QTRY_VERIFY2(assistantBubbleTexts(dock).isEmpty(), "the transcript must clear");
    QVERIFY2(win.chatTextOnlyKey_.isEmpty(), "the §7 text-only latch must re-arm");
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY2(pr && pr->chat.isEmpty(), "the §12 persisted copy must clear (§12.2)");
    }

    // Tidy the dev state dir: drop the project this test created.
    dismissModal("OK");
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
    // The fixture is a flat white image; a perfectly uniform snapshot's contour render
    // can coincide with the plain snapshot bit-for-bit (applyContourRGBA maps ANY
    // uniform image to solid white). One line breaks the uniformity so the edge map
    // and the plain snapshot are guaranteed to differ, regardless of that overlap.
    stencil::core::Line line;
    line.color = "#000000";
    line.thickness = 4;
    line.points.push_back({20.0, 20.0});
    line.points.push_back({100.0, 100.0});
    win.canvas_->setLines({line});
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
  // A tail's WIDGET geometry can look perfectly flush (chatSwapSidesReskinsRetroactively
  // checks exactly that) while the pixels underneath still show a gap, if the
  // card's own corner render ever stopped actually being flat under its tail —
  // this checks the rendered PIXEL at that corner, not just the geometry, so a
  // future edit that broke chatCardStyleSheet()'s flattened-corner radii would
  // fail loudly here instead of only failing a user's eyeball (user report: a
  // tail rendering as "a triangle with a visible gap from the message").
  void chatBubbleTailRendersFlushNoGap() {
    // This case PAINTS: it samples the bubble's own corner. With motion on, the card is
    // hidden behind its arrival dust for the whole flight, so the grab caught motes and
    // the corner came back a different blend every run (and a longer chat clock made it
    // reproducible). Pin motion off for it — the flight has its own cases.
    const auto still = withoutMotion();
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    win.chatDock_->appendUser(QStringLiteral("Give me 3 variants"), {});
    win.chatHistory_.append({QStringLiteral("user"), QStringLiteral("Give me 3 variants"), {}});
    win.chatError(QStringLiteral("not connected to http://localhost:8090 (no token)"), QString());
    QTest::qWait(150);
    QFrame* errCard = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardError")) errCard = f;
    QFrame* userCard = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardUser")) userCard = f;
    QVERIFY(errCard && userCard);
    QImage shot = win.chatDock_->grab().toImage();
    // A pixel just inside the card's own flattened corner (well within the
    // round notch a 10px radius would otherwise leave unfilled there) —
    // sampled against a reference pixel a few px further in, which is
    // unambiguously plain bubble fill either way. Equal ⇒ the corner reads as
    // one continuous fill, same as the reference; a regressed (still rounded)
    // corner would instead sample the transcript's own, different background.
    const auto sampleFlushCorner = [&](QFrame* card, bool right, const char* what) {
      const QPoint corner = card->mapTo(win.chatDock_,
          right ? card->rect().bottomRight() : card->rect().bottomLeft());
      const int dx = right ? -1 : 1;
      const QColor atCorner = shot.pixelColor(corner.x() + dx, corner.y() - 1);
      const QColor reference = shot.pixelColor(corner.x() + dx * 6, corner.y() - 6);
      QVERIFY2(atCorner == reference,
               qPrintable(QStringLiteral("%1: corner pixel %2 != interior fill %3 — a gap")
                              .arg(what, atCorner.name(QColor::HexArgb), reference.name(QColor::HexArgb))));
    };
    sampleFlushCorner(errCard, false, "error card (left tail)");
    sampleFlushCorner(userCard, true, "user card (right tail)");
    beat();
  }

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
  void chatJumpArrowsRestTranslucent() {
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
    // The pills rest at 0.7 — the shared figure across the three surfaces (the browser's
    // .chat-jump-btn and every row "…" trigger); hover restores full opacity and
    // brightens the glyph from --text-muted to --text-main.
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
      QCOMPARE(fx->opacity(), 0.7);
      QVERIFY2(glyphIs(b, muted), "the pill's rest glyph is not --text-muted");
      QEvent enter(QEvent::Enter);
      QCoreApplication::sendEvent(b, &enter);
      QCOMPARE(fx->opacity(), 1.0);
      QVERIFY2(glyphIs(b, main), "hover must brighten the glyph to --text-main");
      QEvent leave(QEvent::Leave);
      QCoreApplication::sendEvent(b, &leave);
      QCOMPARE(fx->opacity(), 0.7);
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

  // The checkbox particle toggle and the combo value exchange are installed ONCE, on the
  // application (support/controlSwap.hpp) — no dialog wires its own. What the real window
  // has to prove is that the hook is actually on, that the real toolbar controls got it
  // without a call site, and that both effects converge on the true state and leave the
  // toolbar's geometry exactly where it was.
  void controlSwapsAreInstalledAppWide() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY2(qApp->findChild<QObject*>(
                 QString::fromLatin1(stencil::gui::kControlSwapFilterName),
                 Qt::FindDirectChildrenOnly),
             "MainWindow never installed the app-wide control-swap filter");
    auto* box = win.showPointsCheck_;
    auto* combo = win.pageSize_;
    QVERIFY(box && combo);
    QTRY_VERIFY(box->property(stencil::gui::kControlSwapWiredProperty).toBool());
    QVERIFY2(combo->property(stencil::gui::kControlSwapWiredProperty).toBool(),
             "a toolbar combo built before the window was shown went unwired");

    // This suite runs under STENCIL_NO_ANIM; lift it just here so the REAL motion runs in
    // the REAL window, then put it back for everything after.
    qunsetenv("STENCIL_NO_ANIM");
    const QRect boxGeom = box->geometry();
    const QRect comboGeom = combo->geometry();
    const bool was = box->isChecked();
    box->setChecked(!was);
    QCOMPARE(box->isChecked(), !was);
    QCOMPARE(box->geometry(), boxGeom);
    // Rapid toggling: the last state is the one that survives, with nothing stranded.
    bool last = false;
    for (int i = 0; i < 6; ++i) { last = i % 2 == 0; box->setChecked(last); QTest::qWait(20); }
    QTRY_VERIFY(win.findChildren<QWidget*>(
                       QString::fromLatin1(stencil::gui::kCheckSwapObjectName)).isEmpty());
    QCOMPARE(box->isChecked(), last);
    QCOMPARE(box->geometry(), boxGeom);

    if (combo->count() > 1) {
      const int other = combo->currentIndex() == 0 ? 1 : 0;
      combo->setCurrentIndex(other);
      QCOMPARE(combo->currentIndex(), other);
      QCOMPARE(combo->geometry(), comboGeom);
      QTRY_VERIFY(!stencil::gui::ValueSwapOverlay::running(combo));
      QCOMPARE(combo->currentIndex(), other);
      QVERIFY2(combo->styleSheet().isEmpty(),
               "the swap left its transparent-text override on the combo");
      QCOMPARE(combo->geometry(), comboGeom);
    }

    qputenv("STENCIL_NO_ANIM", "1");
    box->setChecked(was);
    QCOMPARE(box->isChecked(), was);   // reduced motion still changes the state
    QVERIFY(win.findChildren<QWidget*>(
                   QString::fromLatin1(stencil::gui::kCheckSwapObjectName)).isEmpty());
    beat();
  }

  // A dust flight that leaves the host must not be cropped to it (placeForSurface).
  void surfaceDustLayerCoversTheWholeFlightNotJustTheWindow() {
    using stencil::gui::DisintegrateOverlay;
    const QRect host(120, 122, 900, 620);
    const QRect dragged(879, 613, 620, 700);   // Projects dragged past the bottom-right
    const QPoint icon(300, 200);
    const QRect need = DisintegrateOverlay::surfaceLayerRect(dragged, icon);
    QVERIFY2(!host.contains(need), "the host cannot hold the flight — the layer must escape it");
    QVERIFY2(need.contains(dragged), "the layer must cover the window that is coming apart");
    QVERIFY2(need.contains(icon), "…and the point its motes are pouring into");
    QVERIFY2(need.bottom() > host.bottom() && need.right() > host.right(),
             "the cropped-off part is exactly what the layer has to reach");
    QVERIFY2(!host.contains(DisintegrateOverlay::surfaceLayerRect(QRect(260, 82, 620, 700), icon)),
             "a dialog taller than the window needs the escape too");
    QVERIFY2(host.contains(DisintegrateOverlay::surfaceLayerRect(QRect(400, 300, 200, 160), icon)),
             "a flight that fits must not pay for a window of its own");
  }

  // Offscreen's virtual screen is no real desktop, so the layer stays a child there.
  void surfaceDustStaysAChildWhenThereIsNoDesktop() {
    const auto motion = withMotion();
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
    win.actChat_->setChecked(false);
    QTest::qWait(60);
    auto* fx = surfaceFlight(&win);
    QVERIFY2(fx, "the floating chat's flight did not play");
    QVERIFY2(!fx->isWindow(), "offscreen has no desktop to escape onto");
    QCOMPARE(fx->geometry(), win.rect());
  }

  // Canvas scrollbars are invisible at rest, revealed only by an actual pan or zoom — never
  // just from hovering the canvas — and fade back out once the view settles. Direct opacity
  // checks: a real fade plays only off the offscreen platform, but the opacity value itself
  // is plain state either way.
  void canvasScrollbarsHideUntilPanOrZoom() {
    MainWindow win;
    win.resize(600, 500);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(2000, 2000, QImage::Format_RGB32);   // bigger than the viewport at 100%
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    win.refreshActions();
    QVERIFY(win.vScrollOpacity_ && win.hScrollOpacity_);

    win.setZoom(1.0);
    QCOMPARE(win.vScrollOpacity_->opacity(), 1.0);
    QCOMPARE(win.hScrollOpacity_->opacity(), 1.0);
    // Overlay bars, browser-style: they float INSIDE the viewport (which spans the whole
    // area — no gutter reserved beside/below it) and are only there while there's overflow.
    QScrollBar* vbar = win.canvasScrollBar(Qt::Vertical);
    QScrollBar* hbar = win.canvasScrollBar(Qt::Horizontal);
    QVERIFY(vbar->isVisible() && hbar->isVisible());
    QVERIFY(win.scroll_->viewport()->geometry().contains(vbar->geometry()));
    QVERIFY(win.scroll_->viewport()->geometry().contains(hbar->geometry()));
    QCOMPARE(win.scroll_->viewport()->geometry(), win.scroll_->contentsRect());
    QVERIFY(!vbar->testAttribute(Qt::WA_TransparentForMouseEvents));
    // The thumb is a painted pill in the browser's thumb grey (support/pillScrollBars.hpp,
    // like every bar in the app; QSS cannot round a handle on macOS): its top edge's
    // midpoint carries the thumb colour while the slot's corner beside it does not.
    {
      QStyleOptionSlider opt;
      opt.initFrom(vbar);
      opt.orientation = Qt::Vertical;
      opt.minimum = vbar->minimum(); opt.maximum = vbar->maximum();
      opt.sliderPosition = vbar->sliderPosition(); opt.sliderValue = vbar->value();
      opt.pageStep = vbar->pageStep(); opt.singleStep = vbar->singleStep();
      opt.upsideDown = vbar->invertedAppearance();
      const QRect slider = vbar->style()->subControlRect(QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarSlider, vbar);
      QVERIFY(slider.isValid());
      const QImage shot = vbar->grab().toImage();
      const qreal dpr = shot.devicePixelRatio();
      const QColor thumb = stencil::gui::canvasScrollThumb(stencil::gui::resolveDark(win.settings_.themeMode));
      const auto near = [](const QColor& a, const QColor& b) {
        return qAbs(a.red() - b.red()) < 24 && qAbs(a.green() - b.green()) < 24 && qAbs(a.blue() - b.blue()) < 24;
      };
      const QColor mid = shot.pixelColor(QPoint(slider.center().x(), slider.top() + 1) * dpr);
      const QColor corner = shot.pixelColor(QPoint(slider.left(), slider.top()) * dpr);
      QVERIFY2(near(mid, thumb), qPrintable("the thumb's top-edge midpoint is not the thumb grey: " + mid.name()));
      QVERIFY2(!near(corner, thumb), "the thumb's corner is filled — the thumb is not rounded");
      // Thin at rest, a little thicker under the pointer (browser parity: the thumb
      // grows into its slot on hover): a pixel 4px off the slot's centre line is slot
      // background at rest and thumb once the pointer is on the bar.
      // FOUR, not three: the pill is centred on the slot's true half-pixel centre, so at
      // rest (6px) it spans centre−2.5 … centre+3.5 and the column at centre−3 is half
      // covered — an antialiased blend that is neither colour. centre−4 is the first
      // column wholly outside the resting pill, and wholly inside the 9px hover one.
      const QPoint side(slider.center().x() - 4, slider.top() + 6);
      const QColor slot = corner;
      QVERIFY2(near(shot.pixelColor(side * dpr), slot), "the resting thumb is already wide");
      QEnterEvent enter(QPointF(slider.center()), QPointF(vbar->mapTo(&win, slider.center())),
                        QPointF(vbar->mapToGlobal(slider.center())));
      QCoreApplication::sendEvent(vbar, &enter);
      QTest::qWait(300);   // the 150ms swell
      const QImage hot = vbar->grab().toImage();
      QVERIFY2(!near(hot.pixelColor(side * dpr), slot), "the thumb did not swell under the pointer");
      QEvent leave0(QEvent::Leave);
      QCoreApplication::sendEvent(vbar, &leave0);
      QTest::qWait(300);
      QVERIFY2(near(vbar->grab().toImage().pixelColor(side * dpr), slot), "the thumb did not settle back after the pointer left");
      win.scrollbarHovered_ = false;
      win.revealCanvasScrollbars();   // re-arm the reveal our synthetic Leave just cancelled
    }
    QTest::qWait(1200);   // past the 900ms idle timer
    QVERIFY(vbar->testAttribute(Qt::WA_TransparentForMouseEvents));   // hidden = not there
    QCOMPARE(win.vScrollOpacity_->opacity(), 0.0);
    QCOMPARE(win.hScrollOpacity_->opacity(), 0.0);

    // A pan (here: the vertical scrollbar's own value, exactly what a drag-pan/wheel-scroll
    // drives — see MainWindow::scrollTo) reveals it again, and it fades back out the same way.
    win.scroll_->verticalScrollBar()->setValue(50);
    QCOMPARE(win.vScrollOpacity_->opacity(), 1.0);
    QTest::qWait(1200);
    QCOMPARE(win.vScrollOpacity_->opacity(), 0.0);

    // Hovering the bar itself (to grab it) must never let it fade out from under the cursor.
    QEvent enter(QEvent::Enter);
    QCoreApplication::sendEvent(win.canvasScrollBar(Qt::Vertical), &enter);
    QVERIFY(win.scrollbarHovered_);
    QCOMPARE(win.vScrollOpacity_->opacity(), 1.0);
    QTest::qWait(1200);   // would have hidden by now if hovering didn't suppress it
    QCOMPARE(win.vScrollOpacity_->opacity(), 1.0);
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(win.canvasScrollBar(Qt::Vertical), &leave);
    QVERIFY(!win.scrollbarHovered_);
    QTest::qWait(1200);
    QCOMPARE(win.vScrollOpacity_->opacity(), 0.0);
    // Dragging the floating bar drives the real scroll model, and vice versa.
    vbar->setValue(120);
    QCOMPARE(win.scroll_->verticalScrollBar()->value(), 120);
    win.scroll_->verticalScrollBar()->setValue(60);
    QCOMPARE(vbar->value(), 60);
    // Once an axis fits, its bar goes away entirely rather than lingering as a gutter (the
    // test window's viewport is too short for the image at any zoom, so the horizontal bar
    // is the one that fits), and the survivor then runs the viewport's full length.
    win.setZoom(0.1);
    QTRY_VERIFY(!hbar->isVisible());
    QVERIFY(vbar->isVisible());
    QCOMPARE(vbar->height(), win.scroll_->viewport()->height());
  }

  // A repeat of the same message (e.g. pan/zoom's debounced "Saved") landing while the LAST
  // one is still mid-exit used to coexist with it instead of coalescing — liveToasts() only
  // coalesces into a STANDING toast, so the fresh arrival's opaque label buried the leaving
  // one's still-playing dust. Only one "toast" label should ever exist for a given message.

  // REGRESSION: Right on a submenu row opened the flyout and it vanished ~220ms later
  // (or never got past its reveal). Opening from the keyboard makes Qt re-emit hovered()
  // on the parent for a row the pointer never touched, and SubmenuCloseGuard
  // (menuReveal.cpp) armed its close on that. Only pointer-made hovers may arm it.
  void ctxSubmenuOpenedByKeyboardStaysOpen() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    const QPoint at = win.mapToGlobal(QPoint(500, 400));
    QCursor::setPos(at);   // the pointer rests where the menu opens, as after a right-click
    QTest::qWait(100);
    bool opened = false, stillOpen = false, walkedInside = false, leftClosed = false,
         reopened = false, closedByPointer = false, reachedPoints = false, enterClosed = false,
         pointsBefore = false, noFlash = false, enteredRow = false, leftDusted = false;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      QTest::qWait(400);   // let the reveal land
      QAction* layoutAct = nullptr;
      for (QAction* a : root->actions()) if (a->text() == "Image / Layout") layoutAct = a;
      if (!layoutAct || !layoutAct->menu()) { root->close(); return; }
      // Walk down to the row with the keyboard, like a user would, then open it.
      for (int i = 0; i < 12 && root->activeAction() != layoutAct; ++i) {
        QTest::keyClick(root, Qt::Key_Down);
        QTest::qWait(30);
      }
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* layoutMenu = layoutAct->menu();
      // Veiled from its very first frame when the dust reveal is on: a flyout that
      // paints solid for a frame and THEN plays its reveal reads as a flash.
      noFlash = !stencil::support::dustMotionOk() || !layoutMenu->isVisible() ||
                layoutMenu->windowOpacity() < 1.0;
      for (int i = 0; i < 100 && !layoutMenu->isVisible(); ++i) QTest::qWait(10);
      opened = layoutMenu->isVisible();
      QTest::qWait(900);   // well past the guard's 220/480ms grace
      stillOpen = layoutMenu->isVisible();
      // The rest of the walk: a second Right lands on the flyout's first REAL row (not
      // its "IMAGE" title), Down moves on past the title rows, Left closes it back
      // onto the parent row with the root still up, Right reopens it.
      if (QWidget* p = QApplication::activePopupWidget()) QTest::keyClick(p, Qt::Key_Right);
      QTest::qWait(30);
      enteredRow = layoutMenu->activeAction() && layoutMenu->activeAction()->text().startsWith("Copy Image");
      QTest::keyClick(layoutMenu, Qt::Key_Down);
      QTest::qWait(30);
      walkedInside = layoutMenu->activeAction() == win.actPasteImage_;
      // ← folds it with the same dust every other close plays (Qt hides the popup
      // before aboutToHide fires, which used to leave this close with no flight).
      const auto dustSeen = [&win] {
        for (QWidget* w : win.findChildren<QWidget*>(
                 QString::fromLatin1(stencil::gui::DisintegrateOverlay::kObjectName)))
          if (static_cast<stencil::gui::DisintegrateOverlay*>(w)->surfacePicture().isValid()) return true;
        return false;
      };
      QTest::keyClick(layoutMenu, Qt::Key_Left);
      for (int i = 0; i < 100 && layoutMenu->isVisible(); ++i) { QTest::qWait(10); if (dustSeen()) leftDusted = true; }
      if (dustSeen()) leftDusted = true;
      leftClosed = !layoutMenu->isVisible() && root->isVisible() && root->activeAction() == layoutAct;
      QTest::keyClick(root, Qt::Key_Right);
      for (int i = 0; i < 100 && !layoutMenu->isVisible(); ++i) QTest::qWait(10);
      reopened = layoutMenu->isVisible();
      // …while a real pointer move onto another row still closes it (the guard's job).
      QAction* plainRow = nullptr;
      for (QAction* a : root->actions()) {
        if (a->isSeparator() || a->menu() || !a->isEnabled()) continue;
        plainRow = a; break;
      }
      if (plainRow) {
        const QPoint from = root->actionGeometry(layoutAct).center();
        const QPoint to = root->actionGeometry(plainRow).center();
        for (int i = 1; i <= 8; ++i) {
          const QPoint p = from + (to - from) * i / 8;
          QMouseEvent e(QEvent::MouseMove, QPointF(p), QPointF(root->mapToGlobal(p)),
                        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
          QApplication::sendEvent(root, &e);
          QTest::qWait(15);
        }
        for (int i = 0; i < 80 && layoutMenu->isVisible(); ++i) QTest::qWait(10);
        closedByPointer = !layoutMenu->isVisible();
      }
      // Enter picks a row: walk the root to Show Points and toggle it, which also
      // closes the menu (a picked action, not a hosted checkbox row).
      pointsBefore = win.actShowPoints_->isChecked();
      for (int i = 0; i < 24 && root->activeAction() != win.actShowPoints_; ++i) {
        QTest::keyClick(root, Qt::Key_Down);
        QTest::qWait(20);
      }
      reachedPoints = root->activeAction() == win.actShowPoints_;
      QTest::keyClick(root, Qt::Key_Return);
      for (int i = 0; i < 100 && root->isVisible(); ++i) QTest::qWait(10);
      enterClosed = !root->isVisible();
      if (root->isVisible()) root->close();
    });
    win.showContextMenu(at);
    QVERIFY2(opened, "Right on the Image / Layout row did not open its submenu");
    QVERIFY2(stillOpen, "the keyboard-opened submenu closed on its own");
    QVERIFY2(noFlash, "the flyout painted solid before its reveal played");
    QVERIFY2(enteredRow, "the second Right did not land on Copy Image (the first real row)");
    QVERIFY2(walkedInside, "Down from Copy Image did not reach Paste Image");
    QVERIFY2(leftClosed, "Left did not close the flyout back onto its parent row");
    QVERIFY2(!stencil::support::dustMotionOk() || leftDusted, "Left closed the flyout with no dust flight");
    QVERIFY2(reopened, "Right did not reopen the flyout");
    QVERIFY2(closedByPointer, "hovering the pointer onto another row no longer closes it");
    QVERIFY2(reachedPoints, "the keyboard walk never reached Show Points");
    QVERIFY2(enterClosed, "Return did not pick the row and close the menu");
    QCOMPARE(win.actShowPoints_->isChecked(), !pointsBefore);
    QTest::qWait(260);
  }

  // Tab inside a flyout that hosts real controls (Style's spinners here) walks those
  // controls, wrapping, instead of QMenu's default "Tab is ↓" that never reached them
  // (user report). The keyboard-opened submenu is the active popup, so keys go to it.
  void ctxFlyoutTabWalksItsControls() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    const QPoint at = win.mapToGlobal(QPoint(500, 400));
    QCursor::setPos(at);
    QTest::qWait(100);
    bool opened = false, revealedOnly = false, foldedBack = false, wrappedToLastRow = false;
    QWidget *first = nullptr, *second = nullptr, *backAgain = nullptr, *wrapped = nullptr;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      QTest::qWait(400);
      QAction* styleAct = nullptr;
      for (QAction* a : root->actions()) if (a->text() == "Style") styleAct = a;
      if (!styleAct || !styleAct->menu()) { root->close(); return; }
      root->setActiveAction(styleAct);
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* style = styleAct->menu();
      for (int i = 0; i < 100 && !style->isVisible(); ++i) QTest::qWait(10);
      opened = style->isVisible();
      QTest::qWait(60);
      // The first → only revealed it: no control has focus yet, so ← can fold it back.
      QWidget* popup = QApplication::activePopupWidget();
      revealedOnly = QApplication::focusWidget() != win.pointSpin_ && QApplication::focusWidget() != win.thickSpin_;
      QTest::keyClick(popup, Qt::Key_Left);
      for (int i = 0; i < 100 && style->isVisible(); ++i) QTest::qWait(10);
      foldedBack = !style->isVisible() && root->isVisible();
      QTest::keyClick(root, Qt::Key_Right);
      for (int i = 0; i < 100 && !style->isVisible(); ++i) QTest::qWait(10);
      QTest::qWait(60);
      // The second → enters it, onto the first control; Tab walks on from there.
      popup = QApplication::activePopupWidget();
      QTest::keyClick(popup, Qt::Key_Right);
      QTest::qWait(30);
      first = QApplication::focusWidget();
      QTest::keyClick(popup, Qt::Key_Tab);
      QTest::qWait(30);
      second = QApplication::focusWidget();
      QTest::keyClick(popup, Qt::Key_Backtab);
      QTest::qWait(30);
      backAgain = QApplication::focusWidget();
      QTest::keyClick(popup, Qt::Key_Backtab);   // …and off the first control onto the LAST row
      QTest::qWait(30);
      wrapped = QApplication::focusWidget();
      wrappedToLastRow = style->activeAction() == win.actStyleDotted_;
      root->close();
    });
    win.showContextMenu(at);
    QVERIFY2(opened, "Right on the Style row did not open its flyout");
    QVERIFY2(revealedOnly, "the first Right already moved focus into a control");
    QVERIFY2(foldedBack, "Left after the first Right did not fold the flyout back");
    QCOMPARE(first, static_cast<QWidget*>(win.pointSpin_));
    QCOMPARE(second, static_cast<QWidget*>(win.thickSpin_));
    QCOMPARE(backAgain, static_cast<QWidget*>(win.pointSpin_));
    // Shift+Tab off the first control bridges onto the flyout's LAST plain row (Dotted):
    // the keys go back to the menu (no control focused) and ↑/↓ walk the rows from there.
    QVERIFY2(!wrapped || (wrapped != win.pointSpin_ && wrapped != win.thickSpin_),
             "Shift+Tab off the first control left a spinner focused");
    QVERIFY2(wrappedToLastRow, "Shift+Tab off the first control did not land on the last row");
    QTest::qWait(260);
  }

  // The Assistant flyout's own version of the rule above: the first → reveals the chat,
  // the second → lands in its text box (user decision — the chat's input, not its
  // first button), so a reply can be typed without touching the mouse.
  void ctxAssistantFlyoutSecondRightFocusesItsInput() {
    MainWindow win(nullptr, false);
    win.settings_.llmProvider = "ollama";
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    const QPoint at = win.mapToGlobal(QPoint(500, 400));
    QCursor::setPos(at);
    QTest::qWait(100);
    bool opened = false, revealedOnly = false, entered = false;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      QTest::qWait(400);
      QAction* assistAct = nullptr;
      for (QAction* a : root->actions()) if (a->text() == "Assistant") assistAct = a;
      if (!assistAct || !assistAct->menu()) { root->close(); return; }
      root->setActiveAction(assistAct);
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* chat = assistAct->menu();
      for (int i = 0; i < 100 && !chat->isVisible(); ++i) QTest::qWait(10);
      opened = chat->isVisible();
      QTest::qWait(80);
      revealedOnly = QApplication::focusWidget() != win.chatMenuInput_;
      if (QWidget* p = QApplication::activePopupWidget()) QTest::keyClick(p, Qt::Key_Right);
      QTest::qWait(30);
      entered = QApplication::focusWidget() == win.chatMenuInput_;
      root->close();
    });
    win.showContextMenu(at);
    QVERIFY2(opened, "Right on the Assistant row did not open the chat flyout");
    QVERIFY2(revealedOnly, "the first Right already put the caret in the chat input");
    QVERIFY2(entered, "the second Right did not focus the chat input");
    QTest::qWait(260);
  }


  // Radio-style flyouts pick as the keys move (browser parity: arrowing a radio group
  // applies the option at once, menu still open). Image Filter hosts real QRadioButtons:
  // the second → lands on the checked one, ↓/↑ move to the neighbour AND pick it. Style's
  // Solid / Dashed / Dotted are exclusive checkable rows: walking onto one applies it.
  void ctxRadioFlyoutsPickAsTheKeysMove() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    const QPoint at = win.mapToGlobal(QPoint(500, 400));
    QCursor::setPos(at);
    QTest::qWait(100);
    const auto checkedFilter = [&win] {
      for (QAbstractButton* b : win.filterButtons_->buttons())
        if (b->isChecked()) return b->property("filterValue").toString();
      return QString();
    };
    bool filterOpened = false, landedOnChecked = false, downPicked = false, upPicked = false,
         stayedOpen = false, styleOpened = false, styleApplied = false, styleStayedOpen = false,
         kbIconMotion = false, rootRouteEntered = false, rootRoutePicked = false, rootRouteFolded = false;
    QString afterDown, afterUp, afterRootDown;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      QTest::qWait(400);
      auto rowNamed = [&](const QString& title) -> QAction* {
        for (QAction* a : root->actions()) if (a->text().startsWith(title)) return a;
        return nullptr;
      };
      // Walk down from the top of the root to a row (the keys go to the active popup).
      auto walkTo = [&](QAction* act) {
        for (int i = 0; i < 40 && root->activeAction() != act; ++i) {
          QTest::keyClick(root, Qt::Key_Down);
          QTest::qWait(20);
        }
        return root->activeAction() == act;
      };
      QAction* filterAct = rowNamed("Image Filter");
      if (!filterAct || !filterAct->menu() || !walkTo(filterAct)) { root->close(); return; }
      // Landing on a row with the keys is a hover: its icon motion runs (iconMotion.hpp).
      QTest::qWait(60);
      kbIconMotion = stencil::support::motionReduced()
                     || stencil::gui::icm::runnerOfAction(filterAct) != nullptr;
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* filter = filterAct->menu();
      for (int i = 0; i < 100 && !filter->isVisible(); ++i) QTest::qWait(10);
      filterOpened = filter->isVisible();
      QTest::qWait(600);
      if (QWidget* p = QApplication::activePopupWidget()) QTest::keyClick(p, Qt::Key_Right);   // enter
      QTest::qWait(50);
      auto* focused = qobject_cast<QRadioButton*>(QApplication::focusWidget());
      landedOnChecked = focused && focused->isChecked() && checkedFilter() == "none";
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
      QTest::qWait(80);
      afterDown = checkedFilter();
      downPicked = afterDown == "bw" && qobject_cast<QRadioButton*>(QApplication::focusWidget())
                   && qobject_cast<QRadioButton*>(QApplication::focusWidget())->isChecked();
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
      QTest::qWait(80);
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Up);
      QTest::qWait(80);
      afterUp = checkedFilter();
      upPicked = afterUp == "bw";
      stayedOpen = filter->isVisible() && root->isVisible();
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Left);
      for (int i = 0; i < 100 && filter->isVisible(); ++i) QTest::qWait(10);

      // The other route: a flyout opened the way a HOVER opens it leaves the keyboard
      // with the root. Its keys must still reach the focused radio (stayOpenMenu.cpp
      // forwards them), so → enters and ↓ picks exactly as above.
      root->setActiveAction(filterAct);
      for (int i = 0; i < 100 && !filter->isVisible(); ++i) QTest::qWait(10);
      QTest::qWait(600);
      QTest::keyClick(root, Qt::Key_Right);
      QTest::qWait(50);
      rootRouteEntered = qobject_cast<QRadioButton*>(QApplication::focusWidget()) != nullptr;
      QTest::keyClick(root, Qt::Key_Down);
      QTest::qWait(80);
      afterRootDown = checkedFilter();
      rootRoutePicked = afterRootDown == "sepia";
      QTest::keyClick(root, Qt::Key_Left);
      for (int i = 0; i < 100 && filter->isVisible(); ++i) QTest::qWait(10);
      rootRouteFolded = !filter->isVisible() && root->isVisible();

      // Style: reveal it, enter it (the point-size spinner), Tab past both spinners
      // onto its first plain row, then walk the rows — landing on Dashed applies it.
      // Keys go where the platform sends them: the popup's focus widget if it has one.
      auto keyTo = [](Qt::Key k) {
        QWidget* popup = QApplication::activePopupWidget();
        QWidget* receiver = popup && popup->focusWidget() ? popup->focusWidget() : popup;
        QTest::keyClick(receiver, k);
      };
      QAction* styleAct = rowNamed("Style");
      if (!styleAct || !styleAct->menu()) { root->close(); return; }
      for (int i = 0; i < 40 && root->activeAction() != styleAct; ++i) {
        QTest::keyClick(root, Qt::Key_Up);
        QTest::qWait(20);
      }
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* style = styleAct->menu();
      for (int i = 0; i < 100 && !style->isVisible(); ++i) QTest::qWait(10);
      styleOpened = style->isVisible();
      QTest::qWait(600);
      keyTo(Qt::Key_Right);   // enter: the point-size spinner
      QTest::qWait(40);
      keyTo(Qt::Key_Tab);     // thickness
      QTest::qWait(40);
      keyTo(Qt::Key_Tab);     // off the last control → the first plain row (Solid)
      QTest::qWait(40);
      for (int i = 0; i < 6 && style->activeAction() != win.actStyleDashed_; ++i) {
        keyTo(Qt::Key_Down);
        QTest::qWait(30);
      }
      styleApplied = style->activeAction() == win.actStyleDashed_ && win.actStyleDashed_->isChecked()
                     && win.settings_.defaultStyle == "dashed";
      styleStayedOpen = style->isVisible() && root->isVisible();
      root->close();
    });
    win.showContextMenu(at);
    QVERIFY2(filterOpened, "Right on the Image Filter row did not open its flyout");
    QVERIFY2(landedOnChecked, "the second Right did not land on the checked radio (None)");
    QVERIFY2(downPicked, qPrintable("Down did not pick the next filter — checked: " + afterDown));
    QVERIFY2(upPicked, qPrintable("Up did not pick the previous filter — checked: " + afterUp));
    QVERIFY2(stayedOpen, "picking a filter with the arrows closed the menu");
    QVERIFY2(kbIconMotion, "landing on a row with the keys did not run its icon motion");
    QVERIFY2(rootRouteEntered, "root-held keys: Right did not focus a radio in the hover-opened flyout");
    QVERIFY2(rootRoutePicked, qPrintable("root-held keys: Down did not pick the next filter — checked: " + afterRootDown));
    QVERIFY2(rootRouteFolded, "root-held keys: Left did not fold the flyout");
    QVERIFY2(styleOpened, "Right on the Style row did not open its flyout");
    QVERIFY2(styleApplied, "walking onto Dashed did not apply the dashed style");
    QVERIFY2(styleStayedOpen, "applying a style with the arrows closed the menu");
    win.applyImageFilter("none");
    QTest::qWait(260);
  }


  // The Custom Tint pick shows the "Tint Color…" row at once, and moving off it hides
  // the row again — while the flyout is open (browser parity: .ctx-tint-visible follows
  // the radio change), not only on the next open.
  void ctxCustomTintRowFollowsTheFilterPick() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    const QPoint at = win.mapToGlobal(QPoint(500, 400));
    QCursor::setPos(at);
    QTest::qWait(100);
    bool opened = false, hiddenAtStart = false, shownOnCustom = false, rowLaidOut = false, hiddenAgain = false;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      QTest::qWait(400);
      QAction* filterAct = nullptr;
      for (QAction* a : root->actions()) if (a->text().startsWith("Image Filter")) filterAct = a;
      if (!filterAct || !filterAct->menu()) { root->close(); return; }
      for (int i = 0; i < 40 && root->activeAction() != filterAct; ++i) { QTest::keyClick(root, Qt::Key_Down); QTest::qWait(20); }
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* filter = filterAct->menu();
      for (int i = 0; i < 100 && !filter->isVisible(); ++i) QTest::qWait(10);
      opened = filter->isVisible();
      QTest::qWait(600);
      hiddenAtStart = !win.tintColorAction_->isVisible();
      if (QWidget* p = QApplication::activePopupWidget()) QTest::keyClick(p, Qt::Key_Right);   // enter: None
      QTest::qWait(50);
      for (int i = 0; i < 5; ++i) { QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down); QTest::qWait(60); }
      QTest::qWait(100);
      shownOnCustom = win.settings_.imageFilter == "custom" && win.tintColorAction_->isVisible();
      rowLaidOut = filter->actionGeometry(win.tintColorAction_).isValid()
                   && filter->height() >= filter->actionGeometry(win.tintColorAction_).bottom();
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Up);
      QTest::qWait(100);
      hiddenAgain = win.settings_.imageFilter == "contour" && !win.tintColorAction_->isVisible();
      root->close();
    });
    win.showContextMenu(at);
    QVERIFY2(opened, "Right on the Image Filter row did not open its flyout");
    QVERIFY2(hiddenAtStart, "the tint row was showing with no custom filter active");
    QVERIFY2(shownOnCustom, "picking Custom Tint did not show the tint row");
    QVERIFY2(rowLaidOut, "the tint row is visible but the flyout did not make room for it");
    QVERIFY2(hiddenAgain, "moving off Custom Tint did not hide the tint row");
    win.applyImageFilter("none");
    QTest::qWait(260);

    // Against the screen's bottom edge: the flyout that grows for the tint row must be
    // re-placed to stay on screen, or the new row lands below it, never seen.
    const QRect avail = win.screen()->availableGeometry();
    win.move(avail.left() + 40, avail.bottom() - win.height() - 10);
    QTest::qWait(200);
    const QPoint low = win.mapToGlobal(QPoint(500, win.height() - 60));
    QCursor::setPos(low);
    QTest::qWait(100);
    bool lowOpened = false, onScreen = false, rowOnScreen = false;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      QTest::qWait(400);
      QAction* filterAct = nullptr;
      for (QAction* a : root->actions()) if (a->text().startsWith("Image Filter")) filterAct = a;
      if (!filterAct || !filterAct->menu()) { root->close(); return; }
      root->setActiveAction(filterAct);   // hover-style open
      QMenu* filter = filterAct->menu();
      for (int i = 0; i < 100 && !filter->isVisible(); ++i) QTest::qWait(10);
      lowOpened = filter->isVisible();
      QTest::qWait(600);
      QAbstractButton* custom = nullptr;
      for (QAbstractButton* b : win.filterButtons_->buttons()) if (b->property("filterValue") == "custom") custom = b;
      QTest::mouseClick(custom, Qt::LeftButton, Qt::NoModifier, custom->rect().center());
      QTest::qWait(300);
      onScreen = avail.contains(filter->geometry());
      const QRect row = filter->actionGeometry(win.tintColorAction_);
      rowOnScreen = row.isValid() && avail.contains(QRect(filter->mapToGlobal(row.topLeft()), row.size()));
      root->close();
    });
    win.showContextMenu(low);
    QVERIFY2(lowOpened, "the low flyout did not open");
    QVERIFY2(onScreen, "the flyout grew off the bottom of the screen");
    QVERIFY2(rowOnScreen, "the tint row landed off screen");
    win.applyImageFilter("none");
    QTest::qWait(260);
  }


  // A flyout the first → only revealed still belongs to the parent's walk: ↓ moves the
  // ROOT highlight and folds the flyout (browser parity), and only after the second →
  // do ↑/↓ work inside it. Before, Qt walked the revealed flyout's rows, which for the
  // radio flyout read as "the keys only move the radio focus" (user report).
  void ctxRevealedFlyoutArrowsWalkTheParent() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    const QPoint at = win.mapToGlobal(QPoint(500, 400));
    QCursor::setPos(at);
    QTest::qWait(100);
    bool revealed = false, downFolded = false, upBack = false, reRevealed = false, enteredPick = false;
    QString rootAfterDown;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      QTest::qWait(400);
      QAction* filterAct = nullptr;
      QAction* transformAct = nullptr;
      for (QAction* a : root->actions()) {
        if (a->text().startsWith("Image Filter")) filterAct = a;
        if (a->text().startsWith("Transformation")) transformAct = a;
      }
      if (!filterAct || !filterAct->menu() || !transformAct) { root->close(); return; }
      // keyClick derefs its receiver (QTEST_ASSERT is compiled out in Release), so a chain
      // that has already closed segfaults the whole binary and takes every later case with
      // it. Bail out instead and let the QVERIFY2s below report the miss.
      auto toPopup = [](Qt::Key k) {
        QWidget* p = QApplication::activePopupWidget();
        if (p) QTest::keyClick(p, k);
        return p;
      };
      for (int i = 0; i < 40 && root->activeAction() != filterAct; ++i) { QTest::keyClick(root, Qt::Key_Down); QTest::qWait(20); }
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* filter = filterAct->menu();
      for (int i = 0; i < 100 && !filter->isVisible(); ++i) QTest::qWait(10);
      revealed = filter->isVisible();
      QTest::qWait(600);
      // ↓ while only revealed: the parent walks on (to Transformation) and the flyout folds.
      if (!toPopup(Qt::Key_Down)) { root->close(); return; }
      for (int i = 0; i < 100 && filter->isVisible(); ++i) QTest::qWait(10);
      rootAfterDown = root->activeAction() ? root->activeAction()->text() : QString();
      downFolded = !filter->isVisible() && root->activeAction() == transformAct;
      if (!toPopup(Qt::Key_Up)) { root->close(); return; }
      QTest::qWait(60);
      upBack = root->activeAction() == filterAct && !filter->isVisible();
      // → reveals again, a second → enters, and now ↓ picks inside.
      if (!toPopup(Qt::Key_Right)) { root->close(); return; }
      for (int i = 0; i < 100 && !filter->isVisible(); ++i) QTest::qWait(10);
      reRevealed = filter->isVisible();
      QTest::qWait(600);
      QWidget* popup = toPopup(Qt::Key_Right);
      if (!popup) { root->close(); return; }
      QTest::qWait(50);
      // Keys go where the platform sends them: the popup's focus widget (the radio).
      popup = QApplication::activePopupWidget();
      if (!popup) { root->close(); return; }
      QWidget* focused = popup->focusWidget();
      QTest::keyClick(focused ? focused : popup, Qt::Key_Down);
      QTest::qWait(80);
      enteredPick = win.settings_.imageFilter == "bw" && filter->isVisible();
      root->close();
    });
    win.showContextMenu(at);
    QVERIFY2(revealed, "Right on the Image Filter row did not reveal its flyout");
    QVERIFY2(downFolded, qPrintable("Down on a revealed flyout did not walk the parent on and fold it — root row: " + rootAfterDown));
    QVERIFY2(upBack, "Up did not walk the parent back to Image Filter");
    QVERIFY2(reRevealed, "Right did not reveal the flyout again");
    QVERIFY2(enteredPick, "after the second Right, Down did not pick inside the flyout");
    win.applyImageFilter("none");
    QTest::qWait(260);
  }

  void repeatedToastReplacesAStillLeavingOne() {
    QWidget host;
    host.resize(600, 420);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    stencil::gui::Notifications toasts(&host);
    toasts.show("Saved", stencil::gui::Notifications::Level::Success, /*msec=*/50);
    QTest::qWait(70);   // its life timer fires -> dismiss() -> mid-way through the 160ms fadeOut
    toasts.show("Saved", stencil::gui::Notifications::Level::Success, /*msec=*/3000);
    QTest::qWait(10);
    const auto ts = host.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly);
    QCOMPARE(ts.size(), 1);
    QCOMPARE(ts.first()->property("stencilToastText").toString(), QString("Saved"));
    QVERIFY2(!ts.first()->property("stencilToastLeaving").toBool(),
             "the survivor is the stale leaving one, not the fresh arrival");
  }

  // ── A motion mode changed WHILE a window is up governs how that window LEAVES ──
  // The Visuals & Settings dialog live-applies its own Motion rows (support/motionPrefs.hpp),
  // so switching to "None" in it and closing it must not leave that very window still flying
  // back into its icon — and switching motion back ON must give it the closing flight its
  // open never installed. The close flight therefore asks the mode when it PLAYS, not when
  // it was hung on the dialog (support/modalReveal.cpp CloseFlight::fly).
  void dialogCloseAsksTheMotionModeAgainOnItsWayOut() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    // Any flight at all — the dust cloud or the ghost it falls back to.
    struct FlightSpy : QObject {
      bool seen = false;
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Show) {
          auto* w = qobject_cast<QWidget*>(o);
          if (w && (w->objectName()
                        == QLatin1String(stencil::gui::DisintegrateOverlay::kObjectName)
                    || w->objectName() == QLatin1String("stencilModalGhost")))
            seen = true;
        }
        return false;
      }
    };

    const auto flewOnClose = [&](stencil::support::MotionMode openMode,
                                 stencil::support::MotionMode closeMode) {
      stencil::support::setMotionMode(openMode);
      QDialog dlg(&win);
      dlg.resize(260, 180);
      stencil::support::revealDialog(dlg, nullptr, QRect(40, 40, 26, 26));
      dlg.show();
      QTest::qWait(80);            // the open flight, whichever mode allowed it
      stencil::support::setMotionMode(closeMode);   // …the user moves the setting…
      FlightSpy spy;
      qApp->installEventFilter(&spy);
      dlg.hide();                  // …and closes the window
      QTest::qWait(30);
      qApp->removeEventFilter(&spy);
      return spy.seen;
    };

    QVERIFY2(!flewOnClose(stencil::support::MotionMode::Particles,
                          stencil::support::MotionMode::None),
             "motion turned OFF while the window was up: it must leave without a flight");
    QVERIFY2(flewOnClose(stencil::support::MotionMode::None,
                         stencil::support::MotionMode::Particles),
             "motion turned ON while the window was up: it must leave WITH one");
    stencil::support::setMotionMode(stencil::support::MotionMode::Particles);
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
    const int custom = win.pageSize_->findData(QStringLiteral("custom"));
    QVERIFY(custom >= 0);
    const int a3 = win.pageSize_->findData(QStringLiteral("A3"));
    QVERIFY(a3 >= 0);
    win.pageSize_->setCurrentIndex(a3);   // the everyday state, whatever the settings hold
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
    win.pageSize_->setCurrentIndex(custom);
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
    win.pageSize_->setCurrentIndex(a3);   // this suite shares the real settings file
  }

  // The rename ✓/✗ slide their slots open by animating maximumWidth, so the layout's own
  // cap is parked while that runs (controlReveal parkMaxWidth). Read back off the live
  // value instead, it ratcheted down on every interrupted swap until the pair was slivers.
  void nameChipsSurviveRenamesCutShortMidSlide() {
    MainWindow win(nullptr, false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(png_);
    QTest::qWait(200);
    const auto motion = withMotion();   // the slide is the whole point here
    const int box = win.projectNameAccept_->maximumWidth();
    QVERIFY(box > 20);
    for (int i = 0; i < 6; ++i) {   // in and straight back out, mid-slide every time
      win.enterNameEdit();
      QTest::qWait(60);
      win.cancelProjectName();
      QTest::qWait(60);
    }
    win.enterNameEdit();
    QTRY_COMPARE(win.projectNameAccept_->width(), box);
    QCOMPARE(win.projectNameCancel_->width(), box);
    QVERIFY2(!win.projectNameAccept_->icon().isNull(), "the tick lost its glyph");
    win.cancelProjectName();
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
#include "mainWindow.gui.moc"
