#pragma once
// Form-control state swaps — what a checkbox and a select do when their VALUE changes.
//
//   * QCheckBox — the checked indicator comes APART into particles and FORMS back out of
//     them: disintegrateOverlay's row scatter shrunk to a 16px box, Sweep::Fall out /
//     Sweep::Gather in, the pair the browser's ghostOut/ghostIn already uses.
//   * QComboBox — the outgoing option comes apart and the incoming one forms in place, in
//     SEQUENCE so two values are never legible at once. Clipped by the combo's edit field.
//   * The LIST a combo drops is a surface like every other popup (menuReveal.hpp
//     revealPopup — the same flight context menus and dialogs play).
//
// No motion moves or resizes a box: the clouds live in overlays, so no dialog can reflow
// mid-effect. One application-wide event filter (installControlSwap()) is the trigger, as
// in iconMotion.hpp — no call site should have to know. Opt out with kNoControlSwapProperty.
//
// Header-only and Q_OBJECT-free (no signals/slots of its own), so it needs no MOC.
#include "disintegrateOverlay.hpp"
#include "faceSwap.hpp"      // kFaceSwapMs — the exchange's clock
#include "menuReveal.hpp"    // support::revealPopup() — the dropped list is a surface
#include "modalReveal.hpp"   // support::motionReduced()

#include <QAbstractAnimation>
#include <QCheckBox>
#include <QAbstractItemView>
#include <QEasingCurve>
#include <QComboBox>
#include <QPointer>
#include <QCoreApplication>
#include <QEvent>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QString>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionComboBox>
#include <QVariant>
#include <QVariantAnimation>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <vector>

namespace stencil::gui {

  // Click feedback, not a show: a fifth of a removed row's 900ms.
  inline constexpr int kCheckSwapMs = 213;
  // Cells per side over a 16px indicator — ~2px each, which still reads as grit at
  // dpr 1. The row default (22x11) over a box this small gives four slabs, not dust.
  inline constexpr int kCheckSwapCells = 7;
  // The throw, as a share of a list row's: a 16px control scattering 80px would look
  // like the dialog exploded.
  inline constexpr double kCheckSwapSpread = 0.30;
  // Room around the indicator for the motes to fly into — the far tail is already
  // transparent by the time it reaches this, so nothing visible is ever cut off.
  inline constexpr int kCheckSwapPadPx = 22;
  inline constexpr const char* kCheckSwapObjectName = "stencilCheckSwap";
  inline constexpr const char* kCheckSwapOwnerProperty = "stencilCheckSwapOwner";

  // Motes about this big on screen — a word is small, and a word's grain has to be
  // smaller still or the exchange reads as two halves sliding.
  inline constexpr int kValueSwapCellPx = 3;
  // How far a mote may travel. Small on purpose: the field clips it, and a word that
  // exploded would read as an error rather than as a value changing.
  inline constexpr double kValueSwapThrowPx = 11.0;
  // Where the incoming word starts arriving, as a share of the exchange. The outgoing
  // one is most of the way out by then — the invisible pivot the odometer had.
  inline constexpr double kValueSwapPivot = 0.34;
  // …and how much of it the outgoing word gets. Ending before the exchange does leaves
  // the last beat to the arrival alone, which is the half you actually read.
  inline constexpr double kValueSwapOutShare = 0.7;
  inline constexpr const char* kValueSwapObjectName = "stencilValueSwap";
  // Set while the swap owns the combo's text colour, so the widget stylesheet below can
  // match with the same weight as the app-wide QSS rule (faceSwap's idiom).
  inline constexpr const char* kValueSwapProperty = "stencilValueSwapping";
  inline constexpr const char* kValueSwapTextProperty = "stencilValueSwapText";
  // The value BEFORE the current one — what an editable combo's pick animates from
  // (currentTextChanged has already overwritten the cache by the time textActivated fires).
  inline constexpr const char* kValueSwapPrevProperty = "stencilValueSwapPrev";
  inline constexpr const char* kValueSwapCountProperty = "stencilValueSwapCount";
  inline constexpr const char* kValueSwapSheetProperty = "stencilValueSwapBaseSheet";

  inline constexpr const char* kNoControlSwapProperty = "stencilNoControlSwap";
  inline constexpr const char* kControlSwapWiredProperty = "stencilControlSwapWired";
  inline constexpr const char* kControlSwapFilterName = "stencilControlSwapFilter";

  namespace ctl {

    QRect indicatorRect(const QCheckBox* box);

    QPixmap indicatorPixmap(QCheckBox* box, const QRect& r, bool checked);

    void cancelCheckSwap(QCheckBox* box);

  }  // namespace ctl

  void swapCheckIndicator(QCheckBox* box, bool checked);

  namespace ctl {

    QStyleOptionComboBox comboOption(const QComboBox* cb, const QString& text);

    QRect comboFieldRect(const QComboBox* cb);

    QPixmap comboLabelPixmap(QComboBox* cb, const QString& text);

    void repolish(QWidget* w);

    void hideComboLabel(QComboBox* cb, bool hide);

    // It is a surface like every other popup in the app, so it forms out of motes
    // streaming from the control that owns it (support/menuReveal.hpp revealPopup — the
    // same flight the context menus and the dialogs play). A QComboBox places and shows
    // its own container, so there is nothing to call at the call site: the flight hangs
    // off the container's own Show, which is the first moment its box is final.
    inline constexpr const char* kComboPopupFilterName = "stencilComboPopupDust";
    class ComboPopupDust : public QObject {
     public:
      explicit ComboPopupDust(QComboBox* cb);

     protected:
      bool eventFilter(QObject* o, QEvent* e) override;

     private:
      QPointer<QComboBox> cb_;
    };

    void wireComboPopupDust(QComboBox* cb);

    void rememberComboValue(QComboBox* cb);

  }  // namespace ctl

  // The odometer itself: a mouse-transparent child pinned over the combo, painting the
  // outgoing word out and the incoming one in. Q_OBJECT-free, found by object name.
  class ValueSwapOverlay : public QWidget {
   public:
    static void play(QComboBox* cb, const QString& from, const QString& to,
                     int ms = kFaceSwapMs);

    static void cancel(QComboBox* cb);

    static bool running(const QComboBox* cb);

   protected:
    void paintEvent(QPaintEvent*) override;

    void renderCloud(QImage* layer, const QPixmap& pm, QImage* cells, double t, bool gather);

   private:
    ValueSwapOverlay(QComboBox* cb, const QPixmap& out, const QPixmap& in, const QRect& clip);

    QPixmap out_, in_;
    QImage cellsOut_, cellsIn_;     // each word's colour per grid cell, sampled once
    QImage layerOut_, layerIn_;     // per-frame scratch: each cloud composed on its own
    QRect clip_;
    double t_ = 0.0;
    int ms_ = 1;   // the exchange's length, for a styled word's clock
    bool dark_ = support::particleDark();   // …and the theme, which two tints follow
    support::MoteSprites sprites_;   // shaped grains only — the word's discs draw direct, antialiased
    // The style and palette the exchange plays in, read at its FIRST FRAME rather than at
    // build time: a combo that changes the motion mode swaps its face in the same call
    // that applies it, and read early the swap played in the old mode.
    bool styled_ = false;
    support::ParticleStyle style_ = support::ParticleStyle::Dust;
    QColor accent_, shade_;
  };

  namespace ctl {

    void onComboText(QComboBox* cb, const QString& to);

    void onComboPick(QComboBox* cb, const QString& to);

  }  // namespace ctl

  // The one application-wide watcher. Show/Polish are once-per-control events, so this
  // costs nothing at rest and needs no per-dialog installation — which is what lets a
  // checkbox or combo built anywhere in the app get the motion for free.
  class ControlSwapFilter : public QObject {
   public:
    explicit ControlSwapFilter(QObject* parent);

   protected:
    bool eventFilter(QObject* o, QEvent* e) override;
  };

  void installControlSwap();

}  // namespace stencil::gui
