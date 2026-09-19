#pragma once
// Private seam between the planExecutor TUs: the crop resolver, the frame bookkeeping and the three
// op groups applyAction dispatches through. Each group sets *handled when the op was its own, so a
// case body reads exactly as it did inside the one big switch.
#include "planExecutor.hpp"

#include "CanvasWidget.hpp"
#include "colorNames.hpp"
#include "cropSpec.hpp"
#include "formulaParser.hpp"
#include "opRegistry.hpp"

#include <QColor>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace stencil::llm {

  namespace exec {

    // Resolve a crop action's edge tokens to a pixel rect in rotated-original space - the same core
    // cropSpec path the CLI's --crop drives, with the CLI's px-per-cm derivation and clamping.
    inline bool resolveCrop(const Action& a, const QSize& imageSize, const core::PageSize& page,
                     core::CropRect& out, QString* err) {
      core::CropSpec spec;
      if (!a.x1.isEmpty()) spec.x1 = a.x1.toStdString();
      if (!a.x2.isEmpty()) spec.x2 = a.x2.toStdString();
      if (!a.y1.isEmpty()) spec.y1 = a.y1.toStdString();
      if (!a.y2.isEmpty()) spec.y2 = a.y2.toStdString();
      if (!a.aspect.isEmpty()) spec.aspect = a.aspect.toStdString();

      const double w = imageSize.width();
      const double h = imageSize.height();
      core::CropResolveParams p;
      p.imageW = w;
      p.imageH = h;
      p.pxPerCmX = page.width > 0 ? w / page.width : 0;
      p.pxPerCmY = page.height > 0 ? h / page.height : 0;
      p.pageWidth = page.width;
      p.pageHeight = page.height;
      const bool album = core::isAlbumOrientation(w, h);

      const auto rect = core::resolveCropRect(spec, p, album);
      if (!rect) {
        if (err) *err = QStringLiteral("crop: could not resolve the crop spec");
        return false;
      }
      const int iw = static_cast<int>(std::lround(w));
      const int ih = static_cast<int>(std::lround(h));
      const int x = std::clamp(static_cast<int>(std::lround(rect->x)), 0, std::max(0, iw));
      const int y = std::clamp(static_cast<int>(std::lround(rect->y)), 0, std::max(0, ih));
      const int cw = std::clamp(static_cast<int>(std::lround(rect->width)), 0, iw - x);
      const int ch = std::clamp(static_cast<int>(std::lround(rect->height)), 0, ih - y);
      if (cw <= 0 || ch <= 0) {
        if (err) *err = QStringLiteral("crop: the resolved region is empty");
        return false;
      }
      out = {static_cast<double>(x), static_cast<double>(y), static_cast<double>(cw),
             static_cast<double>(ch)};
      return true;
    }

    // Running model-frame -> current-frame map (contract §1): plan coordinates are in the frame of the
    // image the model saw. Crop composes -origin, rotate the quarter-turn map; blank/frame/clear reset.
    struct FrameMap {
      double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;

      void reset() { *this = FrameMap{}; }
      void composeCrop(const core::CropRect& r) {
        tx -= r.x;
        ty -= r.y;
      }
      // `w`/`h` are the working-image dims BEFORE this quarter turn. Same
      // mapping as core::rotateLinePointsQuarter: cw (x,y)→(h-y,x); ccw (y,w-x).
      void composeRotate(bool clockwise, double w, double h) {
        const FrameMap m = *this;
        if (clockwise) {
          a = -m.c; b = -m.d; tx = h - m.ty;
          c = m.a;  d = m.b;  ty = m.tx;
        } else {
          a = m.c;  b = m.d;  tx = m.ty;
          c = -m.a; d = -m.b; ty = w - m.tx;
        }
      }
      core::Point map(const core::Point& p) const {
        return {a * p.x + b * p.y + tx, c * p.x + d * p.y + ty};
      }
    };

    bool applyImageAction(const Action& a, PlanTarget& target, FrameMap& frame, bool inVariant,
                       QStringList* notes, bool* handled, QString* err);
    bool applyEditAction(const Action& a, PlanTarget& target, FrameMap& frame, bool inVariant,
                       QStringList* notes, bool* handled, QString* err);
    bool applyStateAction(const Action& a, PlanTarget& target, FrameMap& frame, bool inVariant,
                       QStringList* notes, bool* handled, QString* err);

  }  // namespace exec

}  // namespace stencil::llm
