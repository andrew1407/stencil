// The logo stage's table and kinematics against the browser's (support/logoStageRules.cpp,
// logoStageMotion.cpp): the same resolution rows, the same heart, the same steps. Sample values
// printed from node (browser/tests/logoStage*.test.js).
#include "logoStageMotion.hpp"
#include "logoStageRules.hpp"
#include "motionPrefs.hpp"
#include "typedLetter.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <cmath>
#include <cstdio>

#include "../../support/check.hpp"

using namespace stencil::support;

namespace {
  bool near(double a, double b, double eps = 1e-5) { return std::abs(a - b) < eps; }
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  const LogoStageConfig& cfg = logoStageConfig();

  std::printf("the table:\n");
  {
    check(!cfg.shows.isEmpty(), "logoStage.json qrc alias resolves and parses");
    check(cfg.holdMs == 3000, "the hold is three seconds");
    check(!cfg.toast.isEmpty(), "the notice carries its text");
    check(cfg.shows.size() == 12, "every show is read");
    // Every accent preset opens a show — the browser asserts the same. Two rows may share a
    // colour when their `motion` modes tell them apart (grey: dust while it flies, else webcore).
    QFile f(":/config/accents.json");
    check(f.open(QIODevice::ReadOnly), "accents.json reads");
    const QJsonArray accents = QJsonDocument::fromJson(f.readAll()).array();
    check(accents.size() == 16, "16 presets to cover");
    bool covered = true;
    for (const QJsonValue& a : accents) {
      const QString key = a.toObject().value("key").toString();
      int owners = 0;
      for (const StageShow& s : cfg.shows)
        if (s.accents.contains(key)) ++owners;
      if (owners < 1) { covered = false; std::printf("       %s is owned by %d shows\n", qPrintable(key), owners); }
    }
    check(covered, "each preset opens a show");
  }

  std::printf("resolution:\n");
  {
    check(resolveShow("violet", MotionMode::SLIDE) == "neonOn", "violet: neon, whatever moves");
    check(resolveShow("orange", MotionMode::NONE) == "makeSomeSunshine", "orange: sun");
    check(resolveShow("crimson", MotionMode::FIRE) == "firework", "crimson + fire: fire");
    check(resolveShow("crimson", MotionMode::PARTICLES).isEmpty(), "crimson under dust: nothing");
    check(resolveShow("sky", MotionMode::WATER) == "waterShow", "sky + water: water");
    check(resolveShow("bluegray", MotionMode::PARTICLES) == "dustySpot", "bluegray + dust: dust");
    check(resolveShow("grey", MotionMode::PARTICLES) == "dustySpot", "grey + dust: dust");
    check(resolveShow("grey", MotionMode::NONE) == "webcore", "grey with the interface still: the skin");
    check(resolveShow("grey", MotionMode::SLIDE).isEmpty(), "grey under slide: nothing");
    check(resolveShow("grass", MotionMode::NONE) == "makeItSmall", "grass: shrink");
    check(resolveShow("brown", MotionMode::NONE) == "pushToBloat", "brown: grow");
    check(resolveShow("pink", MotionMode::NONE) == "pinkVibe", "pink: the edit");
    check(resolveShow("#ffffff", MotionMode::PARTICLES) == "chaseMe", "white follows");
    check(resolveShow("#000000", MotionMode::PARTICLES) == "runaway", "black escapes");
    check(resolveShow("#123456", MotionMode::PARTICLES) == "randomWalk", "any other custom flies");
    check(resolveShow("nosuch", MotionMode::PARTICLES).isEmpty(), "an unknown accent opens nothing");
    check(showByName("neonOn") && !showByName("nope"), "a show is found by name, and only a real one");
    check(typedWords().contains("neonno") == false && typedWords().contains("neonon"),
          "the typed words are the names, lower-cased");
    check(typedWords().contains("webcore") && showByName("webcore")->effect == StageEffect::WEBCORE,
          "the skin's word is one of them, with its own effect");
    check(resolveShow("violet", MotionMode::NONE) != "webcore" && !showHasCloud("webcore", nullptr),
          "…that only grey reaches and that wears no cloud");
  }

  std::printf("sizes and the heart:\n");
  {
    check(bigLogoSize(1000, 600) == 396, "the mark is a share of the shorter side");
    check(minLogoSize(1000, 600) == 60, "…and so is its smallest");
    check(bigLogoSize(400, 900) == 264, "a tall window measures across");
    check(bounceBigSize(1000, 600) == 540, "a bouncing show's big end nearly fills the window");
    const QVector<QPointF> pts = heartPoints(400, 300, 64);
    check(pts.size() == 64, "the heart is n points round");
    check(near(pts.at(0).x(), 200) && near(pts.at(0).y(), 93.41), "the notch (printed from node)");
    check(near(pts.at(16).x(), 320) && near(pts.at(16).y(), 100.91), "the right shoulder");
    check(near(pts.at(32).x(), 200) && near(pts.at(32).y(), 258.41), "the tip");
    check(near(pts.at(48).x(), 80) && near(pts.at(48).y(), 100.91), "mirrored on the left");

    // The outline a cloud is born on, printed from node for a 400px mark.
    const double deg = 3.14159265358979323846 / 180;
    check(near(markEdge(400, 0).x(), 187.5) && near(markEdge(400, 0).y(), 0), "straight out the panel side");
    check(near(markEdge(400, 20 * deg).y(), 68.2444, 1e-3), "…still flat at 20°");
    check(near(markEdge(400, 45 * deg).x(), 163.7024, 1e-3), "the corner is pulled in by its round");
    check(std::hypot(markEdge(400, 45 * deg).x(), markEdge(400, 45 * deg).y()) < 187.5 * std::sqrt(2.0),
          "…so a grain is never born out in the bare crescent a sharp square leaves");
    check(near(markEdge(400, 225 * deg).x(), -163.7024, 1e-3), "…and every corner is the same");
  }

  std::printf("the cloud a show wears:\n");
  {
    const auto styleOf = [](const char* name) {
      ParticleStyle st = ParticleStyle::DUST;
      return showHasCloud(QString::fromLatin1(name), &st) ? int(st) : -1;
    };
    setMotionMode(MotionMode::PARTICLES);
    check(styleOf("firework") == int(ParticleStyle::FIRE), "a styled show wears its OWN cloud");
    check(styleOf("dustySpot") == int(ParticleStyle::DUST), "…whatever the user is running");
    check(styleOf("pushToBloat") == int(ParticleStyle::DUST), "every other show wears the current one");
    check(styleOf("chaseMe") == int(ParticleStyle::DUST), "…roaming included");
    check(styleOf("neonOn") == -1, "neon IS the light and never wears a cloud");
    check(styleOf("makeSomeSunshine") == -1, "…nor does sun, which is its own ring of beams");
    setMotionMode(MotionMode::WATER);
    check(styleOf("makeItSmall") == int(ParticleStyle::WATER), "…and follows the style in use");
    setMotionMode(MotionMode::NONE);
    check(styleOf("makeItSmall") == int(ParticleStyle::DUST), "particles off: a show still wears dust");
    setMotionOverride({MotionMode::NONE, false, false});
    check(styleOf("makeItSmall") == int(ParticleStyle::DUST) && motionOverridden(),
          "…and a skin's stillness never reaches it");
    clearMotionOverride();
    setMotionMode(MotionMode::PARTICLES);
  }

  std::printf("kinematics:\n");
  {
    const StagePose from{24, 24, 32}, to{500, 300, 330};
    check(near(revealTween(from, to, 0).size, 32) && near(revealTween(from, to, 1).size, 330),
          "the reveal runs mark → stage");
    check(revealTween(from, to, 0.5).size > 181, "and eases out");

    BounceState b = bounceState(330, 60);
    check(near(bounceStep(b, 1000), 330), "at rest the size holds");
    bounceImpulse(b, 1000);
    check(b.to > 60 && b.to < 330, "one click is a STEP, never the whole way");
    check(near(bounceStep(b, 1120), 249), "…landing 30% of the way down, in 120ms");
    // 30% of the range, at the rate recoverMs sets for the whole of it: 900 * 0.3 = 270ms.
    check(near(bounceStep(b, 1120 + 270), 330), "…then glides home over 270ms");

    // A chase is THROWN at the cursor and drags behind: it arrives, carries past, comes back.
    ChaseState c = chaseState(100, 300);
    double closest = 1e9;
    bool overshot = false;
    for (int i = 0; i < 200; ++i) {
      chaseStep(c, QPointF(700, 300), 16, 80, 800, 600, false);
      closest = std::min(closest, std::abs(c.x - 700));
      if (c.x > 700) overshot = true;
    }
    check(closest < 40, "a chase reaches the cursor");
    check(overshot, "…and carries past — that is the inertia");
    check(c.x >= 40 && c.x <= 760, "…and stays inside the window");

    ChaseState far = chaseState(400, 300);
    chaseStep(far, QPointF(400, 1200), 16, 80, 800, 600, true);
    check(std::hypot(far.vx, far.vy) < 1, "a cursor past the radius barely moves a fleeing mark");
    ChaseState near2 = chaseState(400, 300);
    for (int i = 0; i < 40; ++i) chaseStep(near2, QPointF(380, 300), 16, 80, 800, 600, true);
    check(near2.x > 400, "a cursor beside it pushes it away");
    for (int i = 0; i < 400; ++i) chaseStep(near2, QPointF(380, 300), 16, 80, 800, 600, true);
    check(near2.x <= 760 && near2.x >= 40 && near2.y <= 560 && near2.y >= 40, "…and it never leaves");

    check(headingOfState(0, 0) == QPointF(0, 0), "a still mark has no heading");
    check(headingOfState(6, -8) == QPointF(0, 0), "…nor does a crawl — grains ring it instead");
    const QPointF hd = headingOfState(300, -400);
    check(near(hd.x(), 0.6) && near(hd.y(), -0.8), "…and a moving one gives a unit vector");

    FlyState fly = flyState(790, 300, 0);
    flyStep(fly, 100, 100, 800, 600);
    check(fly.vx < 0 && fly.x <= 750, "it bounces off the wall, back inside");
    flyPunch(fly, 3.14159265358979323846 / 2);
    check(near(std::hypot(fly.vx, fly.vy), cfg.flySpeedPx + cfg.flyPunchPx), "a punch is cruise + punch");
    for (int i = 0; i < 100; ++i) flyStep(fly, 100, 100, 800, 600);
    const double mag = std::hypot(fly.vx, fly.vy);
    check(mag < 270 && mag >= cfg.flySpeedPx - 1e-6, "…and damps back to cruise, never under");
  }

  std::printf("the typed letter (browser typedWords.test.js):\n");
  {
    check(latinLetterOfNative(KeyPlatform::MAC, 0x23, 0) == QLatin1Char('p'), "macOS kVK_ANSI_P is p");
    check(latinLetterOfNative(KeyPlatform::LINUX, 0, 33) == QLatin1Char('p'), "evdev KEY_P + 8 is p");
    check(latinLetterOfNative(KeyPlatform::WINDOWS, 'P', 0) == QLatin1Char('p'), "VK_P is p");
    check(latinLetterOfNative(KeyPlatform::MAC, 0x24, 0).isNull(), "Return is no letter");
    check(latinLetterOfNative(KeyPlatform::LINUX, 0, 36).isNull(), "…on any platform");
    bool roundTrip = true;
    for (KeyPlatform p : {KeyPlatform::MAC, KeyPlatform::WINDOWS, KeyPlatform::LINUX})
      for (char c = 'a'; c <= 'z'; ++c) {
        const quint32 code = nativeCodeOfLetter(p, QLatin1Char(c));
        roundTrip = roundTrip && latinLetterOfNative(p, code, code) == QLatin1Char(c);
      }
    check(roundTrip, "every letter's native code reads back as that letter, on every platform");
    const quint32 p = nativeCodeOfLetter(hostKeyPlatform(), QLatin1Char('p'));
    QKeyEvent cyrillic(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, p, p, 0, QString::fromUtf8("з"));
    check(typedLetter(cyrillic) == QLatin1Char('p'), "a Cyrillic key spells its position's US letter");
    QKeyEvent azerty(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier,
                     nativeCodeOfLetter(hostKeyPlatform(), QLatin1Char('q')),
                     nativeCodeOfLetter(hostKeyPlatform(), QLatin1Char('q')), 0, QStringLiteral("a"));
    check(typedLetter(azerty) == QLatin1Char('a'), "a Latin key spells its own label (AZERTY)");
    QKeyEvent digit(QEvent::KeyPress, Qt::Key_1, Qt::NoModifier, QStringLiteral("1"));
    check(typedLetter(digit) == QLatin1Char('1'), "any other character still joins the buffer");
    QKeyEvent shift(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier);
    check(typedLetter(shift).isNull(), "a bare modifier is nothing");
  }

  std::printf(failures ? "FAILED: %d\n" : "all passed (%d failures)\n", failures);
  return failures ? 1 : 0;
}
