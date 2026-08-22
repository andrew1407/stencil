#pragma once
// The app's own control tooltip — the desktop port of #app-tooltip in
// browser/css/components.css, which fades over 90 ms instead of snapping.
//
// Qt's tooltip is a private QTipLabel: QSS has no transitions, and there is no supported
// hook to animate the label Qt shows. So QEvent::ToolTip is swallowed app-wide and this
// frameless panel is shown in its place. Qt still owns the TIMING — a ToolTip event only
// arrives after SH_ToolTip_WakeUpDelay (main.cpp pins it at 120 ms) — and the content is
// still tipContent's rendering, so nothing but the fade changes.
//
// Only a widget with its OWN non-empty toolTip() is taken over. Item views resolve
// per-index tooltips inside viewportEvent and have no widget tooltip of their own, so
// those keep Qt's path untouched.
//
// Header-only and Q_OBJECT-free (no signals/slots of its own), so it needs no MOC.
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QFrame>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QScreen>
#include <QShortcutEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QtGlobal>

#include <cmath>

#include "modalReveal.hpp"   // support::motionReduced()
#include "tipContent.hpp"    // enrichedToolTip()

namespace stencil::gui {

  class AppTooltip : public QFrame {
   public:
    static constexpr int kFadeMs = 90;      // browser: #app-tooltip transition
    static constexpr int kShakeMs = 260;    // one brief attention shake, never repeated
    static constexpr int kShakePx = 4;
    static constexpr int kGap = 15;         // cursor offset, as Qt's own tooltip uses
    static constexpr const char* kObjectName = "stencilAppTooltip";

    explicit AppTooltip(QWidget* parent = nullptr) : QFrame(parent, Qt::ToolTip) {
      setObjectName(QString::fromLatin1(kObjectName));
      setAttribute(Qt::WA_TransparentForMouseEvents);
      setAttribute(Qt::WA_ShowWithoutActivating);
      setFocusPolicy(Qt::NoFocus);
      auto* lay = new QVBoxLayout(this);
      lay->setContentsMargins(0, 0, 0, 0);
      body_ = new QLabel(this);
      body_->setTextFormat(Qt::RichText);
      body_->setObjectName(QStringLiteral("stencilAppTooltipBody"));
      lay->addWidget(body_);
      hide();

      fade_ = new QVariantAnimation(this);
      fade_->setDuration(kFadeMs);
      QObject::connect(fade_, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) { setWindowOpacity(v.toDouble()); });
      QObject::connect(fade_, &QVariantAnimation::finished, this, [this] {
        if (closing_) { closing_ = false; QFrame::hide(); }
      });
      // Anti-stranding heartbeat: a fast pointer sweep can leave the owner without ever
      // sending a Leave we see (it is destroyed, re-laid-out, or the cursor jumps clear).
      // Whatever happened, the tooltip goes when the pointer is no longer on its owner.
      auto* beat = new QTimer(this);
      beat->setInterval(200);
      QObject::connect(beat, &QTimer::timeout, this, [this] {
        if (!isVisible() || closing_) return;
        if (!owner_ || !owner_->isVisible() || !owner_->window()->isActiveWindow()
            || !owner_->rect().contains(owner_->mapFromGlobal(QCursor::pos())))
          hideTip();
      });
      beat->start();
    }

    QWidget* owner() const { return owner_.data(); }

    // Show `owner`'s tooltip near `globalPos`. Rich text is used as given; a plain string
    // goes through the same rendering Qt's tooltip would have shown.
    void showFor(QWidget* owner, const QString& text, const QPoint& globalPos) {
      const QString rich = text.trimmed().startsWith('<') ? text : enrichedToolTip(text);
      if (rich.isEmpty()) { hideTip(); return; }
      owner_ = owner;
      body_->setText(rich);
      adjustSize();
      place(globalPos);
      closing_ = false;
      fade_->stop();
      if (support::motionReduced()) {   // no fade; the end state, at once
        setWindowOpacity(1.0);
        show();
        raise();
        return;
      }
      const qreal from = isVisible() ? windowOpacity() : 0.0;
      setWindowOpacity(from);
      show();
      raise();
      fade_->setStartValue(from);
      fade_->setEndValue(1.0);
      fade_->start();
    }

    // Fade out and then hide. Idempotent, and a showFor() mid-fade takes it straight back
    // up from wherever it got to rather than blinking.
    void hideTip() {
      if (!isVisible()) { owner_.clear(); return; }
      owner_.clear();
      fade_->stop();
      if (support::motionReduced()) { closing_ = false; QFrame::hide(); return; }
      closing_ = true;
      fade_->setStartValue(windowOpacity());
      fade_->setEndValue(0.0);
      fade_->start();
    }

    // A brief attention shake — "yes, that is the shortcut you just pressed". One damped
    // left-right pass, never a loop, and it settles exactly where it was placed.
    void shakeKeys() {
      if (!isVisible() || support::motionReduced()) return;
      if (shake_ && shake_->state() == QAbstractAnimation::Running) return;   // no restacking
      if (!shake_) {
        shake_ = new QVariantAnimation(this);
        shake_->setDuration(kShakeMs);
        shake_->setStartValue(0.0);
        shake_->setEndValue(1.0);
        QObject::connect(shake_, &QVariantAnimation::valueChanged, this,
                         [this](const QVariant& v) { applyShake(v.toDouble()); });
        QObject::connect(shake_, &QVariantAnimation::finished, this,
                         [this] { applyShake(1.0); });
      }
      shake_->start();
    }

    // The offset the shake is at, for tests: 0 when settled.
    int shakeOffset() const { return x() - home_.x(); }
    bool shaking() const { return shake_ && shake_->state() == QAbstractAnimation::Running; }
    bool fadingOut() const { return closing_; }

   private:
    void place(const QPoint& cursor) {
      const QScreen* scr = QGuiApplication::screenAt(cursor);
      if (!scr) scr = QGuiApplication::primaryScreen();
      const QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1024, 768);
      int left = cursor.x() + kGap;
      int top = cursor.y() + kGap;
      if (left + width() > avail.right()) left = cursor.x() - width() - kGap;
      if (top + height() > avail.bottom()) top = cursor.y() - height() - kGap;
      left = qBound(avail.left() + 10, left, qMax(avail.left() + 10, avail.right() - width()));
      top = qBound(avail.top() + 10, top, qMax(avail.top() + 10, avail.bottom() - height()));
      home_ = QPoint(left, top);
      move(home_);
    }
    // Three half-cycles, amplitude decaying to nothing, so it settles exactly at home.
    void applyShake(double t) {
      const double off = t >= 1.0 ? 0.0 : std::sin(t * 3.0 * M_PI) * kShakePx * (1.0 - t);
      move(home_ + QPoint(qRound(off), 0));
    }

    QLabel* body_ = nullptr;
    QVariantAnimation* fade_ = nullptr;
    QVariantAnimation* shake_ = nullptr;
    QPointer<QWidget> owner_;
    QPoint home_;
    bool closing_ = false;
  };

  // The app-wide filter that hands QEvent::ToolTip to AppTooltip. Q_OBJECT-free for the
  // same reason as the panel: it only overrides eventFilter.
  class AppTooltipFilter : public QObject {
   public:
    explicit AppTooltipFilter(QObject* parent = nullptr) : QObject(parent) {}

    AppTooltip* tip() {
      if (!tip_) tip_ = new AppTooltip(nullptr);
      return tip_;
    }

    // The action a control fires, for the keycap shake: a tool button's default action,
    // else the widget's first action. Null when the control has none.
    static QAction* actionOf(const QWidget* w) {
      if (!w) return nullptr;
      if (const auto* tb = qobject_cast<const QToolButton*>(w))
        if (QAction* a = tb->defaultAction()) return a;
      const QList<QAction*> acts = w->actions();
      return acts.isEmpty() ? nullptr : acts.first();
    }

    static bool actionAnswersTo(const QAction* a, const QKeySequence& seq) {
      if (!a || seq.isEmpty()) return false;
      for (const QKeySequence& s : a->shortcuts())
        if (!s.isEmpty() && s.matches(seq) == QKeySequence::ExactMatch) return true;
      return false;
    }

   protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      auto* w = qobject_cast<QWidget*>(o);
      switch (e->type()) {
        case QEvent::ToolTip: {
          // Only a widget carrying its OWN tooltip; anything else (item views resolving a
          // per-index tooltip in viewportEvent) keeps Qt's path.
          if (!w || w->toolTip().isEmpty()) break;
          tip()->showFor(w, w->toolTip(), static_cast<QHelpEvent*>(e)->globalPos());
          return true;   // Qt's own label must not also appear
        }
        case QEvent::Shortcut: {
          // A LIVE shortcut never arrives as a key press — Qt consumes the key and sends
          // this instead — so the shake has to be driven from here.
          if (!tip_ || !tip_->isVisible()) break;
          if (actionAnswersTo(actionOf(tip_->owner()), static_cast<QShortcutEvent*>(e)->key()))
            tip_->shakeKeys();     // "yes, that one" — and the tooltip stays up
          else
            tip_->hideTip();       // someone else's chord: the tooltip has been overtaken
          break;
        }
        case QEvent::KeyPress: {
          const auto* ke = static_cast<QKeyEvent*>(e);
          if (ke->isAutoRepeat()) break;
          // Whatever reaches here is NOT a bound shortcut (Qt would have eaten it), so it
          // dismisses — Escape included. The shake acknowledges a shortcut; it is not a
          // way to pin the tooltip open.
          if (tip_ && tip_->isVisible()) tip_->hideTip();
          break;
        }
        case QEvent::Leave:
        case QEvent::Hide:
        case QEvent::WindowDeactivate:
          if (tip_ && w && w == tip_->owner()) tip_->hideTip();
          break;
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
          if (tip_) tip_->hideTip();
          break;
        default:
          break;
      }
      return QObject::eventFilter(o, e);
    }

   private:
    AppTooltip* tip_ = nullptr;
  };

  // Install the fading tooltip on the running application. Idempotent — the filter and
  // the panel are process-wide, like Qt's own tooltip.
  inline AppTooltipFilter* installAppTooltips() {
    static QPointer<AppTooltipFilter> filter;
    if (!filter && qApp) {
      filter = new AppTooltipFilter(qApp);
      qApp->installEventFilter(filter);
    }
    return filter.data();
  }

  // The live panel, creating it on demand; null only with no QApplication.
  inline AppTooltip* appTooltip() {
    AppTooltipFilter* f = installAppTooltips();
    return f ? f->tip() : nullptr;
  }

}  // namespace stencil::gui
