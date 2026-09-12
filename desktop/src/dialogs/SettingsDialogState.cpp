#include "../support/SearchCombo.hpp"
#include "../support/motionIcons.hpp"
#include "SettingsDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../support/modalChrome.hpp"   // the browser modal shell + .vs-row rows
#include "../support/modalReveal.hpp"
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
    for (Group& g : groups_) {
      bool groupAny = false;
      for (const auto& [label, w] : g.rows) {
        const bool match = q.isEmpty() || label.toLower().contains(q);
        w->setVisible(match);
        groupAny = groupAny || match;
      }
      g.title->setVisible(groupAny);
      any = any || groupAny;
    }
    empty_->setVisible(!any);
  }

  void SettingsDialog::applyLive() {
    if (onChange_) onChange_(result());
  }

  Settings SettingsDialog::result() const {
    Settings s = base_;  // keep fields not exposed here (formulas, tooltip, llm*…)
    s.themeMode = theme_->currentData().toString();
    s.accentColor = accent_->currentData().toString();
    s.nativeMenuBar = nativeMenuBar_->isChecked();
    s.autosave = autosave_->isChecked();
    s.showPoints = showPoints_->isChecked();
    s.showLines = showLines_->isChecked();
    s.defaultColor = colorHex_;
    s.defaultThickness = thickness_->value();
    s.defaultPointSize = pointSize_->value();
    s.defaultStyle = style_->currentData().toString();
    s.defaultFillColor = fillHex_;
    s.selGlowColor = selGlowHex_;
    s.hoverRingColor = hoverRingHex_;
    s.focusRingColor = focusRingHex_;
    s.pageSize = page_->currentData().toString();
    s.customPageWidth = customW_->value();
    s.customPageHeight = customH_->value();
    s.holdDrawDelay = holdDelay_->value();
    s.drawingAnimations = drawAnim_->isChecked();
    s.motionMode = motionMode_->currentData().toString();
    s.browserBaseUrl = browserUrl_->text().trimmed();
    s.telegramBotUsername = botUsername_->text().trimmed().remove(QLatin1Char('@'));
    return s;
  }
}

