#include "ProjectsDialog.hpp"

#include "ProjectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "ProjectsDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../../support/guiHelpers.hpp"

#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPolygonF>
#include <QStyle>

// The temporary row's icon and the placeholder every thumbless row gets.

namespace stencil::gui {

  // The temporary row's tile - the browser's .project-thumb: a 56px rounded box holding the 24px
  // pencil, or the incognito mask. The box is what puts the glyph where the browser's sits.
  QPixmap ProjectsDialog::temporaryIcon(bool incognito) const {
    QPixmap pm(56, 56);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    // Mid = --text-muted, the browser's .project-thumb-placeholder colour. NOT the Disabled group
    // (--disabled-text): that is dimmer, and the pen read darker here than in the browser.
    const QColor muted = palette().color(QPalette::Mid);
    p.setBrush(palette().color(QPalette::AlternateBase));      // --bg-info
    p.setPen(QPen(palette().color(QPalette::Dark), 1));         // --border-main
    p.drawRoundedRect(QRectF(0.5, 0.5, 55, 55), 6, 6);
    const QString glyph = incognito ? QStringLiteral("incognito") : QStringLiteral("pencil");
    if (hasIcon(glyph)) {
      themedIcon(glyph, muted, 24).paint(&p, QRect(16, 16, 24, 24));
    } else {   // no such glyph in the set: a plain pen stroke
      p.setBrush(Qt::NoBrush);
      p.setPen(QPen(muted, 3, Qt::SolidLine, Qt::RoundCap));
      p.drawLine(QPointF(20, 36), QPointF(36, 20));
      p.drawLine(QPointF(18, 39), QPointF(26, 39));
    }
    return pm;
  }

  void ProjectsDialog::setTemporary(bool temporary, bool incognito) {
    if (this->temporary == temporary && this->incognito == incognito) return;
    this->temporary = temporary;
    this->incognito = incognito;
    refresh();
  }

  QPixmap ProjectsDialog::placeholderIcon(bool remote) const {
    QPixmap pm(56, 56);
    pm.fill(Qt::transparent);
    // Hand-drawn 56×56 glyphs (QStyle's SP_DriveNetIcon globe jars on macOS): gold
    // "rack" for server rows (matching the Servers dialog), muted picture for local.
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (remote) {
      const QColor gold("#d4a017");
      p.setPen(QPen(gold, 3));
      p.setBrush(Qt::NoBrush);
      const QRectF top(14, 15, 28, 11);
      const QRectF bot(14, 30, 28, 11);
      p.drawRoundedRect(top, 3, 3);
      p.drawRoundedRect(bot, 3, 3);
      p.setPen(Qt::NoPen);
      p.setBrush(gold);
      p.drawEllipse(QPointF(20, top.center().y()), 2, 2);
      p.drawEllipse(QPointF(20, bot.center().y()), 2, 2);
    } else {
      const QColor muted = palette().color(QPalette::Mid);   // --text-muted, as above
      const QRectF frame(13, 15, 30, 26);
      p.setPen(QPen(muted, 2.5));
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(frame, 4, 4);
      p.setClipRect(frame);  // keep the little scene inside the frame
      p.setPen(Qt::NoPen);
      p.setBrush(muted);
      p.drawEllipse(QPointF(22, 23), 3, 3);  // sun
      QPolygonF mountain;
      mountain << QPointF(16, 41) << QPointF(27, 29) << QPointF(34, 35)
               << QPointF(41, 27) << QPointF(44, 41);
      p.drawPolygon(mountain);
    }
    return pm;
  }

}  // namespace stencil::gui
