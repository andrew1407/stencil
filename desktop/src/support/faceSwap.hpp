#pragma once
// Toggle FACE swap — port of browser js/ui/motion.js swapContent. A QAbstractButton has
// one text and one icon, so the halves run in SEQUENCE and the face is exchanged at the
// pivot; the label's fade goes through a widget stylesheet. Q_OBJECT-free, no MOC.
#include "iconSet.hpp"
#include "modalReveal.hpp"   // support::motionReduced()

#include <QAbstractAnimation>
#include <QAbstractButton>
#include <QColor>
#include <QGuiApplication>
#include <QIcon>
#include <QObject>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QStyle>
#include <QVariant>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace stencil::gui {

  // Browser swapFaceOut 0.22s / swapGlyphIn 0.26s overlap; sequential here, each half halved.
  inline constexpr int kFaceSwapOutMs = 110;
  inline constexpr int kFaceSwapInMs = 130;
  inline constexpr int kFaceSwapMs = kFaceSwapOutMs + kFaceSwapInMs;
  inline constexpr double kFaceSwapPivot = double(kFaceSwapOutMs) / double(kFaceSwapMs);
  // Browser: swapGlyphIn `rotate(-115deg) scale(0.55)`.
  inline constexpr double kFaceSwapTurnDeg = 115.0;
  inline constexpr double kFaceSwapMinScale = 0.55;

  inline constexpr const char* kFaceSwapAnimName = "stencilFaceSwap";
  // Set while the swap owns the button's colour (the widget stylesheet's selector).
  inline constexpr const char* kFaceSwappingProperty = "stencilFaceSwapping";
  // QToolButton::setDefaultAction re-copies text and icon from the action, so neither
  // can be trusted as the outgoing face.
  inline constexpr const char* kFaceGlyphProperty = "stencilFaceGlyph";
  inline constexpr const char* kFaceLabelProperty = "stencilFaceLabel";
  inline constexpr const char* kFaceGlyphColorProperty = "stencilFaceGlyphColor";
  inline constexpr const char* kFaceTextColorProperty = "stencilFaceTextColor";
  inline constexpr const char* kFaceIconSizeProperty = "stencilFaceIconSize";
  inline constexpr const char* kFaceGapProperty = "stencilFaceGap";
  inline constexpr const char* kFaceBaseSheetProperty = "stencilFaceBaseSheet";
  inline constexpr const char* kFaceLabelColorProperty = "stencilFaceLabelColor";

  struct FaceSpec {
    QString glyph;        // iconSet name ("play" / "stop" / "line" / "rect")
    QString label;        // the word beside it; null = leave the text alone
    QColor glyphColor;    // the glyph's tint at rest
    QColor textColor;     // the label's colour at rest; invalid = don't touch the colour
    int iconSize = 16;
    // Qt's text-beside-icon gap is a fixed 4px and QSS `spacing` does nothing for a
    // QToolButton, so the room has to be in the PIXMAP.
    int gapPx = 0;
  };

  struct FaceSwapFrame {
    bool incoming;   // false = the old face leaving, true = the new one arriving
    double alpha;
    double deg;
    double scale;
  };

  double faceEaseInCubic(double u);
  double faceEaseOutExpo(double u);

  FaceSwapFrame faceSwapFrame(double t);

  namespace detail {

    QIcon withGap(const QIcon& base, int size, int gap);

    QIcon faceIcon(const FaceSpec& f, const FaceSwapFrame& fr);

    void repolish(QWidget* w);

    void setLabelAlpha(QAbstractButton* btn, const QColor& color, double alpha);

    void clearLabelAlpha(QAbstractButton* btn);

    void rememberFace(QAbstractButton* btn, const FaceSpec& f);

    FaceSpec paintedFace(const QAbstractButton* btn, bool* known);

    void paintFace(QAbstractButton* btn, const FaceSpec& f, const FaceSwapFrame& fr);

    void settleFace(QAbstractButton* btn, const FaceSpec& f);

  }  // namespace detail

  bool faceSwapping(const QAbstractButton* btn);

  void repaintFace(QAbstractButton* btn);

  void swapFace(QAbstractButton* btn, const FaceSpec& to,
                const std::function<void()>& applyState = {},
                int ms = kFaceSwapMs);

}  // namespace stencil::gui
