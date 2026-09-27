// The shared SSRF host corpus (browser/js/config/fixtures/net/hosts.json) through the real guard:
// each host read as the address it names (or as a name), then judged under all four policy variants
// of net/blockedRanges.json, as a bare literal and, for `fetch`, inside a URL. A local override
// (tests/fixtureOverrides.json, "net/<name>") replaces verdicts with measured ones.
#include "fetchGuard.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <cstdio>

#include "../support/check.hpp"
#include "../support/fixtureCorpus.hpp"

namespace guard = stencil::net::fetchGuard;
namespace ranges = stencil::net::blockedRanges;

namespace {

  struct Variant {
    const char* key;
    ranges::Policy policy;
    ranges::Options options;
  };

  const Variant VARIANTS[] = {
      {"fetch", ranges::Policy::FETCH, {false, false}},
      {"fetch+allowLoopback", ranges::Policy::FETCH, {true, false}},
      {"serverTarget", ranges::Policy::SERVER_TARGET, {false, false}},
      {"serverTarget+allowPrivate", ranges::Policy::SERVER_TARGET, {false, true}},
  };

  // How a walker writes the host into a URL authority: IPv6 bracketed, a zone's `%` as `%25`.
  QUrl urlFor(QString host) {
    host.replace(QLatin1Char('%'), QStringLiteral("%25"));
    if (host.contains(QLatin1Char(':')) && !host.startsWith(QLatin1Char('['))) host = '[' + host + ']';
    return QUrl(QStringLiteral("http://%1/x.png").arg(host));
  }

  // Every way this case disagrees with its expectation, "" when none.
  QString disagreements(const QString& host, const QJsonValue& address, const QJsonObject& expect) {
    QStringList out;
    if (address.isNull()) {
      if (guard::isNumericHost(host)) out << QStringLiteral("a name read as a literal");
      for (const Variant& v : VARIANTS)
        if (guard::refusesLiteral(host, v.policy, v.options)) out << QStringLiteral("a name judged under %1").arg(v.key);
      return out.join(QStringLiteral("; "));
    }
    if (guard::hostAddress(host) != QHostAddress(address.toString()))
      out << QStringLiteral("read as %1").arg(guard::hostAddress(host).toString());
    for (const Variant& v : VARIANTS) {
      const bool want = expect.value(QLatin1String(v.key)).toString() == QLatin1String("block");
      if (guard::refusesLiteral(host, v.policy, v.options) != want) out << QStringLiteral("%1 literal").arg(v.key);
      if (v.policy != ranges::Policy::FETCH) continue;
      const bool strict = !v.options.allowLoopback;
      if (!guard::blockedReason(urlFor(host), strict).isEmpty() != want) out << QStringLiteral("%1 URL").arg(v.key);
    }
    return out.join(QStringLiteral("; "));
  }

}  // namespace

void checkHostsCorpus() {
  std::printf("host corpus (fixtures/net/hosts.json, net/blockedRanges.json):\n");
  check(ranges::tableError().isEmpty(), "the net/blockedRanges.json qrc alias loads");
  const QJsonArray cases = readJsonFile(corpusPath("fixtures/net/hosts.json")).array();
  check(cases.size() >= 80, qPrintable(QStringLiteral("hosts.json holds %1 cases").arg(cases.size())));
  int overridden = 0;
  for (const QJsonValue& cv : cases) {
    const QJsonObject c = cv.toObject();
    const QString name = c.value("name").toString(), host = c.value("host").toString();
    QJsonObject expect = c.value("expect").toObject();
    const FixtureOverride ov = findOverride(QStringLiteral("net"), name);
    if (ov.present) {
      ++overridden;
      const QJsonObject local = ov.verdict.toObject();
      for (auto it = local.begin(); it != local.end(); ++it) expect.insert(it.key(), it.value());
    }
    const QString wrong = disagreements(host, c.value("address"), expect);
    check(wrong.isEmpty(), qPrintable(QStringLiteral("%1 (%2)%3%4").arg(name, host, ov.present ? " [override]" : "",
                                                                         wrong.isEmpty() ? "" : " — " + wrong)));
  }
  std::printf("  local overrides %d\n", overridden);
}
