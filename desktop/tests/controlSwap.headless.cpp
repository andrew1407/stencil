// Headless check of the form-control state swaps (src/support/controlSwap.hpp) — the
// checkbox's particle toggle and the combo's value exchange.
//
// What is pinned here: that both effects actually run and converge on the TRUE state,
// that reduced motion lands on the end state with no motion at all, that neither ever
// resizes or moves the control it decorates, that a burst of rapid changes always ends
// on the last one asked for with nothing stranded behind it, and that the app-wide
// installer wires controls built after it without any call site's help.
#include "controlSwap.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QVBoxLayout>

#include <cstdio>
#include <functional>

#include "support/check.hpp"

using stencil::gui::installControlSwap;
using stencil::gui::kCheckSwapMs;
using stencil::gui::kCheckSwapObjectName;
using stencil::gui::kControlSwapWiredProperty;
using stencil::gui::kNoControlSwapProperty;
using stencil::gui::kValueSwapProperty;
using stencil::gui::swapCheckIndicator;
using stencil::gui::ValueSwapOverlay;

static void pumpFor(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

// Wait for a condition rather than a clock: an offscreen animation driver ticks at
// whatever rate the harness gives it, and none of these assertions are about its speed.
static bool pumpUntil(const std::function<bool()>& done, int budgetMs = 4000) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < budgetMs) {
    if (done()) return true;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }
  return done();
}

// The checkbox's scatter lives in the WINDOW, not in the box (particles have to leave
// the control's own 16px box to read as particles at all).
static int liveCheckOverlays(QWidget* win) {
  return int(win->findChildren<QWidget*>(QString::fromLatin1(kCheckSwapObjectName)).size());
}

// How many pixels of `im` are neither transparent nor the backdrop — a cheap "is
// anything drawn here" probe for the offscreen grabs.
static int inkedPixels(const QImage& im) {
  int n = 0;
  for (int y = 0; y < im.height(); ++y)
    for (int x = 0; x < im.width(); ++x)
      if (im.pixelColor(x, y).alpha() > 24) ++n;
  return n;
}

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

  check(box->property(kControlSwapWiredProperty).toBool()
            && combo->property(kControlSwapWiredProperty).toBool(),
        "the app-wide filter wires a checkbox and a combo it never heard about");

  // ── the checkbox's two states are actually DIFFERENT pictures ───────────────
  // The whole effect rests on the style rendering :checked off the option's own state,
  // so both looks can be taken from one live widget without poking it.
  {
    const QRect r = stencil::gui::ctl::indicatorRect(box);
    check(r.width() >= 6 && r.height() >= 6, "the indicator has a box worth gridding");
    const QImage on = stencil::gui::ctl::indicatorPixmap(box, r, true).toImage();
    const QImage off = stencil::gui::ctl::indicatorPixmap(box, r, false).toImage();
    check(inkedPixels(on) > 0 && inkedPixels(off) > 0, "both indicator states draw something");
    check(on != off,
          "…and they differ — the particles are the CHECKED look, over the empty one");
  }

  // ── checking: the check forms out of particles ──────────────────────────────
  const QRect boxGeom = box->geometry();
  const QRect comboGeom = combo->geometry();

  box->setChecked(true);
  check(box->isChecked(), "the state flips at once — the effect never owns the truth");
  check(liveCheckOverlays(&host) == 1, "…and a scatter is up over its indicator");
  check(box->geometry() == boxGeom, "no layout shift: the box keeps its slot");
  {
    // Mid-effect: particles are on their way in, so the overlay has ink but is not yet
    // the settled check.
    pumpFor(kCheckSwapMs / 3);
    QWidget* fx = host.findChild<QWidget*>(QString::fromLatin1(kCheckSwapObjectName));
    const QSize ind = stencil::gui::ctl::indicatorRect(box).size();
    check(fx != nullptr
              && fx->size() == ind + QSize(2 * stencil::gui::kCheckSwapPadPx,
                                           2 * stencil::gui::kCheckSwapPadPx),
          "the overlay is the indicator plus slack — the motes leave the box, the box doesn't");
    check(fx != nullptr && fx->testAttribute(Qt::WA_TransparentForMouseEvents),
          "…and that slack overhangs its neighbours without ever taking a click");
    check(fx != nullptr && fx->parentWidget() == &host,
          "the scatter lives in the window, so nothing clips it to the control");
  }
  check(pumpUntil([&host] { return liveCheckOverlays(&host) == 0; }, kCheckSwapMs + 3000),
        "the gather converges and cleans itself up");
  check(box->isChecked() && box->geometry() == boxGeom,
        "…leaving the box checked, in the same place");

  // ── unchecking: the check disperses ─────────────────────────────────────────
  box->setChecked(false);
  check(liveCheckOverlays(&host) == 1, "unchecking scatters too");
  check(pumpUntil([&host] { return liveCheckOverlays(&host) == 0; }, kCheckSwapMs + 3000),
        "…and that scatter converges as well");
  check(!box->isChecked(), "the box ends unchecked");

  // ── rapid toggling ──────────────────────────────────────────────────────────
  // A superseding toggle cancels the one in flight: never two scatters over one 16px
  // indicator, and never a stranded overlay after the burst.
  for (int i = 0; i < 9; ++i) {
    box->setChecked(i % 2 == 0);
    check(liveCheckOverlays(&host) <= 1, "one scatter at a time, however fast the clicks");
    pumpFor(kCheckSwapMs / 8);
  }
  const bool lastState = box->isChecked();
  check(pumpUntil([&host] { return liveCheckOverlays(&host) == 0; }, kCheckSwapMs + 3000),
        "a burst of toggles leaves nothing stranded");
  check(box->isChecked() == lastState && box->geometry() == boxGeom,
        "…and the box shows the state the LAST toggle asked for");

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
  check(combo->property(kValueSwapProperty).toBool(),
        "the swap owns the combo's text colour while it runs, so only one word shows");
  check(combo->geometry() == comboGeom, "no layout shift, and no resize under the cursor");
  {
    QWidget* fx = combo->findChild<QWidget*>(QString::fromLatin1(
        stencil::gui::kValueSwapObjectName));
    check(fx != nullptr && fx->geometry() == combo->rect(),
          "the odometer is pinned inside the combo — the word can never slide outside it");
  }
  check(pumpUntil([combo] { return !ValueSwapOverlay::running(combo); }),
        "the exchange converges and stops");
  check(combo->currentText() == QLatin1String("Letter"),
        "…on the option that was picked");
  check(!combo->property(kValueSwapProperty).toBool() && combo->styleSheet().isEmpty(),
        "nothing of the swap survives: the combo gets its own stylesheet back");
  check(combo->geometry() == comboGeom, "…and its own geometry");

  // Rapid picks: each supersedes the one in flight, and the combo ends on the last.
  for (int i = 0; i < 8; ++i) {
    combo->setCurrentIndex(i % 4);
    check(combo->findChildren<QWidget*>(
              QString::fromLatin1(stencil::gui::kValueSwapObjectName)).size() <= 1,
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

  // ── opting out ──────────────────────────────────────────────────────────────
  box->setProperty(kNoControlSwapProperty, true);
  box->setChecked(true);
  check(liveCheckOverlays(&host) == 0 && box->isChecked(),
        "an opted-out control still changes state, it just does not scatter");
  box->setProperty(kNoControlSwapProperty, false);
  box->setChecked(false);
  pumpUntil([&host] { return liveCheckOverlays(&host) == 0; }, kCheckSwapMs + 3000);

  // ── reduced motion ──────────────────────────────────────────────────────────
  // The end state at once, and — the part that matters — the STATE still changes.
  qputenv("STENCIL_NO_ANIM", "1");
  box->setChecked(true);
  check(box->isChecked(), "reduced motion still checks the box");
  check(liveCheckOverlays(&host) == 0, "…with no particles at all");
  combo->setCurrentIndex(1);
  check(combo->currentText() == QLatin1String("A3"), "reduced motion still changes the value");
  check(!ValueSwapOverlay::running(combo) && combo->styleSheet().isEmpty(),
        "…with no exchange and no colour override");
  check(box->geometry() == boxGeom && combo->geometry() == comboGeom,
        "…and nothing has moved");
  qunsetenv("STENCIL_NO_ANIM");

  // A hidden control is not worth animating, and must never leave an overlay behind a
  // closed dialog.
  host.hide();
  box->setChecked(false);
  combo->setCurrentIndex(0);
  check(liveCheckOverlays(&host) == 0 && !ValueSwapOverlay::running(combo),
        "a hidden dialog's controls change state without animating");

  // Degenerate calls are no-ops, not crashes.
  swapCheckIndicator(nullptr, true);

  std::printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
