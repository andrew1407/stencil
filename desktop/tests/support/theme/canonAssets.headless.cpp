// Drift guards for the data the desktop READS instead of embedding: themeTokens.json → theme.cpp's
// Palette and its token helpers, in both themes; resources/app.qss → buildStylesheet's token map;
// mediaTypes.json `surfaces.desktop` → mediaTypes.cpp's two suffix sniffers; llm/systemPrompt.json
// contextSuffix* → the chat's per-turn context line; and the committed "Open in…" template → the
// deep-link scheme. A broken app.qrc alias parses to nothing, so every block fails fast.
#include "fileStore.hpp"
#include "launchOptions.hpp"
#include "MediaLoader.hpp"
#include "opRegistry.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cstdio>

#include "../../support/check.hpp"

using namespace stencil::gui;

namespace {
  QJsonObject readConfig(const char* res) {
    QFile f(res);
    if (!f.open(QIODevice::ReadOnly)) return QJsonObject();
    return QJsonDocument::fromJson(f.readAll()).object();
  }

  void sameColor(const QColor& got, const QString& want, const char* what) {
    check(got.isValid() && got.name() == QColor(want).name(), what);
    if (got.name() != QColor(want).name())
      std::printf("       %s: %s, canon %s\n", what, qPrintable(got.name()), qPrintable(want));
  }
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  // ── themeTokens.json → the Palette ────────────────────────────────────────
  const QJsonObject tokens = readConfig(":/config/themeTokens.json").value("tokens").toObject();
  check(tokens.size() > 40, "themeTokens.json qrc alias resolves and carries the token set");
  for (const bool dark : {false, true}) {
    const QString side = dark ? QStringLiteral("dark") : QStringLiteral("light");
    const auto canon = [&](const char* css) {
      return tokens.value(QLatin1String(css)).toObject().value(side).toString();
    };
    const Palette p = themePalette(dark, QStringLiteral("violet"));
    const struct { const char* css; QColor got; } rows[] = {
        {"--bg-page", p.bgPage},               {"--bg-container", p.bgContainer},
        {"--bg-controls", p.bgControls},       {"--bg-sel-panel", p.bgSelPanel},
        {"--border-main", p.borderMain},       {"--border-canvas", p.borderCanvas},
        {"--border-sel", p.borderSel},         {"--text-main", p.textMain},
        {"--text-muted", p.textMuted},         {"--text-sel-label", p.textSelLabel},
        {"--bg-sel-btn", p.bgSelBtn},          {"--bg-sel-btn-hov", p.bgSelBtnHov},
        {"--text-sel-btn", p.textSelBtn},      {"--input-bg", p.inputBg},
        {"--input-text", p.inputText},         {"--danger", p.danger},
        {"--bg-coord-hover", p.bgCoordHover},  {"--warning", p.warning},
        {"--disabled-text", p.disabledText},   {"--bg-info", infoBackground(dark)},
        {"--danger-2", dangerHover(dark)},     {"--sb-thumb", canvasScrollThumb(dark)},
    };
    for (const auto& r : rows)
      sameColor(r.got, canon(r.css), r.css);
    check(rows[0].got.isValid() && !canon("--bg-page").isEmpty(),
          "the canon answers for this theme");
    // The accent-derived three are NOT read from the canon: --accent is only the violet
    // default there, and --text-key/--on-accent are color-mix expressions.
    sameColor(p.accent, accentPrimary(QStringLiteral("violet")).name(), "--accent (violet default)");
    sameColor(p.textKey, accentShade(p.accent, dark).name(), "--text-key = accentShade");
    sameColor(p.onAccent, onAccentInk(p.accent).name(), "--on-accent = onAccentInk");
  }
  // The two Palette fields that are DEFAULT_VISUALS, not theme.css tokens.
  {
    const QJsonObject vis = readConfig(":/config/constants.json").value("DEFAULT_VISUALS").toObject();
    const Palette p = themePalette(false, QStringLiteral("violet"));
    sameColor(p.selGlow, vis.value("selGlowColor").toString(), "DEFAULT_VISUALS.selGlowColor");
    sameColor(p.hoverRing, vis.value("hoverRingColor").toString(), "DEFAULT_VISUALS.hoverRingColor");
  }

  // ── app.qss → the token map in theme.cpp ──────────────────────────────────
  {
    QFile qss(QStringLiteral(":/qss/app.qss"));
    check(qss.open(QIODevice::ReadOnly), "app.qss qrc alias resolves");
    const QString sheet = QString::fromUtf8(qss.readAll());
    check(sheet.size() > 40000, "app.qss carries the whole stylesheet");
    static const QRegularExpression token(QStringLiteral("%[A-Z0-9_]+%"));
    check(token.match(sheet).hasMatch(), "the template still spends %TOKEN% placeholders");
    for (const bool dark : {false, true}) {
      const QString built = buildStylesheet(dark, QStringLiteral("violet"));
      const QRegularExpressionMatch left = token.match(built);
      check(!left.hasMatch(), "every %TOKEN% in app.qss is one theme.cpp fills");
      if (left.hasMatch()) std::printf("       unfilled: %s\n", qPrintable(left.captured(0)));
      check(built.size() > sheet.size() - 4000, "the built sheet is the template, filled");
    }
  }

  // ── mediaTypes.json surfaces.desktop → the suffix sniffers ────────────────
  {
    const QJsonObject desktop = readConfig(":/config/mediaTypes.json")
                                    .value("surfaces").toObject().value("desktop").toObject();
    const QJsonArray video = desktop.value("video").toArray();
    const QJsonArray image = desktop.value("image").toArray();
    check(video.size() == 13 && image.size() == 6, "the canon lists 13 video + 6 image suffixes");
    bool all = true;
    for (const QJsonValue& v : video) all = all && isVideoFileName("clip." + v.toString());
    for (const QJsonValue& v : image) all = all && isImageFileName("shot." + v.toString());
    check(all, "every canon suffix is recognised (upper/lower aside)");
    check(isVideoFileName("clip.MP4") && isImageFileName("shot.JPEG"), "suffixes are case-folded");
    // Outside surfaces.desktop: the contract set's extras stay unrecognised until that
    // widening is its own, stated change (the asset's `drift` note).
    check(!isVideoFileName("clip.gifv") && !isImageFileName("shot.avif"),
          "the wider contract set is NOT what the desktop accepts");
  }

  // ── llm/systemPrompt.json contextSuffix* → the chat context line ──────────
  {
    const QJsonObject prose = readConfig(":/config/llm/systemPrompt.json");
    for (const char* key : {"contextSuffixImage", "contextSuffixNoImage",
                            "contextSuffixVideoFrames", "contextSuffixVideo"}) {
      const QString canon = prose.value(QLatin1String(key)).toString();
      check(!canon.isEmpty() && stencil::llm::promptText(QLatin1String(key)) == canon,
            "promptText serves the canon's context suffix");
    }
    check(prose.value("contextSuffixImage").toString()
              == QStringLiteral("Current context: the working image is %1×%2 px."),
          "the image context line is byte-identical to the wording it replaced");
    check(prose.value("contextSuffixVideoFrames").toString()
              == QStringLiteral("The current input is a video with about %1 frames."),
          "the video context line is byte-identical to the wording it replaced");
  }

  // ── the "Open in…" template → the deep link the other surfaces build ──────
  {
    const QJsonObject cfg = readConfig(":/config/openInConfig.example.json");
    const QString scheme = cfg.value("desktopScheme").toString();
    check(scheme == QLatin1String("stencil"), "the canon names the scheme the desktop answers to");
    const LaunchOptions o = parseStencilUrl(QUrl(scheme + "://open?server=h:1&id=p1"));
    check(o.serverProjectId == QLatin1String("p1"), "a link in the canon's scheme parses");
    check(cfg.value("telegramBotUsername").toString().isEmpty()
              && Settings().telegramBotUsername.isEmpty(),
          "Telegram stays hidden until a bot username is configured, on either surface");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
