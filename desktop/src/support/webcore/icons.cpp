#include "icons.hpp"

#include "skinPrefs.hpp"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStringList>

static void ensureAppResources() { Q_INIT_RESOURCE(app); }

namespace stencil::support {

  namespace {
    struct PixelSet {
      int size = 16;
      QChar logoFrame;                 // the mark's ring: the one cell the accent inks
      QHash<QChar, QString> palette;   // a char → its hex; the dot is absent
      QHash<QChar, QString> dark;      // the characters the dark face re-inks
      QHash<QString, QStringList> icons;
    };

    QHash<QChar, QString> readPalette(const QJsonObject& o) {
      QHash<QChar, QString> out;
      for (auto it = o.begin(); it != o.end(); ++it)
        if (!it.value().isNull() && !it.key().isEmpty()) out.insert(it.key().at(0), it.value().toString());
      return out;
    }

    const PixelSet& pixelSet() {
      static const PixelSet set = [] {
        ensureAppResources();
        PixelSet s;
        QFile f(":/config/iconsWebcore.json");
        if (!f.open(QIODevice::ReadOnly)) return s;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        s.size = root.value("size").toInt(s.size);
        const QString frame = root.value("logoFrame").toString();
        if (!frame.isEmpty()) s.logoFrame = frame.at(0);
        s.palette = readPalette(root.value("palette").toObject());
        s.dark = readPalette(root.value("paletteDark").toObject());
        const QJsonObject icons = root.value("icons").toObject();
        for (auto it = icons.begin(); it != icons.end(); ++it) {
          QStringList rows;
          for (const QJsonValue& r : it.value().toArray()) rows << r.toString();
          s.icons.insert(it.key(), rows);
        }
        return s;
      }();
      return set;
    }

    // `ink` overrides single characters (the dark face, and the mark's accent ring).
    QString rectRuns(const QStringList& rows, const QHash<QChar, QString>& ink) {
      const PixelSet& s = pixelSet();
      QString out;
      for (int y = 0; y < rows.size(); ++y) {
        const QString& row = rows.at(y);
        for (int x = 0; x < row.size();) {
          const QChar ch = row.at(x);
          int w = 1;
          while (x + w < row.size() && row.at(x + w) == ch) ++w;
          const QString fill = ink.value(ch, s.palette.value(ch));
          if (!fill.isEmpty())
            out += QString(R"(<rect x="%1" y="%2" width="%3" height="1" fill="%4"/>)").arg(x).arg(y).arg(w).arg(fill);
          x += w;
        }
      }
      return out;
    }

    QHash<QChar, QString> themeInk() {
      return isWebcore() && skinDark() ? pixelSet().dark : QHash<QChar, QString>();
    }

    QString document(const QString& runs) {
      return QString(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %1 %1" )"
                     R"(shape-rendering="crispEdges">%2</svg>)")
          .arg(pixelSet().size)
          .arg(runs);
    }
  }  // namespace

  bool hasPixelIcon(const QString& name) { return pixelSet().icons.contains(name); }

  QString pixelIconSvg(const QString& name) {
    static QHash<QString, QString> docs;
    const bool dark = isWebcore() && skinDark();
    const QString key = name + (dark ? "|d" : "|l");
    const auto hit = docs.constFind(key);
    if (hit != docs.constEnd()) return *hit;
    const PixelSet& s = pixelSet();
    const auto rows = s.icons.constFind(name);
    if (rows == s.icons.constEnd()) return QString();
    const QString doc = document(rectRuns(*rows, themeInk()));
    docs.insert(key, doc);
    return doc;
  }

  QString pixelLogoSvg(const QColor& frame) {
    const PixelSet& s = pixelSet();
    const auto rows = s.icons.constFind(QStringLiteral("logo"));
    if (rows == s.icons.constEnd()) return QString();
    QHash<QChar, QString> ink = themeInk();
    if (frame.isValid() && !s.logoFrame.isNull()) ink.insert(s.logoFrame, frame.name());
    return document(rectRuns(*rows, ink));
  }

  QImage pixelIconImage(const QString& name, bool dark, int scale) {
    const PixelSet& s = pixelSet();
    const QStringList rows = s.icons.value(name);
    if (rows.isEmpty()) return QImage();
    QImage img(rows.size() * scale, rows.size() * scale, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    for (int y = 0; y < rows.size(); ++y)
      for (int x = 0; x < rows.at(y).size(); ++x) {
        const QChar ch = rows.at(y).at(x);
        const QString hex = dark ? s.dark.value(ch, s.palette.value(ch)) : s.palette.value(ch);
        if (!hex.isEmpty()) p.fillRect(x * scale, y * scale, scale, scale, QColor(hex));
      }
    p.end();
    return img;
  }

}  // namespace stencil::support
