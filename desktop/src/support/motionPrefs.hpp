#pragma once
// The switches every animation asks before it plays. Header-only on purpose: the motion
// helpers sit in different link targets, and a gate they all consult must not drag a .cpp.
#include <QColor>
#include <QGuiApplication>
#include <QLatin1String>
#include <QString>
#include <QtGlobal>

namespace stencil::support {

  // Particles (default) / Water / Fire / Slide (no particles, each surface's own ghost
  // flight) / None (what STENCIL_NO_ANIM has always meant).
  enum class MotionMode { Particles, Water, Fire, Slide, None };

  // Browser dustCloud.js PARTICLE_STYLES.
  enum class ParticleStyle { Dust, Water, Fire };

  namespace detail {
    // One instance per program (C++17 inline-function statics); GUI thread only.
    inline MotionMode& motionModeState() {
      static MotionMode mode = MotionMode::Particles;
      return mode;
    }
    inline bool& drawingAnimationsState() {
      static bool on = true;
      return on;
    }
    // Pushed by MainWindow::applyTheme; violet and its shade until then.
    inline QColor& particleAccentState() {
      static QColor c(0x7c, 0x3a, 0xed);
      return c;
    }
    inline QColor& particleShadeState() {
      static QColor c(0x6b, 0x32, 0xcc);
      return c;
    }
    // Two of a cloud's tints flip with the theme (dustKit.hpp).
    inline bool& particleDarkState() {
      static bool dark = false;
      return dark;
    }
  }  // namespace detail

  inline MotionMode motionMode() { return detail::motionModeState(); }
  inline void setMotionMode(MotionMode mode) { detail::motionModeState() = mode; }

  // Browser MOTION_MODES. An unknown key reads as Particles, so another build's file is never silent.
  inline MotionMode motionModeFromKey(const QString& key) {
    if (key == QLatin1String("water")) return MotionMode::Water;
    if (key == QLatin1String("fire")) return MotionMode::Fire;
    if (key == QLatin1String("slide")) return MotionMode::Slide;
    if (key == QLatin1String("none")) return MotionMode::None;
    return MotionMode::Particles;
  }
  inline QString motionModeKey(MotionMode mode) {
    switch (mode) {
      case MotionMode::Water: return QStringLiteral("water");
      case MotionMode::Fire: return QStringLiteral("fire");
      case MotionMode::Slide: return QStringLiteral("slide");
      case MotionMode::None: return QStringLiteral("none");
      case MotionMode::Particles: break;
    }
    return QStringLiteral("particles");
  }

  inline void setParticlePalette(const QColor& accent, const QColor& shade, bool dark = false) {
    if (accent.isValid()) detail::particleAccentState() = accent;
    detail::particleShadeState() = shade.isValid() ? shade : detail::particleAccentState();
    detail::particleDarkState() = dark;
  }
  inline QColor particleAccent() { return detail::particleAccentState(); }
  inline QColor particleShade() { return detail::particleShadeState(); }
  inline bool particleDark() { return detail::particleDarkState(); }

  // The canvas stroke motion (canvas/strokeGrowth.hpp), switchable on its own.
  inline bool drawingAnimations() { return detail::drawingAnimationsState(); }
  inline void setDrawingAnimations(bool on) { detail::drawingAnimationsState() = on; }

  // Qt has no portable reduce-motion hint; this env var is the opt-out that overrides the mode.
  inline bool motionReduced() {
    return motionMode() == MotionMode::None
           || !qEnvironmentVariableIsEmpty("STENCIL_NO_ANIM");
  }

  inline bool drawingMotionOk() { return drawingAnimations() && !motionReduced(); }

  // Checked inside the DisintegrateOverlay factories, so every cloud is behind it.
  inline bool dustAllowed() {
    const MotionMode m = motionMode();
    return m != MotionMode::Slide && m != MotionMode::None && !motionReduced();
  }

  // Dust for any mode that flies none — callers ask dustAllowed() first.
  inline ParticleStyle particleStyle() {
    switch (motionMode()) {
      case MotionMode::Water: return ParticleStyle::Water;
      case MotionMode::Fire: return ParticleStyle::Fire;
      default: return ParticleStyle::Dust;
    }
  }

  // dustAllowed() plus the offscreen platform (no compositor; the gui tests run there).
  inline bool dustMotionOk() {
    return dustAllowed() && QGuiApplication::platformName() != QLatin1String("offscreen");
  }

}  // namespace stencil::support
