#pragma once
// Shared ground for the MainWindow GUI suites (tests/MainWindow.*.gui.cpp): everything
// they include, plus the app-level helpers — boot a loaded window, run or pin motion,
// answer a modal. The per-area suites carry the cases themselves.
#include "MainWindow.hpp"
#include "support/browserCopy.hpp"
#include "../src/support/dust/ThemeSwapOverlay.hpp"
#include "../src/support/notify/Notifications.hpp"
#include "../src/support/motion/DisintegrateOverlay.hpp"
#include "../src/support/dockGrip.hpp"   // DockEdgeOverlay: the chat dock's resize-edge tint
#include "../src/canvas/overlay/DropZonesOverlay.hpp"
#include "../src/canvas/CanvasTooltip.hpp"
#include "CanvasWidget.hpp"
#include "ChatDock.hpp"
#include "../src/app/meta/DataExportController.hpp"
#include "PillSplitter.hpp"
#include "../src/app/selection/SelectionPanel.hpp"
#include "../src/app/selection/SelectedLineBar.hpp"
#include "../src/support/motion/scrollReveal.hpp"
#include "fileStore.hpp"
#include "ConnectDialog.hpp"
#include "../src/support/tip/AppTooltip.hpp"
#include "../src/support/share/shareImage.hpp"   // isShareSheetAvailable — the Share button's gate
#include "ServerClient.hpp"
#include "LlmSettingsForm.hpp"
#include "../src/app/chat/planTarget/ChatPlanTarget.hpp"   // §10 chatPanel: the plan target that places the dock
#include "../src/llm/plan/opPlan.hpp"
#include "../src/llm/plan/executor/planExecutor.hpp"
#include "MediaLoader.hpp"
#include "popover.hpp"
#include "iconSet.hpp"
#include "faceSwap.hpp"
#include "controlSwap.hpp"
#include "iconMotion.hpp"
#include "guiHelpers.hpp"
#include <QStyleFactory>
#include "theme.hpp"
#include "modalReveal.hpp"
#include "SettingsDialog.hpp"
#include "../src/app/mainWindowHelpers.hpp"   // NAME_CHIP_BOX
#include "../src/support/menu/SearchCombo.hpp"
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
#include "../src/support/tip/tipContent.hpp"
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
#include <QWidgetAction>
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
#include <QTimer>
#include <QToolBar>
#include <QVariantAnimation>
#include <memory>

#include "../src/canvas/overlay/IncognitoOverlay.hpp"

using stencil::gui::MainWindow;
using stencil::gui::CanvasWidget;
using stencil::gui::ThemeSwapOverlay;
using stencil::gui::Settings;
#include "MainWindowFlight.gui.hpp"
#include "MainWindowChat.gui.hpp"

namespace stencil::guitest {
  // Find a shared QAction by its visible label (the menu bar, toolbar, and context menu
  // all reuse the same QAction objects, so this reaches the real UI wiring).
  inline QAction* actionByText(const QWidget* w, const QString& text) {
    for (QAction* a : w->findChildren<QAction*>())
      if (a->text().compare(text, Qt::CaseInsensitive) == 0) return a;
    return nullptr;
  }

  // Total points across all lines (committed + in-progress), for draw/undo assertions.
  inline int totalPoints(const CanvasWidget* c) {
    int n = 0;
    for (const auto& line : c->allLines()) n += static_cast<int>(line.points.size());
    return n;
  }

  // Optional watch-along pause: set STENCIL_GUI_SLOWMO=<ms> and run headed (no
  // QT_QPA_PLATFORM=offscreen) to actually see each step. No effect in CI (unset → 0).
  inline void beat() {
    bool ok = false;
    const int ms = qEnvironmentVariableIntValue("STENCIL_GUI_SLOWMO", &ok);
    if (ok && ms > 0) QTest::qWait(ms);
  }

  // Arm BEFORE triggering an action that blocks on a confirmation: it polls until a modal CARRYING
  // that button appears — an unrelated modal it may sit over is skipped, not clicked — and answers it.
  inline void dismissModal(const QString& buttonText) {
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

  // The image every case opens: album-orientation and larger than the tiny shared
  // fixture, so the fit-scaled canvas has room for well-separated draw clicks.
  inline QString& guiTestImage() {
    static QString path;
    return path;
  }

  // Shared initTestCase body. The quit-confirmation case closes its window; keep that
  // from ending the shared QApplication with it.
  inline void prepareGuiTestCase() {
    qApp->setQuitOnLastWindowClosed(false);
    // Write the defaults out before any case runs: a state dir that has never been written leaves every
    // unset key at whatever the loader defaults to THIS run, so two runs of the binary differ.
    stencil::gui::fileStore::saveSettings(stencil::gui::fileStore::loadSettings());
    QImage img(240, 160, QImage::Format_RGB32);
    img.fill(Qt::white);
    guiTestImage() = QDir::temp().filePath(QStringLiteral("stencil_e2e_input.png"));
    QVERIFY(img.save(guiTestImage(), "PNG"));
  }

  // The suite runs with STENCIL_NO_ANIM=1, and a case that moves that pin either way must put back
  // what it found, or the next case's waits race a flight. One guard for both directions.
  inline auto motionPinned(bool on) {
    const QByteArray had = qgetenv("STENCIL_NO_ANIM");
    if (on) qunsetenv("STENCIL_NO_ANIM"); else qputenv("STENCIL_NO_ANIM", "1");
    return qScopeGuard([had] {
      if (had.isEmpty()) qunsetenv("STENCIL_NO_ANIM"); else qputenv("STENCIL_NO_ANIM", had);
    });
  }

  inline auto withoutMotion() { return motionPinned(false); }
  inline auto withMotion() { return motionPinned(true); }

  // Poll until `cond` holds, giving up after `capMs`: the early-exit stand-in for a fixed
  // sleep. Most callers pin motion off, so the first look usually already answers.
  template <class F>
  void settle(F cond, int capMs = 1000) { (void)QTest::qWaitFor(cond, capMs); }

  // After clearImage() the "＋ Blank image" card is held back while the clear dust flies,
  // and PAINTING is what records its rect — so each look repaints. Polls the hold out.
  inline bool waitForIdleCard(MainWindow& win, int capMs = 2200) {
    CanvasWidget* canvas = win.findChild<CanvasWidget*>();
    QElapsedTimer t;
    t.start();
    for (;;) {
      canvas->grab();
      if (canvas->idleCardGlobalRect().isValid()) return true;
      if (t.elapsed() >= capMs) return false;
      QTest::qWait(40);
    }
  }

  // Build a shown MainWindow with our test image loaded; returns its live canvas.
  inline CanvasWidget* openLoaded(MainWindow& win) {
    win.resize(1000, 760);
    win.show();
    win.raise();
    win.activateWindow();
    beat();                       // (watch mode) empty editor
    win.openPathFromOS(guiTestImage());
    return win.findChild<CanvasWidget*>();
  }
}  // namespace stencil::guitest

using namespace stencil::guitest;

