#pragma once
// Form-control value swaps: a checkbox indicator and a combo's text come apart into
// particles and re-form (browser ghostOut/ghostIn). Clouds live in overlays, so no dialog
// reflows mid-effect; installControlSwap() is the one trigger. Q_OBJECT-free, no MOC.
#include "disintegrateOverlay.hpp"
#include "faceSwap.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"

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

  inline constexpr int kCheckSwapMs = 213;   // click feedback: a fifth of a removed row's
  inline constexpr int kCheckSwapCells = 7;  // ~2px cells over a 16px indicator
  inline constexpr double kCheckSwapSpread = 0.30;   // share of a list row's throw
  inline constexpr int kCheckSwapPadPx = 22;
  inline constexpr const char* kCheckSwapObjectName = "stencilCheckSwap";
  inline constexpr const char* kCheckSwapOwnerProperty = "stencilCheckSwapOwner";

  inline constexpr int kValueSwapCellPx = 3;
  inline constexpr double kValueSwapThrowPx = 11.0;   // the field clips; a word must not explode
  inline constexpr double kValueSwapPivot = 0.34;     // share of the exchange the arrival starts at
  inline constexpr double kValueSwapOutShare = 0.7;
  inline constexpr const char* kValueSwapObjectName = "stencilValueSwap";
  // Set while the swap owns the combo's text colour (faceSwap's stylesheet idiom).
  inline constexpr const char* kValueSwapProperty = "stencilValueSwapping";
  inline constexpr const char* kValueSwapTextProperty = "stencilValueSwapText";
  // currentTextChanged has already overwritten the cache by the time textActivated fires.
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

    // The dropped list is a surface (menuReveal.hpp revealPopup); the flight hangs off
    // the container's own Show, the first moment its box is final.
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

  // Q_OBJECT-free, found by object name.
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
    QImage cellsOut_, cellsIn_;
    QImage layerOut_, layerIn_;     // per-frame scratch
    QRect clip_;
    double t_ = 0.0;
    int ms_ = 1;
    bool dark_ = support::particleDark();
    support::MoteSprites sprites_;   // shaped grains only — discs draw direct, antialiased
    // Read at the FIRST FRAME, not build time: the motion-mode combo swaps its face in
    // the same call that applies the new mode.
    bool styled_ = false;
    support::ParticleStyle style_ = support::ParticleStyle::Dust;
    QColor accent_, shade_;
  };

  namespace ctl {

    void onComboText(QComboBox* cb, const QString& to);

    void onComboPick(QComboBox* cb, const QString& to);

  }  // namespace ctl

  // Application-wide; Show/Polish are once-per-control, so it costs nothing at rest.
  class ControlSwapFilter : public QObject {
   public:
    explicit ControlSwapFilter(QObject* parent);

   protected:
    bool eventFilter(QObject* o, QEvent* e) override;
  };

  void installControlSwap();

}  // namespace stencil::gui
