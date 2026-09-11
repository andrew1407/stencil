#include "mainWindow.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "infoDialog.hpp"
#include "linksDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "dataExportController.hpp"
#include "projectTransferController.hpp"
#include "selectionPanel.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "../support/controlReveal.hpp"
#include "../support/modalChrome.hpp"

#include <QApplication>
#include <QJsonObject>
#include <QSignalBlocker>

// The layout envelope this window carries, and adopting a server's copy of it.

namespace stencil::gui {

  // The current page format + x/y formulas (from global settings) as a layout-envelope meta.
  fileStore::LayoutMeta MainWindow::currentLayoutMeta() const {
    fileStore::LayoutMeta m;
    m.pageSize = settings_.pageSize;
    m.customPageWidth = settings_.customPageWidth;
    m.customPageHeight = settings_.customPageHeight;
    m.allowFormulas = settings_.allowFormulas;
    m.formulaX = settings_.formulaX;
    m.formulaY = settings_.formulaY;
    return m;
  }

  // Adopt a fetched layout's page format + formulas into the toolbar + settings (only the keys
  // it carries, so older projects keep the user's current page/formulas). Signals blocked.
  void MainWindow::adoptServerLayoutMeta(const QJsonObject& layout) {
    if (layout.contains("pageSize")) {
      const fileStore::LayoutMeta m = fileStore::parseLayoutMeta(layout);
      if (m.customPageWidth > 0) settings_.customPageWidth = m.customPageWidth;
      if (m.customPageHeight > 0) settings_.customPageHeight = m.customPageHeight;
      {
        QSignalBlocker bs(pageSize_);
        const int idx = pageSize_->findData(m.pageSize);
        if (idx >= 0) pageSize_->setCurrentIndex(idx);
      }
      settings_.pageSize = pageSizeValue();
      revealControls(customGroup_, settings_.pageSize == "custom");
      if (customW_ && customH_) {
        QSignalBlocker bw(customW_), bh(customH_);
        const double f = unitFormat().factor;
        customW_->setValue(settings_.customPageWidth * f);
        customH_->setValue(settings_.customPageHeight * f);
      }
    }
    if (layout.contains("allowFormulas") || layout.contains("formulaX") ||
        layout.contains("formulaY")) {
      const bool allow = layout.value("allowFormulas").toBool(false);
      // Keep the expressions regardless of the toggle (allow only gates visibility + applying).
      const QString fx = layout.value("formulaX").toString();
      const QString fy = layout.value("formulaY").toString();
      settings_.allowFormulas = allow;
      settings_.formulaX = fx;
      settings_.formulaY = fy;
      {
        QSignalBlocker ba(allowFormulas_);
        allowFormulas_->setChecked(allow);
      }
      if (actAllowFormulas_) {
        QSignalBlocker b(actAllowFormulas_);
        actAllowFormulas_->setChecked(allow);
      }
      revealControls(formulaGroup_, allow);
      {
        QSignalBlocker bx(formulaX_), by(formulaY_);
        formulaX_->setText(fx);
        formulaY_->setText(fy);
      }
      if (formulaError_) formulaError_->setVisible(false);
    }
    persistSettings();
  }

  void MainWindow::openProjectInNewWindow(const QString& id) {
    // A fresh window loads the saved projects from disk in its constructor, so it
    // already knows this project. It owns itself and is destroyed on close.
    auto* win = new MainWindow();
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    if (!win->loadProjectIntoCanvas(id)) {
      notify_->error("Could not open the project in a new window");
      win->close();  // auto-close the failed load
    }
  }

  bool MainWindow::projectOpenInOtherWindow(const QString& id) const {
    if (id.isEmpty()) return false;
    for (QWidget* w : QApplication::topLevelWidgets()) {
      auto* mw = qobject_cast<MainWindow*>(w);
      if (mw && mw != this && mw->activeProjectId_ == id) return true;
    }
    return false;
  }

  // Local↔server project transfer (move/copy to/from a server, + the import helper) lives in
  // ProjectTransferController (projectTransferController.hpp), constructed as projectTransfer_.

}  // namespace stencil::gui
