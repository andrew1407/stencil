// The driver on a live control: reduced motion wins, a settle converges, a hold holds until the
// pointer leaves, the opt-out is honoured and a greyed control still mimes. Then the idle card.
#include "iconMotionParts.hpp"

void driverOnALiveButton() {
  installIconMotion();
  {
    auto* btn = new QToolButton;
    btn->setIconSize(QSize(18, 18));
    btn->resize(28, 28);
    btn->show();

    // Reduced motion: the preference wins, and the glyph stays in its rest pose.
    qputenv("STENCIL_NO_ANIM", "1");
    btn->setIcon(themedIcon(QStringLiteral("plus"), QColor(Qt::black), 18, 1.0));
    const qint64 restKey = btn->icon().cacheKey();
    hover(btn, true);
    pumpFor(60);
    check(btn->icon().cacheKey() == restKey, "reduced motion runs no icon motion at all");
    hover(btn, false);
    qunsetenv("STENCIL_NO_ANIM");

    // A settle plays and comes back on its own.
    btn->setIcon(themedIcon(QStringLiteral("plus"), QColor(Qt::black), 18, 1.0));
    const QSize box = btn->sizeHint();
    hover(btn, true);
    check(pumpUntil([&] { return btn->icon().cacheKey() != restKey; }, 500),
          "a hover starts the motion");
    check(btn->sizeHint() == box, "…without resizing the control");
    check(pumpUntil([&] { return btn->icon().cacheKey() == restKey; }, 2000),
          "…and the settle converges back on the rest glyph");
    hover(btn, false);

    // A hold holds while the pointer rests, and eases back when it leaves.
    btn->setIcon(themedIcon(QStringLiteral("trash"), QColor(Qt::black), 18, 1.0));
    const qint64 trashKey = btn->icon().cacheKey();
    hover(btn, true);
    check(pumpUntil([&] { return btn->icon().cacheKey() != trashKey; }, 500),
          "a hold motion moves on hover");
    pumpFor(350);
    check(btn->icon().cacheKey() != trashKey, "…and stays put while hovered");
    hover(btn, false);
    check(pumpUntil([&] { return btn->icon().cacheKey() == trashKey; }, 2000),
          "…then eases back on leave");

    // The opt-out (a fold chevron, whose angle is state) is honoured.
    btn->setProperty(NO_ICON_MOTION_PROPERTY, true);
    btn->setIcon(themedIcon(QStringLiteral("chevron-up"), QColor(Qt::black), 18, 1.0));
    const qint64 chevKey = btn->icon().cacheKey();
    hover(btn, true);
    pumpFor(80);
    check(btn->icon().cacheKey() == chevKey, "an opted-out control keeps its glyph still");
    hover(btn, false);
    btn->setProperty(NO_ICON_MOTION_PROPERTY, false);

    // A DISABLED control still reacts (iconMotion.json trigger.disabled): Enter/Leave reach a disabled
    // widget — which is how its tooltip shows the reason — and a frozen glyph reads as dead toolbar.
    btn->setEnabled(false);
    btn->setIcon(themedIcon(QStringLiteral("plus"), QColor(Qt::black), 18, 1.0));
    const qint64 plusKey = btn->icon().cacheKey();
    hover(btn, true);
    check(pumpUntil([&] { return btn->icon().cacheKey() != plusKey; }, 2000),
          "a greyed control still mimes what it would do");
    check(pumpUntil([&] { return btn->icon().cacheKey() == plusKey; }, 2000),
          "…and its settle comes back to the rest glyph");
    hover(btn, false);
    btn->setEnabled(true);
    delete btn;
  }
}

void idleCardGlyph() {
  {
    const IconMotionSpec* image = iconMotionFor(QStringLiteral("image"));
    check(image != nullptr, "the canon carries an `image` entry");
    if (image) {
      check(!image->hold, "…as a settle, which is what the card plays once per hover");
      const IconMotionPart* ridge = nullptr;
      const IconMotionPart* orb = nullptr;
      for (const IconMotionPart& part : image->parts) {
        if (part.hook == QLatin1String("ic-ridge")) ridge = &part;
        if (part.hook == QLatin1String("ic-orb")) orb = &part;
      }
      check(ridge && orb, "…with the ridge and the sun as its two parts");
      if (ridge && orb) {
        check(qFuzzyCompare(ridge->durationMs + 1, IDLE_GLYPH_RIDGE_MS + 1), "ridge duration");
        check(qFuzzyCompare(ridge->dashArray + 1, IDLE_GLYPH_RIDGE_LEN + 1), "ridge dash length");
        check(qFuzzyCompare(orb->durationMs + 1, IDLE_GLYPH_ORB_MS + 1), "sun duration");
        check(qFuzzyCompare(orb->delayMs + 1, IDLE_GLYPH_ORB_DELAY_MS + 1), "sun delay");
        check(orb->keys.size() == 3, "the sun's three keyframes");
        if (orb->keys.size() == 3) {
          check(qFuzzyCompare(orb->keys.first().pose.ty + 1, IDLE_GLYPH_ORB_DROP + 1),
                "…the height it drops from");
          check(qFuzzyCompare(orb->keys.at(1).pose.ty + 1, IDLE_GLYPH_ORB_OVERSHOOT + 1),
                "…and the overshoot it lands through");
          check(qFuzzyCompare(orb->keys.at(1).at + 1, 70.0 + 1), "…at 70% of the play");
          check(qFuzzyCompare(orb->keys.last().pose.ty + 1, 1.0), "…ending where it rests");
        }
        check(qFuzzyCompare(image->totalMs + 1, IDLE_GLYPH_PLAY_MS + 1),
              "the card's play is the whole entry's");
      }
    }
  }
}
