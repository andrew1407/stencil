#pragma once

// A page's natural size in cm, the proportions a crop or a blank picture takes.

#include "pageMetrics.hpp"

#include <QString>

namespace stencil::gui {

  // NOT orientation-swapped — only the proportions matter. Browser: cropModal.pageDims.
  inline core::PageSize naturalPageCm(const QString& pageSize, double customW,
                                      double customH) {
    if (pageSize == "custom") return {customW, customH};
    const core::PageSize ps = core::namedPageSize(pageSize.toStdString());
    return ps.width > 0 ? ps : core::namedPageSize("A4");
  }

}  // namespace stencil::gui
