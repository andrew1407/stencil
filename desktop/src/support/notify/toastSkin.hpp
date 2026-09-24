#pragma once
// The dress a toast wears (browser css/components/notifications.css and, under the skin,
// css/webcore/windows.css .notify-toast): its face and ink, the sheet that boxes it, and the
// title strip the skin lays over its top edge.
#include "logoStageRules.hpp"
#include "skinPrefs.hpp"

#include <QColor>
#include <QEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QString>
#include <QWidget>

namespace stencil::gui {

  struct ToastDress {
    QColor bg, ink, titleA, titleB;
    bool skinned = false;
  };

  inline ToastDress toastDress(bool special, bool error, const QColor& normalBg, const QColor& errorBg) {
    const support::LogoStageConfig& stage = support::logoStageConfig();
    const support::SkinBevel bevel = support::isWebcore() ? support::skinBevel() : support::SkinBevel{};
    ToastDress d;
    d.skinned = bevel.hilight.isValid();
    // Gold under the skin too: the browser sets a show's notice inline, over the face rule.
    d.bg = special ? QColor(stage.toastGold) : d.skinned ? bevel.face : error ? errorBg : normalBg;
    d.ink = special ? QColor(stage.toastInk) : d.skinned ? bevel.ink : QColor(Qt::white);
    d.titleA = bevel.title;
    d.titleB = bevel.titleB;
    return d;
  }

  // Browser .notify-toast padding (10/12; skinned 23/12/8/8, the 23 the title strip's room) less
  // the 2px QSS border; a show's notice gives a pixel back to its taller mark, so pills match.
  inline QString toastSheet(const ToastDress& d, bool special) {
    if (!d.skinned)
      return QString("QLabel#toast { background: %1; color: %2; padding: %3px 8px; border-radius: 6px; }")
          .arg(d.bg.name(), d.ink.name())
          .arg(special ? 5 : 6);
    const support::SkinBevel bevel = support::skinBevel();
    return QString("QLabel#toast { background: %1; color: %2; border-radius: 0; "
                   "padding: %3px 10px 6px 6px; "
                   "border-top: 2px solid %4; border-left: 2px solid %4; "
                   "border-bottom: 2px solid %5; border-right: 2px solid %5; }")
        .arg(d.bg.name(), d.ink.name())
        .arg(special ? 20 : 21)
        .arg(bevel.hilight.name(), bevel.dark.name());
  }

  // The strip the skin's notice wears (browser .notify-toast::before): the title gradient across
  // the top edge, inset past the bevel, following the pill's own width.
  class ToastTitleStrip : public QWidget {
   public:
    static constexpr int INSET = 3, HEIGHT = 15;

    ToastTitleStrip(QWidget* toast, const QColor& a, const QColor& b) : QWidget(toast), from(a), to(b) {
      setObjectName(QStringLiteral("toastTitle"));
      setAttribute(Qt::WA_TransparentForMouseEvents);
      toast->installEventFilter(this);
      place();
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
      if (watched == parentWidget() && event->type() == QEvent::Resize) place();
      return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent*) override {
      QLinearGradient g(0, 0, width(), 0);
      g.setColorAt(0, from);
      g.setColorAt(1, to);
      QPainter p(this);
      p.fillRect(rect(), g);
    }

   private:
    void place() {
      setGeometry(INSET, INSET, parentWidget()->width() - 2 * INSET, HEIGHT);
      raise();
    }

    QColor from, to;
  };

}  // namespace stencil::gui
