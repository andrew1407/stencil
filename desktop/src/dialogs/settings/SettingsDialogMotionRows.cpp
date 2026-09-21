// The Motion section: the two toggles and the interface-animation mode with its glyphs.
#include "SettingsDialog.hpp"
#include "../../support/menu/SearchCombo.hpp"
#include "../../support/icon/motionIcons.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QListView>
#include <QPalette>

namespace stencil::gui {

  void SettingsDialog::buildMotionRows(Rows& r, const Settings& current) {
    // Motion (browser modal.js "Motion", the same two rows in the same order). Live-applied,
    // so the dialog's own closing flight is already the mode you just picked.
    addSection(r, tr("Motion"));

    addCheck(r, drawAnim, current.drawingAnimations,
          "On: a new point flies to where you put it, popping and rippling as it lands.\n"
          "Off: every point goes straight down.");
    addRow(r, tr("Drawing animation"), drawAnim, /*column=*/false);

    addCheck(r, modalBackdrop, current.modalBackdrop,
          "On: an open window dims and blurs what it covers.\nOff: it sits on a sharp page.");
    addRow(r, tr("Dim and blur behind windows"), modalBackdrop, /*column=*/false);

    motionMode = addCombo(r, QString());   // no tooltip — the browser's dropdown has none (the glyphs say it)
    motionMode->setObjectName(QStringLiteral("motionModeCombo"));
    // The browser's MOTION_MODE_LABELS, in its order (ui/prefs.js).
    motionMode->addItem("Dust", "particles");
    motionMode->addItem("Water", "water");
    motionMode->addItem("Fire", "fire");
    motionMode->addItem("Sliding", "slide");
    motionMode->addItem("None", "none");
    // Each mode's glyph (support/motionIcons.hpp — the browser's motion/icons.js): on the
    // trigger at rest, and on the popup rows animated as they are hovered.
    {
      // In the text colour, like the labels (never the accent — user decision).
      const QColor ink = palette().color(QPalette::Text);
      for (int i = 0; i < motionMode->count(); ++i)
        motionMode->setItemIcon(i, support::motionModeIcon(motionMode->itemData(i).toString(), ink));
      auto* mm = static_cast<SearchComboBox*>(motionMode);   // addCombo(r, ) builds SearchComboBoxes
      mm->setListDelegate(new support::MotionIconDelegate(mm->popupList(), mm));
      new support::MotionIconFace(mm);   // the face's glyph plays on change and on hover
    }
    {
      const int idx = motionMode->findData(current.motionMode);
      motionMode->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    addRow(r, tr("Interface animation"), motionMode);
    connect(motionMode, &QComboBox::activated, this, [this] { applyLive(); });
  }

}
