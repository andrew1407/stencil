#include "CliBanner.hpp"
#include "consoleBanner.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>

namespace stencil::gui {

  namespace {
    const QSet<QChar>& borderGlyphs() {
      static const QSet<QChar> set{QChar(0x256D), QChar(0x256E), QChar(0x2570), QChar(0x256F),
                                   QChar(0x2571), QChar(0x2572), QChar(0x2502), QChar(0x2500)};
      return set;
    }
    constexpr double WIPE_DEGREES = 360;
  }  // namespace

  const CliFx& cliFx() {
    static const CliFx fx = [] {
      CliFx out;
      QFile f(QStringLiteral(":/config/console/fx.json"));
      if (!f.open(QIODevice::ReadOnly)) return out;
      const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
      const QJsonObject w = root.value(QLatin1String("wipe")).toObject();
      out.wipeJumps = w.value(QLatin1String("jumps")).toInt(out.wipeJumps);
      out.wipeStepMs = w.value(QLatin1String("stepMs")).toInt(out.wipeStepMs);
      out.flourishStepMs = w.value(QLatin1String("flourishStepMs")).toInt(out.flourishStepMs);
      out.ruleWipePct = w.value(QLatin1String("ruleWipePct")).toInt(out.ruleWipePct);
      out.iconCols = w.value(QLatin1String("iconCols")).toInt(out.iconCols);
      out.cellAspect = w.value(QLatin1String("cellAspect")).toInt(out.cellAspect);
      out.iconSpeed = w.value(QLatin1String("iconSpeed")).toDouble(out.iconSpeed);
      out.pressMs = root.value(QLatin1String("press")).toObject().value(QLatin1String("ms")).toInt(out.pressMs);
      const QJsonObject r = root.value(QLatin1String("rain")).toObject();
      out.rainFrames = r.value(QLatin1String("frames")).toInt(out.rainFrames);
      out.rainFrameMs = r.value(QLatin1String("frameMs")).toInt(out.rainFrameMs);
      out.rainTrail = r.value(QLatin1String("trail")).toInt(out.rainTrail);
      return out;
    }();
    return fx;
  }

  int bannerLabelRow(bool compact) {
    const model::BannerLayout l = model::consoleBannerLayout(compact);
    return l.labelRow + 1 + l.curveRows;
  }

  int bannerHeaderRows(bool compact) {
    const model::BannerLayout l = model::consoleBannerLayout(compact);
    return l.frameHeight + 2 + 2 * l.curveRows;
  }

  QVector<QVector<BannerCell>> bannerCells(const CliSkin* skin, bool compact, const CliSkinClock* clock) {
    const model::BannerLayout l = model::consoleBannerLayout(compact);
    const QStringList rows = model::consoleBannerRows(skin ? skin->mark : QStringLiteral("S"), compact);
    const int artStart = l.indent + 1 + l.margin, artEnd = artStart + l.frameWidth;
    const int bodyTop = 1 + l.curveRows;
    QVector<QVector<BannerCell>> out;
    for (int r = 0; r < rows.size(); ++r) {
      QVector<BannerCell> cells;
      const QString& row = rows.at(r);
      for (int c = 0; c < row.size(); ++c) {
        const QChar ch = row.at(c);
        const bool inArt = c >= artStart && c < artEnd;
        // The panel's own rule rows run through the art columns and stay the border's.
        const bool rule = r == 0 || r == rows.size() - 1;
        BannerRole role = BannerRole::PANEL;
        if (borderGlyphs().contains(ch) && (!inArt || rule)) role = BannerRole::ACCENT;
        else if (inArt) role = ch != QLatin1Char(' ') ? BannerRole::MARK : BannerRole::ART;
        cells.push_back({ch, role});
      }
      // The meow skin walks its cat across the frame rows in place of the mark.
      if (skin && skin->movesLogo && clock && r >= bodyTop && r < bodyTop + l.frameHeight) {
        const CatPose pose = cliCatPose(l.frameWidth, l.frameHeight, *clock);
        for (int c = artStart; c < artEnd && c < cells.size(); ++c) cells[c] = {QLatin1Char(' '), BannerRole::ART};
        if (r - bodyTop == pose.row) {
          int c = artStart + pose.col;
          for (const QChar ch : pose.text) {
            if (c >= artEnd || c >= cells.size()) break;
            const bool wide = ch.unicode() >= 0xFF01 && ch.unicode() <= 0xFF60;
            cells[c++] = {ch, BannerRole::MARK, wide};
            if (wide && c < artEnd && c < cells.size()) cells[c++] = {QChar(), BannerRole::ART};
          }
        }
      }
      out.push_back(cells);
    }
    return out;
  }

  namespace cliwipe {

    double cellAngle(int row, int col, double cy, double cx) {
      const double dx = col - cx;
      const double dy = (cy - row) * cliFx().cellAspect;
      if (dx == 0 && dy == 0) return 0;
      const double deg = std::atan2(dx, dy) * 180.0 / M_PI;
      return deg < 0 ? deg + 360 : deg;
    }

    QVector<Span> sweptSpansAt(int row, int cols, double cy, double cx, double deg) {
      QVector<Span> out;
      if (deg <= 0 || cols == 0) return out;
      if (deg >= WIPE_DEGREES) return {Span(0, cols)};
      int start = -1;
      for (int c = 0; c < cols; ++c) {
        const bool passed = cellAngle(row, c + 1, cy, cx) < deg;
        if (passed && start < 0) start = c;
        else if (!passed && start >= 0) {
          out.push_back(Span(start, c));
          start = -1;
        }
      }
      if (start >= 0) out.push_back(Span(start, cols));
      return out;
    }

    double iconDegrees(int x, int reach) {
      if (reach == 0 || x >= reach) return WIPE_DEGREES;
      return std::min(WIPE_DEGREES, WIPE_DEGREES * x * cliFx().iconSpeed / reach);
    }

    QVector<Span> iconSpans(int row, int headerRows, int cols, double deg, int x) {
      const int w = std::min(cliFx().iconCols, cols);
      QVector<Span> spans = sweptSpansAt(row, w, (headerRows + 1) / 2.0, (w + 1) / 2.0, deg);
      if (x > w) spans.push_back(Span(w, std::min(x, cols)));
      return spans;
    }

    QVector<RowSpan> wipeFrame(int headerRows, int cols, int x, int reach) {
      const double deg = iconDegrees(x, reach);
      QVector<RowSpan> out;
      for (int row = 1; row <= headerRows; ++row)
        for (const Span& s : iconSpans(row, headerRows, cols, deg, x)) out.push_back({row, s.first, s.second});
      return out;
    }

    int ruleReach(int cols, int x, int reach) {
      return std::min(cols, cols * x * cliFx().ruleWipePct / (std::max(1, reach) * 100));
    }

    QVector<Event> wipeSchedule(int reach, int letters) {
      const CliFx& fx = cliFx();
      const int perJump = std::max(1, (reach + fx.wipeJumps - 1) / fx.wipeJumps);
      QVector<Event> events;
      for (int x = perJump, t = 0; x < reach; x += perJump, t += fx.wipeStepMs) events.push_back({t, true, x});
      for (int i = 0, t = 0; i < letters; ++i, t += fx.flourishStepMs) events.push_back({t, false, i});
      std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return a.atMs != b.atMs ? a.atMs < b.atMs : (a.wash && !b.wash);
      });
      return events;
    }

  }  // namespace cliwipe

}  // namespace stencil::gui
