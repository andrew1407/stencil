#pragma once
// The .stencil merge rule, private to the stencilFileSync*.cpp TUs.
#include "mainWindow.hpp"

namespace stencil::gui {

  // Union two line lists, de-duplicating by the compact JSON of each line (a merge that keeps
  // both editors' annotations without duplicating a round-tripped twin — mirrors browser mergeLines).
  inline core::Lines mergeLinesUnion(const core::Lines& base, const core::Lines& extra) {
    core::Lines out = base;
    QSet<QString> seen;
    auto keyOf = [](const core::Line& l) {
      return QString::fromUtf8(QJsonDocument(fileStore::lineToJson(l)).toJson(QJsonDocument::Compact));
    };
    for (const auto& l : base) seen.insert(keyOf(l));
    for (const auto& l : extra) {
      const QString k = keyOf(l);
      if (!seen.contains(k)) { out.push_back(l); seen.insert(k); }
    }
    return out;
  }

}  // namespace stencil::gui
