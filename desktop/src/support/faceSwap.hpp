#pragma once
// Toggle FACE swap — one shared exchange for the controls whose glyph and word change
// together (Draw's Start ▶ / Stop ■, and its Line/Rect neighbour). Port of the browser's
// js/ui/motion.js swapContent (.swapping / .swap-ghost in animations.css): the outgoing
// face turns away, shrinking and fading; the incoming one arrives from a quarter-turn back
// and rises to rest. The browser overlays a ghost of the old face; a QAbstractButton has
// one text and one icon, so the halves run in SEQUENCE instead and the face is exchanged at
// the pivot — where nothing is on screen, which is also where a caller's state flip (the
// accent fill) hides.
//
// Nothing new in the motion vocabulary: the turn is guiHelpers::spinIcon's (rotatedIcon,
// re-rendered per frame), the fade is dissolveEffect's/filterFade's, and motionReduced()
// lands on the end state at once, as everywhere else.
//
// The label cannot be moved (Qt lays a button's text out itself), so its share of the
// motion is the fade — pushed through a widget stylesheet, the only per-widget lever that
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
  inline constexpr const char* kFaceHaloProperty = "stencilFaceHalo";
  inline constexpr const char* kFaceBaseSheetProperty = "stencilFaceBaseSheet";
  inline constexpr const char* kFaceLabelColorProperty = "stencilFaceLabelColor";

  // One side of a toggle: what the button says and shows once it settles there.
  struct FaceSpec {
    QString glyph;        // iconSet name ("play" / "stop" / "line" / "rect")
    QString label;        // the word beside it; null = leave the text alone
    QColor glyphColor;    // the glyph's tint at rest
    QColor textColor;     // the label's colour at rest; invalid = don't touch the colour
    int iconSize = 16;
    bool halo = false;    // dark halo under a white glyph on a light accent
  };

  // One frame of the exchange: which face it belongs to and how it is drawn.
  struct FaceSwapFrame {
    bool incoming;   // false = the old face leaving, true = the new one arriving
    double alpha;
    double deg;
    double scale;
  };

  inline double faceEaseInCubic(double u) {
    u = std::clamp(u, 0.0, 1.0);
    return u * u * u;
  }
  // The browser's cubic-bezier(0.16, 1, 0.3, 1) in spirit: nearly all of the distance is
  // covered up front, so the arriving face reads as settling rather than sliding.
  inline double faceEaseOutExpo(double u) {
    u = std::clamp(u, 0.0, 1.0);
    return u >= 1.0 ? 1.0 : 1.0 - std::pow(2.0, -10.0 * u);
  }

  // Progress (0..1) → the frame to paint. Pure, and the whole shape of the motion:
  // both ends are the face at rest (alpha 1, no turn, full size) and the pivot is
  // invisible, so the exchange itself is never seen. The turn's SIGN flips across the
  // pivot — the old glyph leaves at +115°, the new one comes in from -115° — which is
  // what reads as one continuous turn rather than two.
  inline FaceSwapFrame faceSwapFrame(double t) {
    t = std::clamp(t, 0.0, 1.0);
    if (t < kFaceSwapPivot) {
      const double u = faceEaseInCubic(t / kFaceSwapPivot);
      return {false, 1.0 - u, kFaceSwapTurnDeg * u,
              1.0 - (1.0 - kFaceSwapMinScale) * u};
    }
    const double u = faceEaseOutExpo((t - kFaceSwapPivot) / (1.0 - kFaceSwapPivot));
    return {true, u, -kFaceSwapTurnDeg * (1.0 - u),
            kFaceSwapMinScale + (1.0 - kFaceSwapMinScale) * u};
  }

  namespace detail {

    // The glyph turned, shrunk and faded. rotatedIcon bakes the colour into the SVG and
    // QColor::name() drops alpha, so the fade and the scale are composited here.
    inline QIcon faceIcon(const FaceSpec& f, const FaceSwapFrame& fr) {
      const int size = std::max(1, f.iconSize);
      const QIcon base = std::abs(fr.deg) < 0.01
                             ? themedIcon(f.glyph, f.glyphColor, size, f.halo)
                             : rotatedIcon(f.glyph, f.glyphColor, size, fr.deg);
      if (base.isNull()) return base;
      if (fr.alpha >= 0.999 && fr.scale >= 0.999) return base;
      const qreal dpr = qApp ? qApp->devicePixelRatio() : qreal(1);
      const QPixmap src = base.pixmap(QSize(size, size), dpr);
      QPixmap out(src.size());
      out.setDevicePixelRatio(src.devicePixelRatio());
      out.fill(Qt::transparent);
      {
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setOpacity(std::clamp(fr.alpha, 0.0, 1.0));
        const QPointF c(size / 2.0, size / 2.0);
        p.translate(c);
        p.scale(fr.scale, fr.scale);
        p.translate(-c);
        p.drawPixmap(QPointF(0, 0), src);
      }
      return QIcon(out);
    }

    inline void repolish(QWidget* w) {
      w->style()->unpolish(w);
      w->style()->polish(w);
      w->update();
    }

    // Fade the LABEL. A widget stylesheet is the only lever that beats the app-wide
    // `QToolButton[...] { color: … }` rules; the property selector gives it their weight,
    // and every state is listed so a hover/press mid-swap can't outrank it.
    inline void setLabelAlpha(QAbstractButton* btn, const QColor& color, double alpha) {
      if (!color.isValid()) return;
      // Quantised, because every write re-polishes the widget and the ease flattens near
      // both ends: a frame whose colour is already up is skipped outright.
      const double q = std::lround(std::clamp(alpha, 0.0, 1.0) * 50.0) / 50.0;
      const QString rgba = QStringLiteral("rgba(%1,%2,%3,%4)")
                               .arg(color.red())
                               .arg(color.green())
                               .arg(color.blue())
                               .arg(q, 0, 'f', 2);
      const bool already = btn->property(kFaceSwappingProperty).toBool();
      if (already && btn->property(kFaceLabelColorProperty).toString() == rgba) return;
      btn->setProperty(kFaceLabelColorProperty, rgba);
      btn->setProperty(kFaceSwappingProperty, true);
      btn->setStyleSheet(
          QStringLiteral("QToolButton[%1=\"true\"],QToolButton[%1=\"true\"]:hover,"
                         "QToolButton[%1=\"true\"]:pressed,QToolButton[%1=\"true\"]:disabled,"
                         "QPushButton[%1=\"true\"],QPushButton[%1=\"true\"]:hover"
                         "{color:%2;}")
              .arg(QString::fromLatin1(kFaceSwappingProperty), rgba));
      // Qt matches property selectors at POLISH time, so the frame that turns the property
      // on has to re-polish or the whole rule is skipped (the fade would never show).
      if (!already) repolish(btn);
    }

    // Hand the button its own stylesheet back — a settled face is styled by the app QSS
    // alone, so nothing of the swap survives it.
    inline void clearLabelAlpha(QAbstractButton* btn) {
      if (!btn->property(kFaceSwappingProperty).toBool()) return;
      btn->setProperty(kFaceSwappingProperty, false);
      btn->setProperty(kFaceLabelColorProperty, QString());
      btn->setStyleSheet(btn->property(kFaceBaseSheetProperty).toString());
      repolish(btn);   // …and the frame that turns it off, for the same reason
    }

    inline void rememberFace(QAbstractButton* btn, const FaceSpec& f) {
      btn->setProperty(kFaceGlyphProperty, f.glyph);
      btn->setProperty(kFaceLabelProperty, f.label);
      btn->setProperty(kFaceGlyphColorProperty, f.glyphColor);
      btn->setProperty(kFaceTextColorProperty, f.textColor);
      btn->setProperty(kFaceIconSizeProperty, f.iconSize);
      btn->setProperty(kFaceHaloProperty, f.halo);
    }

    // The face a previous swap left painted. `known` is false the first time, when the
    // button has no history to leave from and the new face just goes on.
    inline FaceSpec paintedFace(const QAbstractButton* btn, bool* known) {
      FaceSpec f;
      f.glyph = btn->property(kFaceGlyphProperty).toString();
      *known = !f.glyph.isEmpty();
      f.label = btn->property(kFaceLabelProperty).toString();
      f.glyphColor = btn->property(kFaceGlyphColorProperty).value<QColor>();
      f.textColor = btn->property(kFaceTextColorProperty).value<QColor>();
      f.iconSize = btn->property(kFaceIconSizeProperty).toInt();
      f.halo = btn->property(kFaceHaloProperty).toBool();
      return f;
    }

    inline void paintFace(QAbstractButton* btn, const FaceSpec& f, const FaceSwapFrame& fr) {
      btn->setIcon(faceIcon(f, fr));
      if (!f.label.isNull()) btn->setText(f.label);
      setLabelAlpha(btn, f.textColor, fr.alpha);
    }

    inline void settleFace(QAbstractButton* btn, const FaceSpec& f) {
      btn->setIcon(themedIcon(f.glyph, f.glyphColor, std::max(1, f.iconSize), f.halo));
      if (!f.label.isNull()) btn->setText(f.label);
      clearLabelAlpha(btn);
      rememberFace(btn, f);
    }

  }  // namespace detail

  // True while `btn` is mid-exchange. A superseded animation is stopped and deleteLater'd,
  // so it can still be a child for a turn of the loop — RUNNING is the question, not there.
  inline bool faceSwapping(const QAbstractButton* btn) {
    if (!btn) return false;
    for (QVariantAnimation* a : btn->findChildren<QVariantAnimation*>(
             QString::fromLatin1(kFaceSwapAnimName), Qt::FindDirectChildrenOnly))
      if (a->state() == QAbstractAnimation::Running) return true;
    return false;
  }

  // Put the REMEMBERED face back on the button, unchanged — for undoing something else's
  // meddling (a QToolButton re-copies its action's icon on every ActionChanged). It is not
  // a transition: it never touches the state, and it keeps its hands off a running swap,
  // which is already painting every frame.
  inline void repaintFace(QAbstractButton* btn) {
    if (!btn || faceSwapping(btn)) return;
    bool known = false;
    const FaceSpec painted = detail::paintedFace(btn, &known);
    if (!known) return;
    btn->setIcon(themedIcon(painted.glyph, painted.glyphColor,
                            std::max(1, painted.iconSize), painted.halo));
    if (!painted.label.isNull()) btn->setText(painted.label);
  }

  // Swap `btn`'s face to `to`, leaving from whatever a previous swap painted.
  //
  // `applyState` is the caller's own flip (the accent fill, a repolish) and runs ONCE, at
  // the pivot, hidden behind the invisible frame. It must SET the state absolutely, never
  // toggle it: a swap superseded before its pivot is dropped, and the superseding one's
  // applyState is then what the button ends on — which is how holding the shortcut always
  // converges on the real state instead of stranding a stale face.
  //
  // Reduced motion (or a first paint, or a hidden button) goes straight to `to`.
  inline void swapFace(QAbstractButton* btn, const FaceSpec& to,
                       const std::function<void()>& applyState = {},
                       int ms = kFaceSwapMs) {
    if (!btn || to.glyph.isEmpty()) return;
    if (!btn->property(kFaceBaseSheetProperty).isValid())
      btn->setProperty(kFaceBaseSheetProperty, btn->styleSheet());
    for (QVariantAnimation* old : btn->findChildren<QVariantAnimation*>(
             QString::fromLatin1(kFaceSwapAnimName), Qt::FindDirectChildrenOnly)) {
      old->stop();   // a mid-flight stop() never emits finished()
      old->deleteLater();
    }
    bool known = false;
    const FaceSpec from = detail::paintedFace(btn, &known);
    if (ms <= 0 || !known || !btn->isVisible() || support::motionReduced()) {
      if (applyState) applyState();
      detail::settleFace(btn, to);
      return;
    }
    auto applied = std::make_shared<bool>(false);
    auto* anim = new QVariantAnimation(btn);
    anim->setObjectName(QString::fromLatin1(kFaceSwapAnimName));
    anim->setDuration(ms);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    // Linear — the shaping lives in faceSwapFrame's two curves.
    QObject::connect(anim, &QVariantAnimation::valueChanged, btn,
                     [btn, from, to, applyState, applied](const QVariant& v) {
                       const FaceSwapFrame fr = faceSwapFrame(v.toDouble());
                       if (fr.incoming && !*applied) {
                         *applied = true;
                         if (applyState) applyState();
                       }
                       detail::paintFace(btn, fr.incoming ? to : from, fr);
                     });
    QObject::connect(anim, &QVariantAnimation::finished, btn, [btn, to, applyState, applied] {
      if (!*applied) {
        *applied = true;
        if (applyState) applyState();
      }
      detail::settleFace(btn, to);
    });
    detail::paintFace(btn, from, faceSwapFrame(0.0));   // the old face, still at rest
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

}  // namespace stencil::gui
