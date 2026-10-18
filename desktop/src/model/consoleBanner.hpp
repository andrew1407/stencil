#pragma once
// The console logo's rows and layout out of core/format/logoBanner, Qt-shaped for the CLI
// stage: the one include of that core header (the model seam). Browser twin:
// js/core/format/logoBanner.js.
#include "logoBanner.hpp"
#include <QString>
#include <QStringList>

namespace stencil::model {

  using core::BannerLayout;

  inline core::LogoMark logoMarkOf(const QString& mark) {
    if (mark == QLatin1String("P")) return core::LogoMark::P;
    if (mark == QLatin1String("M")) return core::LogoMark::M;
    return core::LogoMark::S;
  }

  inline BannerLayout consoleBannerLayout(bool compact) { return core::logoBannerLayout(compact); }

  inline QStringList consoleBannerRows(const QString& mark, bool compact) {
    QStringList out;
    for (const std::string& row : core::logoBannerRows(logoMarkOf(mark), compact))
      out << QString::fromStdString(row);
    return out;
  }

}  // namespace stencil::model
