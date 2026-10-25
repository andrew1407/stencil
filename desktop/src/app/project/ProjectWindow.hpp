#pragma once
// What the project layer (ProjectFlows, ProjectCopy) reaches of its window, and nothing more:
// references to the window's parts and callbacks for its own steps, bound once by
// MainWindow::projectSeam(). Browser twin: the DrawingApp members core/project/ reads.
#include "fileStore.hpp"   // Project, Settings, LayoutMeta and core::ProjectsStore

#include <QImage>
#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <functional>
#include <memory>

class QAction;
class QColor;
class QDialog;
class QScrollArea;
class QWidget;

namespace stencil::net { class ConnectionManager; }

namespace stencil::gui {

  class CanvasWidget;
  class ChatSessionController;
  class Notifications;
  class PopoverHost;
  class ProjectNameBar;
  class ProjectTitleController;
  class ProjectTransferController;
  class RemoteSyncController;
  class SessionController;
  class StencilFileSync;
  class UnitsController;
  struct RemoteState;
  struct ToolbarControls;
  struct WindowActions;
  struct WindowOverlays;
  struct WindowParts;

  struct ProjectWindow {
    QWidget* window;   // the parent of every dialog a flow opens, and the context of its callbacks
    CanvasWidget*& canvas;
    QPointer<Notifications>& notify;
    Settings& settings;
    RemoteState& remote;
    ToolbarControls& tools;
    WindowActions& acts;
    UnitsController& units;
    ProjectNameBar& nameBar;
    QScrollArea*& scroll;
    WindowOverlays& overlays;
    WindowParts& parts;
    PopoverHost& pop;
    SessionController& session;
    const bool& tearingDown;
    std::unique_ptr<RemoteSyncController>& remoteSync;
    std::unique_ptr<ProjectTransferController>& projectTransfer;
    std::unique_ptr<ProjectTitleController>& projectTitle;
    std::unique_ptr<ChatSessionController>& chatSession;
    std::unique_ptr<StencilFileSync>& stencilSync;

    std::function<void()> refreshActions, warnInsecureConnections, updateStatusIdle,
        saveSessionNow, playImageArrival, persistSettings, newBlankImage, fitToWindow;
    std::function<QString()> pageSizeValue;
    std::function<fileStore::LayoutMeta()> currentLayoutMeta;
    std::function<double()> unitFactor;   // display units per cm
    std::function<stencil::net::ConnectionManager*()> ensureConnections;
    std::function<QWidget*(QAction*)> buttonForAction;
    std::function<void(const Settings&, bool)> applySettings;

    std::function<int(QDialog&, QAction*)> execMaybePopoverFn;
    std::function<void(double, bool)> setZoomFn;
    std::function<void(const QColor&, bool)> applyTintColorFn;
    std::function<void(const QString&, bool)> applyImageFilterFn;
    std::function<void(const QImage&, const QJsonObject&, const QByteArray&, const QString&)> loadImageWithLayoutFn;
    std::function<void(std::function<QImage()>, std::function<void(const QImage&)>, std::function<void()>)>
        decodeForCanvasFn;

    // The window's own defaults, kept at the call (MainWindow.hpp declares the same ones).
    int execMaybePopover(QDialog& dlg, QAction* opener = nullptr) const { return execMaybePopoverFn(dlg, opener); }
    void setZoom(double scale, bool syncCombo = true) const { setZoomFn(scale, syncCombo); }
    void applyTintColor(const QColor& color, bool asUndoStep = true) const { applyTintColorFn(color, asUndoStep); }
    void applyImageFilter(const QString& mode, bool asUndoStep = true) const { applyImageFilterFn(mode, asUndoStep); }
    void loadImageWithLayout(const QImage& img, const QJsonObject& layout, const QByteArray& bytes = {},
                             const QString& ext = {}) const {
      loadImageWithLayoutFn(img, layout, bytes, ext);
    }
    void decodeForCanvas(std::function<QImage()> decode, std::function<void(const QImage&)> adopt,
                         std::function<void()> dropped = {}) const {
      decodeForCanvasFn(std::move(decode), std::move(adopt), std::move(dropped));
    }
  };

}  // namespace stencil::gui
