// Headless check for the untrusted-fetch SSRF guard (net/fetchGuard) — the desktop port
// of cli/src/net.zig, so these cases mirror its own tests: every internal class is
// refused for an IP literal AND for the alternate numeric encodings a resolver accepts,
// loopback flips with `strict`, IPv4-mapped IPv6 is classified by its embedded IPv4, a
// normal public host is allowed, and the request the guard hands out carries the house
// timeout and follows no redirect. Pure QtNetwork — nothing is fetched.
#include "fetchGuard.hpp"

#include <QCoreApplication>
#include <QNetworkRequest>
#include <QUrl>
#include <cstdio>
#include <iterator>

#include "support/check.hpp"

namespace guard = stencil::net::fetchGuard;

namespace {

  struct HostCase {
    const char* host;
    bool blockedLoose;  // strict=false (a URL the user named)
    bool blockedStrict;
    const char* why;
  };

  // The blocked set, by class. Loopback is the only row that differs between the modes.
  const HostCase HOSTS[] = {
      {"0.0.0.0", true, true, "0.0.0.0/8 this-network"},
      {"0.1.2.3", true, true, "…and the rest of 0/8"},
      {"10.0.0.5", true, true, "10/8 private"},
      {"100.64.0.1", true, true, "100.64/10 CGNAT"},
      {"100.127.255.255", true, true, "…to the top of the CGNAT block"},
      {"169.254.169.254", true, true, "169.254/16 cloud metadata"},
      {"172.16.4.4", true, true, "172.16/12 private"},
      {"172.31.255.255", true, true, "…to the top of 172.16/12"},
      {"192.168.1.1", true, true, "192.168/16 private"},
      {"192.0.0.1", true, true, "192.0.0/24 IETF protocol assignments"},
      {"192.0.2.7", true, true, "TEST-NET-1"},
      {"198.18.0.1", true, true, "198.18/15 benchmarking"},
      {"198.19.255.1", true, true, "…and its second half"},
      {"198.51.100.4", true, true, "TEST-NET-2"},
      {"203.0.113.9", true, true, "TEST-NET-3"},
      {"240.0.0.1", true, true, "240/4 reserved"},
      {"255.255.255.255", true, true, "the broadcast address"},
      {"::", true, true, "the IPv6 unspecified address"},
      {"fe80::1", true, true, "fe80::/10 link-local"},
      {"febf::1", true, true, "…to the top of fe80::/10"},
      {"fec0::1", true, true, "fec0::/10 site-local"},
      {"fc00::1", true, true, "fc00::/7 unique-local"},
      {"fd12:3456::1", true, true, "…and its fd00::/8 half"},
      {"::ffff:169.254.169.254", true, true, "IPv4-mapped metadata"},
      {"::ffff:10.0.0.1", true, true, "IPv4-mapped private"},
      // Loopback: a local dev/fixture server the USER named is legitimate; a URL taken
      // from scanned or shared content must not reach it (net.zig's two-tier design).
      {"127.0.0.1", false, true, "loopback"},
      {"127.9.9.9", false, true, "…anywhere in 127/8"},
      {"::1", false, true, "IPv6 loopback"},
      {"::ffff:127.0.0.1", false, true, "IPv4-mapped loopback"},
      {"localhost", false, true, "the literal name"},
      {"LOCALHOST", false, true, "…case-insensitively"},
      // Public hosts stay reachable in both modes.
      {"example.com", false, false, "a public name"},
      {"cdn.example.org", false, false, "a public sub-domain"},
      {"8.8.8.8", false, false, "a public IPv4"},
      {"93.184.216.34", false, false, "…and another"},
      {"2606:2800:220:1:248:1893:25c8:1946", false, false, "a public IPv6"},
      {"12345.example.com", false, false, "a name that merely starts numeric"},
  };

  // Alternate numeric encodings of the SAME internal addresses — what a resolver would
  // accept but a dotted-quad-only parser misses.
  const HostCase ENCODINGS[] = {
      {"2852039166", true, true, "169.254.169.254 as bare decimal"},
      {"0xA9FEA9FE", true, true, "…as hex"},
      {"0xa9fea9fe", true, true, "…as lower-case hex"},
      {"0251.0376.0251.0376", true, true, "…as octal quads"},
      {"169.254.43518", true, true, "…short-dotted (3 parts)"},
      {"169.16689662", true, true, "…short-dotted (2 parts)"},
      {"167772161", true, true, "10.0.0.1 as decimal"},
      {"0x0A000001", true, true, "…as hex"},
      {"10.0", true, true, "10.0.0.0 short-dotted"},
      {"0300.0250.0.1", true, true, "192.168.0.1 with octal parts"},
      {"2130706433", false, true, "127.0.0.1 as decimal — loopback, so mode-dependent"},
      {"0x7f000001", false, true, "…as hex"},
      {"134744072", false, false, "8.8.8.8 as decimal is public"},
      {"999999999999", false, false, "a value past u32 is not an IPv4 form"},
      {"1.2.3.4.5", false, false, "five parts is not an IPv4 form"},
      {"10.0.0.256", false, false, "an out-of-range octet is not an IPv4 form"},
      {"0x", false, false, "a bare 0x prefix is not a number"},
      {"09", false, false, "a bad octal digit is not a number"},
  };

  void runHostTable(const HostCase* cases, int n, const char* label) {
    std::printf("%s:\n", label);
    for (int i = 0; i < n; ++i) {
      const HostCase& c = cases[i];
      char msg[192];
      std::snprintf(msg, sizeof(msg), "%s (%s) — %s%s", c.host, c.why,
                    c.blockedLoose ? "blocked" : "allowed",
                    c.blockedStrict == c.blockedLoose ? " in both modes"
                                                      : ", blocked when strict");
      check(guard::isBlockedHost(QString::fromLatin1(c.host), false) == c.blockedLoose &&
                guard::isBlockedHost(QString::fromLatin1(c.host), true) == c.blockedStrict,
            msg);
    }
  }

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  runHostTable(HOSTS, int(std::size(HOSTS)), "host classes");
  runHostTable(ENCODINGS, int(std::size(ENCODINGS)), "alternate numeric encodings");

  std::printf("numeric vs name:\n");
  check(guard::isNumericHost("169.254.169.254") && guard::isNumericHost("::1") &&
            guard::isNumericHost("0xA9FEA9FE") && guard::isNumericHost("10.0"),
        "every IP form reads as numeric (so it needs no lookup)");
  check(!guard::isNumericHost("example.com") && !guard::isNumericHost("localhost") &&
            !guard::isNumericHost("12345.example.com"),
        "…and a DNS name does not");

  std::printf("URLs:\n");
  check(!guard::blockedReason(QUrl("http://169.254.169.254/latest/meta-data/"), false).isEmpty(),
        "a metadata URL is refused");
  check(!guard::blockedReason(QUrl("http://[::1]:9000/x"), true).isEmpty(),
        "a bracketed IPv6 loopback URL is refused when strict");
  check(!guard::blockedReason(QUrl("http://user:pass@10.0.0.1:8080/x"), false).isEmpty(),
        "userinfo and a port do not hide the host");
  check(!guard::blockedReason(QUrl("http:///only-path"), false).isEmpty(),
        "a URL with no host is refused");
  check(!guard::blockedReason(QUrl("file:///etc/passwd"), false).isEmpty(),
        "a file: URL is refused");
  check(guard::blockedReason(QUrl("https://example.com/a.png"), true).isEmpty(),
        "…while a normal public image URL is allowed, even when strict");
  check(guard::blockedReason(QUrl("http://127.0.0.1:8080/fixture.png"), false).isEmpty(),
        "…and the user's own localhost fixture server stays reachable");

  std::printf("scheme gate (the OS URL handler):\n");
  check(guard::isWebScheme(QUrl("http://example.com")) &&
            guard::isWebScheme(QUrl("https://example.com")) &&
            guard::isWebScheme(QUrl("HTTPS://example.com")),
        "http(s) may be opened");
  check(!guard::isWebScheme(QUrl("file:///etc/passwd")) &&
            !guard::isWebScheme(QUrl("smb://host/share")) &&
            !guard::isWebScheme(QUrl("javascript:alert(1)")) &&
            !guard::isWebScheme(QUrl("data:text/html,x")) &&
            !guard::isWebScheme(QUrl("ftp://host/x")),
        "…and nothing else is");

  std::printf("request shape:\n");
  const QNetworkRequest req = guard::request(QUrl("https://example.com/a.png"));
  check(req.transferTimeout() == guard::FETCH_TIMEOUT_MS, "the house transfer timeout is set");
  check(req.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() ==
            int(QNetworkRequest::ManualRedirectPolicy),
        "…and redirects are refused: a public first hop cannot 30x-bounce inward");
  check(guard::MAX_FETCH_BYTES == 64 * 1024 * 1024, "the body cap matches the CLI's 64 MiB");

  // The DNS leg, exercised on the one name every machine resolves internally.
  std::printf("resolution:\n");
  check(guard::resolvesToBlocked("localhost", true),
        "a name resolving to loopback is refused when strict");
  check(!guard::resolvesToBlocked("localhost", false),
        "…and allowed when not");
  check(!guard::resolvesToBlocked("stencil.invalid", true),
        "a lookup failure is not a block — the fetch surfaces its own error");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
