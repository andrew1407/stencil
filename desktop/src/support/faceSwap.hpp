#pragma once
// Toggle FACE swap — one shared exchange for the controls whose glyph and word change
// together (Draw's Start ▶ / Stop ■, and its Line/Rect neighbour). Port of the browser's
// js/ui/motion.js swapContent (.swapping / .swap-ghost in animations.css). The browser
// overlays a ghost of the old face; a QAbstractButton has one text and one icon, so the
// halves run in SEQUENCE and the face is exchanged at the pivot — where nothing is on
// screen, which is also where a caller's state flip (the accent fill) hides.
//
// The label cannot be moved (Qt lays a button's text out itself), so its share of the
// motion is a fade pushed through a widget stylesheet — the only per-widget lever that
// outranks the app-wide QSS colour.
//
// Header-only and Q_OBJECT-free (no signals/slots), so it needs no MOC.
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

  // Browser timings: swapFaceOut 0.22s ease-in, swapGlyphIn 0.26s — overlapped there,
  // sequential here, so each half is halved to keep the whole exchange under a click's
  // worth of time (a held shortcut repeats faster than that, see swapFace).
  inline constexpr int kFaceSwapOutMs = 110;
  inline constexpr int kFaceSwapInMs = 130;
  inline constexpr int kFaceSwapMs = kFaceSwapOutMs + kFaceSwapInMs;
  // Where the exchange sits in the run, as a share of it.
  inline constexpr double kFaceSwapPivot = double(kFaceSwapOutMs) / double(kFaceSwapMs);
  // Browser: swapGlyphIn `rotate(-115deg) scale(0.55)`.
  inline constexpr double kFaceSwapTurnDeg = 115.0;
  inline constexpr double kFaceSwapMinScale = 0.55;

  inline constexpr const char* kFaceSwapAnimName = "stencilFaceSwap";
  // Set while the swap owns the button's colour, so the widget stylesheet below can
  // match with the same weight as the app-wide state rules (and lose to nothing).
  inline constexpr const char* kFaceSwappingProperty = "stencilFaceSwapping";
  // The face currently PAINTED, kept on the button: QToolButton::setDefaultAction
  // re-copies text and icon from the action, so neither can be trusted as the outgoing
  // face by the time a caller asks for a swap.
  inline constexpr const char* kFaceGlyphProperty = "stencilFaceGlyph";
  inline constexpr const char* kFaceLabelProperty = "stencilFaceLabel";
  inline constexpr const char* kFaceGlyphColorProperty = "stencilFaceGlyphColor";
  inline constexpr const char* kFaceTextColorProperty = "stencilFaceTextColor";
  inline constexpr const char* kFaceIconSizeProperty = "stencilFaceIconSize";
  inline constexpr const char* kFaceGapProperty = "stencilFaceGap";
  inline constexpr const char* kFaceBaseSheetProperty = "stencilFaceBaseSheet";
  inline constexpr const char* kFaceLabelColorProperty = "stencilFaceLabelColor";

  // One side of a toggle: what the button says and shows once it settles there.
  struct FaceSpec {
    QString glyph;        // iconSet name ("play" / "stop" / "line" / "rect")
    QString label;        // the word beside it; null = leave the text alone
    QColor glyphColor;    // the glyph's tint at rest
    QColor textColor;     // the label's colour at rest; invalid = don't touch the colour
    int iconSize = 16;
    // Transparent air carried on the glyph's RIGHT, so the label is not welded to it. Qt's
    // text-beside-icon gap is a fixed 4px (pixmap width + 4) and QSS `spacing` does nothing
    // for a QToolButton, so the room has to be in the PIXMAP.
    int gapPx = 0;
  };

  // One frame of the exchange: which face it belongs to and how it is drawn.
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
