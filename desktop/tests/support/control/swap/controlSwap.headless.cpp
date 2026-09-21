// Headless check of the form-control state swaps (src/support/controlSwap.hpp), split across
// controlSwap*.headless.cpp. This TU owns the host dialog and the two controls the sections drive,
// and the geometry snapshots they assert nothing moved against.
#include "controlSwapParts.hpp"

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qunsetenv("STENCIL_NO_ANIM");   // this suite drives the real motion; one case re-sets it
  QApplication app(argc, argv);
  app.setStyleSheet(stencil::gui::buildStylesheet(/*dark=*/false));

  // The installer goes on FIRST, before a single control exists — the whole point of a
  // central hook is that controls built later need no call site of their own.
  installControlSwap();
  installControlSwap();   // idempotent: a second MainWindow must not double-wire

  QDialog host;
  auto* lay = new QVBoxLayout(&host);
  auto* box = new QCheckBox("Show points", &host);
  auto* combo = new QComboBox(&host);
  combo->addItems({"A4", "A5", "Letter", "Legal"});
  lay->addWidget(box);
  lay->addWidget(combo);
  host.resize(320, 140);
  host.show();
  pumpUntil([box] { return box->isVisible(); });
  pumpFor(60);

  check(box->property(CONTROL_SWAP_WIRED_PROPERTY).toBool()
            && combo->property(CONTROL_SWAP_WIRED_PROPERTY).toBool(),
        "the app-wide filter wires a checkbox and a combo it never heard about");

  // The checkbox's two states are actually DIFFERENT pictures: the whole effect rests on the style
  // rendering :checked off the option's own state, so both looks come from one live widget.
  {
    const QRect r = stencil::gui::ctl::indicatorRect(box);
    check(r.width() >= 6 && r.height() >= 6, "the indicator has a box worth gridding");
    const QImage on = stencil::gui::ctl::indicatorPixmap(box, r, true).toImage();
    const QImage off = stencil::gui::ctl::indicatorPixmap(box, r, false).toImage();
    check(inkedPixels(on) > 0 && inkedPixels(off) > 0, "both indicator states draw something");
    check(on != off,
          "…and they differ — the particles are the CHECKED look, over the empty one");
  }

  const QRect boxGeom = box->geometry();
  const QRect comboGeom = combo->geometry();

  checkScatter(host, box, boxGeom);
  comboValueExchange(host, combo, lay, comboGeom);
  optOutAndReducedMotion(host, lay, box, combo, boxGeom, comboGeom);

  std::printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
