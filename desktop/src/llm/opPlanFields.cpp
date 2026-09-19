// The validated + normalized action (declared keys, registry defaults, trims) becoming the typed
// Action: the per-op field filling, plus the layout envelope's own. Types, enums, ranges, grammars
// and presence rules belong to the generic check; these are the desktop extras the registry lacks.
#include "opPlanParts.hpp"

#include "colorNames.hpp"   // core::parseColor — the only core seam left in this family

namespace stencil::llm::opdetail {

  // A colour NAME must be one the core recognizes (fixture 137 knownDivergence.desktop);
  // "#rrggbb" already passed the grammar.
  static bool isKnownColor(const QString& t) {
    return t.startsWith(QLatin1Char('#')) || core::parseColor(t.toStdString()).has_value();
  }

    bool fillLayout(const QJsonObject& n, Action& a, QString* e) {
      for (const QJsonValue& lv : n.value("lines").toArray()) {
        const QJsonObject o = lv.toObject();
        core::Line line;
        for (const QJsonValue& pv : o.value("points").toArray()) {
          const QJsonObject po = pv.toObject();
          line.points.push_back({po.value("x").toDouble(), po.value("y").toDouble()});
        }
        if (line.points.empty())
          return err(e, QStringLiteral("Invalid layout action: a line needs a non-empty \"points\" array"));
        if (present(o, "color")) line.color = o.value("color").toString().toStdString();
        if (present(o, "thickness")) line.thickness = o.value("thickness").toDouble();
        if (present(o, "pointSize")) line.pointSize = o.value("pointSize").toDouble();
        if (present(o, "style")) line.style = o.value("style").toString().toStdString();
        if (present(o, "locked")) line.locked = o.value("locked").toBool();
        if (present(o, "fillColor")) line.fillColor = o.value("fillColor").toString().toStdString();
        a.lines.push_back(std::move(line));
      }
      return true;
    }

    bool fillAction(const QString& op, const QJsonObject& n, Action& a, QString* e) {
      if (!opKindFor(op, &a.op))
        return err(e, QStringLiteral("Invalid %1 action: no desktop executor").arg(op));
      const auto str = [&](const char* k) { return n.value(QLatin1String(k)).toString(); };
      const auto num = [&](const char* k) { return n.value(QLatin1String(k)).toDouble(); };
      const auto integer = [&](const char* k) { return n.value(QLatin1String(k)).toInt(); };
      const auto flag = [&](const char* k) { return n.value(QLatin1String(k)).toBool(); };
      switch (a.op) {
        case OpKind::CROP: {
          const QJsonObject spec = n.value("spec").toObject();
          a.x1 = spec.value("x1").toString();
          a.x2 = spec.value("x2").toString();
          a.y1 = spec.value("y1").toString();
          a.y2 = spec.value("y2").toString();
          a.aspect = spec.value("aspect").toString();
          break;
        }
        case OpKind::ROTATE:
          a.rotateLeft = str("dir") == QLatin1String("left");
          a.times = integer("times");
          break;
        case OpKind::FILTER:
          a.mode = str("mode");
          a.tint = str("tint");
          break;
        case OpKind::LAYOUT:
          return fillLayout(n, a, e);
        case OpKind::FORMULA:
          if (present(n, "enabled")) {
            a.formulaEnabled = flag("enabled") ? 1 : 0;
          } else {
            a.axis = str("axis").at(0);
            a.expr = str("expr").trimmed();   // blank = clear that axis
          }
          break;
        case OpKind::PAGE:
          a.format = str("format");
          a.widthCm = num("width");
          a.heightCm = num("height");
          break;
        case OpKind::BLANK:
          a.color = str("color");
          if (!isKnownColor(a.color))
            return err(e, QStringLiteral("Invalid blank action: \"color\" must be #rrggbb or a CSS colour name"));
          a.format = str("format");
          a.widthCm = num("width");
          a.heightCm = num("height");
          break;
        case OpKind::UNDO:
        case OpKind::REDO:
          a.steps = integer("steps");
          break;
        case OpKind::FRAME:
          if (present(n, "index")) a.indices.push_back(integer("index"));
          for (const QJsonValue& v : n.value("indices").toArray()) a.indices.push_back(v.toInt());
          break;
        case OpKind::THEME:
          a.mode = str("mode");
          break;
        case OpKind::ACCENT:
          a.color = str("color");
          a.preset = str("preset");
          break;
        case OpKind::LINE_STYLE:
          a.color = str("color");
          a.thickness = integer("thickness");
          a.pointSize = integer("pointSize");
          a.style = str("style");
          // "" = follow the stroke — meaningful, so a presence flag rides along.
          a.pointColorSet = present(n, "pointColor");
          a.pointColor = str("pointColor");
          a.drawMode = str("drawMode");
          a.fillColor = str("fillColor");
          break;
        case OpKind::UNITS:
          a.value = str("value");
          break;
        case OpKind::VIEW:
          if (present(n, "points")) a.viewPoints = flag("points") ? 1 : 0;
          if (present(n, "lines")) a.viewLines = flag("lines") ? 1 : 0;
          break;
        case OpKind::CLEAR:
        case OpKind::CLEAR_CHAT:
          break;
        case OpKind::COPY:
          a.what = str("what");
          break;
        case OpKind::OPEN_URL:
          a.url = str("url");
          a.incognito = flag("incognito");
          break;
        case OpKind::OPEN_FILE:
          // A LOCAL file (the registry rejects URL schemes) in a format this app
          // opens; whether the USER wrote it is the executor's echo guard.
          a.path = str("path");
          if (!isOpenableFile(a.path))
            return err(e, QStringLiteral("Invalid openFile action: \"%1\" is not an image, video, "
                                         ".json layout or .stencil project")
                              .arg(a.path));
          break;
        case OpKind::CONNECT:
        case OpKind::DISCONNECT:
          a.server = str("server").trimmed();
          break;
        case OpKind::REMOVE_PROJECT:
          a.name = str("name");
          a.current = flag("current");
          break;
        case OpKind::CLEAR_PROJECTS:
          a.current = flag("keepCurrent");   // the shared "the open one" presence flag
          break;
        case OpKind::COMPARE:
          a.mode = str("mode");
          a.split = num("split");
          break;
        case OpKind::ZOOM:
          a.fit = flag("fit");
          a.percent = static_cast<int>(std::lround(num("percent")));
          break;
        case OpKind::RENAME_PROJECT:
          a.name = str("name");
          break;
        case OpKind::PROJECT_COLOR:
          a.color = str("color");   // "" = the explicit clear
          break;
        case OpKind::BLANK_COLOR:
          a.color = str("color");
          if (!isKnownColor(a.color))
            return err(e, QStringLiteral("Invalid blankColor action: \"color\" must be #rrggbb or a CSS colour name"));
          break;
        case OpKind::OPEN_PROJECT:
          a.name = str("name");
          a.current = flag("last");   // "the latest one" rides removeProject's flag
          break;
        case OpKind::INCOGNITO:
          a.incognito = flag("on");
          break;
        case OpKind::CHAT_PANEL:
          if (present(n, "open")) a.chatOpen = flag("open") ? 1 : 0;
          a.dock = str("dock");
          break;
        case OpKind::DIALOG:
          a.dialog = str("name");
          a.current = flag("close");   // "close what is open" rides the shared flag
          break;
        case OpKind::IMAGE:
          a.index = integer("index");
          break;
        case OpKind::SAVE:
          a.name = str("name");
          a.path = str("path");   // trimmed by the registry; "" = no destination
          break;
      }
      return true;
    }
}  // namespace stencil::llm::opdetail
