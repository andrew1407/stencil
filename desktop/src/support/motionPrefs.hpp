#pragma once
// ── Motion preferences (browser twin: browser/js/ui/motionPrefs.js) ─────────
// The two switches every animation in the app asks before it plays, and the one place
// they live. Header-only and free of the rest of the motion machinery on purpose: the
// canvas, the dust overlays and the dialog flights all sit in different link targets
// (the headless test binaries each list their own sources), and a gate they all consult
// must not drag a .cpp behind it.
//
// Set from the Visuals & Settings dialog and persisted with the rest of the
// settings (io/fileStore Settings::motionMode / drawingAnimations); MainWindow pushes
// them here in applySettings().
#include <QGuiApplication>
#include <QLatin1String>
#include <QString>
#include <QtGlobal>

namespace stencil::support {

  // How the interface moves:
  //   Particles — the default: windows, menus, marks and the canvas form out of dust;
  //   Slide     — no dust anywhere; each surface keeps its own ghost/geometry flight,
  //               which is exactly what the cloud normally stands in for;
  //   None      — nothing moves (what STENCIL_NO_ANIM has always meant).
  enum class MotionMode { Particles, Slide, None };

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
  }  // namespace detail

  inline MotionMode motionMode() { return detail::motionModeState(); }
  inline void setMotionMode(MotionMode mode) { detail::motionModeState() = mode; }

  // "particles" | "slide" | "none" ↔ the enum. An unknown key reads as Particles, so a
  // settings file from another build can never leave the app silent.
  inline MotionMode motionModeFromKey(const QString& key) {
    if (key == QLatin1String("slide")) return MotionMode::Slide;
    if (key == QLatin1String("none")) return MotionMode::None;
    return MotionMode::Particles;
  }
  inline QString motionModeKey(MotionMode mode) {
    switch (mode) {
      case MotionMode::Slide: return QStringLiteral("slide");
      case MotionMode::None: return QStringLiteral("none");
      case MotionMode::Particles: break;
    }
    return QStringLiteral("particles");
  }

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

  // The mode allows PARTICLES. Checked inside the DisintegrateOverlay factories, so
  // every cloud in the app is behind it whether or not its call site remembered to ask.
  inline bool dustAllowed() { return motionMode() == MotionMode::Particles && !motionReduced(); }

  // dustAllowed() plus the offscreen platform (no compositor; the gui tests run there) —
  // the one gate every dust flight checks before playing.
  inline bool dustMotionOk() {
    return dustAllowed() && QGuiApplication::platformName() != QLatin1String("offscreen");
  }

}  // namespace stencil::support
