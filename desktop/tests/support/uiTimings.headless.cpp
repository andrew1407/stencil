// The desktop's shared interaction timings (support/uiTimings.hpp) are read from
// common/config/constants.json and motion.json, the sources both surfaces read: read, fallback
// and table agree, and the FLIP glide's curve is the CSS cubic-bezier exactly.
#include "uiTimings.hpp"

#include <QCoreApplication>
#include <cmath>
#include <cstdio>

#include "check.hpp"

namespace {
  int tableValue(const char* section, const char* key) {
    QFile f(QStringLiteral(":/config/constants.json"));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String(section)).toObject()
        .value(QLatin1String(key)).toInt(-1);
  }

  void pin(const char* section, const char* key, int read, int fallback) {
    const QByteArray name = QByteArray(section) + "." + key;
    check(tableValue(section, key) == read, (name + " is read from constants.json").constData());
    check(fallback == read, (name + " falls back to the table's value").constData());
  }

  QJsonValue motionValue(const char* key) {
    QFile f(QStringLiteral(":/config/motion.json"));
    if (!f.open(QIODevice::ReadOnly)) return QJsonValue();
    return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("ui")).toObject()
        .value(QLatin1String(key));
  }

  // CSS cubic-bezier(x1, y1, x2, y2) at x: x(u) solved by bisection, then y(u).
  double cssAt(double x1, double y1, double x2, double y2, double x) {
    const auto bez = [](double a, double b, double u) {
      return 3 * a * u * (1 - u) * (1 - u) + 3 * b * u * u * (1 - u) + u * u * u;
    };
    double lo = 0, hi = 1;
    for (int i = 0; i < 60; ++i) {
      const double mid = (lo + hi) / 2;
      if (bez(x1, x2, mid) < x) lo = mid;
      else hi = mid;
    }
    return bez(y1, y2, (lo + hi) / 2);
  }

  bool sameCurve(const QEasingCurve& a, const QEasingCurve& b) {
    for (double t = 0; t <= 1.0; t += 0.05)
      if (std::abs(a.valueForProgress(t) - b.valueForProgress(t)) > 1e-9) return false;
    return a.type() == b.type();
  }
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const stencil::support::UiTimings& t = stencil::support::uiTimings();
  const stencil::support::UiTimings f;
  pin("DEBOUNCE", "previewHoverMs", t.previewHoverMs, f.previewHoverMs);
  pin("POPOVER", "doubleClickMs", t.doubleClickMs, f.doubleClickMs);
  pin("POPOVER", "pressSlopPx", t.pressSlopPx, f.pressSlopPx);
  pin("POPOVER", "lingerCloseMs", t.lingerCloseMs, f.lingerCloseMs);

  namespace support = stencil::support;
  const support::FlipMotion& flip = support::flipMotion();
  const support::FlipMotion flipFallback;
  check(motionValue("FLIP_MS").toInt(-1) == flip.ms, "FLIP_MS is read from motion.json");
  check(flipFallback.ms == flip.ms, "FLIP_MS falls back to the table's value");
  const QString easing = motionValue("FLIP_EASING").toString();
  check(easing == QStringLiteral("cubic-bezier(0.16, 1, 0.22, 1)"), "motion.json holds FLIP_EASING");
  check(flip.easing.type() == QEasingCurve::BezierSpline, "FLIP_EASING is read as a spline, not a stand-in");
  check(sameCurve(flip.easing, flipFallback.easing), "FLIP_EASING falls back to the table's curve");
  bool exact = true;
  for (double x : {0.05, 0.2, 0.5, 0.8, 0.95})
    exact = exact && std::abs(flip.easing.valueForProgress(x) - cssAt(0.16, 1, 0.22, 1, x)) < 1e-3;
  check(exact, "the glide follows cubic-bezier(0.16, 1, 0.22, 1)");
  const QEasingCurve stand(QEasingCurve::OutQuint);
  check(sameCurve(support::cssCubicBezier(QStringLiteral("ease-out"), stand), stand) &&
            sameCurve(support::cssCubicBezier(QStringLiteral("cubic-bezier(1.5, 0, 0, 1)"), stand), stand) &&
            sameCurve(support::cssCubicBezier(QStringLiteral("cubic-bezier(0, 1)"), stand), stand),
        "text that is not a CSS cubic-bezier keeps the fallback");
  std::printf("%s\n", failures == 0 ? "uiTimings: all checks passed" : "uiTimings: FAILURES");
  return failures == 0 ? 0 : 1;
}
