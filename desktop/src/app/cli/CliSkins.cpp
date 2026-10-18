#include "CliSkins.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace stencil::gui {

  namespace {
    struct Table {
      QVector<CliSkin> skins;
      QString listWord;
      QColor gold{0xff, 0xd7, 0x00};
      int frameMs = 80;
      int hueStep = 22;
      QVector<QColor> bulbs;
      int bulbFrames = 30;
    };
    QJsonObject rootObject() {
      QFile f(QStringLiteral(":/config/console/skins.json"));
      if (!f.open(QIODevice::ReadOnly)) return {};
      return QJsonDocument::fromJson(f.readAll()).object();
    }
    const Table& table() {
      static const Table t = [] {
        Table out;
        const QJsonObject root = rootObject();
        out.listWord = root.value(QLatin1String("listWord")).toString();
        out.gold = QColor(root.value(QLatin1String("gold")).toString(out.gold.name()));
        out.frameMs = root.value(QLatin1String("frameMs")).toInt(out.frameMs);
        const QJsonObject hues = root.value(QLatin1String("hues")).toObject();
        out.hueStep = hues.value(QLatin1String("hueStep")).toInt(out.hueStep);
        out.bulbFrames = hues.value(QLatin1String("bulbFrames")).toInt(out.bulbFrames);
        for (const QJsonValue& b : hues.value(QLatin1String("bulbs")).toArray()) out.bulbs.push_back(QColor(b.toString()));
        const QJsonObject skins = root.value(QLatin1String("skins")).toObject();
        for (auto it = skins.begin(); it != skins.end(); ++it) {
          const QJsonObject o = it.value().toObject();
          CliSkin s;
          s.key = it.key();
          s.word = o.value(QLatin1String("word")).toString().toLower();
          s.phrase = o.value(QLatin1String("phrase")).toString();
          s.phraseOff = o.value(QLatin1String("phraseOff")).toString();
          s.wordmark = o.value(QLatin1String("wordmark")).toString();
          s.title = o.value(QLatin1String("title")).toString();
          s.body = o.value(QLatin1String("body")).toString();
          s.mark = o.value(QLatin1String("mark")).toString(s.mark);
          s.wordmarkAccented = o.value(QLatin1String("wordmarkAccented")).toBool();
          s.animated = o.value(QLatin1String("animated")).toBool();
          s.movesLogo = o.value(QLatin1String("movesLogo")).toBool();
          s.paintsRuns = o.value(QLatin1String("paintsRuns")).toBool();
          s.slides = o.value(QLatin1String("slides")).toBool();
          s.paintsCells = o.value(QLatin1String("paintsCells")).toBool();
          s.replacesLetters = o.value(QLatin1String("replacesLetters")).toBool();
          s.fg = QColor(o.value(QLatin1String("fg")).toString());
          s.bg = QColor(o.value(QLatin1String("bg")).toString());
          s.titleFg = QColor(o.value(QLatin1String("titleFg")).toString());
          s.titleBg = QColor(o.value(QLatin1String("titleBg")).toString());
          s.wash = QColor(o.value(QLatin1String("wash")).toString());
          s.markColor = QColor(o.value(QLatin1String("markColor")).toString());
          out.skins.push_back(s);
        }
        return out;
      }();
      return t;
    }
  }  // namespace

  const QVector<CliSkin>& cliSkins() { return table().skins; }

  const CliSkin* cliSkinByWord(const QString& word) {
    const QString w = word.trimmed().toLower();
    for (const CliSkin& s : table().skins)
      if (s.word == w) return &s;
    return nullptr;
  }

  QString cliListWord() { return table().listWord; }
  QColor cliGold() { return table().gold; }
  int cliFrameMs() { return table().frameMs; }
  QString cliWordmark(const CliSkin* skin) {
    return skin && !skin->wordmark.isEmpty() ? skin->wordmark : QStringLiteral("S T E N C I L");
  }

  QColor cliAccentUnder(const CliSkin* skin, const QColor& accent) {
    if (!skin) return accent;
    if (skin->paintsCells) return skin->fg;
    if (skin->replacesLetters) return cliGold();
    return accent;
  }

  bool cliRestyles(const CliSkin* skin) { return skin && (skin->paintsCells || skin->paintsRuns || skin->replacesLetters); }

  bool cliSwappable(QChar ch) { return ch.unicode() > ' ' && ch.unicode() < 0x7f; }

  void CliSkinClock::start(qint64 nowMs) {
    startMs = nowMs;
    frame = 0;
    seed = quint32(nowMs);
    bulb = 0;
    bulbDue = 0;
  }

  bool CliSkinClock::sync(qint64 nowMs) {
    const int want = int(qMax<qint64>(0, (nowMs - startMs) / qMax(1, cliFrameMs())));
    if (want == frame) return false;
    frame = want;
    return true;
  }

  std::optional<QColor> CliSkinClock::nextBulb() {
    if (frame < bulbDue || table().bulbs.isEmpty()) return std::nullopt;
    bulbDue = frame + table().bulbFrames;
    bulb = (bulb + 1) % table().bulbs.size();
    return table().bulbs.at(bulb);
  }

  QColor cliHueAt(const CliSkin& skin, int col, int frame) {
    const int slid = col - (skin.slides ? frame : 0);
    const int hue = ((slid * table().hueStep) % 360 + 360) % 360;
    return QColor::fromHsvF(hue / 360.0, 0.75, 1.0);
  }

}  // namespace stencil::gui
