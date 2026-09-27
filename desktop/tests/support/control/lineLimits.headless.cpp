// The thickness and point-size ranges (support/control/lineLimits.hpp) come from
// browser/js/config/constants.json LIMITS, with the values the spin boxes always took; read,
// fallback and the former literals agree.
#include "lineLimits.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

#include "../check.hpp"

namespace {
  int tableValue(const char* key) {
    QFile f(QStringLiteral(":/config/constants.json"));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("LIMITS")).toObject()
        .value(QLatin1String(key)).toInt(-1);
  }

  void pin(const char* key, int read, int fallback, int literal) {
    const QByteArray what = QByteArray("LIMITS.") + key;
    check(tableValue(key) == read, (what + " is read from constants.json").constData());
    check(fallback == read, (what + " falls back to the table's value").constData());
    check(read == literal, (what + " keeps the spin boxes' range").constData());
  }
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  namespace limits = stencil::support::lineLimits;
  const limits::Table& t = limits::table();
  const limits::Table f;
  pin("thickMin", t.thickMin, f.thickMin, 1);
  pin("thickMax", t.thickMax, f.thickMax, 20);
  pin("pointMin", t.pointMin, f.pointMin, 1);
  pin("pointMax", t.pointMax, f.pointMax, 30);
  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
