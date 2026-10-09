// The Animation speed: its range out of motion.json, the clamp, the duration scale, and the key.
#include "motionPrefsParts.hpp"

#include <limits>

namespace motionprefs {

  void checkSpeed() {
    std::printf("speed:\n");
    const support::MotionSpeedRange& r = support::motionSpeedRange();
    check(r.min == 0.1 && r.max == 1.0 && r.def == 0.37 && r.step == 0.01,
          "the range is motion.json's speed (0.1–1, default 0.37, step 0.01)");
    check(Settings().motionSpeed == r.def, "the settings default is the table's");

    check(support::clampMotionSpeed(0.05) == 0.1 && support::clampMotionSpeed(4.0) == 1.0,
          "out of range clamps to the ends");
    check(support::clampMotionSpeed(-1.0) == 0.1, "a negative speed is the slowest");
    check(support::clampMotionSpeed(std::numeric_limits<double>::quiet_NaN()) == 0.37
              && support::clampMotionSpeed(std::numeric_limits<double>::infinity()) == 0.37,
          "a non-number is the default");
    check(support::clampMotionSpeed(0.456) == 0.46, "kept to hundredths");

    support::setMotionSpeed(r.def);
    check(support::motionScale() == 1.0, "the default scales by exactly 1");
    bool same = true;
    for (int ms : {1, 16, 140, 220, 413, 470, 760, 1650}) same = same && support::motionMs(ms) == ms;
    check(same && support::motionElapsed(1234) == 1234, "…so every tuned clock plays as tuned");

    support::setMotionSpeed(1.0);
    check(support::motionMs(1000) == 370 && support::motionElapsed(370) == 1000,
          "the fastest plays at 0.37 of the length");
    check(support::motionMs(1) == 1 && support::motionMs(0) == 0, "never below a millisecond, 0 stays 0");
    {
      QVariantAnimation anim;
      anim.setLoopCount(-1);
      support::startTuned(&anim, 1000);
      check(anim.duration() == 370 && anim.state() == QAbstractAnimation::Running,
            "an animation started tuned plays at the speed in force as it starts");
      anim.stop();
    }
    support::setMotionSpeed(0.1);
    check(support::motionMs(1000) == 3700, "the slowest plays 3.7 times as long");
    check(support::motionElapsed(370, 0.37) == 1000 && support::motionElapsed(370, 1.0) == 370,
          "a play reads the scale it started with, whatever the speed is now");
    support::setMotionSpeed(9.0);
    check(support::motionSpeed() == 1.0, "the setter clamps");
    support::setMotionSpeed(r.def);

    Settings s;
    s.motionSpeed = 0.8;
    check(fileStore::settingsFromJson(fileStore::settingsToJson(s)).motionSpeed == 0.8, "the speed round-trips");
    check(fileStore::settingsFromJson(QJsonObject()).motionSpeed == r.def, "an absent key is the default");
  }

}  // namespace motionprefs
