#pragma once
// Keycap chips for the shortcut/info dialogs — the tooltip's own caps (TipBody).
// KeycapChip shakes on hover (browser .hotkey-cell:hover .tip-key); ComboCell captures
// a keypress on click, Esc cancels. Q_OBJECT-free (callbacks, not signals), no MOC.
#include "AppTooltip.hpp"   // TipBody — the cap-hunting, shakeable label
#include "modalReveal.hpp"  // support::motionReduced()

#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QVariantAnimation>

#include <functional>

namespace stencil::gui {

  class KeycapChip : public TipBody {
   public:
    explicit KeycapChip(QWidget* parent = nullptr) : TipBody(parent) {
      setAttribute(Qt::WA_Hover, true);
      shake = new QVariantAnimation(this);
      shake->setDuration(AppTooltip::SHAKE_MS);
      shake->setStartValue(0.0);
      shake->setEndValue(1.0);
      QObject::connect(shake, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) { setShake(v.toDouble()); });
      QObject::connect(shake, &QVariantAnimation::finished, this, [this] { settle(); });
    }

    // New caps: the hunt runs again on the next hover.
    void setCaps(const QString& rich) {
      shake->stop();
      setTip(rich);
    }

   protected:
    void enterEvent(QEnterEvent* e) override {
      TipBody::enterEvent(e);
      if (support::motionReduced() || capCount() == 0) return;
      shake->stop();
      settle();
      shake->start();
    }
    void leaveEvent(QEvent* e) override {
      TipBody::leaveEvent(e);
      shake->stop();
      settle();
    }

   private:
    QVariantAnimation* shake = nullptr;
  };

  class ComboCell : public KeycapChip {
   public:
    explicit ComboCell(QWidget* parent = nullptr) : KeycapChip(parent) {
      setFocusPolicy(Qt::ClickFocus);
      setCursor(Qt::PointingHandCursor);
    }

    // "" never arrives.
    std::function<void(const QString&)> onCaptured;
    // A capture swaps in the placeholder.
    void setResting(const QString& rich) {
      resting = rich;
      if (!capturing) setCaps(rich);
    }
    void setPlaceholder(const QString& rich) { placeholder = rich; }

   protected:
    void mousePressEvent(QMouseEvent* e) override {
      if (e->button() == Qt::LeftButton) setFocus(Qt::MouseFocusReason);
      KeycapChip::mousePressEvent(e);
    }
    void focusInEvent(QFocusEvent* e) override {
      KeycapChip::focusInEvent(e);
      capturing = true;
      setProperty("capturing", true);
      repolish();
      setCaps(placeholder);
    }
    void focusOutEvent(QFocusEvent* e) override {
      KeycapChip::focusOutEvent(e);
      endCapture();
    }
    void keyPressEvent(QKeyEvent* e) override {
      if (!capturing) { KeycapChip::keyPressEvent(e); return; }
      const int key = e->key();
      if (key == Qt::Key_Escape) {   // Esc cancels (browser parity) — never binds "Esc"
        e->accept();
        clearFocus();
        return;
      }
      switch (key) {   // a bare modifier keeps waiting
        case Qt::Key_Control: case Qt::Key_Shift: case Qt::Key_Alt: case Qt::Key_Meta:
        case Qt::Key_AltGr: case Qt::Key_CapsLock: case Qt::Key_unknown:
          e->accept();
          return;
        default:
          break;
      }
      e->accept();
      const QKeySequence seq(QKeyCombination(e->modifiers(), Qt::Key(key)));
      const QString portable = seq.toString(QKeySequence::PortableText);
      clearFocus();   // the capture is over either way
      if (onCaptured && !portable.isEmpty()) onCaptured(portable);
    }

   private:
    void endCapture() {
      if (!capturing) return;
      capturing = false;
      setProperty("capturing", false);
      repolish();
      setCaps(resting);
    }
    void repolish() {
      style()->unpolish(this);
      style()->polish(this);
      update();
    }
    QString resting;
    QString placeholder;
    bool capturing = false;
  };

}  // namespace stencil::gui
