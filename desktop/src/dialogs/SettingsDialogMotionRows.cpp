// The Motion section: the two toggles and the interface-animation mode with its glyphs.
#include "SettingsDialog.hpp"
#include "../support/SearchCombo.hpp"
#include "../support/motionIcons.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QListView>
#include <QPalette>

namespace stencil::gui {

  void SettingsDialog::buildMotionRows(Rows& r, const Settings& current) {
    // Motion (browser visualsModal.js "Motion", the same two rows in the same order). Live-applied,
    // so the dialog's own closing flight is already the mode you just picked.
    addSection(r, tr("Motion"));

    addCheck(r, drawAnim_, current.drawingAnimations,
          "On: a new point flies to where you put it, popping and rippling as it lands.\n"
          "Off: every point goes straight down.");
    addRow(r, tr("Drawing animation"), drawAnim_, /*column=*/false);

    addCheck(r, modalBackdrop_, current.modalBackdrop,
          "On: an open window dims and blurs what it covers.\nOff: it sits on a sharp page.");
    addRow(r, tr("Dim and blur behind windows"), modalBackdrop_, /*column=*/false);

    motionMode_ = addCombo(r, QString());   // no tooltip — the browser's dropdown has none (the glyphs say it)
    motionMode_->setObjectName(QStringLiteral("motionModeCombo"));
    // The browser's MOTION_MODE_LABELS, in its order (ui/motionPrefs.js).
    motionMode_->addItem("Dust", "particles");
    motionMode_->addItem("Water", "water");
    motionMode_->addItem("Fire", "fire");
    motionMode_->addItem("Sliding", "slide");
    motionMode_->addItem("None", "none");
    // Each mode's glyph (support/motionIcons.hpp — the browser's motionIcons.js): on the
    // trigger at rest, and on the popup rows animated as they are hovered.
    {
      // In the text colour, like the labels (never the accent — user decision).
      const QColor ink = palette().color(QPalette::Text);
      for (int i = 0; i < motionMode_->count(); ++i)
        motionMode_->setItemIcon(i, support::motionModeIcon(motionMode_->itemData(i).toString(), ink));
      auto* mm = static_cast<SearchComboBox*>(motionMode_);   // addCombo(r, ) builds SearchComboBoxes
      mm->setListDelegate(new support::MotionIconDelegate(mm->popupList(), mm));
      new support::MotionIconFace(mm);   // the face's glyph plays on change and on hover
    }
    {
      const int idx = motionMode_->findData(current.motionMode);
      motionMode_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    addRow(r, tr("Interface animation"), motionMode_);
    connect(motionMode_, &QComboBox::activated, this, [this] { applyLive(); });
  }

}
