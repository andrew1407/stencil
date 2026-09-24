#pragma once
// The switches every animation asks before it plays. Header-only on purpose: the motion
// helpers sit in different link targets, and a gate they all consult must not drag a .cpp.
#include <QColor>
#include <QGuiApplication>
#include <QLatin1String>
#include <QString>
#include <QtGlobal>
#include <optional>

namespace stencil::support {

  // Particles (default) / Water / Fire / Slide (no particles, each surface's own ghost
  // flight) / None (what STENCIL_NO_ANIM has always meant).
  enum class MotionMode { PARTICLES, WATER, FIRE, SLIDE, NONE };

  // Browser dust/cloud.js PARTICLE_STYLES.
  enum class ParticleStyle { DUST, WATER, FIRE };

  // The three switches together, so a skin can lay its own set over the stored ones.
  struct MotionSwitches {
    MotionMode mode = MotionMode::PARTICLES;
    bool drawing = true;
    bool backdrop = true;   // browser list/prefs.js DEFAULT_MODAL_BACKDROP
  };

  namespace detail {
    // One instance per program (C++17 inline-function statics); GUI thread only.
    inline MotionSwitches& storedState() {
      static MotionSwitches s;
      return s;
    }
    inline std::optional<MotionSwitches>& overrideState() {
      static std::optional<MotionSwitches> o;
      return o;
    }
    inline const MotionSwitches& live() {
      return overrideState() ? *overrideState() : storedState();
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

  inline MotionMode motionMode() { return detail::live().mode; }
  inline void setMotionMode(MotionMode mode) { detail::storedState().mode = mode; }

  // A session layer over the stored switches (support/skinPrefs.hpp), written to no file.
  // Browser twin: motionPrefs.js setMotionOverride.
  inline void setMotionOverride(const MotionSwitches& s) { detail::overrideState() = s; }
  inline void clearMotionOverride() { detail::overrideState().reset(); }
  inline bool motionOverridden() { return detail::overrideState().has_value(); }

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
  inline bool drawingAnimations() { return detail::live().drawing; }
  inline void setDrawingAnimations(bool on) { detail::storedState().drawing = on; }

  // Whether an open window dims and blurs what is behind it (support/ModalBackdrop).
  // Browser twin: list/prefs.js modalBackdrop().
  inline bool modalBackdrop() { return detail::live().backdrop; }
  inline void setModalBackdrop(bool on) { detail::storedState().backdrop = on; }

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
  inline ParticleStyle particleStyleOf(MotionMode mode) {
    switch (mode) {
      case MotionMode::WATER: return ParticleStyle::WATER;
      case MotionMode::FIRE: return ParticleStyle::FIRE;
      default: return ParticleStyle::DUST;
    }
  }
  inline ParticleStyle particleStyle() { return particleStyleOf(motionMode()); }

  // A logo show is ASKED for, so no setting of the user's silences it: it plays whatever the
  // interface is set to, wearing their own style or dust. Only the test switch stills it.
  inline MotionMode storedMotionMode() { return detail::storedState().mode; }
  inline bool showMotionReduced() { return !qEnvironmentVariableIsEmpty("STENCIL_NO_ANIM"); }
  // …but 'slide' and 'none' in force, a session override included, get the mark and its light,
  // never a cloud. Browser twin: motionPrefs.js showDustAllowed().
  inline bool showDustAllowed() {
    const MotionMode m = motionMode();
    return m != MotionMode::SLIDE && m != MotionMode::NONE && !showMotionReduced();
  }
  inline ParticleStyle showParticleStyle() { return particleStyleOf(motionMode()); }

  // isDustAllowed() plus the offscreen platform (no compositor; the gui tests run there).
  inline bool isDustMotionOk() {
    return isDustAllowed() && QGuiApplication::platformName() != QLatin1String("offscreen");
  }

}  // namespace stencil::support
