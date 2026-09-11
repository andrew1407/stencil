#pragma once
// Shared ground for the MainWindow GUI suites (tests/mainWindow.*.gui.cpp): everything
// they include, plus the app-level helpers — boot a loaded window, run or pin motion,
// answer a modal. The per-area suites carry the cases themselves.
#include "mainWindow.hpp"
#include "support/browserCopy.hpp"
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
#include "../src/support/scrollReveal.hpp"
#include "fileStore.hpp"
#include "connectDialog.hpp"
#include "../src/support/appTooltip.hpp"
#include "../src/support/shareImage.hpp"   // shareSheetAvailable — the Share button's gate
#include "serverClient.hpp"
#include "llmSettingsForm.hpp"
#include "../src/app/chatPlanTarget.hpp"   // §10 chatPanel: the plan target that places the dock
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
#include "mainWindowFlight.gui.hpp"
#include "mainWindowChat.gui.hpp"

namespace stencil::guitest {
  // Comfortably past the f(x,y) idle-commit delay (mainWindow.cpp kFormulaCommitMs), so a
  // "stopped typing" wait can't race the timer on a loaded machine.
  inline constexpr int kFormulaSettleMs = 1600;

  // The shared box of the panel's two collapse chevrons (mainWindow kPanelToggleBox /
  // selectionPanel kToggleBox) — asserted equal so the pair can't drift apart.
  inline constexpr int kPanelChevronBox = 24;

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

  // A confirmation is a modal dialog that blocks the triggering call (Quit, Delete Project
  // File, …) — a QMessageBox, or the chrome-styled confirmModal (modalChrome.cpp) the
  // projects flows use. Arm this BEFORE triggering the action: it polls until a modal
  // CARRYING the named button appears (an unrelated modal — e.g. the projects dialog the
  // question will sit over — is skipped, not clicked) and clicks it, letting the
  // otherwise-blocked trigger() return with that answer.
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

  // The image every case opens: a generous album-orientation PNG (larger than the tiny
  // shared fixture) so the fit-scaled canvas has room for well-separated draw clicks.
  inline QString& guiTestImage() {
    static QString path;
    return path;
  }

  // Shared initTestCase body. The quit-confirmation case closes its window; keep that
  // from ending the shared QApplication (and the rest of the run) with it.
  inline void prepareGuiTestCase() {
    qApp->setQuitOnLastWindowClosed(false);
    QImage img(240, 160, QImage::Format_RGB32);
    img.fill(Qt::white);
    guiTestImage() = QDir::temp().filePath(QStringLiteral("stencil_e2e_input.png"));
    QVERIFY(img.save(guiTestImage(), "PNG"));
  }

  // The suite runs with STENCIL_NO_ANIM=1, but a case that turns motion on can leave it
  // on for whatever runs next, and then a fixed wait races a flight. Pins it off for the
  // caller's own scope.
  [[nodiscard]] inline auto withoutMotion() {
    const QByteArray had = qgetenv("STENCIL_NO_ANIM");
    qputenv("STENCIL_NO_ANIM", "1");
    return qScopeGuard([had] {
      if (had.isEmpty()) qunsetenv("STENCIL_NO_ANIM"); else qputenv("STENCIL_NO_ANIM", had);
    });
  }

  // The counterpart: a case that drives real motion turns the suite's pin off for as
  // long as it holds the returned guard.
  [[nodiscard]] inline auto withMotion() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    return qScopeGuard([noAnim] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
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

