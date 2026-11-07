#pragma once
// common/config/motion.json's `ui` table through the qrc: the clocks and shares both surfaces tune in one place.
// A build without the resource answers each reader's fallback.
// Browser twin: ui/motion/tune.js TUNE.
class QJsonObject;

namespace stencil::support {

  const QJsonObject& motionTable();
  // A clock (ms) by its key; `fallback` where the table has none.
  int motionTableMs(const char* key, int fallback);
  // A share (0–1) by its key.
  double motionTableShare(const char* key, double fallback);

}  // namespace stencil::support
