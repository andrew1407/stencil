#include "blockedRanges.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>

#include <cstring>

namespace stencil::net::blockedRanges {

  namespace {
    // An address as bytes: 4 for IPv4, 16 for IPv6; a CIDR adds its prefix length.
    struct Bytes {
      int size = 0;
      quint8 b[16] = {};
    };

    struct Cidr {
      Bytes net;
      int bits = 0;
    };

    struct Embed {
      Cidr prefix;
      int offset = 0;
      QVector<Cidr> except;
    };

    // A policy's rows: `unless` "" = always refused, else the option that relaxes the row.
    struct Rule {
      QVector<Cidr> cidrs;
      QString unless;
    };

    struct Table {
      QString error;
      QVector<Embed> embeds;
      QVector<Rule> fetch;
      QVector<Rule> serverTarget;
    };

    Bytes bytesOf(const QHostAddress& a) {
      Bytes out;
      if (a.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 v = a.toIPv4Address();
        out.size = 4;
        for (int i = 0; i < 4; ++i) out.b[i] = quint8(v >> (24 - 8 * i));
      } else if (a.protocol() == QAbstractSocket::IPv6Protocol) {
        const Q_IPV6ADDR v6 = a.toIPv6Address();
        out.size = 16;
        std::memcpy(out.b, v6.c, 16);
      }
      return out;
    }

    bool contains(const Cidr& c, const Bytes& a) {
      if (c.net.size != a.size) return false;
      int left = c.bits;
      for (int i = 0; i < a.size && left > 0; ++i, left -= 8) {
        const quint8 mask = left >= 8 ? 0xff : quint8(0xff << (8 - left));
        if ((c.net.b[i] ^ a.b[i]) & mask) return false;
      }
      return true;
    }

    bool parseCidr(const QJsonValue& text, Cidr* out) {
      const QPair<QHostAddress, int> subnet = QHostAddress::parseSubnet(text.toString());
      if (subnet.first.isNull() || subnet.second < 0) return false;
      *out = {bytesOf(subnet.first), subnet.second};
      return out->net.size > 0;
    }

    bool cidrsOf(const QJsonObject& classes, const QJsonArray& names, QVector<Cidr>* out) {
      for (const QJsonValue& name : names) {
        const QJsonValue list = classes.value(name.toString());
        if (!list.isArray()) return false;
        for (const QJsonValue& text : list.toArray()) {
          Cidr c;
          if (!parseCidr(text, &c)) return false;
          out->append(c);
        }
      }
      return true;
    }

    bool rulesOf(const QJsonObject& classes, const QJsonObject& policy, QVector<Rule>* out) {
      Rule always;
      if (!cidrsOf(classes, policy.value("blocks").toArray(), &always.cidrs)) return false;
      out->append(always);
      const QJsonObject unless = policy.value("blocksUnless").toObject();
      for (auto it = unless.begin(); it != unless.end(); ++it) {
        Rule r{{}, it.key()};
        if (!cidrsOf(classes, it.value().toArray(), &r.cidrs)) return false;
        out->append(r);
      }
      return true;
    }

    Table load() {
      Table t;
      QFile f(QStringLiteral(":/config/net/blockedRanges.json"));
      const QJsonObject root = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
      const QJsonObject classes = root.value("classes").toObject();
      const QJsonObject policies = root.value("policies").toObject();
      bool ok = !classes.isEmpty() && policies.value("fetch").isObject() && policies.value("serverTarget").isObject();
      for (const QJsonValue& v : root.value("embedsV4").toArray()) {
        const QJsonObject e = v.toObject();
        Embed embed;
        embed.offset = e.value("offset").toInt(-1);
        ok = ok && parseCidr(e.value("prefix"), &embed.prefix) && embed.prefix.net.size == 16 && embed.offset >= 0 &&
             embed.offset <= 12;
        for (const QJsonValue& x : e.value("except").toArray()) {
          Cidr c;
          ok = ok && parseCidr(x, &c);
          embed.except.append(c);
        }
        t.embeds.append(embed);
      }
      ok = ok && rulesOf(classes, policies.value("fetch").toObject(), &t.fetch) &&
           rulesOf(classes, policies.value("serverTarget").toObject(), &t.serverTarget);
      if (!ok) t.error = QStringLiteral("blockedRanges.json is missing or malformed");
      return t;
    }

    const Table& table() {
      static const Table t = load();
      return t;
    }

    // The address a policy judges: an IPv6 address carrying an IPv4 one is that IPv4 address.
    Bytes judged(const Bytes& a) {
      if (a.size != 16) return a;
      for (const Embed& e : table().embeds) {
        if (!contains(e.prefix, a)) continue;
        for (const Cidr& x : e.except)
          if (contains(x, a)) return a;
        Bytes v4;
        v4.size = 4;
        std::memcpy(v4.b, a.b + e.offset, 4);
        return v4;
      }
      return a;
    }
  }  // namespace

  bool blocked(const QHostAddress& address, Policy policy, Options options) {
    const Table& t = table();
    const Bytes a = judged(bytesOf(address));
    if (!t.error.isEmpty() || a.size == 0) return true;
    for (const Rule& r : policy == Policy::FETCH ? t.fetch : t.serverTarget) {
      if ((r.unless == QLatin1String("allowLoopback") && options.allowLoopback) ||
          (r.unless == QLatin1String("allowPrivate") && options.allowPrivate))
        continue;
      for (const Cidr& c : r.cidrs)
        if (contains(c, a)) return true;
    }
    return false;
  }

  QString tableError() { return table().error; }

}  // namespace stencil::net::blockedRanges
