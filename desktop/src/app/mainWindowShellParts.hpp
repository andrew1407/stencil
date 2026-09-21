#pragma once
// The shell TUs' shared ground: the include block MainWindow.cpp's own group all pay for, plus the
// modal chord-forwarding helper and the popover's motion. Private to the MainWindow shell TUs.
#include "MainWindow.hpp"
#include "ChatDock.hpp"
#include "StayOpenMenu.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "DockZonesOverlay.hpp"
#include "PillSplitter.hpp"
#include "colorNames.hpp"
#include "ChatMenuPanel.hpp"
#include "opPlan.hpp"
#include "planExecutor.hpp"
#include "popover.hpp"
#include "QtLlmTransport.hpp"
#include "deepLink.hpp"
#include "fetchGuard.hpp"
#include "displayName.hpp"
#include "OpenImageDialog.hpp"
#include "../support/localPath.hpp"
#include "../support/rowWork.hpp"    // support::forEachSlice() — parallel thumb decode
#include "OpenInDialog.hpp"
#include "CanvasTooltip.hpp"
#include "CanvasWidget.hpp"
#include "OverlayScrollArea.hpp"
#include "DropZonesOverlay.hpp"
#include "IncognitoOverlay.hpp"
#include "ProjectDragZones.hpp"
#include "cropGeometry.hpp"
#include "imageFilter.hpp"
#include "pageMetrics.hpp"
#include "CropDialog.hpp"
#include "tooltipRows.hpp"
#include "zoomPan.hpp"
#include "guiHelpers.hpp"
#include "MenuHotkeys.hpp"
#include "menuReveal.hpp"
#include "MenuShimmer.hpp"
#include "modalReveal.hpp"
#include "SearchCombo.hpp"
#include "ControlsPill.hpp"
#include "iconSet.hpp"
#include "numericInput.hpp"
#include "InfoDialog.hpp"
#include "launchOptions.hpp"
#include "LinksDialog.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "ConnectDialog.hpp"
#include "connectionStore.hpp"
#include "DataExportController.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"
#include "ProjectTransferController.hpp"
#include "LiveFeed.hpp"
#include "ServerClient.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "AssistantSettingsDialog.hpp"
#include "SettingsDialog.hpp"
#include "ShortcutsDialog.hpp"
#include "theme.hpp"
#include "mainWindowShared.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QBuffer>
#include <QCryptographicHash>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QEasingCurve>
#include <QEventLoop>
#include <QGraphicsOpacityEffect>

#include "../support/dust/ThemeSwapOverlay.hpp"  // palette-swap wipe
#include "../support/tip/AppTooltip.hpp"           // the fading control tooltip
#include "../support/motion/DisintegrateOverlay.hpp"  // the canvas scatters when cleared
#include "../support/control/swap/controlSwap.hpp"         // checkbox particles + combo value swap
#include "../support/control/reveal/controlReveal.hpp"        // a group of fields comes and goes as sand
#include "../support/control/WrapRow.hpp"               // the tool rows wrap, so their height follows the width
#include "../support/dockGrip.hpp"             // animated canvas↔panel separator grip
#include "../support/modal/modalChrome.hpp"          // confirmModal — the browser-styled question
#include "../support/icon/iconMotion.hpp"          // the per-icon hover motion
#include "../support/motion/HoverSlide.hpp"          // the row hover slide (browser translateX)
#include "../support/motion/ShimmerOverlay.hpp"      // the shared hover sweep
#include <QHBoxLayout>
#include <QLayout>
#include <QVBoxLayout>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QShortcut>
#include <QShowEvent>
#include <QVariantAnimation>
#include <QIcon>
#include <QImage>
#include <QVariant>
#include <QImageReader>
#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QSplitter>
#include <QSplitterHandle>
#include <QPainter>
#include <QPaintEvent>
#include <QAbstractItemView>
#include <QAbstractButton>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <QStyleHints>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QCloseEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QGridLayout>
#include <QKeyEvent>
#include <QNativeGestureEvent>
#include <QWheelEvent>
#include <QKeySequence>
#include <QPalette>
#include <QRandomGenerator>
#include <QDockWidget>
#include <QScrollArea>
#include <QShortcut>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QStyle>
#include <QToolButton>
#include <QFontDatabase>
#include <QLabel>
#include <QToolTip>
#include <QWidgetAction>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>

namespace stencil::gui {

  namespace {
    // A modal dialog runs its own event loop, so the main window's QActions never fire there: it carries copies of those
    // chords while showing and the originals are parked (a live twin with the same chord would be ambiguous).
    template <typename Actions>
    void wireWindowSwitching(QDialog& dlg, const Actions& actions, QAction* opener) {
      for (QAction* a : actions) {
        if (!a || a->shortcut().isEmpty()) continue;
        auto* sc = new QShortcut(a->shortcut(), &dlg);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        QObject::connect(sc, &QShortcut::activated, &dlg, [&dlg, a, opener] {
          // A different window's chord: close, then open that one once this dialog's loop has unwound.
          if (a != opener) QTimer::singleShot(0, a, &QAction::trigger);
          dlg.reject();
        });
      }
    }
    // The popover's motion: the dialog reveal (modalReveal.cpp) ×1.5.
    constexpr int POPOVER_OPEN_MS = 450, POPOVER_CLOSE_MS = 360;
  }  // namespace
}  // namespace stencil::gui
