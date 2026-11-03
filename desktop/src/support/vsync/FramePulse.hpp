#pragma once
// The app's one frame clock: every frame-rate motion (Qt's animations through AnimationDriver.hpp, the
// genie, the dust, the swaps) steps in the same event-loop turn, once per display refresh, so a frame
// lays out and repaints once. No browser twin: requestAnimationFrame already is this.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace stencil::support {

  class FramePulse : public QObject {
   public:
    // EARLY steps run first in a beat: a ghost that follows a sliding toggle reads the layout this beat set.
    enum Order { EARLY, LATE };

    static FramePulse& instance() {
      if (!slot()) slot() = new FramePulse(QCoreApplication::instance());
      return *slot();
    }

    // `step` runs once per beat while `owner` lives and it returns true.
    void join(QObject* owner, std::function<bool()> step, Order order = EARLY) {
      const auto at = std::find_if(subs.begin(), subs.end(), [order](const SubPtr& s) { return s->order > order; });
      subs.insert(at, std::make_shared<Sub>(Sub{QPointer<QObject>(owner), std::move(step), order}));
      if (!watchdog.isActive()) wake();
    }

    int size() const { return int(subs.size()); }
    // True when the display's refresh beats the clock (cocoa's CVDisplayLink), not a timer.
    bool isSynced() const { return beatWindow != nullptr; }

    // The refresh interval of the screen the user works on (ms, 4–16).
    static int frameMs() {
      const QWindow* focus = QGuiApplication::focusWindow();
      const QScreen* s = focus && focus->screen() ? focus->screen() : QGuiApplication::primaryScreen();
      const double hz = s ? s->refreshRate() : 60.0;
      return hz > 1.0 ? std::clamp(int(std::floor(1000.0 / hz)), 4, 16) : 16;
    }

   private:
    // Never shown: cocoa answers its update requests from the screen's display link, a window it need not draw.
    class BeatWindow : public QWindow {
     public:
      explicit BeatWindow(FramePulse* pulse) : pulse(pulse) {
        setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
        setGeometry(0, 0, 1, 1);
        create();
      }
     protected:
      bool event(QEvent* e) override {
        if (e->type() != QEvent::UpdateRequest) return QWindow::event(e);
        pulse->beat(true);
        return true;
      }
     private:
      FramePulse* pulse;
    };

    // Shared, so a beat's snapshot copies pointers rather than every step's closure.
    struct Sub { QPointer<QObject> owner; std::function<bool()> step; Order order; bool isDone = false; };
    using SubPtr = std::shared_ptr<Sub>;

    explicit FramePulse(QObject* parent) : QObject(parent) {
      watchdog.setSingleShot(true);
      watchdog.setTimerType(Qt::PreciseTimer);
      connect(&watchdog, &QTimer::timeout, this, [this] { beat(false); });
      if (QGuiApplication::platformName() == QLatin1String("cocoa") && !qEnvironmentVariableIsSet("STENCIL_FRAME_TIMER")) {
        beatWindow = new BeatWindow(this);
        qAddPostRoutine(&dropBeatWindow);
      }
      since.start();
    }
    static QPointer<FramePulse>& slot() {
      static QPointer<FramePulse> pulse;
      return pulse;
    }
    // At the app's teardown, while the platform that made the window still stands.
    static void dropBeatWindow() {
      if (!slot()) return;
      delete slot()->beatWindow;
      slot()->beatWindow = nullptr;
      slot()->watchdog.stop();
    }

    // The first beat: the display's, or a frame's wait on the timer.
    void wake() {
      requestBeat();
      watchdog.start(isSynced() ? STALL_FRAMES * frameMs() : frameMs());
    }

    void requestBeat() {
      if (!beatWindow) return;
      const QWindow* focus = QGuiApplication::focusWindow();
      if (QScreen* s = focus ? focus->screen() : nullptr; s && s != beatWindow->screen()) beatWindow->setScreen(s);
      beatWindow->requestUpdate();
    }

    // `fromLink`: the display's beat; else the watchdog's, which carries on at the refresh rate until the link
    // answers again (a screen gone offline, a window server that stalls it). A link that fires faster than half a
    // frame (cocoa's own timer fallback) is not stepped twice.
    void beat(bool fromLink) {
      if (subs.empty()) { watchdog.stop(); return; }
      const int frame = frameMs();
      if (!fromLink || since.elapsed() * 2 >= frame) {
        since.restart();
        const std::vector<SubPtr> now = subs;   // a step may join another mid-beat
        for (const SubPtr& s : now)
          if (!s->owner || !s->step()) s->isDone = true;
        subs.erase(std::remove_if(subs.begin(), subs.end(), [](const SubPtr& s) { return s->isDone || !s->owner; }),
                   subs.end());
      }
      if (subs.empty()) { watchdog.stop(); return; }
      requestBeat();
      watchdog.start(isSynced() && fromLink ? STALL_FRAMES * frame : frame);
    }

    // Beats the display may miss before the timer steps in.
    static constexpr int STALL_FRAMES = 3;
    BeatWindow* beatWindow = nullptr;
    QTimer watchdog;
    QElapsedTimer since;
    std::vector<SubPtr> subs;
  };

  // A QTimer's start/stop/isActive over the pulse: `tick` runs once per beat while active.
  class FrameTicker : public QObject {
   public:
    FrameTicker(QObject* parent, std::function<void()> tick, FramePulse::Order order = FramePulse::EARLY)
        : QObject(parent), tick(std::move(tick)), order(order) {}

    void start() {
      active = true;
      if (joined) return;
      joined = true;
      QPointer<FrameTicker> self(this);
      FramePulse::instance().join(this, [self] {
        if (!self->active) return self->joined = false;
        self->tick();   // may stop, restart or delete the ticker
        if (!self) return false;
        return self->joined = self->active;
      }, order);
    }
    void stop() { active = false; }
    bool isActive() const { return active; }

   private:
    std::function<void()> tick;
    FramePulse::Order order;
    bool active = false;
    bool joined = false;   // a step is on the pulse; a restart between beats reuses it
  };

}  // namespace stencil::support
