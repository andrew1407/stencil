#pragma once
#include <QHostAddress>
#include <QString>

// The SSRF address table (browser/js/config/net/blockedRanges.json, qrc alias net/blockedRanges.json)
// and its evaluator: classed CIDRs, the IPv6 prefixes that carry an IPv4 address, and the two
// policies. Twin of cli/src/net/ranges.zig; semantics in the table's README.
namespace stencil::net::blockedRanges {

  enum class Policy { FETCH, SERVER_TARGET };

  // `allowLoopback` relaxes FETCH (a URL the user typed); `allowPrivate` relaxes SERVER_TARGET.
  struct Options {
    bool allowLoopback = false;
    bool allowPrivate = false;
  };

  // True when `policy` refuses `address` under `options`; an address inside an embedsV4 prefix is
  // judged as the IPv4 address it carries. A table that failed to load refuses everything.
  bool blocked(const QHostAddress& address, Policy policy, Options options);

  // "" when the table loaded; else why not.
  QString tableError();

}  // namespace stencil::net::blockedRanges
