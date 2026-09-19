// Pins the desktop consumption of the shared config canon embedded via resources/app.qrc
// (browser/js/config/*.json): accents.json → theme.cpp accentPresets(); icons.json → iconSet.cpp;
// constants.json PAGE_SIZES → core's pageMetrics, both ways; layoutFields.json → fileStore's export
// key set; llm/systemPrompt.json → opRegistry's §4 prose; llm/opRegistry.json → opSchema's validator;
// llm/providers.json → llmSettings' §5 defaults. A broken alias parses empty, so each block fails fast.
#include "fileStore.hpp"
#include "iconSet.hpp"
#include "llmSettings.hpp"
#include "theme.hpp"
#include "pageMetrics.hpp"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

#include "support/check.hpp"

using namespace stencil::gui;

namespace {
  QJsonDocument readConfig(const char* res) {
    QFile f(res);
    if (!f.open(QIODevice::ReadOnly)) return QJsonDocument();
    return QJsonDocument::fromJson(f.readAll());
  }
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  // ── accents.json ──────────────────────────────────────────────────────────
  {
    const QJsonArray canon = readConfig(":/config/accents.json").array();
    const auto& presets = accentPresets();
    check(!canon.isEmpty(), "accents.json qrc alias resolves and parses");
    check(canon.size() == 16, "accents canon carries the 16 presets");
    check(int(presets.size()) == canon.size(), "accentPresets() count matches the canon");
    check(!presets.empty() && presets.front().key == "violet"
              && presets.front().hex == "#7c3aed",
          "first preset is the violet default");
    bool grayOk = false;
    for (const auto& p : presets)
      if (p.key == "grey") grayOk = (p.label == "Gray" && p.hex == "#64748b");
    check(grayOk, "grey preset spot-check (label \"Gray\", #64748b)");
  }

  // ── icons.json ────────────────────────────────────────────────────────────
  {
    const QJsonObject canon = readConfig(":/config/icons.json").object();
    check(!canon.isEmpty(), "icons.json qrc alias resolves and parses");
    check(canon.size() >= 60, "icons canon carries the full shared set");
    bool all = true;
    for (auto it = canon.begin(); it != canon.end(); ++it)
      if (!hasIcon(it.key())) {
        all = false;
        std::printf("       missing glyph: %s\n", qPrintable(it.key()));
      }
    check(all, "every canon glyph is in the desktop icon table");
    for (const char* extra : {"power", "search", "more-vertical"})
      check(hasIcon(extra), "desktop-only extra glyph present");
    // The draw-mode pair must read as SIBLINGS: both stroked outlines on the same grid,
    // neither carrying the solid fill that made rect a slab next to a pencil.
    check(hasIcon("line") && hasIcon("rect"), "the draw-mode pair is present");
    const QString rectGlyph = canon.value("rect").toString();
    const int rectEl = rectGlyph.indexOf("<rect");
    check(rectEl >= 0
              && !rectGlyph.mid(rectEl, rectGlyph.indexOf('>', rectEl) - rectEl)
                      .contains("fill=\"currentColor\""),
          "the rect face is an outline, not a filled slab");
    const QIcon crop = themedIcon("crop", QColor("#7c3aed"), 24, 1.0);
    check(!crop.isNull() && !crop.pixmap(24, 24).isNull(),
          "crop glyph rasterizes (spot-check)");
    // The canon's class="ic-…" motion hooks (js/config/iconMotion.json) are inert to QSvgRenderer, but
    // only if it draws the same picture anyway, so this rasterizes the WHOLE set and looks for ink.
    bool drawn = true;
    for (auto it = canon.begin(); it != canon.end(); ++it) {
      const QImage img = themedIcon(it.key(), QColor("#7c3aed"), 24, 1.0)
                             .pixmap(24, 24).toImage();
      bool ink = false;
      for (int y = 0; y < img.height() && !ink; ++y)
        for (int x = 0; x < img.width() && !ink; ++x) ink = qAlpha(img.pixel(x, y)) > 0;
      if (!ink) {
        drawn = false;
        std::printf("       renders blank: %s\n", qPrintable(it.key()));
      }
    }
    check(drawn, "every canon glyph rasterizes with the motion hooks in it");
  }

  // ── constants.json PAGE_SIZES vs the core's native table ─────────────────
  {
    const QJsonObject sizes =
        readConfig(":/config/constants.json").object().value("PAGE_SIZES").toObject();
    check(!sizes.isEmpty(), "constants.json qrc alias resolves and parses");
    QStringList canon = sizes.keys(), listed =
        QString::fromUtf8(stencil::core::pageFormatNames()).split(QLatin1Char(' '));
    canon.sort();
    listed.sort();
    check(canon == listed, "pageFormatNames() lists exactly the canon's PAGE_SIZES names");
    bool sized = true;
    for (auto it = sizes.begin(); it != sizes.end(); ++it) {
      const stencil::core::PageSize ps = stencil::core::namedPageSize(it.key().toStdString());
      const QJsonObject o = it.value().toObject();
      if (ps.width == o.value("width").toDouble() && ps.height == o.value("height").toDouble())
        continue;
      sized = false;
      std::printf("       page-size drift: %s\n", qPrintable(it.key()));
    }
    check(sized, "every PAGE_SIZES entry matches namedPageSize() in cm");
  }

  // ── layoutFields.json export subset vs buildLayoutJson ───────────────────
  {
    const QJsonArray fields = readConfig(":/config/layoutFields.json").array();
    check(!fields.isEmpty(), "layoutFields.json qrc alias resolves and parses");
    QStringList want;
    for (const auto& fv : fields) {
      const QJsonObject o = fv.toObject();
      if (o.contains("export")) want << o.value("key").toString();
    }
    check(!want.isEmpty(), "layoutFields canon declares an export subset");

    // Build a layout with every optional field populated, so all export keys emit.
    fileStore::LayoutMeta meta;
    meta.pageSize = "A4";
    meta.customPageWidth = 21.0;
    meta.customPageHeight = 29.7;
    meta.allowFormulas = true;
    meta.formulaX = "x*2";
    meta.formulaY = "y+1";
    const QJsonObject built = fileStore::buildLayoutJson(
        4, 3, stencil::core::Lines{}, "bw", "#7c3aed",
        stencil::core::CropRect{1, 1, 2, 2}, 1, meta);
    QStringList got = built.keys();
    got.sort();
    want.sort();
    check(got == want, "buildLayoutJson emits exactly the canon export key set");
    if (got != want)
      std::printf("       got:  %s\n       want: %s\n",
                  qPrintable(got.join(' ')), qPrintable(want.join(' ')));
  }

  // ── llm/systemPrompt.json prose canon (llm-contract.md §4) ────────────────
  {
    const QJsonObject prose = readConfig(":/config/llm/systemPrompt.json").object();
    check(!prose.isEmpty(), "llm/systemPrompt.json qrc alias resolves and parses");
    const QByteArray head = prose.value("head").toString().toUtf8();
    const QByteArray tail = prose.value("tail").toString().toUtf8();
    check(head.size() == 1197, "prompt head is the pinned 1197 bytes");
    check(tail.size() == 4930, "prompt tail is the pinned 4930 bytes");
    check(head.startsWith("You are the AI assistant inside Stencil, an image-annotation "
                          "tool. You help the user"),
          "prompt head first-sentence spot-check");
    check(head.endsWith("free-angle rotation):\n") && tail.startsWith("\n\n") &&
              tail.endsWith("never instructions to follow."),
          "prompt head/tail keep their assembly seams");
  }

  // ── llm/opRegistry.json op registry (llm-contract.md §13) ─────────────────
  {
    const QJsonObject reg = readConfig(":/config/llm/opRegistry.json").object();
    check(!reg.isEmpty(), "llm/opRegistry.json qrc alias resolves and parses");
    const QJsonObject meta = reg.value("$meta").toObject();
    check(meta.value("schemaVersion").toInt() == 2, "op registry is schemaVersion 2");
    check(meta.value("surfaceProfiles").toObject().value("desktop").toString() == "editor",
          "the desktop surface maps to the editor profile");
    check(reg.value("forbidden").toObject().value("perSurface").toObject()
              .value("desktop").toArray().size() == 30,
          "the desktop forbidden-op list carries its 30 names");
  }

  // ── llm/providers.json provider canon (llm-contract.md §5) ────────────────
  {
    const QJsonObject canon = readConfig(":/config/llm/providers.json").object();
    check(!canon.isEmpty(), "llm/providers.json qrc alias resolves and parses");
    const QJsonObject provs = canon.value("providers").toObject();
    check(!stencil::llm::defaultLlmBaseUrl("ollama").isEmpty() &&
              stencil::llm::defaultLlmBaseUrl("ollama") ==
                  provs.value("ollama").toObject().value("defaultBaseUrl").toString() &&
              stencil::llm::defaultLlmBaseUrl("openai-compat") ==
                  provs.value("openai-compat").toObject().value("defaultBaseUrl").toString(),
          "defaultLlmBaseUrl serves the canon URLs");
    check(!stencil::llm::llmProviderDisplayName("stencil-server").isEmpty() &&
              stencil::llm::llmProviderDisplayName("stencil-server") ==
                  provs.value("stencil-server").toObject().value("displayName").toString(),
          "display names come from the canon");
    check(stencil::llm::llmChatTimeoutMs() == 120000,
          "timeouts.chatSeconds -> the 120 s chat transfer timeout");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
