// Headless check of the form-control state swaps (src/support/controlSwap.hpp) — the
// checkbox's particle toggle and the combo's value exchange.
//
// …plus the two that travel with them: the sand a combo's dropped LIST forms out of, and
// the sand a whole GROUP of controls comes and goes as (support/controlReveal.hpp).
//
// What is pinned here: that the effects actually run and converge on the TRUE state,
// that reduced motion lands on the end state with no motion at all, that neither ever
// resizes or moves the control it decorates, that a burst of rapid changes always ends
// on the last one asked for with nothing stranded behind it, and that the app-wide
// installer wires controls built after it without any call site's help.
#include "controlReveal.hpp"
#include "controlSwap.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QAbstractItemView>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
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
using stencil::gui::kControlRevealInMs;
using stencil::gui::kFaceSwapMs;
using stencil::gui::kControlRevealOutMs;
using stencil::gui::revealControls;
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
          "the exchange is pinned inside the combo — a mote can no more leave the field "
          "than the word could");
    // Mid-exchange the overlay is DRAWING: the outgoing word's sand is on its way out
    // and the incoming one's is arriving, so what is on screen is neither settled word.
    pumpFor(kFaceSwapMs / 3);
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

  // ── the list a combo drops ──────────────────────────────────────────────────
  // It is a surface like every other popup (support/menuReveal.hpp revealPopup), and the
  // watcher arms that flight on the container itself — no call site is involved, which is
  // the whole point. The flight declines offscreen (no compositor for windowOpacity), so
  // what is pinned here is the wiring, not the pixels.
  {
    QWidget* popup = combo->view() ? combo->view()->window() : nullptr;
    check(popup != nullptr && popup != combo->window(),
          "a combo's list lives in a popup window of its own");
    // The watcher is parented to the combo, not to Qt's container.
    const QString name = QString::fromLatin1(stencil::gui::ctl::kComboPopupFilterName);
    check(combo->findChild<QObject*>(name, Qt::FindDirectChildrenOnly) != nullptr,
          "…and the app-wide watcher armed its dust without any call site's help");
    stencil::gui::ctl::wireComboPopupDust(combo);   // idempotent: never a second filter
    int filters = 0;
    for (QObject* o : combo->children())
      if (o->objectName() == name) ++filters;
    check(filters == 1, "…exactly once");
  }

  // ── a GROUP of controls coming and going (support/controlReveal.hpp) ─────────
  // The f(x,y) fields and the custom page's W/H boxes: visibility lands at once in both
  // directions, the sand is a snapshot with a life of its own, and nothing is ever left
  // dimmed behind a flight that was interrupted.
  {
    host.show();
    pumpUntil([&host] { return host.isVisible(); });
    auto* group = new QWidget(&host);
    auto* gl = new QVBoxLayout(group);
    gl->addWidget(new QLabel("x(x)=", group));
    group->setFixedSize(160, 28);
    lay->addWidget(group);
    group->setVisible(false);
    pumpFor(60);
    const auto liveReveals = [&host] {
      return int(host.findChildren<QWidget*>(
                     QString::fromLatin1(stencil::gui::kControlRevealObjectName)).size());
    };

    revealControls(group, true);
    check(group->isVisible(), "the group is there at once — the layout never waits");
    // Its own slot opens from zero in step with the dust: painted at 0 from the FIRST
    // frame (set before Show), so a neighbouring control never sees it jump to full
    // width and back.
    check(group->maximumWidth() == 0, "the slot starts at zero width, not a flash of full");
    check(pumpUntil([&] { return liveReveals() == 1; }, 2000),
          "…and its motes gather over it once the pending layout has placed it");
    check(group->graphicsEffect() != nullptr, "…with the real group veiled behind them");
    check(pumpUntil([&] { return group->maximumWidth() >= 160; }, kControlRevealInMs + 3000),
          "…while its slot grows to the group's true width");
    check(pumpUntil([&] { return liveReveals() == 0; }, kControlRevealInMs + 3000),
          "the gather converges and cleans itself up");
    check(pumpUntil([&] { return group->graphicsEffect() == nullptr; }, 2000),
          "…and the veil comes off, so the group is never left dimmed");
    check(group->isVisible(), "…leaving it shown");

    revealControls(group, false);
    // The slot closes under the dust rather than jumping shut — group stays visible
    // (still occupying its shrinking width) until the collapse actually finishes.
    check(group->isVisible(), "the group stays up while its slot closes");
    check(liveReveals() == 1, "…handing the picture to a cloud that outlives it");
    check(pumpUntil([&] { return !group->isVisible(); }, kControlRevealOutMs + 3000),
          "…and hides once the slot has fully closed");
    check(pumpUntil([&] { return liveReveals() == 0; }, kControlRevealOutMs + 3000),
          "…the cloud converges too");

    // What FLIES is the controls, not the strip behind them: QWidget::grab() paints the
    // palette's Window brush under the children, and a group photographed on a toolbar
    // then flew as a dark slab over a lighter bar (user report: "black lines next to the
    // inputs"). The gaps a group carries — it is wider than its fields whenever the row
    // hands it slack — must come out CLEAR.
    {
      QWidget wide(&host);
      wide.setFixedSize(200, 28);
      auto* only = new QLabel("x(x)=", &wide);
      only->setGeometry(0, 0, 60, 28);
      only->setAutoFillBackground(true);
      wide.show();
      pumpFor(60);
      const QImage shot = stencil::gui::ctl::groupShot(&wide).toImage();
      check(!shot.isNull() && shot.hasAlphaChannel(), "the group's picture carries alpha");
      const int y = shot.height() / 2;
      check(shot.pixelColor(shot.width() - 4, y).alpha() == 0,
            "the slack a group carries flies as nothing at all, not as a slab of page colour");
      check(shot.pixelColor(4, y).alpha() > 0, "…while the control itself is really in it");
    }

    // A group the row hands SLACK to (Expanding — the f(x,y) pair) must FLY at the width
    // the layout really gives it, not at its own size hint: the hint is only what its
    // contents ask for, so the picture flew narrow and the fields jumped wider the instant
    // the dust handed over (user report).
    {
      auto* row = new QWidget(&host);
      auto* rowLay = new QHBoxLayout(row);
      rowLay->setContentsMargins(0, 0, 0, 0);
      row->setFixedWidth(600);
      auto* wideGroup = new QWidget(row);
      wideGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
      auto* wgl = new QHBoxLayout(wideGroup);
      wgl->setContentsMargins(0, 0, 0, 0);
      auto* field = new QLineEdit(wideGroup);
      field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      wgl->addWidget(field);
      rowLay->addWidget(wideGroup);
      lay->addWidget(row);
      wideGroup->setVisible(false);
      pumpFor(60);
      const int hintW = wideGroup->sizeHint().width();

      revealControls(wideGroup, true);
      pumpUntil([&] { return liveReveals() == 1; }, 2000);
      const auto clouds = host.findChildren<QWidget*>(
          QString::fromLatin1(stencil::gui::kControlRevealObjectName));
      const int pictureW =
          clouds.isEmpty() ? -1 : clouds.first()->width() - 2 * stencil::gui::kControlRevealPadPx;
      check(pumpUntil([&] { return liveReveals() == 0; }, kControlRevealInMs + 3000),
            "the gather over a stretchy group converges");
      pumpUntil([&] { return wideGroup->width() > hintW; }, 2000);
      check(wideGroup->width() > hintW, "the row really does hand this group slack");
      check(pictureW == wideGroup->width(),
            "…and its picture flew at the width it settles at, so nothing jumps at the hand-over");
      delete row;
      pumpFor(30);
    }

    // Asking for the state it already has is not a flight.
    revealControls(group, false);
    check(liveReveals() == 0 && !group->isVisible(),
          "a group already in the asked-for state just stays there");

    // Reduced motion: the end state, with nothing in the air and no effect left behind.
    qputenv("STENCIL_NO_ANIM", "1");
    revealControls(group, true);
    check(group->isVisible() && liveReveals() == 0 && group->graphicsEffect() == nullptr,
          "reduced motion shows the group with no motion at all");
    revealControls(group, false);
    check(!group->isVisible() && liveReveals() == 0, "…and hides it the same way");
    qunsetenv("STENCIL_NO_ANIM");
    delete group;
    host.hide();
  }

  // Degenerate calls are no-ops, not crashes.
  swapCheckIndicator(nullptr, true);
  revealControls(nullptr, true);

  std::printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
