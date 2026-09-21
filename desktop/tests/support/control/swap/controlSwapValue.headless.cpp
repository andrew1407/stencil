// The combo's value exchange: the old label leaves as the new one arrives, over the style's own
// rendering of each option, with no resize under the cursor.
#include "controlSwapParts.hpp"

void comboValueExchange(QDialog& host, QComboBox* combo, QVBoxLayout* lay, const QRect& comboGeom) {
  // ── the combo's value exchange ──────────────────────────────────────────────
  {
    const QImage a4 = stencil::gui::ctl::comboLabelPixmap(combo, "A4").toImage();
    const QImage letter = stencil::gui::ctl::comboLabelPixmap(combo, "Letter").toImage();
    check(inkedPixels(a4) > 0, "the style renders a label for an arbitrary option text");
    check(a4 != letter, "…and a different option is a different picture");
  }

  combo->setCurrentIndex(2);   // A4 → Letter
  check(combo->currentText() == QLatin1String("Letter"),
        "the value changes at once — the effect never owns the truth");
  check(ValueSwapOverlay::running(combo), "…and the exchange is running over it");
  check(combo->property(VALUE_SWAP_PROPERTY).toBool(),
        "the swap owns the combo's text colour while it runs, so only one word shows");
  check(combo->geometry() == comboGeom, "no layout shift, and no resize under the cursor");
  {
    QWidget* fx = combo->findChild<QWidget*>(QString::fromLatin1(
        stencil::gui::VALUE_SWAP_OBJECT_NAME));
    check(fx != nullptr && fx->geometry() == combo->rect(),
          "the exchange is pinned inside the combo — a mote can no more leave the field "
          "than the word could");
    // Mid-exchange the overlay is DRAWING: the outgoing word's sand is on its way out
    // and the incoming one's is arriving, so what is on screen is neither settled word.
    pumpFor(FACE_SWAP_MS / 3);
    const QImage mid = fx ? fx->grab().toImage().convertToFormat(QImage::Format_ARGB32)
                          : QImage();
    check(inkedPixels(mid) > 0, "…and it really paints the sand, not an empty layer");
    check(mid != stencil::gui::ctl::comboLabelPixmap(combo, "Letter").toImage()
                     .convertToFormat(QImage::Format_ARGB32),
          "…which is not simply the settled word drawn early");
  }
  check(pumpUntil([combo] { return !ValueSwapOverlay::running(combo); }),
        "the exchange converges and stops");
  check(combo->currentText() == QLatin1String("Letter"),
        "…on the option that was picked");
  check(!combo->property(VALUE_SWAP_PROPERTY).toBool() && combo->styleSheet().isEmpty(),
        "nothing of the swap survives: the combo gets its own stylesheet back");
  check(combo->geometry() == comboGeom, "…and its own geometry");

  // Rapid picks: each supersedes the one in flight, and the combo ends on the last.
  for (int i = 0; i < 8; ++i) {
    combo->setCurrentIndex(i % 4);
    check(combo->findChildren<QWidget*>(
              QString::fromLatin1(stencil::gui::VALUE_SWAP_OBJECT_NAME)).size() <= 1,
          "one exchange at a time, however fast the picks");
    pumpFor(20);
  }
  const QString landed = combo->currentText();
  check(pumpUntil([combo] { return !ValueSwapOverlay::running(combo); }),
        "a burst of picks leaves no exchange running");
  check(combo->currentText() == landed && combo->styleSheet().isEmpty(),
        "…and the combo shows the last value picked, with its colour handed back");

  // A REPOPULATION is not a pick: rebuilding the list must not deal the dialog in.
  combo->clear();
  combo->addItems({"Custom", "A3"});
  check(!ValueSwapOverlay::running(combo),
        "refilling the model changes the value without animating it");
  check(combo->styleSheet().isEmpty(), "…and leaves no colour override behind");
  combo->addItems({"A4", "A5"});
  combo->setCurrentIndex(2);
  check(pumpUntil([combo] { return !ValueSwapOverlay::running(combo); }),
        "…while a pick after it still animates and converges");
  check(combo->currentText() == QLatin1String("A4"), "…onto the right value");

  // An EDITABLE combo is a text field, not a chooser: blanking what the user is typing
  // would be sabotage, so it is left alone however its text changes.
  {
    auto* typed = new QComboBox(&host);
    typed->setEditable(true);
    typed->addItems({"100%", "150%"});
    lay->addWidget(typed);
    pumpFor(60);
    typed->setCurrentIndex(1);
    typed->setEditText("175%");
    check(!ValueSwapOverlay::running(typed) && typed->styleSheet().isEmpty(),
          "an editable combo never has its text blanked out from under the caret");
    delete typed;
  }

  // The f(x,y) pill hides its tick and carries its state in the whole chip's fill, so
  // there is no check to disperse — it must skip, not scatter an invisible box.
  {
    auto* pill = new QCheckBox("f(x,y)", &host);
    pill->setObjectName("formulaPill");
    lay->addWidget(pill);
    pumpFor(60);
    pill->setChecked(true);
    check(liveCheckOverlays(&host) == 0 && pill->isChecked(),
          "a checkbox with no visible indicator toggles without scattering nothing");
    delete pill;
  }
}
