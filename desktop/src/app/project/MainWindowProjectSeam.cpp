// The window bound into ProjectWindow: its parts by reference and its steps as callbacks, so
// ProjectFlows and ProjectCopy reach exactly these and no other member of MainWindow.
#include "MainWindow.hpp"
#include "ChatSessionController.hpp"
#include "ProjectTitleController.hpp"
#include "ProjectTransferController.hpp"
#include "RemoteSyncController.hpp"
#include "StencilFileSync.hpp"

namespace stencil::gui {

  ProjectWindow MainWindow::projectSeam() {
    ProjectWindow seam{this, canvas, notify, settings, remote, tools, acts, units, nameBar, scroll, overlays,
                       parts, pop, session, tearingDown, remoteSync, projectTransfer, projectTitle, chatSession,
                       stencilSync};
    seam.refreshActions = [this] { refreshActions(); };
    seam.warnInsecureConnections = [this] { warnInsecureConnections(); };
    seam.updateStatusIdle = [this] { updateStatusIdle(); };
    seam.saveSessionNow = [this] { saveSessionNow(); };
    seam.playImageArrival = [this] { playImageArrival(); };
    seam.persistSettings = [this] { persistSettings(); };
    seam.newBlankImage = [this] { newBlankImage(); };
    seam.fitToWindow = [this] { fitToWindow(); };
    seam.pageSizeValue = [this] { return pageSizeValue(); };
    seam.currentLayoutMeta = [this] { return currentLayoutMeta(); };
    seam.unitFactor = [this] { return unitFormat().factor; };
    seam.ensureConnections = [this] { return ensureConnections(); };
    seam.buttonForAction = [this](QAction* act) { return buttonForAction(act); };
    seam.applySettings = [this](const Settings& s, bool persist) { applySettings(s, persist); };
    seam.setZoomFn = [this](double scale, bool syncCombo) { setZoom(scale, syncCombo); };
    seam.execMaybePopoverFn = [this](QDialog& dlg, QAction* opener) { return execMaybePopover(dlg, opener); };
    seam.applyTintColorFn = [this](const QColor& c, bool undo) { applyTintColor(c, undo); };
    seam.applyImageFilterFn = [this](const QString& mode, bool undo) { applyImageFilter(mode, undo); };
    seam.loadImageWithLayoutFn = [this](const QImage& img, const QJsonObject& layout, const QByteArray& bytes,
                                        const QString& ext) { loadImageWithLayout(img, layout, bytes, ext); };
    seam.decodeForCanvasFn = [this](std::function<QImage()> decode, std::function<void(const QImage&)> adopt,
                                    std::function<void()> dropped) {
      decodeForCanvas(std::move(decode), std::move(adopt), std::move(dropped));
    };
    return seam;
  }

}  // namespace stencil::gui
