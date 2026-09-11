#pragma once
// The two switches every animation in the app asks before it plays, and the one place
// they live. Header-only and free of the rest of the motion machinery on purpose: the
// canvas, the dust overlays and the dialog flights all sit in different link targets
// (the headless test binaries each list their own sources), and a gate they all consult
// must not drag a .cpp behind it.
//
// Set from the Visuals & Settings dialog and persisted with the rest of the
// settings (io/fileStore Settings::motionMode / drawingAnimations); MainWindow pushes
// them here in applySettings().
#include <QColor>
#include <QGuiApplication>
#include <QLatin1String>
#include <QString>
#include <QtGlobal>

namespace stencil::support {

  // How the interface moves:
  //   Particles — the default: windows, menus, marks and the canvas form out of dust;
  //   Water     — the same particles as drops: sagging, swaying, shimmering, painted in
  //               the theme's main colour and its shade (dustKit.hpp styleFrame);
  //   Fire      — …as embers: lifting, wavering, flickering, in the same two colours;
  //   Slide     — no particles anywhere; each surface keeps its own ghost/geometry
  //               flight, which is exactly what the cloud normally stands in for;
  //   None      — nothing moves (what STENCIL_NO_ANIM has always meant).
  enum class MotionMode { Particles, Water, Fire, Slide, None };

  // The style the particles wear (browser dustCloud.js PARTICLE_STYLES) — Dust is the
  // flight as tabulated; the other two lay their own touch over it.
  enum class ParticleStyle { Dust, Water, Fire };

  namespace detail {
    // One instance per program (C++17 inline-function statics), read on the GUI thread
    // by every motion helper and written once per settings change.
    inline MotionMode& motionModeState() {
      static MotionMode mode = MotionMode::Particles;
      return mode;
    }
    inline bool& drawingAnimationsState() {
      static bool on = true;
      return on;
    }
    // The two colours a cloud is painted in, pushed here by MainWindow::applyTheme.
    // Violet and its light-mode shade until then, so a cloud is never colourless.
    inline QColor& particleAccentState() {
      static QColor c(0x7c, 0x3a, 0xed);
      return c;
    }
    inline QColor& particleShadeState() {
      static QColor c(0x6b, 0x32, 0xcc);
      return c;
    }
    // …and which theme they came from: two of a cloud's tints flip with it (dustKit.hpp).
    inline bool& particleDarkState() {
      static bool dark = false;
      return dark;
    }
  }  // namespace detail

  inline MotionMode motionMode() { return detail::motionModeState(); }
  inline void setMotionMode(MotionMode mode) { detail::motionModeState() = mode; }

  // "particles" | "water" | "fire" | "slide" | "none" ↔ the enum (browser MOTION_MODES).
  // An unknown key reads as Particles, so another build's settings file is never silent.
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

  // The particle palette: the accent, its shade and the theme, as last set.
  inline void setParticlePalette(const QColor& accent, const QColor& shade, bool dark = false) {
    if (accent.isValid()) detail::particleAccentState() = accent;
    detail::particleShadeState() = shade.isValid() ? shade : detail::particleAccentState();
    detail::particleDarkState() = dark;
  }
  inline QColor particleAccent() { return detail::particleAccentState(); }
  inline QColor particleShade() { return detail::particleShadeState(); }
  inline bool particleDark() { return detail::particleDarkState(); }

  // The canvas stroke motion (a vertex flying to where it was put, its landing pop and
  // ripple — canvas/strokeGrowth.hpp), which the user can turn off on its own.
  inline bool drawingAnimations() { return detail::drawingAnimationsState(); }
  inline void setDrawingAnimations(bool on) { detail::drawingAnimationsState() = on; }

  // Qt has no portable reduce-motion hint, so the mode is ours — and this env var is the
  // opt-out that still overrides it (the browser side uses prefers-reduced-motion).
  inline bool motionReduced() {
    return motionMode() == MotionMode::None
           || !qEnvironmentVariableIsEmpty("STENCIL_NO_ANIM");
  }

  // …and what the canvas asks: the preference, and motion at all.
  inline bool drawingMotionOk() { return drawingAnimations() && !motionReduced(); }

  // The mode allows PARTICLES (dust, water or fire). Checked inside the DisintegrateOverlay
  // factories, so every cloud is behind it whether or not its call site asked.
  inline bool dustAllowed() {
    const MotionMode m = motionMode();
    return m != MotionMode::Slide && m != MotionMode::None && !motionReduced();
  }

  // …and which style those particles wear. Dust for any mode that flies none — callers
  // ask dustAllowed() first, so the answer only matters when it does.
  inline ParticleStyle particleStyle() {
    switch (motionMode()) {
      case MotionMode::Water: return ParticleStyle::Water;
      case MotionMode::Fire: return ParticleStyle::Fire;
      default: return ParticleStyle::Dust;
    }
  }

  // dustAllowed() plus the offscreen platform (no compositor; the gui tests run there) —
  // the one gate every dust flight checks before playing.
  inline bool dustMotionOk() {
    return dustAllowed() && QGuiApplication::platformName() != QLatin1String("offscreen");
  }

}  // namespace stencil::support
