#pragma once
// The chat dock's slide and its dust flight, one clock each way, ms: common/config/motion.json's
// CHAT_SURFACE_IN_MS / CHAT_SURFACE_OUT_MS through the qrc, the pair the browser's chat panel flies
// on; read once, tests/app/chrome/chatSlideClocks holds them.
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>

namespace stencil::gui {

  struct ChatSlideClocks {
    int inMs = 630;    // opening: the grains gather while the dock grows
    int outMs = 510;   // leaving, the quicker half; a compact popover waits it out
  };

  inline const ChatSlideClocks& chatSlideClocks() {
    static const ChatSlideClocks c = [] {
      ChatSlideClocks out;
      QFile f(QStringLiteral(":/config/motion.json"));
      if (!f.open(QIODevice::ReadOnly)) return out;
      const QJsonObject ui = QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("ui")).toObject();
      const auto ms = [&ui](const char* key, int fallback) {
        const int v = ui.value(QLatin1String(key)).toInt(0);
        return v > 0 ? v : fallback;
      };
      out.inMs = ms("CHAT_SURFACE_IN_MS", out.inMs);
      out.outMs = ms("CHAT_SURFACE_OUT_MS", out.outMs);
      return out;
    }();
    return c;
  }

  // The docked chat's close, tuned by eye against the browser's (user report): 1.5x quicker, then 1.3x
  // slower again; the table keeps the browser's.
  inline int chatCloseMs() { return int(std::lround(chatSlideClocks().outMs / 1.5 * 1.3)); }

}  // namespace stencil::gui
