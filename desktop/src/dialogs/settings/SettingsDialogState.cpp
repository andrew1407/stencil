#include "SettingsDialog.hpp"
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QResizeEvent>

#include "guiHelpers.hpp"
#include "modalChrome.hpp"

namespace stencil::gui {

  // The column grows with the window in proportion, 180 at the 560 it opens at (browser
  // --vs-ctrl-pct), so a widened window widens the controls rather than the gap before them.
  void SettingsDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    const int want = qMax(CTRL_W, CTRL_W * width() / MODAL_WIDTH);
    if (want == ctrlW) return;
    ctrlW = want;
    for (QWidget* f : columnFields) {
      auto* well = qobject_cast<QPushButton*>(f);
      if (well && well->property("swatchColor").isValid())
        setColorSwatch(well, well->property("swatchColor").value<QColor>(), QSize(ctrlW, f->height()), true);
      else
        f->setFixedWidth(ctrlW);
    }
  }

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
    // Untouched, the motion rows hand back what is STORED: they were seeded from what is in
    // force, and a skin's stillness must not persist through an unrelated edit.
    s.drawingAnimations = motionTouched ? drawAnim->isChecked() : heldDrawAnim;
    s.modalBackdrop = motionTouched ? modalBackdrop->isChecked() : heldBackdrop;
    s.multiWindow = multiWindow->isChecked();
    s.motionMode = motionTouched ? motionMode->currentData().toString() : heldMotionMode;
    s.notifyChannel = notifyChannel->currentData().toString();
    s.browserBaseUrl = browserUrl->text().trimmed();
    s.telegramBotUsername = botUsername->text().trimmed().remove(QLatin1Char('@'));
    return s;
  }
}

