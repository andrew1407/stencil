#pragma once
// Form-control state swaps — what a checkbox and a select do when their VALUE changes.
//
// Two motions, one app-wide installer, no new vocabulary:
//
//   * QCheckBox — the checked indicator (accent fill + tick) comes APART into particles
//     when it goes away and FORMS out of them when it arrives. Same engine as a deleted
//     row's scatter (disintegrateOverlay.hpp), shrunk to a 16px box: a fine grid, a
//     short throw and about a quarter of the runtime, so it reads as particles rather
//     than as a handful of slabs sliding off. Sweep::Fall out, Sweep::Gather in — the
//     pair the browser already uses for ghostOut/ghostIn.
//
//   * QComboBox — the outgoing option leaves and the incoming one arrives on the app's
//     existing exchange curve (faceSwap's faceSwapFrame): sequential, with an invisible
//     pivot, so two values are never legible at once. The glyph TURN is read here as the
//     chat card's vertical slide (chatDock kAppearSlidePx) — a word cannot rotate and
//     stay a word — giving an odometer, clipped by the combo's own edit field.
//
// Neither motion moves a box: the checkbox's particles fly in an overlay parented to the
// window, and the combo's word slides inside a child overlay pinned to the edit field.
// No control is ever resized, so no dialog can reflow mid-effect.
//
// The trigger is one application-wide event filter (installControlSwap()), the same
// reason iconMotion.hpp has one: checkboxes and combos are built in a dozen dialogs and
// no call site should have to know. A control opts out with kNoControlSwapProperty.
//
// Header-only and Q_OBJECT-free (no signals/slots of its own), so it needs no MOC.
#include "disintegrateOverlay.hpp"
#include "faceSwap.hpp"      // faceSwapFrame(), kFaceSwapMs, kFaceSwapTurnDeg
#include "modalReveal.hpp"   // support::motionReduced()

#include <QAbstractAnimation>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionComboBox>
#include <QVariant>
#include <QVariantAnimation>
#include <QWidget>

#include <algorithm>

namespace stencil::gui {

  // ── the checkbox's particles ─────────────────────────────────────────────────
  // Click feedback, not a show: a fifth of a removed row's 900ms.
  inline constexpr int kCheckSwapMs = 260;
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

  // ── the combo's value exchange ───────────────────────────────────────────────
  inline constexpr int kValueSwapSlidePx = 6;   // chatDock kAppearSlidePx
  inline constexpr const char* kValueSwapObjectName = "stencilValueSwap";
  // Set while the swap owns the combo's text colour, so the widget stylesheet below can
  // match with the same weight as the app-wide QSS rule (faceSwap's idiom).
  inline constexpr const char* kValueSwapProperty = "stencilValueSwapping";
  inline constexpr const char* kValueSwapTextProperty = "stencilValueSwapText";
  inline constexpr const char* kValueSwapCountProperty = "stencilValueSwapCount";
  inline constexpr const char* kValueSwapSheetProperty = "stencilValueSwapBaseSheet";

  // ── shared ───────────────────────────────────────────────────────────────────
  inline constexpr const char* kNoControlSwapProperty = "stencilNoControlSwap";
  inline constexpr const char* kControlSwapWiredProperty = "stencilControlSwapWired";
  inline constexpr const char* kControlSwapFilterName = "stencilControlSwapFilter";

  namespace ctl {

    // ── checkbox ──────────────────────────────────────────────────────────────
    inline QRect indicatorRect(const QCheckBox* box) {
      QStyleOptionButton opt;
      opt.initFrom(box);
      opt.rect = box->rect();
      return box->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, box);
    }

    // The indicator as the style would paint it in `checked`. RENDERED, not grabbed:
    // photographing both states off the live widget would mean forcing a state onto it
    // mid-signal, and a checkbox in a button group cannot be poked like that safely.
    inline QPixmap indicatorPixmap(QCheckBox* box, const QRect& r, bool checked) {
      const qreal dpr = box->devicePixelRatioF();
      QPixmap pm(QSize(qRound(r.width() * dpr), qRound(r.height() * dpr)));
      pm.setDevicePixelRatio(dpr);
      pm.fill(Qt::transparent);
      QStyleOptionButton opt;
      opt.initFrom(box);
      opt.rect = QRect(QPoint(0, 0), r.size());
      // QStyleSheetStyle reads :checked off the OPTION's state, so both looks come from
      // one live widget. Sunken is dropped — a snapshot must not be caught mid-press.
      opt.state &= ~(QStyle::State_On | QStyle::State_Off | QStyle::State_NoChange
                     | QStyle::State_Sunken);
      opt.state |= checked ? QStyle::State_On : QStyle::State_Off;
      QPainter p(&pm);
      box->style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &opt, &p, box);
      return pm;
    }

    // Drop the scatter `box` has in flight. The real indicator is already in its true
    // state underneath, so cancelling can never strand a stale check.
    inline void cancelCheckSwap(QCheckBox* box) {
      QWidget* host = box->window();
      if (!host) return;
      const auto live = host->findChildren<QWidget*>(QString::fromLatin1(kCheckSwapObjectName));
      for (QWidget* w : live)
        if (w->property(kCheckSwapOwnerProperty).value<QObject*>() == box) delete w;
    }

  }  // namespace ctl

  // Play the indicator's arrival/departure. `checked` is the state the box has JUST
  // reached, so a check GATHERS and an uncheck SCATTERS. Reduced motion, a hidden box
  // and an indicator too small to grid (the f(x,y) pill hides its own) all do nothing —
  // the state itself has already changed, which is the part that must never be dropped.
  inline void swapCheckIndicator(QCheckBox* box, bool checked) {
    if (!box || box->property(kNoControlSwapProperty).toBool()) return;
    ctl::cancelCheckSwap(box);
    if (!box->isVisible() || support::motionReduced()) return;
    QWidget* host = box->window();
    if (!host) return;
    const QRect r = ctl::indicatorRect(box);
    if (r.width() < 6 || r.height() < 6) return;
    const QPixmap on = ctl::indicatorPixmap(box, r, true);
    const QPixmap off = ctl::indicatorPixmap(box, r, false);
    // A control that says nothing with its indicator has nothing to scatter — the f(x,y)
    // pill hides its tick and carries the state in the whole chip's fill.
    if (on.toImage() == off.toImage()) return;
    const QRect at(box->mapTo(host, r.topLeft()), r.size());
    DisintegrateOverlay* fx = DisintegrateOverlay::overPixmaps(
        on, off, at, host,
        checked ? DisintegrateOverlay::Sweep::Gather : DisintegrateOverlay::Sweep::Fall,
        kCheckSwapCells, kCheckSwapCells, kCheckSwapMs, kCheckSwapSpread, kCheckSwapPadPx,
        QString::fromLatin1(kCheckSwapObjectName));
    if (fx) fx->setProperty(kCheckSwapOwnerProperty, QVariant::fromValue<QObject*>(box));
  }

  namespace ctl {

    // ── combo ─────────────────────────────────────────────────────────────────
    inline QStyleOptionComboBox comboOption(const QComboBox* cb, const QString& text) {
      QStyleOptionComboBox o;
      o.initFrom(cb);
      o.rect = cb->rect();
      o.subControls = QStyle::SC_All;
      o.editable = cb->isEditable();
      o.frame = cb->hasFrame();
      o.iconSize = cb->iconSize();
      o.currentText = text;
      return o;
    }

    // Where the chosen option is drawn, in the combo's own coordinates.
    inline QRect comboFieldRect(const QComboBox* cb) {
      QStyleOptionComboBox o = comboOption(cb, cb->currentText());
      const QRect r =
          cb->style()->subControlRect(QStyle::CC_ComboBox, &o, QStyle::SC_ComboBoxEditField, cb);
      return r.isValid() && !r.isEmpty() ? r : cb->rect();
    }

    // The combo's LABEL alone, drawn by the style into a transparent, control-sized
    // pixmap — so the word is the one the combo would paint, in the colour the theme
    // gives it, with no palette guessing. Must run BEFORE the label is hidden: the same
    // widget stylesheet that blanks the real text would blank this too.
    inline QPixmap comboLabelPixmap(QComboBox* cb, const QString& text) {
      const qreal dpr = cb->devicePixelRatioF();
      QPixmap pm(QSize(qRound(cb->width() * dpr), qRound(cb->height() * dpr)));
      pm.setDevicePixelRatio(dpr);
      pm.fill(Qt::transparent);
      QStyleOptionComboBox o = comboOption(cb, text);
      QPainter p(&pm);
      cb->style()->drawControl(QStyle::CE_ComboBoxLabel, &o, &p, cb);
      return pm;
    }

    inline void repolish(QWidget* w) {
      w->style()->unpolish(w);
      w->style()->polish(w);
      w->update();
    }

    // Blank the REAL label for the duration, so only the overlay's two words are ever on
    // screen. A widget stylesheet is the only lever that beats the app-wide
    // `QComboBox { color: … }`; the property selector gives it that rule's weight, and
    // every state is listed so a hover mid-swap can't outrank it. Colour only — nothing
    // in the box model is touched, so the control cannot change size.
    inline void hideComboLabel(QComboBox* cb, bool hide) {
      if (hide == cb->property(kValueSwapProperty).toBool()) return;
      if (hide) {
        cb->setProperty(kValueSwapSheetProperty, cb->styleSheet());
        cb->setProperty(kValueSwapProperty, true);
        const QString p = QString::fromLatin1(kValueSwapProperty);
        cb->setStyleSheet(cb->property(kValueSwapSheetProperty).toString()
                          + QStringLiteral("QComboBox[%1=\"true\"],QComboBox[%1=\"true\"]:hover,"
                                           "QComboBox[%1=\"true\"]:focus,"
                                           "QComboBox[%1=\"true\"]:on"
                                           "{color:rgba(0,0,0,0);}")
                                .arg(p));
      } else {
        cb->setProperty(kValueSwapProperty, false);
        cb->setStyleSheet(cb->property(kValueSwapSheetProperty).toString());
      }
      // Qt matches property selectors at POLISH time, so both edges have to re-polish or
      // the rule is skipped entirely and the old word stays up under the swap.
      repolish(cb);
    }

    inline void rememberComboValue(QComboBox* cb) {
      cb->setProperty(kValueSwapTextProperty, cb->currentText());
      cb->setProperty(kValueSwapCountProperty, cb->count());
    }

  }  // namespace ctl

  // The odometer itself: a mouse-transparent child pinned over the combo, painting the
  // outgoing word out and the incoming one in. Q_OBJECT-free, found by object name.
  class ValueSwapOverlay : public QWidget {
   public:
    // Swap `cb`'s displayed value from `from` to `to`. A superseding change deletes the
    // one in flight and starts over, so a burst of picks always ends on the last value.
    static void play(QComboBox* cb, const QString& from, const QString& to,
                     int ms = kFaceSwapMs) {
      if (!cb || ms <= 0) return;
      cancel(cb);
      // Rendered first — hideComboLabel would blank these too.
      const QPixmap out = ctl::comboLabelPixmap(cb, from);
      const QPixmap in = ctl::comboLabelPixmap(cb, to);
      ctl::hideComboLabel(cb, true);
      auto* fx = new ValueSwapOverlay(cb, out, in, ctl::comboFieldRect(cb));
      fx->setGeometry(cb->rect());
      fx->show();
      fx->raise();
      auto* anim = new QVariantAnimation(fx);
      anim->setDuration(ms);
      anim->setStartValue(0.0);
      anim->setEndValue(1.0);
      // Linear — the shaping lives in faceSwapFrame's two curves.
      connect(anim, &QVariantAnimation::valueChanged, fx, [fx](const QVariant& v) {
        fx->t_ = v.toDouble();
        fx->update();
      });
      connect(anim, &QVariantAnimation::finished, fx, [fx] {
        if (auto* owner = qobject_cast<QComboBox*>(fx->parentWidget()))
          ctl::hideComboLabel(owner, false);   // the combo paints its own word again
        fx->deleteLater();
      });
      anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    // Drop any swap `cb` has in flight and hand it its own label back.
    static void cancel(QComboBox* cb) {
      const auto live =
          cb->findChildren<QWidget*>(QString::fromLatin1(kValueSwapObjectName),
                                     Qt::FindDirectChildrenOnly);
      for (QWidget* w : live) delete w;   // a deleted animation never emits finished()
      ctl::hideComboLabel(cb, false);
    }

    // True while `cb` is mid-exchange.
    static bool running(const QComboBox* cb) {
      return cb
             && cb->findChild<QWidget*>(QString::fromLatin1(kValueSwapObjectName),
                                        Qt::FindDirectChildrenOnly) != nullptr;
    }

   protected:
    void paintEvent(QPaintEvent*) override {
      const FaceSwapFrame fr = faceSwapFrame(t_);
      const QPixmap& pm = fr.incoming ? in_ : out_;
      if (pm.isNull()) return;
      QPainter p(this);
      p.setRenderHint(QPainter::SmoothPixmapTransform, true);
      p.setClipRect(clip_);   // clipped by the edit field, the way an odometer is
      p.setOpacity(std::clamp(fr.alpha, 0.0, 1.0));
      // faceSwap's quarter-turn, read as the chat card's rise: the outgoing word lifts
      // away and the incoming one comes up from below. The turn's sign already reverses
      // across the pivot, so the two directions fall out of the same curve.
      p.drawPixmap(QPointF(0.0, -fr.deg / kFaceSwapTurnDeg * kValueSwapSlidePx), pm);
    }

   private:
    ValueSwapOverlay(QComboBox* cb, const QPixmap& out, const QPixmap& in, const QRect& clip)
        : QWidget(cb), out_(out), in_(in), clip_(clip) {
      setObjectName(QString::fromLatin1(kValueSwapObjectName));
      setAttribute(Qt::WA_TransparentForMouseEvents, true);
      setAttribute(Qt::WA_NoSystemBackground, true);
      setAttribute(Qt::WA_TranslucentBackground, true);
      hide();
    }

    QPixmap out_, in_;
    QRect clip_;
    double t_ = 0.0;
  };

  namespace ctl {

    // A combo's text changed. Only a PICK animates: a repopulation (the item count moved)
    // and the first value a combo ever shows go straight up, or every dialog would deal
    // itself in on open.
    inline void onComboText(QComboBox* cb, const QString& to) {
      const QVariant prev = cb->property(kValueSwapTextProperty);
      const int prevCount = cb->property(kValueSwapCountProperty).toInt();
      const QString from = prev.toString();
      rememberComboValue(cb);   // the cache is the TRUE current value from here on
      if (cb->property(kNoControlSwapProperty).toBool()) return;
      // An EDITABLE combo (the zoom box, the LLM model box) has no chosen option to
      // exchange: its value is a QLineEdit the user is typing into, and blanking that
      // text for 240ms per keystroke would be sabotage, not motion.
      if (cb->isEditable()) return;
      if (support::motionReduced() || !cb->isVisible()) {
        ValueSwapOverlay::cancel(cb);
        return;
      }
      if (!prev.isValid() || from.isEmpty() || to.isEmpty() || from == to) return;
      if (prevCount != cb->count()) return;
      ValueSwapOverlay::play(cb, from, to);
    }

  }  // namespace ctl

  // The one application-wide watcher. Show/Polish are once-per-control events, so this
  // costs nothing at rest and needs no per-dialog installation — which is what lets a
  // checkbox or combo built anywhere in the app get the motion for free.
  class ControlSwapFilter : public QObject {
   public:
    explicit ControlSwapFilter(QObject* parent) : QObject(parent) {
      setObjectName(QString::fromLatin1(kControlSwapFilterName));
    }

   protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      const QEvent::Type type = e->type();
      if (type != QEvent::Show && type != QEvent::Polish)
        return QObject::eventFilter(o, e);
      if (o->property(kControlSwapWiredProperty).toBool()) return QObject::eventFilter(o, e);
      if (auto* box = qobject_cast<QCheckBox*>(o)) {
        box->setProperty(kControlSwapWiredProperty, true);
        connect(box, &QCheckBox::toggled, box,
                [box](bool on) { swapCheckIndicator(box, on); });
      } else if (auto* cb = qobject_cast<QComboBox*>(o)) {
        cb->setProperty(kControlSwapWiredProperty, true);
        ctl::rememberComboValue(cb);
        connect(cb, &QComboBox::currentTextChanged, cb,
                [cb](const QString& to) { ctl::onComboText(cb, to); });
      }
      return QObject::eventFilter(o, e);
    }
  };

  // Install the watcher on the application. Idempotent — every MainWindow calls it, and
  // only the first one takes.
  inline void installControlSwap() {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app
        || app->findChild<QObject*>(QString::fromLatin1(kControlSwapFilterName),
                                    Qt::FindDirectChildrenOnly))
      return;
    app->installEventFilter(new ControlSwapFilter(app));
  }

}  // namespace stencil::gui
