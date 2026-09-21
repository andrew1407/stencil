// Opting out, reduced motion and a hidden dialog — each landing on the true state with no motion —
// then the sand a dropped LIST forms out of and the sand a whole GROUP comes and goes as.
#include "controlSwapParts.hpp"

void optOutAndReducedMotion(QDialog& host, QVBoxLayout* lay, QCheckBox* box, QComboBox* combo,
                            const QRect& boxGeom, const QRect& comboGeom) {
  // ── opting out ──────────────────────────────────────────────────────────────
  box->setProperty(NO_CONTROL_SWAP_PROPERTY, true);
  box->setChecked(true);
  check(liveCheckOverlays(&host) == 0 && box->isChecked(),
        "an opted-out control still changes state, it just does not scatter");
  box->setProperty(NO_CONTROL_SWAP_PROPERTY, false);
  box->setChecked(false);
  pumpUntil([&host] { return liveCheckOverlays(&host) == 0; }, CHECK_SWAP_MS + 3000);

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

  // The list a combo drops is a surface like every other popup (support/menuReveal.hpp revealPopup),
  // armed on the container itself with no call site. The flight declines offscreen, so this pins wiring.
  {
    QWidget* popup = combo->view() ? combo->view()->window() : nullptr;
    check(popup != nullptr && popup != combo->window(),
          "a combo's list lives in a popup window of its own");
    // The watcher is parented to the combo, not to Qt's container.
    const QString name = QString::fromLatin1(stencil::gui::ctl::COMBO_POPUP_FILTER_NAME);
    check(combo->findChild<QObject*>(name, Qt::FindDirectChildrenOnly) != nullptr,
          "…and the app-wide watcher armed its dust without any call site's help");
    stencil::gui::ctl::wireComboPopupDust(combo);   // idempotent: never a second filter
    int filters = 0;
    for (QObject* o : combo->children())
      if (o->objectName() == name) ++filters;
    check(filters == 1, "…exactly once");
  }

  // A GROUP of controls coming and going (support/controlReveal.hpp): visibility lands at once in both
  // directions, the sand is a snapshot with a life of its own, and an interruption leaves nothing dimmed.
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
                     QString::fromLatin1(stencil::gui::CONTROL_REVEAL_OBJECT_NAME)).size());
    };

    revealControls(group, true);
    check(group->isVisible(), "the group is there at once — the layout never waits");
    // Its own slot opens from zero in step with the dust, painted at 0 from the FIRST frame (set before
    // Show), so a neighbouring control never sees it jump to full width and back.
    check(group->maximumWidth() == 0, "the slot starts at zero width, not a flash of full");
    check(pumpUntil([&] { return liveReveals() == 1; }, 2000),
          "…and its motes gather over it once the pending layout has placed it");
    check(group->graphicsEffect() != nullptr, "…with the real group veiled behind them");
    check(pumpUntil([&] { return group->maximumWidth() >= 160; }, CONTROL_REVEAL_IN_MS + 3000),
          "…while its slot grows to the group's true width");
    check(pumpUntil([&] { return liveReveals() == 0; }, CONTROL_REVEAL_IN_MS + 3000),
          "the gather converges and cleans itself up");
    check(pumpUntil([&] { return group->graphicsEffect() == nullptr; }, 2000),
          "…and the veil comes off, so the group is never left dimmed");
    check(group->isVisible(), "…leaving it shown");

    revealControls(group, false);
    // The slot closes under the dust rather than jumping shut — group stays visible
    // (still occupying its shrinking width) until the collapse actually finishes.
    check(group->isVisible(), "the group stays up while its slot closes");
    check(liveReveals() == 1, "…handing the picture to a cloud that outlives it");
    check(pumpUntil([&] { return !group->isVisible(); }, CONTROL_REVEAL_OUT_MS + 3000),
          "…and hides once the slot has fully closed");
    check(pumpUntil([&] { return liveReveals() == 0; }, CONTROL_REVEAL_OUT_MS + 3000),
          "…the cloud converges too");

    // What FLIES is the controls, not the strip behind them: QWidget::grab() paints the palette's Window
    // brush under the children, so a group photographed on a toolbar flew as a dark slab over a light bar.
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

    // A group the row hands SLACK to (Expanding — the f(x,y) pair) must FLY at the width the layout really
    // gives it, not at its own size hint, which is only what its contents ask for.
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
          QString::fromLatin1(stencil::gui::CONTROL_REVEAL_OBJECT_NAME));
      const int pictureW =
          clouds.isEmpty() ? -1 : clouds.first()->width() - 2 * stencil::gui::CONTROL_REVEAL_PAD_PX;
      check(pumpUntil([&] { return liveReveals() == 0; }, CONTROL_REVEAL_IN_MS + 3000),
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
}
