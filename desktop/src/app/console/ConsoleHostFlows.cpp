#include "ConsoleHost.hpp"
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "ChatPlanTarget.hpp"
#include "DataExportController.hpp"
#include "RemoteSession.hpp"
#include "ScriptDoc.hpp"
#include "scriptRun.hpp"
#include "theme.hpp"
#include "CliStage.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <memory>

// The status line and the edit, page, colour and theme flows the console commands drive, each
// through the same appliers a click or a script op takes. Browser twin: the facade calls in
// js/console/shell/handlersEdit.js.

namespace stencil::gui {

  using support::ConsoleOutput;

  namespace {
    constexpr int MAX_UNDO = 1000;
    QString round1(double v) { return QString::number(qRound(v * 10) / 10.0); }
    bool isColour(const QString& c) { return QColor(c).isValid(); }
    QString orDefault(const QString& v, const char* fallback) { return v.isEmpty() ? QString::fromLatin1(fallback) : v; }
  }  // namespace

  bool ConsoleHost::hasImage() const { return w.canvas && w.canvas->hasImage(); }

  // cli/src/console/ui.zig status(): `image: <name> (<w>x<h> px · <page>)  [in-memory, temporary]  [n/m]`.
  ConsoleHost::Status ConsoleHost::status() const {
    Status s;
    s.hasImage = hasImage();
    if (!s.hasImage) {
      s.text = QStringLiteral("image: (none) — upload, paste, or create one first");
      return s;
    }
    s.name = w.projectBaseName();
    const QString ext = w.canvas->imageExt();
    if (!ext.isEmpty() && !s.name.endsWith(QLatin1Char('.') + ext, Qt::CaseInsensitive)) s.name += QLatin1Char('.') + ext;
    s.color = w.parts.projects.projectNameColor();
    const QString fmt = w.pageSizeValue();
    const core::PageSize page = w.currentPageDimensions();
    const bool temporary = w.incognito || (w.activeProjectId.isEmpty() && w.remote.session->getLink().id.isEmpty());
    // The pristine picture is the history's floor (step -1), so it counts as state 1.
    const int steps = w.canvas->historySize() + 1;
    const int at = w.canvas->historyStep() + 2;
    s.text = QStringLiteral("image: %1 (%2x%3 px · %4 %5×%6cm)%7%8")
                 .arg(s.name).arg(w.canvas->imageWidth()).arg(w.canvas->imageHeight())
                 .arg(fmt == QLatin1String("custom") ? fmt : fmt.toUpper(), round1(page.width), round1(page.height),
                      temporary ? QStringLiteral("  [in-memory, temporary]") : QString(),
                      steps > 1 ? QStringLiteral("  [%1/%2]").arg(at).arg(steps) : QString());
    return s;
  }

  void ConsoleHost::runScript(const QString& text, support::ConsoleDone done) {
    auto target = std::make_shared<ChatPlanTarget>(w);
    runScriptThen(model::ScriptDoc::parse(text), *target, [target, done](const ScriptRunResult& r) {
      if (r.isOk) return done(ConsoleOutput());
      done(ConsoleOutput::error(r.line > 0 ? QStringLiteral("line %1: %2").arg(r.line).arg(r.error) : r.error));
    });
  }

  void ConsoleHost::rotate(int quarters) {
    const int q = ((quarters % 4) + 4) % 4;
    if (q == 3 && w.acts.rotateLeft) return w.acts.rotateLeft->trigger();
    for (int i = 0; i < q; ++i) w.acts.rotateRight->trigger();
  }
  void ConsoleHost::flip() { w.acts.flipImage->trigger(); }

  bool ConsoleHost::step(bool redo) {
    if (redo ? !w.canvas->canRedo() : !w.canvas->canUndo()) return false;
    (redo ? w.acts.redo : w.acts.undo)->trigger();
    return true;
  }
  int ConsoleHost::undoAll() {
    int n = 0;
    while (n < MAX_UNDO && step(false)) ++n;
    return n;
  }

  // `[format] [w h] [color]`: a page format or an explicit pixel pair, never both, then a colour.
  QString ConsoleHost::blank(const QStringList& words) {
    static const QString usage = QStringLiteral("blank takes '[format] [w h] [color]' — e.g. '/blank 800 600 white' or '/blank b5 pink'");
    QString format, color = QStringLiteral("#ffffff");
    int width = 0, height = 0, i = 0;
    if (i < words.size() && core::namedPageSize(words[i].toLower().toStdString()).width > 0) format = words[i++].toUpper();
    if (i < words.size() && words[i].toInt() > 0) {
      if (!format.isEmpty() || i + 1 >= words.size() || words[i + 1].toInt() <= 0) return usage;
      width = words[i].toInt();
      height = words[i + 1].toInt();
      i += 2;
    }
    if (i < words.size()) color = words[i++];
    if (i != words.size()) return usage;
    if (!isColour(color)) return QStringLiteral("'%1' is not a colour").arg(color);
    if (width > 0) {
      w.parts.sourceOpener.createBlankImage(QColor(color), width, height);
      return {};
    }
    QString err;
    ChatPlanTarget(w).newBlank(color, format, 0, 0, &err);
    return err;
  }

  QStringList ConsoleHost::format(const QStringList& words, QString* err) {
    ChatPlanTarget target(w);
    if (words.isEmpty())
      return {QStringLiteral("page format: ") + w.pageSizeValue(),
              QStringLiteral("formats: %1 custom <w> <h>").arg(QString::fromLatin1(core::pageFormatNames()))};
    if (words[0].toLower() == QLatin1String("custom")) {
      const double cw = words.value(1).toDouble(), ch = words.value(2).toDouble();
      if (!(cw > 0 && ch > 0)) {
        *err = QStringLiteral("custom takes '<w> <h>' in cm — e.g. '/format custom 20 15'");
        return {};
      }
      target.setPageCustom(cw, ch);
      return {QStringLiteral("page format: custom %1×%2cm").arg(cw).arg(ch)};
    }
    if (core::namedPageSize(words[0].toLower().toStdString()).width <= 0) {
      *err = QStringLiteral("no page format '%1' — '/format' lists them").arg(words[0]);
      return {};
    }
    target.setPageFormat(words[0].toUpper());
    return {QStringLiteral("page format: ") + w.pageSizeValue()};
  }

  QString ConsoleHost::filterLabel() const {
    return w.settings.imageFilter + (w.settings.imageFilter == QLatin1String("custom") ? QLatin1Char(' ') + w.settings.filterColor : QString());
  }

  QString ConsoleHost::lineColor() const { return w.settings.defaultColor; }
  QString ConsoleHost::setLineColor(const QString& color) {
    if (!isColour(color)) return QStringLiteral("color takes '#hex' or a colour name");
    llm::Action a;
    a.color = color;
    ChatPlanTarget(w).setDefaultLineStyle(a);
    return {};
  }
  QString ConsoleHost::pointColor() const { return orDefault(w.settings.defaultPointColor, "(follows the line colour)"); }
  QString ConsoleHost::setPointColor(const QString& color) {
    if (!color.isEmpty() && !isColour(color)) return QStringLiteral("point-color takes '#hex', a name, or 'clear'");
    llm::Action a;
    a.pointColorSet = true;
    a.pointColor = color;
    ChatPlanTarget(w).setDefaultLineStyle(a);
    return {};
  }
  QString ConsoleHost::projectColor() const { return orDefault(w.parts.projects.activeProjectColor(), "(default)"); }
  QString ConsoleHost::setProjectColor(const QString& color) {
    if (w.activeProjectId.isEmpty() && w.remote.session->getLink().id.isEmpty()) return QStringLiteral("save the project first");
    if (color.isEmpty()) {
      w.parts.projects.setActiveProjectColor(QString());
      return {};
    }
    const std::optional<QString> hex = w.parts.projects.normalizeProjectColor(color);
    if (!hex) return QStringLiteral("project-color takes '#hex', a name, or 'clear'");
    w.parts.projects.setActiveProjectColor(*hex);
    return {};
  }

  void ConsoleHost::applyLayoutJson(const QString& json, support::ConsoleDone done) {
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return done(ConsoleOutput::error(QStringLiteral("apply: that is not a layout JSON object")));
    w.dataExport->applyLayoutJson(doc.object());
    done(ConsoleOutput::plain(QStringLiteral("layout drawn")));
  }

  void ConsoleHost::save(const QString& path, support::ConsoleDone done) {
    if (path.isEmpty()) {
      QString err;
      const bool ok = ChatPlanTarget(w).saveProject(QString(), QString(), &err);
      return done(ok ? ConsoleOutput::plain(QStringLiteral("saved")) : ConsoleOutput::error(err));
    }
    ChatPlanTarget(w).saveProjectThen(QString(), path, [done, path](bool ok, const QString& err) {
      done(ok ? ConsoleOutput::plain(QStringLiteral("saved ") + path) : ConsoleOutput::error(err));
    });
  }
  void ConsoleHost::saveLayout() { w.dataExport->downloadLayout(); }

  // `dark|light|auto` is the theme mode (auto = the system's); anything else names the accent.
  QStringList ConsoleHost::theme(const QString& arg, QString* err) {
    const QString word = arg.toLower();
    QStringList presets;
    for (const AccentPreset& p : accentPresets()) presets << p.key;
    if (word.isEmpty())
      return {QStringLiteral("theme: %1 · accent: %2").arg(w.settings.themeMode, w.settings.accentColor),
              QStringLiteral("modes: light dark auto · accents: %1 #hex").arg(presets.join(QLatin1Char(' ')))};
    ChatPlanTarget target(w);
    if (stage) stage->wearSkin(nullptr);   // a worn skin comes off first, as the Zig /theme does
    if (word == QLatin1String("dark") || word == QLatin1String("light") || word == QLatin1String("auto") || word == QLatin1String("system")) {
      target.setTheme(word == QLatin1String("auto") ? QStringLiteral("system") : word);
      return {QStringLiteral("theme: ") + w.settings.themeMode};
    }
    QString note;
    if (presets.contains(word)) target.setAccentPreset(word, &note);
    else if (word.startsWith(QLatin1Char('#')) && isColour(word)) target.setAccent(word);
    else {
      *err = QStringLiteral("no theme '%1' — modes: light dark auto; accents: %2 #hex").arg(arg, presets.join(QLatin1Char(' ')));
      return {};
    }
    return {QStringLiteral("theme set to %1 (%2)").arg(w.settings.accentColor, accentPrimary(w.settings.accentColor).name())};
  }

}  // namespace stencil::gui
