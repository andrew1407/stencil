// The shared op-plan corpus as the desktop walkers read it: the hand-written, generated and
// adversarial bundles under common/fixtures/llm/opPlan/, each reply text built the way
// genOpPlanFixtures.mjs builds it (JS key order, raw bytes, repeated parts). Twin of
// core/tests/opplan/opplanCorpus.hpp.
#pragma once

#include "jsText.hpp"
#include "jsonReader.hpp"
#include "jsonWriter.hpp"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <string>
#include <vector>

#include "../../support/fixtureCorpus.hpp"

namespace opPlanCorpus {

  using stencil::core::json::Value;

  struct Case {
    QString label;        // the hand-written "file", else "<name>.json" (generated) or the oracle name
    std::string source;   // "hand" | "generated" | "oracle"
    Value fx;
    std::string text;     // the reply's bytes
  };

  inline Value readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return Value();
    const QByteArray bytes = f.readAll();
    return stencil::core::json::readJson(std::string_view(bytes.constData(), bytes.size())).value;
  }

  inline std::string caseText(const Value& c) {
    using namespace stencil::core::json;
    if (const Value* b = c.get("inputBase64"))
      return QByteArray::fromBase64(QByteArray::fromStdString(b->text)).toStdString();
    std::string text;
    if (const Value* parts = c.get("parts")) {
      for (const Value& p : parts->items)
        for (int i = 0; i < static_cast<int>(p.items[1].number); ++i) text += p.items[0].text;
    } else if (const Value* in = c.get("input")) {
      text = in->isString() ? in->text : toJson(*in, LoneSurrogates::ESCAPE);
    }
    return wellFormed(text);
  }

  inline std::vector<Case> load() {
    const QString dir = corpusPath("llm/opPlan");
    const std::pair<const char*, QString> bundles[] = {
        {"hand", dir + "/cases.json"},
        {"generated", dir + "/generated/cases.json"},
        {"oracle", dir + "/oracle/inputs.json"},
    };
    std::vector<Case> out;
    for (const auto& [source, path] : bundles) {
      const Value doc = readFile(path);
      const Value* cases = doc.get("cases");
      if (!cases) continue;
      for (const Value& c : cases->items) {
        const Value* file = c.get("file");
        const QString name = QString::fromStdString(c.get("name") ? c.get("name")->text : std::string());
        const QString label = file ? QString::fromStdString(file->text)
                              : std::string(source) == "generated" ? name + ".json" : name;
        out.push_back({label, source, c, caseText(c)});
      }
    }
    return out;
  }

}  // namespace opPlanCorpus
