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
  inline constexpr int FACE_SWAP_OUT_MS = 110;
  inline constexpr int FACE_SWAP_IN_MS = 130;
  inline constexpr int FACE_SWAP_MS = FACE_SWAP_OUT_MS + FACE_SWAP_IN_MS;
  inline constexpr double FACE_SWAP_PIVOT = double(FACE_SWAP_OUT_MS) / double(FACE_SWAP_MS);
  // Browser: swapGlyphIn `rotate(-115deg) scale(0.55)`.
  inline constexpr double FACE_SWAP_TURN_DEG = 115.0;
  inline constexpr double FACE_SWAP_MIN_SCALE = 0.55;

  inline constexpr const char* FACE_SWAP_ANIM_NAME = "stencilFaceSwap";
  // Set while the swap owns the button's colour (the widget stylesheet's selector).
  inline constexpr const char* FACE_SWAPPING_PROPERTY = "stencilFaceSwapping";
  // QToolButton::setDefaultAction re-copies text and icon from the action, so neither
  // can be trusted as the outgoing face.
  inline constexpr const char* FACE_GLYPH_PROPERTY = "stencilFaceGlyph";
  inline constexpr const char* FACE_LABEL_PROPERTY = "stencilFaceLabel";
  inline constexpr const char* FACE_GLYPH_COLOR_PROPERTY = "stencilFaceGlyphColor";
  inline constexpr const char* FACE_TEXT_COLOR_PROPERTY = "stencilFaceTextColor";
  inline constexpr const char* FACE_ICON_SIZE_PROPERTY = "stencilFaceIconSize";
  inline constexpr const char* FACE_GAP_PROPERTY = "stencilFaceGap";
  inline constexpr const char* FACE_BASE_SHEET_PROPERTY = "stencilFaceBaseSheet";
  inline constexpr const char* FACE_LABEL_COLOR_PROPERTY = "stencilFaceLabelColor";

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
                int ms = FACE_SWAP_MS);

}  // namespace stencil::gui
