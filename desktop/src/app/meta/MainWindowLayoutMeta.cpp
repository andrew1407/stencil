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
#include "../../support/control/reveal/controlReveal.hpp"
#include "../../support/modal/modalChrome.hpp"

#include <QApplication>
#include <QJsonObject>
#include <QSignalBlocker>

// The layout envelope this window carries, and adopting a server's copy of it.

namespace stencil::gui {

  fileStore::LayoutMeta MainWindow::currentLayoutMeta() const {
    fileStore::LayoutMeta m;
    m.pageSize = settings.pageSize;
    m.customPageWidth = settings.customPageWidth;
    m.customPageHeight = settings.customPageHeight;
    m.allowFormulas = settings.allowFormulas;
    m.formulaX = settings.formulaX;
    m.formulaY = settings.formulaY;
    return m;
  }

  // Only the keys it carries, so older projects keep the current page/formulas. Signals blocked.
  void MainWindow::adoptServerLayoutMeta(const QJsonObject& layout) {
    if (layout.contains("pageSize")) {
      const fileStore::LayoutMeta m = fileStore::parseLayoutMeta(layout);
      if (m.customPageWidth > 0) settings.customPageWidth = m.customPageWidth;
      if (m.customPageHeight > 0) settings.customPageHeight = m.customPageHeight;
      {
        QSignalBlocker bs(units.pageSize);
        const int idx = units.pageSize->findData(m.pageSize);
        if (idx >= 0) units.pageSize->setCurrentIndex(idx);
      }
      settings.pageSize = pageSizeValue();
      revealControls(units.customGroup, settings.pageSize == "custom");
      if (units.customW && units.customH) {
        QSignalBlocker bw(units.customW), bh(units.customH);
        const double f = unitFormat().factor;
        units.customW->setValue(settings.customPageWidth * f);
        units.customH->setValue(settings.customPageHeight * f);
      }
    }
    if (layout.contains("allowFormulas") || layout.contains("formulaX") ||
        layout.contains("formulaY")) {
      const bool allow = layout.value("allowFormulas").toBool(false);
      // Keep the expressions regardless of the toggle.
      const QString fx = layout.value("formulaX").toString();
      const QString fy = layout.value("formulaY").toString();
      settings.allowFormulas = allow;
      settings.formulaX = fx;
      settings.formulaY = fy;
      {
        QSignalBlocker ba(allowFormulas);
        allowFormulas->setChecked(allow);
      }
      if (actAllowFormulas) {
        QSignalBlocker b(actAllowFormulas);
        actAllowFormulas->setChecked(allow);
      }
      revealControls(formulaGroup, allow);
      {
        QSignalBlocker bx(formulaX), by(formulaY);
        formulaX->setText(fx);
        formulaY->setText(fy);
      }
      if (formulaError) formulaError->setVisible(false);
    }
    persistSettings();
  }

  void MainWindow::openProjectInNewWindow(const QString& id) {
    // A fresh window loads the saved projects itself; it owns itself and dies on close.
    auto* win = new MainWindow();
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    if (!win->loadProjectIntoCanvas(id)) {
      notify->error("Could not open the project in a new window");
      win->close();  // auto-close the failed load
    }
  }

  bool MainWindow::projectOpenInOtherWindow(const QString& id) const {
    if (id.isEmpty()) return false;
    for (QWidget* w : QApplication::topLevelWidgets()) {
      auto* mw = qobject_cast<MainWindow*>(w);
      if (mw && mw != this && mw->activeProjectId == id) return true;
    }
    return false;
  }

  // Local↔server transfer lives in ProjectTransferController (projectTransfer).

}  // namespace stencil::gui
