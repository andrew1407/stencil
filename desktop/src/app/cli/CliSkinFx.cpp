#include "CliSkinFx.hpp"
#include "CliBanner.hpp"

#include <QDateTime>
#include <QRandomGenerator>
#include <QTimer>

namespace stencil::gui {

  CliSkinFx::CliSkinFx(QObject* parent) : QObject(parent), ticker(new QTimer(this)), rainTimer(new QTimer(this)) {
    now = [] { return QDateTime::currentMSecsSinceEpoch(); };
    connect(ticker, &QTimer::timeout, this, &CliSkinFx::tick);
    connect(rainTimer, &QTimer::timeout, this, &CliSkinFx::rainStep);
  }

  void CliSkinFx::start(const CliSkin& skin, int bodyCols, int bodyRows) {
    stop();
    on = true;
    fairy = skin.animated && !skin.paintsRuns && !skin.replacesLetters && !skin.movesLogo;
    clk.start(now());
    if (skin.animated) ticker->start(cliFrameMs());
    // The matrix skin: paintsCells with no title bar rains its digits down the body first.
    if (skin.paintsCells && skin.title.isEmpty() && bodyCols > 0 && bodyRows > 0) {
      const CliFx& fx = cliFx();
      rain = QStringList();
      for (int r = 0; r < bodyRows; ++r) rain << QString(bodyCols, QLatin1Char(' '));
      heads.clear();
      for (int c = 0; c < bodyCols; ++c) heads.push_back(-int(QRandomGenerator::global()->bounded(bodyRows + fx.rainTrail)));
      rainLeft = fx.rainFrames;
      rainTimer->start(fx.rainFrameMs);
    }
  }

  void CliSkinFx::stop() {
    ticker->stop();
    rainTimer->stop();
    on = false;
    fairy = false;
    if (rainLeft > 0) {
      rainLeft = 0;
      rain.clear();
      emit rainChanged();
    }
  }

  void CliSkinFx::cutShort() {
    if (rainLeft <= 0) return;
    rainTimer->stop();
    rainLeft = 0;
    rain.clear();
    emit rainChanged();
  }

  void CliSkinFx::tick() {
    if (!on || !clk.sync(now())) return;
    if (fairy) {
      if (const std::optional<QColor> next = clk.nextBulb()) emit bulb(*next);
      return;
    }
    emit frame();
  }

  // One frame's drops: every head falls a row; one past the bottom by the trail starts afresh above.
  void CliSkinFx::rainStep() {
    if (rainLeft <= 0) return;
    const int rows = rain.size();
    const int trail = cliFx().rainTrail;
    auto* rng = QRandomGenerator::global();
    for (int c = 0; c < heads.size(); ++c) {
      const int next = heads[c] + 1;
      heads[c] = next - trail > rows ? -int(rng->bounded(qMax(1, rows))) : next;
      const int r = heads[c];
      if (r >= 0 && r < rows) rain[r][c] = rng->bounded(2) ? QLatin1Char('1') : QLatin1Char('0');
      if (r - trail >= 0 && r - trail < rows) rain[r - trail][c] = QLatin1Char(' ');
    }
    if (--rainLeft <= 0) {
      rainTimer->stop();
      rain.clear();
    }
    emit rainChanged();
  }

}  // namespace stencil::gui
