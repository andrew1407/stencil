#include "MainWindow.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "InfoDialog.hpp"
#include "LinksDialog.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "ConnectDialog.hpp"
#include "DataExportController.hpp"
#include "ProjectTransferController.hpp"
#include "SelectionPanel.hpp"
#include "SettingsDialog.hpp"
#include "ShortcutsDialog.hpp"
#include "../support/controlReveal.hpp"
#include "../support/modalChrome.hpp"

#include <QApplication>
#include <QJsonObject>
#include <QSignalBlocker>

// The layout envelope this window carries, and adopting a server's copy of it.

namespace stencil::gui {

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

  // Only the keys it carries, so older projects keep the current page/formulas. Signals blocked.
  void MainWindow::adoptServerLayoutMeta(const QJsonObject& layout) {
    if (layout.contains("pageSize")) {
      const fileStore::LayoutMeta m = fileStore::parseLayoutMeta(layout);
      if (m.customPageWidth > 0) settings_.customPageWidth = m.customPageWidth;
      if (m.customPageHeight > 0) settings_.customPageHeight = m.customPageHeight;
      {
        QSignalBlocker bs(units_.pageSize);
        const int idx = units_.pageSize->findData(m.pageSize);
        if (idx >= 0) units_.pageSize->setCurrentIndex(idx);
      }
      settings_.pageSize = pageSizeValue();
      revealControls(units_.customGroup, settings_.pageSize == "custom");
      if (units_.customW && units_.customH) {
        QSignalBlocker bw(units_.customW), bh(units_.customH);
        const double f = unitFormat().factor;
        units_.customW->setValue(settings_.customPageWidth * f);
        units_.customH->setValue(settings_.customPageHeight * f);
      }
    }
    if (layout.contains("allowFormulas") || layout.contains("formulaX") ||
        layout.contains("formulaY")) {
      const bool allow = layout.value("allowFormulas").toBool(false);
      // Keep the expressions regardless of the toggle.
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
    // A fresh window loads the saved projects itself; it owns itself and dies on close.
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

  // Local↔server transfer lives in ProjectTransferController (projectTransfer_).

}  // namespace stencil::gui
