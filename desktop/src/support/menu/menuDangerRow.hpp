#pragma once
// A row menu's highlight colours. A destructive row wears its colour on BOTH halves (browser
// .is-danger): Qt has no per-action colour once QStyleSheetStyle owns QMenu::item, so it is the
// menu's DEFAULT item, styled via `QMenu::item:default`. A row menu hovers in the accent.
#include "skinPrefs.hpp"

#include <QAction>
#include <QColor>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QString>

namespace stencil::support {

  // Call AFTER any helper that replaces the menu's stylesheet — this appends to it.
  inline void markDangerRow(QMenu& menu, QAction* action, const QColor& danger) {
    if (!action) return;
    menu.setDefaultAction(action);
    menu.setStyleSheet(
        menu.styleSheet()
        + QStringLiteral(
              "\nQMenu::item:default{color:%1;font-weight:normal;}"
              "\nQMenu::item:default:selected{background:%1;color:#ffffff;}")
              .arg(danger.name()));
  }

  // The row menus (browser .project-menu-item, .chat-row-menu-item) highlight in the accent: the
  // `accentRows` property keys menus.qss, and each glyph takes `ink` as its Active pixmap, the mode
  // Qt draws a highlighted item's icon in. The danger row's red highlight takes white glyphs.
  inline void markAccentRows(QMenu& menu, const QColor& ink, QAction* danger = nullptr, int px = 16) {
    menu.setProperty("accentRows", true);
    for (QAction* a : menu.actions()) {
      if (QMenu* sub = a->menu()) markAccentRows(*sub, ink, danger, px);
      if (a->icon().isNull() || isWebcore()) continue;   // the skin's glyphs keep their own halo
      QIcon icon = a->icon();
      const QSize size(px, px);
      for (const qreal dpr : {1.0, 2.0}) {
        QPixmap lit = icon.pixmap(size, dpr);
        if (lit.isNull()) continue;
        QPainter p(&lit);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(QRect(QPoint(0, 0), size), a == danger ? QColor(Qt::white) : ink);
        p.end();
        icon.addPixmap(lit, QIcon::Active);
      }
      a->setIcon(icon);
    }
  }

}  // namespace stencil::support
