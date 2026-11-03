#pragma once
// Qt's animations (every QPropertyAnimation and QVariantAnimation) advanced on the frame pulse
// (FramePulse.hpp) rather than Qt's own fixed 16 ms timer, which beats against the display and doubles
// a step now and then. Installed once, by the app's main(). No browser twin.
#include "FramePulse.hpp"

#include <QAbstractAnimation>
#include <QPointer>

namespace stencil::support {

  class AnimationDriver : public QAnimationDriver {
   public:
    using QAnimationDriver::QAnimationDriver;

    // The app's driver, parented to the application; idempotent.
    static void installForApp() {
      static QPointer<AnimationDriver> driver;
      if (driver || !QCoreApplication::instance()) return;
      driver = new AnimationDriver(QCoreApplication::instance());
      driver->QAnimationDriver::install();
    }

    void start() override {
      QAnimationDriver::start();
      if (joined) return;
      joined = true;
      FramePulse::instance().join(this, [this] {
        if (isRunning()) advance();
        return joined = isRunning();
      });
    }

   private:
    bool joined = false;   // a step is on the pulse; a stop and start between beats reuses it
  };

}  // namespace stencil::support
