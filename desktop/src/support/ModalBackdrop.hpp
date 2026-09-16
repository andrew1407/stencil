#pragma once
#include "motionPrefs.hpp"

#include <QDialog>
#include <QPainter>
#include <QPixmap>
#include <QWidget>

namespace stencil::support {

  // The browser's `.app-modal-overlay` scrim plus its `backdrop-filter: blur()`, which Qt
  // has no equivalent for: the window behind is photographed once as the dialog opens,
  // blurred and painted under the scrim. Both halves are the one Visuals switch.
  // Header-only and Q_OBJECT-free on purpose: modalReveal.cpp is listed by a dozen test
  // targets, which would each need a new source otherwise.
  class ModalBackdrop : public QWidget {
   public:
    // The blur the snapshot gets (browser blur(3px)) and the scrim over it (rgba(0,0,0,0.5)).
    static constexpr int BLUR_PX = 3;
    static constexpr double SCRIM_ALPHA = 0.5;

    // Downscale-then-upscale: a cheap blur whose cost does not grow with the window.
    static QPixmap blurred(const QPixmap& shot, int radiusPx) {
      if (shot.isNull() || radiusPx < 1) return shot;
      const qreal dpr = shot.devicePixelRatio() > 0 ? shot.devicePixelRatio() : 1.0;
      const int div = radiusPx * 2;   // two 1/(2r) passes read softer than one of 1/r
      const QSize small(qMax(2, int(shot.width() / dpr) / div),
                        qMax(2, int(shot.height() / dpr) / div));
      QPixmap out = shot.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                        .scaled(shot.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      out.setDevicePixelRatio(dpr);
      return out;
    }

    // Covers `host` and goes when `dlg` closes. Null when the switch is off, or when there
    // is nothing worth photographing.
    static ModalBackdrop* behind(QDialog* dlg, QWidget* host) {
      if (!modalBackdrop() || !dlg || !host) return nullptr;
      if (host->width() < 8 || host->height() < 8) return nullptr;
      const QPixmap shot = host->grab();
      if (shot.isNull()) return nullptr;
      auto* bd = new ModalBackdrop(host);
      bd->shot_ = blurred(shot, BLUR_PX);
      bd->show();
      bd->raise();
      QObject::connect(dlg, &QDialog::finished, bd, &QWidget::deleteLater);
      return bd;
    }

   protected:
    void paintEvent(QPaintEvent*) override {
      QPainter p(this);
      if (!shot_.isNull()) p.drawPixmap(rect(), shot_);
      p.fillRect(rect(), QColor(0, 0, 0, int(255 * SCRIM_ALPHA)));
    }

   private:
    explicit ModalBackdrop(QWidget* host) : QWidget(host) {
      setObjectName(QStringLiteral("modalBackdrop"));
      setAttribute(Qt::WA_TransparentForMouseEvents);   // the dialog is modal; this only paints
      setGeometry(host->rect());
    }
    QPixmap shot_;
  };

}  // namespace stencil::support
