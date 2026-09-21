#include "../../support/menu/SearchCombo.hpp"
#include "../../support/icon/motionIcons.hpp"
#include "SettingsDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../../support/modal/modalChrome.hpp"   // the browser modal shell + .vs-row rows
#include "../../support/modal/modalReveal.hpp"
#include "theme.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QListView>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace stencil::gui {

  // Rows filter by their label (the browser matches the <label> text); a section
  // whose rows all hid hides too, and an emptied list shows the muted line.
  void SettingsDialog::applyFilter(const QString& query) {
    const QString q = query.trimmed().toLower();
    bool any = false;
    for (Group& g : groups) {
      bool groupAny = false;
      for (const auto& [label, w] : g.rows) {
        const bool match = q.isEmpty() || label.toLower().contains(q);
        w->setVisible(match);
        groupAny = groupAny || match;
      }
      g.title->setVisible(groupAny);
      any = any || groupAny;
    }
    empty->setVisible(!any);
  }

  void SettingsDialog::applyLive() {
    if (onChange) onChange(result());
  }

  Settings SettingsDialog::result() const {
    Settings s = base;  // keep fields not exposed here (formulas, tooltip, llm*…)
    s.themeMode = theme->currentData().toString();
    s.accentColor = accent->currentData().toString();
    s.nativeMenuBar = nativeMenuBar->isChecked();
    s.autosave = autosave->isChecked();
    s.showPoints = showPoints->isChecked();
    s.showLines = showLines->isChecked();
    s.defaultColor = colorHex;
    s.defaultThickness = thickness->value();
    s.defaultPointSize = pointSize->value();
    s.defaultStyle = style->currentData().toString();
    s.defaultFillColor = fillHex;
    s.selGlowColor = selGlowHex;
    s.hoverRingColor = hoverRingHex;
    s.focusRingColor = focusRingHex;
    s.pageSize = page->currentData().toString();
    s.customPageWidth = customW->value();
    s.customPageHeight = customH->value();
    s.holdDrawDelay = holdDelay->value();
    s.drawingAnimations = drawAnim->isChecked();
    s.modalBackdrop = modalBackdrop->isChecked();
    s.motionMode = motionMode->currentData().toString();
    s.browserBaseUrl = browserUrl->text().trimmed();
    s.telegramBotUsername = botUsername->text().trimmed().remove(QLatin1Char('@'));
    return s;
  }
}

