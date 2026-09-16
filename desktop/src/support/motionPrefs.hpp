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
  enum class MotionMode { PARTICLES, WATER, FIRE, SLIDE, NONE };

  // Browser dustCloud.js PARTICLE_STYLES.
  enum class ParticleStyle { DUST, WATER, FIRE };

  namespace detail {
    // One instance per program (C++17 inline-function statics); GUI thread only.
    inline MotionMode& motionModeState() {
      static MotionMode mode = MotionMode::PARTICLES;
      return mode;
    }
    inline bool& drawingAnimationsState() {
      static bool on = true;
      return on;
    }
    inline bool& modalBackdropState() {
      static bool on = true;   // browser motionPrefs.js DEFAULT_MODAL_BACKDROP
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
    if (key == QLatin1String("water")) return MotionMode::WATER;
    if (key == QLatin1String("fire")) return MotionMode::FIRE;
    if (key == QLatin1String("slide")) return MotionMode::SLIDE;
    if (key == QLatin1String("none")) return MotionMode::NONE;
    return MotionMode::PARTICLES;
  }
  inline QString motionModeKey(MotionMode mode) {
    switch (mode) {
      case MotionMode::WATER: return QStringLiteral("water");
      case MotionMode::FIRE: return QStringLiteral("fire");
      case MotionMode::SLIDE: return QStringLiteral("slide");
      case MotionMode::NONE: return QStringLiteral("none");
      case MotionMode::PARTICLES: break;
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
  inline bool isParticleDark() { return detail::particleDarkState(); }

  // The canvas stroke motion (canvas/strokeGrowth.hpp), switchable on its own.
  inline bool drawingAnimations() { return detail::drawingAnimationsState(); }
  inline void setDrawingAnimations(bool on) { detail::drawingAnimationsState() = on; }

  // Whether an open window dims and blurs what is behind it (support/ModalBackdrop).
  // Browser twin: motionPrefs.js modalBackdrop().
  inline bool modalBackdrop() { return detail::modalBackdropState(); }
  inline void setModalBackdrop(bool on) { detail::modalBackdropState() = on; }

  // Qt has no portable reduce-motion hint; this env var is the opt-out that overrides the mode.
  inline bool motionReduced() {
    return motionMode() == MotionMode::NONE
           || !qEnvironmentVariableIsEmpty("STENCIL_NO_ANIM");
  }

  inline bool isDrawingMotionOk() { return drawingAnimations() && !motionReduced(); }

  // Checked inside the DisintegrateOverlay factories, so every cloud is behind it.
  inline bool isDustAllowed() {
    const MotionMode m = motionMode();
    return m != MotionMode::SLIDE && m != MotionMode::NONE && !motionReduced();
  }

  // Dust for any mode that flies none — callers ask isDustAllowed() first.
  inline ParticleStyle particleStyle() {
    switch (motionMode()) {
      case MotionMode::WATER: return ParticleStyle::WATER;
      case MotionMode::FIRE: return ParticleStyle::FIRE;
      default: return ParticleStyle::DUST;
    }
  }

  // isDustAllowed() plus the offscreen platform (no compositor; the gui tests run there).
  inline bool isDustMotionOk() {
    return isDustAllowed() && QGuiApplication::platformName() != QLatin1String("offscreen");
  }

}  // namespace stencil::support
