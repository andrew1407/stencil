#include "projectRowDelegate.hpp"

#include "../support/appTooltip.hpp"
#include "displayName.hpp"
#include "iconSet.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <algorithm>


namespace stencil::gui {

  void ProjectRowDelegate::paintRow(QPainter* p, const QStyleOptionViewItem& opt,
                                    const QModelIndex& idx) const {
    const QStyleOptionViewItem& o = opt;
    const QString full = o.text;
    const bool realRow = !idx.data(Qt::UserRole).isNull();
    const bool temp = idx.data(TEMP_ROLE).toBool();
    QStyle* st = o.widget ? o.widget->style() : QApplication::style();
    if (!realRow && !temp) {
      QStyledItemDelegate::paint(p, o, idx);
      return;
    }
    // State_Selected is cleared: the style repaints a selected item's ICON in QIcon::Selected mode
    // (blended with the highlight), which washed a white thumbnail lilac. The fill is already transparent.
    QStyleOptionViewItem bg(o);
    bg.text.clear();
    bg.state &= ~QStyle::State_Selected;
    st->drawControl(QStyle::CE_ItemViewItem, &bg, p, o.widget);

    const bool remote = !idx.data(Qt::UserRole + 1).toString().isEmpty();
    const bool fileOrigin = idx.data(Qt::UserRole + 6).toBool();
    const bool current = idx.data(ACTIVE_ROLE).toBool();
    // Highlight = accent, Link = the accent-2 shade (--text-key) — theme.cpp buildQPalette.
    const QColor accent = o.palette.color(QPalette::Highlight);

    // Browser .project-row borders: gold = server, bronze = .stencil file (each with a soft 1px ring),
    // the accent-2 SHADE for the project open in THIS editor, neutral hairline otherwise.
    QColor edge;
    QColor ring;
    if (remote) { edge = GOLD_EDGE; ring = edge; ring.setAlpha(140); }
    else if (fileOrigin) { edge = BRONZE_EDGE; ring = edge; ring.setAlpha(140); }
    else if (current) { edge = o.palette.color(QPalette::Link); }
    else { edge = o.palette.color(QPalette::Dark); }
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setBrush(Qt::NoBrush);
    // DASHED, as the browser's .project-temp.
    p->setPen(QPen(edge, 1, temp ? Qt::DashLine : Qt::SolidLine));
    p->drawRoundedRect(QRectF(opt.rect).adjusted(1.5, 1.5, -1.5, -1.5), 8, 8);
    if (ring.isValid()) {
      p->setPen(QPen(ring, 1));
      p->drawRoundedRect(QRectF(opt.rect).adjusted(0.5, 0.5, -0.5, -0.5), 9, 9);
    }
    p->restore();

    // Stacked column (browser row layout): name / meta / origin.
    const QRect base = st->subElementRect(QStyle::SE_ItemViewItemText, &o, o.widget);
    const int colLeft = base.left() + THUMB_TEXT_GAP;
    const int colWidth = kebabZone(opt.rect).left() - 8 - colLeft;
    if (colWidth > 20) {
      const QString name = idx.data(Qt::UserRole + 3).toString();
      const QString nameStr = name.isEmpty() ? full : name;
      QString meta = idx.data(META_ROLE).toString();
      const QColor def = o.palette.color(QPalette::Text);
      QColor nameCol = idx.data(Qt::UserRole + 4).value<QColor>();
      if (!nameCol.isValid()) nameCol = def;
      // Browser .project-name / .project-sub sizes (14 / 12 px), not the app font.
      QFont nameF(o.font);
      nameF.setBold(true);
      nameF.setPixelSize(14);
      QFont metaF(o.font);
      metaF.setPixelSize(12);
      const QFontMetrics nfm(nameF), mfm(metaF);
      // More air under the name: the inline editor otherwise sat on the "Created …" line.
      constexpr int LINE_GAP = 6;
      // Centred on the lines this row actually draws — the temporary row has no origin line.
      int totalH = nfm.height();
      if (!meta.isEmpty()) totalH += LINE_GAP + mfm.height();
      if (!temp) totalH += LINE_GAP + mfm.height();
      int y = opt.rect.top() + (opt.rect.height() - totalH) / 2;
      p->save();
      p->setFont(nameF);
      p->setPen(nameCol);
      p->drawText(QRect(colLeft, y, colWidth, nfm.height()),
                  Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                  nfm.elidedText(nameStr, Qt::ElideRight, colWidth));
      const int nameW = std::min(nfm.horizontalAdvance(nameStr), colWidth);
      nameRects_[idx.row()] = QRect(colLeft, y, nameW, nfm.height());
      y += nfm.height() + LINE_GAP;
      if (!meta.isEmpty()) {
        p->setFont(metaF);
        p->setPen(o.palette.color(QPalette::PlaceholderText));
        p->drawText(QRect(colLeft, y, colWidth, mfm.height()),
                    Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                    mfm.elidedText(meta, Qt::ElideRight, colWidth));
        y += mfm.height() + LINE_GAP;
      }
      // Origin line: server (gold) / .stencil (bronze) / computer (grey), plus the accent "(Current)".
      // The temporary row has none, and no "⋯" either.
      if (temp) { p->restore(); return; }
      const QString gname = remote ? QStringLiteral("server")
                                   : (fileOrigin ? QStringLiteral("file-text")
                                                 : QStringLiteral("monitor"));
      const QColor gcol = remote ? GOLD_EDGE : (fileOrigin ? BRONZE_EDGE : GREY_ORIGIN);
      const QString gtext = remote ? idx.data(Qt::UserRole + 1).toString()
                                   : (fileOrigin ? QStringLiteral(".stencil")
                                                 : QStringLiteral("computer"));
      if (hasIcon(gname)) {
        const int gs = 13;
        p->setRenderHint(QPainter::Antialiasing, true);
        themedIcon(gname, gcol, gs).paint(
            p, QRect(colLeft, y + (mfm.height() - gs) / 2, gs, gs));
        p->setFont(metaF);
        p->setPen(gcol);
        int tx = colLeft + gs + 4;
        const int gw = std::min(mfm.horizontalAdvance(gtext), colWidth - (gs + 4));
        // Only a server ADDRESS is ellipsised; the other two are short words that always fit.
        p->drawText(QRect(tx, y, gw, mfm.height()),
                    Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                    remote ? mfm.elidedText(gtext, Qt::ElideRight, gw) : gtext);
        tx += gw;
        if (current) {
          p->setPen(accent);
          p->drawText(QRect(tx, y, colWidth - (tx - colLeft), mfm.height()),
                      Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                      QStringLiteral(" (Current)"));
        }
      }
      p->restore();
    }

    // Browser "…" more-actions button: reacts to ITS OWN hover only, then a band of glass sweeps across it.
    const QRect chip = kebabChip(opt.rect);
    const bool onKebab = idx.row() == kebabRow_;
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Qt::NoPen);
    p->setBrush(onKebab ? accent.lighter(115) : accent);
    p->drawRoundedRect(chip, 8, 8);
    p->setBrush(Qt::white);
    const int cx = chip.center().x();
    const int cy = chip.center().y();
    for (int dx = -5; dx <= 5; dx += 5) p->drawEllipse(QPointF(cx + dx, cy + 2), 1.6, 1.6);
    p->restore();
  }

}  // namespace stencil::gui
