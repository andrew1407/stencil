// The checkbox: checking forms the tick out of particles, unchecking disperses it, and a burst of
// toggles ends on the state the LAST one asked for with nothing stranded and no layout shift.
#include "controlSwapParts.hpp"

void checkScatter(QDialog& host, QCheckBox* box, const QRect& boxGeom) {

  box->setChecked(true);
  check(box->isChecked(), "the state flips at once — the effect never owns the truth");
  check(liveCheckOverlays(&host) == 1, "…and a scatter is up over its indicator");
  check(box->geometry() == boxGeom, "no layout shift: the box keeps its slot");
  {
    // Mid-effect: particles are on their way in, so the overlay has ink but is not yet
    // the settled check.
    pumpFor(CHECK_SWAP_MS / 3);
    QWidget* fx = host.findChild<QWidget*>(QString::fromLatin1(CHECK_SWAP_OBJECT_NAME));
    const QSize ind = stencil::gui::ctl::indicatorRect(box).size();
    check(fx != nullptr
              && fx->size() == ind + QSize(2 * stencil::gui::CHECK_SWAP_PAD_PX,
                                           2 * stencil::gui::CHECK_SWAP_PAD_PX),
          "the overlay is the indicator plus slack — the motes leave the box, the box doesn't");
    check(fx != nullptr && fx->testAttribute(Qt::WA_TransparentForMouseEvents),
          "…and that slack overhangs its neighbours without ever taking a click");
    check(fx != nullptr && fx->parentWidget() == &host,
          "the scatter lives in the window, so nothing clips it to the control");
  }
  check(pumpUntil([&host] { return liveCheckOverlays(&host) == 0; }, CHECK_SWAP_MS + 3000),
        "the gather converges and cleans itself up");
  check(box->isChecked() && box->geometry() == boxGeom,
        "…leaving the box checked, in the same place");

  // ── unchecking: the check disperses ─────────────────────────────────────────
  box->setChecked(false);
  check(liveCheckOverlays(&host) == 1, "unchecking scatters too");
  check(pumpUntil([&host] { return liveCheckOverlays(&host) == 0; }, CHECK_SWAP_MS + 3000),
        "…and that scatter converges as well");
  check(!box->isChecked(), "the box ends unchecked");

  // Rapid toggling: a superseding toggle cancels the one in flight — never two scatters over one 16px
  // indicator, and never a stranded overlay after the burst.
  for (int i = 0; i < 9; ++i) {
    box->setChecked(i % 2 == 0);
    check(liveCheckOverlays(&host) <= 1, "one scatter at a time, however fast the clicks");
    pumpFor(CHECK_SWAP_MS / 8);
  }
  const bool lastState = box->isChecked();
  check(pumpUntil([&host] { return liveCheckOverlays(&host) == 0; }, CHECK_SWAP_MS + 3000),
        "a burst of toggles leaves nothing stranded");
  check(box->isChecked() == lastState && box->geometry() == boxGeom,
        "…and the box shows the state the LAST toggle asked for");
}
