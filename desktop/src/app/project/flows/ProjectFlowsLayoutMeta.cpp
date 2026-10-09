// The page format and formulas a server layout carries, taken into the window and its settings.
#include "MainWindow.hpp"
#include "../../../support/control/reveal/controlReveal.hpp"
#include "ProjectFlows.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>

namespace stencil::gui {

  // Only the keys it carries, so older projects keep the current page/formulas. Signals blocked.
  void ProjectFlows::adoptServerLayoutMeta(const QJsonObject& layout) {
    if (layout.contains("pageSize")) {
      const fileStore::LayoutMeta m = fileStore::parseLayoutMeta(layout);
      if (m.customPageWidth > 0) w.settings.customPageWidth = m.customPageWidth;
      if (m.customPageHeight > 0) w.settings.customPageHeight = m.customPageHeight;
      {
        QSignalBlocker bs(w.units.pageSize);
        const int idx = w.units.pageSize->findData(m.pageSize);
        if (idx >= 0) w.units.pageSize->setCurrentIndex(idx);
      }
      w.settings.pageSize = w.pageSizeValue();
      revealControls(w.units.customGroup, w.settings.pageSize == "custom");
      if (w.units.customW && w.units.customH) {
        QSignalBlocker bw(w.units.customW), bh(w.units.customH);
        const double f = w.unitFormat().factor;
        w.units.customW->setValue(w.settings.customPageWidth * f);
        w.units.customH->setValue(w.settings.customPageHeight * f);
      }
    }
    if (layout.contains("allowFormulas") || layout.contains("formulaX") ||
        layout.contains("formulaY")) {
      const bool allow = layout.value("allowFormulas").toBool(false);
      // Keep the expressions regardless of the toggle.
      const QString fx = layout.value("formulaX").toString();
      const QString fy = layout.value("formulaY").toString();
      w.settings.allowFormulas = allow;
      w.settings.formulaX = fx;
      w.settings.formulaY = fy;
      {
        QSignalBlocker ba(w.tools.allowFormulas);
        w.tools.allowFormulas->setChecked(allow);
      }
      if (w.acts.allowFormulas) {
        QSignalBlocker b(w.acts.allowFormulas);
        w.acts.allowFormulas->setChecked(allow);
      }
      revealControls(w.tools.formulaGroup, allow);
      {
        QSignalBlocker bx(w.tools.formulaX), by(w.tools.formulaY);
        w.tools.formulaX->setText(fx);
        w.tools.formulaY->setText(fy);
      }
      if (w.tools.formulaError) w.tools.formulaError->setVisible(false);
    }
    w.persistSettings();
  }

}  // namespace stencil::gui
