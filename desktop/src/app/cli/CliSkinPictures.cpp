#include "CliSkins.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// The pictures the fruit, cat and pie skins put in place of accent letters, and the cat's walk:
// ports of cli/src/app/skin/pictures.zig and cat.zig. FNV-1a stands in for Wyhash — the draw
// is random by design.

namespace stencil::gui {

  namespace {
    struct CatStyle {
      QVector<QPair<QString, int>> looks;   // open, blink, left, right, up, down
    };
    struct Pictures {
      QStringList fruits, pies, cats;
      int swapFrames = 83;
      QVector<CatStyle> styles;
      int legFrames = 24;
      int stepFrames = 2;
    };
    const Pictures& pictures() {
      static const Pictures p = [] {
        Pictures out;
        QFile f(QStringLiteral(":/config/console/skins.json"));
        if (!f.open(QIODevice::ReadOnly)) return out;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        const QJsonObject pics = root.value(QLatin1String("pictures")).toObject();
        for (const QJsonValue& v : pics.value(QLatin1String("fruits")).toArray()) out.fruits << v.toString();
        for (const QJsonValue& v : pics.value(QLatin1String("pies")).toArray()) out.pies << v.toString();
        for (const QJsonValue& v : pics.value(QLatin1String("cats")).toArray()) out.cats << v.toString();
        out.swapFrames = pics.value(QLatin1String("swapFrames")).toInt(out.swapFrames);
        const QJsonObject cat = root.value(QLatin1String("cat")).toObject();
        out.legFrames = cat.value(QLatin1String("legFrames")).toInt(out.legFrames);
        out.stepFrames = cat.value(QLatin1String("stepFrames")).toInt(out.stepFrames);
        for (const QJsonValue& style : cat.value(QLatin1String("styles")).toArray()) {
          CatStyle s;
          for (const QJsonValue& look : style.toArray()) {
            const QJsonArray pair = look.toArray();
            s.looks.push_back({pair.at(0).toString(), pair.at(1).toInt()});
          }
          out.styles.push_back(s);
        }
        return out;
      }();
      return p;
    }

    quint32 fnv(std::initializer_list<quint32> nums) {
      quint32 h = 0x811c9dc5u;
      for (quint32 n : nums)
        for (int i = 0; i < 4; ++i) {
          h ^= (n >> (i * 8)) & 0xffu;
          h *= 0x01000193u;
        }
      return h;
    }

    const QStringList& listOf(const CliSkin& skin) {
      if (skin.movesLogo) return pictures().cats;
      if (skin.mark == QLatin1String("P")) return pictures().pies;
      return pictures().fruits;
    }

    struct Spot {
      int col, row;
    };
    Spot spot(int leg, int span, int rows, quint32 seed) {
      const quint32 h = fnv({seed, quint32(leg)});
      return {int(h % quint32(span + 1)), int((h >> 20) % quint32(qMax(rows, 1)))};
    }
    int approach(int a, int b, int steps) { return a < b ? qMin(b, a + steps) : b + qMax(0, a - b - steps); }
    enum class Look { OPEN, BLINK, LEFT, RIGHT, UP, DOWN };
  }  // namespace

  int cliRowKey(const QString& text) {
    quint32 h = 0x811c9dc5u;
    for (const QChar c : text) {
      h ^= c.unicode() & 0xffu;
      h *= 0x01000193u;
    }
    return int(h);
  }

  int cliPictureAt(const CliSkin& skin, int rowKey, int col, int prev, const CliSkinClock& clock) {
    const quint32 cell = fnv({clock.seed ^ quint32(rowKey), quint32(col)});
    const int swap = pictures().swapFrames;
    const int beat = (clock.frame + int(cell % quint32(swap))) / swap;
    quint32 h = fnv({cell, quint32(beat)});
    if (skin.mark == QLatin1String("P") && !skin.movesLogo) return h % 5 == 0 ? 1 : 0;
    const int n = listOf(skin).size();
    if (n == 0) return 0;
    int i = int(h % quint32(n));
    if (prev == i && n > 1) {
      h >>= 8;
      i = (i + 1 + int(h % quint32(n - 1))) % n;
    }
    return i;
  }

  QString cliPicture(const CliSkin& skin, int index) { return listOf(skin).value(index); }

  // The cat walks a cell a step between random spots and glances about or blinks at rest.
  CatPose cliCatPose(int width, int rows, const CliSkinClock& clock) {
    const Pictures& p = pictures();
    CatPose pose;
    if (p.styles.isEmpty()) return pose;
    const CatStyle& style = p.styles.at(int(clock.seed % quint32(p.styles.size())));
    const int span = qMax(0, width - style.looks.at(int(Look::OPEN)).second);
    const int f = clock.frame / qMax(1, p.stepFrames);
    const int leg = f / qMax(1, p.legFrames), t = f % qMax(1, p.legFrames);
    const Spot from = spot(leg, span, rows, clock.seed), to = spot(leg + 1, span, rows, clock.seed);
    const int col = approach(from.col, to.col, t), row = approach(from.row, to.row, t);
    Look look = Look::OPEN;
    if (col != to.col) look = to.col > col ? Look::RIGHT : Look::LEFT;
    else if (row != to.row) look = to.row > row ? Look::DOWN : Look::UP;
    else {
      const quint32 h = fnv({clock.seed + 1, quint32(leg)});
      if (t + 3 >= p.legFrames && t + 1 < p.legFrames && h % 2 == 0) look = Look::BLINK;
      else if (h % 3 == 0 && t % 12 >= 6) look = Look(2 + int((h >> 8) % 4));
    }
    const QPair<QString, int>& face = style.looks.at(int(look));
    pose.text = face.first;
    pose.cols = face.second;
    pose.col = qMin(col, qMax(0, width - pose.cols));
    pose.row = row;
    return pose;
  }

}  // namespace stencil::gui
