#pragma once
// An animated skin's runtime on the CLI stage: the frame clock the letters, hues and cat move on,
// the matrix skin's digit rain down the body, and the fairy lights' bulbs recolouring the accent.
// Ports of cli/src/console/render/logoFx/rain.zig and skin/hues.zig; browser twin js/ui/cli/skin/.
#include "CliSkins.hpp"
#include <QObject>
#include <QStringList>
#include <QVector>
#include <functional>

class QTimer;

namespace stencil::gui {

  class CliSkinFx : public QObject {
    Q_OBJECT
   public:
    explicit CliSkinFx(QObject* parent = nullptr);

    void start(const CliSkin& skin, int bodyCols, int bodyRows);
    void stop();
    bool running() const { return on; }
    const CliSkinClock* clock() const { return on ? &clk : nullptr; }

    bool raining() const { return rainLeft > 0; }
    const QStringList& rainRows() const { return rain; }
    // A keystroke ends the rain early.
    void cutShort();

    void setClockSource(std::function<qint64()> now) { this->now = std::move(now); }
    // The timers' bodies, for a test to drive.
    void tick();
    void rainStep();

   signals:
    void frame();
    void bulb(const QColor& color);
    void rainChanged();

   private:
    QTimer* ticker;
    QTimer* rainTimer;
    CliSkinClock clk;
    std::function<qint64()> now;
    bool on = false;
    bool fairy = false;
    QStringList rain;
    QVector<int> heads;
    int rainLeft = 0;
  };

}  // namespace stencil::gui
