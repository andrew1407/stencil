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
//   * QComboBox — the outgoing option comes APART into particles and the incoming one
//     FORMS out of them, in place: the same sand, on the same sequential timing the old
//     odometer had (the outgoing word is well on its way out before the incoming one
//     starts arriving), so two values are never legible at once. Clipped by the combo's
//     own edit field, so a mote can no more leave the field than the word could.
//
//   * The LIST a combo drops is a surface like every other popup: it forms out of motes
//     streaming from the combo and comes apart into them (support/menuReveal.hpp
//     revealPopup — the same flight the context menus and dialogs play).
//
// None of these motions moves a box: the checkbox's particles fly in an overlay parented
// to the window, and the combo's two clouds inside a child overlay pinned to it. No
// control is ever resized, so no dialog can reflow mid-effect.
//
// The trigger is one application-wide event filter (installControlSwap()), the same
// reason iconMotion.hpp has one: checkboxes and combos are built in a dozen dialogs and
// no call site should have to know. A control opts out with kNoControlSwapProperty.
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
      QPainter p(&pm);
      if (cb->isEditable()) {
        // An editable combo's word lives in its QLineEdit — CE_ComboBoxLabel paints
        // nothing for it, so draw the value by hand in the field's own font/colour.
        p.setFont(cb->font());
        p.setPen(cb->palette().color(QPalette::Text));
        p.drawText(comboFieldRect(cb).adjusted(3, 0, -2, 0),
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
        return pm;
      }
      QStyleOptionComboBox o = comboOption(cb, text);
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
                                           "QComboBox[%1=\"true\"]:on,"
                                           "QComboBox[%1=\"true\"] QLineEdit"
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

    // ── the list a combo drops ────────────────────────────────────────────────
    // It is a surface like every other popup in the app, so it forms out of motes
    // streaming from the control that owns it (support/menuReveal.hpp revealPopup — the
    // same flight the context menus and the dialogs play). A QComboBox places and shows
    // its own container, so there is nothing to call at the call site: the flight hangs
    // off the container's own Show, which is the first moment its box is final.
    inline constexpr const char* kComboPopupFilterName = "stencilComboPopupDust";
    class ComboPopupDust : public QObject {
     public:
      explicit ComboPopupDust(QComboBox* cb) : QObject(cb), cb_(cb) {
        setObjectName(QString::fromLatin1(kComboPopupFilterName));
      }

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (!cb_) return QObject::eventFilter(o, e);
        auto* popup = qobject_cast<QWidget*>(o);
        if (!popup) return QObject::eventFilter(o, e);
        if (e->type() == QEvent::Show) {
          support::revealPopup(*popup, cb_);
        } else if (e->type() == QEvent::Hide) {
          // Same reasoning as the Show branch: Qt hides its own container, so the
          // close has to hang off that Hide too, or it never played an exit flight.
          support::dismissPopup(*popup, cb_);
        }
        return QObject::eventFilter(o, e);
      }

     private:
      QPointer<QComboBox> cb_;
    };

    // Arm it once per combo. view()->window() is the container Qt drops; asking for the
    // view is what creates it, which is exactly why this can be done at wire time.
    inline void wireComboPopupDust(QComboBox* cb) {
      QAbstractItemView* view = cb->view();
      QWidget* popup = view ? view->window() : nullptr;
      if (!popup || popup == cb->window()) return;
      // The watcher is parented to the COMBO (the popup container is Qt's, and outlives
      // nothing of ours), so that is where the once-only guard looks.
      if (cb->findChild<QObject*>(QString::fromLatin1(kComboPopupFilterName),
                                  Qt::FindDirectChildrenOnly))
        return;
      popup->installEventFilter(new ComboPopupDust(cb));
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
      QPainter p(this);
      p.setClipRect(clip_);   // clipped by the edit field, the way the word itself is
      // Sequential, like the odometer this replaces: the outgoing word is most of the way
      // out before the incoming one starts arriving, so two values are never legible at
      // once — only, now, both are sand. Each cloud is composed on its own layer first:
      // a word is the picture wherever its cells are at home and cut away wherever they
      // are not, and the second word's cut-outs must not take the first word's flying
      // grains with them.
      renderCloud(&layerOut_, out_, &cellsOut_,
                  std::clamp(t_ / kValueSwapOutShare, 0.0, 1.0), false);
      renderCloud(&layerIn_, in_, &cellsIn_,
                  std::clamp((t_ - kValueSwapPivot) / (1.0 - kValueSwapPivot), 0.0, 1.0), true);
      p.drawImage(rect(), layerOut_);
      p.drawImage(rect(), layerIn_);
    }

    // One cloud of a WORD onto `layer`: the label picture, minus the cells that have
    // left it, plus those cells as round grains of their own ink — the twin of
    // DisintegrateOverlay's Fall/Gather at word scale, on the same hashes and the same
    // bend, so the app's sand all behaves alike. `t` is this cloud's own progress;
    // `gather` reads the same journey backwards.
    void renderCloud(QImage* layer, const QPixmap& pm, QImage* cells, double t, bool gather) {
      const qreal dpr = devicePixelRatioF();
      const QSize px(std::max(1, qRound(width() * dpr)), std::max(1, qRound(height() * dpr)));
      if (layer->size() != px) {
        *layer = QImage(px, QImage::Format_ARGB32_Premultiplied);
        layer->setDevicePixelRatio(dpr);
      }
      layer->fill(Qt::transparent);
      if (pm.isNull() || (gather ? t <= 0.0 : t >= 1.0)) return;
      const QRectF box(clip_);
      if (box.width() < 2 || box.height() < 2) return;
      const int cols = std::max(1, qRound(box.width() / kValueSwapCellPx));
      const int rows = std::max(1, qRound(box.height() / kValueSwapCellPx));
      const double cw = box.width() / cols;
      const double ch = box.height() / rows;
      if (cells->width() != cols || cells->height() != rows) {
        // The word's colour per cell, once: the field's slice of the label sheet (which
        // covers the whole control, device-pixel scaled), area-averaged down to the grid.
        const double sx = double(pm.width()) / std::max(1, width());
        const double sy = double(pm.height()) / std::max(1, height());
        const QRect src(qRound(box.x() * sx), qRound(box.y() * sy),
                        std::max(1, qRound(box.width() * sx)), std::max(1, qRound(box.height() * sy)));
        *cells = DisintegrateOverlay::sampleCells(pm.copy(src), cols, rows);
      }
      QPainter p(layer);
      p.setRenderHint(QPainter::SmoothPixmapTransform, true);
      static const QEasingCurve kOut(QEasingCurve::OutQuint);
      struct Grain { QPointF at; double r; QColor c; };
      std::vector<Grain> grains;
      std::vector<QRect> cut;
      for (int cy = 0; cy < rows; ++cy) {
        const int y0 = qRound(box.y() + cy * ch);
        const int y1 = cy == rows - 1 ? int(std::ceil(box.bottom())) : qRound(box.y() + (cy + 1) * ch);
        int runStart = -1;
        for (int cx = 0; cx <= cols; ++cx) {
          bool away = false;
          if (cx < cols) {
            const double n = DisintegrateOverlay::cellNoise(cx, cy);
            const double m = DisintegrateOverlay::cellNoise(cx + 41, cy + 17);
            const double q = DisintegrateOverlay::cellNoise(cx + 97, cy + 53);
            // A word is READ left to right, so it comes apart that way — and gathers
            // back the same sweep reversed, the rule every other flight here follows.
            const double along = cols > 1 ? double(cx) / (cols - 1) : 0.0;
            const double delay = (gather ? 1.0 - along : along) * 0.4 + n * 0.1;
            double k = (t - delay) / std::max(0.05, 1.0 - delay);
            // At home — not yet left, or already landed — the cell is the word itself.
            away = gather ? k < 1.0 : k > 0.0;
            if (away) {
              k = std::clamp(k, 0.0, 1.0);
              const double e = kOut.valueForProgress(k);
              const double far = gather ? 1.0 - e : e;
              QColor c = DisintegrateOverlay::cellColour(*cells, cx, cy);
              const double alpha = c.alphaF() * (0.78 + n * 0.22)
                  * (gather ? DisintegrateOverlay::gatherAlpha(k) : DisintegrateOverlay::scatterAlpha(k));
              if (far < 1.0 && alpha > 0.02) {
                const QPointF home(box.x() + (cx + 0.5) * cw, box.y() + (cy + 0.5) * ch);
                const double tx = (m - 0.5) * kValueSwapThrowPx;
                const double ty = (0.3 + n * 0.7) * kValueSwapThrowPx;
                c.setAlphaF(std::min(1.0, alpha));
                grains.push_back({home + QPointF(far * tx, far * ty)
                                      + DisintegrateOverlay::swirlAt(far, tx, ty, q),
                                  DisintegrateOverlay::moteRadius(cw, ch, n) * (1.0 - far * (0.6 - n * 0.25)),
                                  c});
              }
            }
          }
          if (away) {
            if (runStart < 0) runStart = cx;
          } else if (runStart >= 0) {
            const int x1 = cx == cols ? int(std::ceil(box.right())) : qRound(box.x() + cx * cw);
            cut.push_back(QRect(QPoint(qRound(box.x() + runStart * cw), y0), QPoint(x1 - 1, y1 - 1)));
            runStart = -1;
          }
        }
      }
      // The word, minus the cells that have left it — a clip on the blit, never a clear
      // (disintegrateOverlay.hpp paintEvent explains why).
      QRegion keep(rect());
      if (!cut.empty()) {
        QRegion gone;
        gone.setRects(cut.data(), int(cut.size()));
        keep -= gone;
      }
      if (!keep.isEmpty()) {
        p.save();
        p.setClipRegion(keep);
        p.drawPixmap(0, 0, pm);
        p.restore();
      }
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setPen(Qt::NoPen);
      for (const Grain& g : grains) {
        p.setBrush(g.c);
        p.drawEllipse(g.at, g.r, g.r);
      }
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
    QImage cellsOut_, cellsIn_;     // each word's colour per grid cell, sampled once
    QImage layerOut_, layerIn_;     // per-frame scratch: each cloud composed on its own
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
      // An EDITABLE combo (the zoom box, the LLM model box) must not animate per
      // KEYSTROKE — but a PICK from its list is a value exchange like any other, so
      // the outgoing value is stashed for onComboPick (textActivated fires after
      // currentTextChanged has already overwritten the cache).
      if (cb->isEditable()) {
        cb->setProperty(kValueSwapPrevProperty, from);
        return;
      }
      if (support::motionReduced() || !cb->isVisible()) {
        ValueSwapOverlay::cancel(cb);
        return;
      }
      if (!prev.isValid() || from.isEmpty() || to.isEmpty() || from == to) return;
      if (prevCount != cb->count()) return;
      ValueSwapOverlay::play(cb, from, to);
    }

    // A PICK from an editable combo's dropped list (the zoom presets, the model box):
    // the one moment such a combo exchanges values rather than being typed into —
    // browser parity: the zoom preset pick plays markSwap on its input.
    inline void onComboPick(QComboBox* cb, const QString& to) {
      if (!cb->isEditable()) return;   // non-editables animate via currentTextChanged
      if (cb->property(kNoControlSwapProperty).toBool()) return;
      if (support::motionReduced() || !cb->isVisible()) {
        ValueSwapOverlay::cancel(cb);
        return;
      }
      const QString from = cb->property(kValueSwapPrevProperty).toString();
      if (from.isEmpty() || to.isEmpty() || from == to) return;
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
        ctl::wireComboPopupDust(cb);
        connect(cb, &QComboBox::currentTextChanged, cb,
                [cb](const QString& to) { ctl::onComboText(cb, to); });
        connect(cb, &QComboBox::textActivated, cb,
                [cb](const QString& to) { ctl::onComboPick(cb, to); });
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
