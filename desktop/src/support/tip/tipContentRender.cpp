// Rendering a parsed Tip to the HTML Qt draws, and the palette that html carries literally — so a
// theme change rebuilds every enriched tooltip from its plain text rather than recolouring in place.
// Back the other way, a rich tip's plain reading: what accessibility reads in place of its markup.
#include "tipContentParts.hpp"
#include <QApplication>
#include <QBuffer>
#include <QImage>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>
#include <QToolTip>
#include <QWidget>

namespace stencil::gui {

  using tipdetail::highlightKeys;
  using tipdetail::keysHtml;

  namespace {
    Palette g_pal = themePalette(false);
  }  // namespace

  QString renderTip(const QString& text, const Palette& pal, bool mac, const QFont* font) {
    const Tip tip = parseTip(text);
    if (tip.title.isEmpty() && tip.blocks.isEmpty()) return {};
    QString caps;
    for (const QString& k : tip.keys) caps += keysHtml(k, pal, mac);
    const QString title =
        tip.title.isEmpty() ? QString() : "<b>" + highlightKeys(tip.title, pal, mac) + "</b>";

    QString body;
    QString open;
    auto close = [&] {
      if (open == "rows") body += "</table>";
      open.clear();
    };
    for (const TipBlock& b : tip.blocks) {
      if (b.kind == TipBlock::Kind::ROW) {
        if (open != "rows") {
          close();
          body += "<table cellspacing=\"0\" cellpadding=\"1\">";
          open = "rows";
        }
        body += "<tr><td><b>• " + highlightKeys(b.term, pal, mac) + "</b>&nbsp;&nbsp;</td><td>" +
                highlightKeys(b.text, pal, mac) + "</td></tr>";
        continue;
      }
      if (b.kind == TipBlock::Kind::BULLET) {
        // A drawn "•", not a <ul>: Qt indents a real list by 40px and counts it in the width.
        close();
        body += "<div style=\"margin-top:2px;\">• " + highlightKeys(b.text, pal, mac) + "</div>";
        continue;
      }
      close();
      // Browser .tip-note: --warning, 5px above.
      const bool note = b.kind == TipBlock::Kind::NOTE;
      const QString colour = note                             ? pal.warning.name()
                             : b.kind == TipBlock::Kind::HINT ? pal.textMuted.name()
                                                              : pal.textMain.name();
      body += "<div style=\"color:" + colour + "; margin-top:" + (note ? "5" : "3") + "px;\">" +
              highlightKeys(b.text, pal, mac) + "</div>";
    }
    close();

    // A word-wrapped QLabel lays out to a roughly SQUARE block; pinning the measured
    // width defeats that. The cap is the browser's #app-tooltip max-width.
    const int width = [&] {
      QTextDocument doc;
      doc.setDefaultFont(font ? *font : QToolTip::font());
      // Margin ZERO, or the document's 4px margins are baked in and added again at render.
      doc.setDocumentMargin(0);
      doc.setHtml(title + (caps.isEmpty() ? QString() : "&nbsp;&nbsp;" + caps) + body);
      doc.setTextWidth(-1);
      return qBound(1, qCeil(doc.idealWidth()), 380);
    }();

    // Browser .tip-head. Middle-aligned because a keycap is taller than the type; the
    // caps cell never breaks, a chord is one thing.
    QString head = title;
    if (!caps.isEmpty())
      head = "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>"
             "<td style=\"vertical-align: middle;\">" + title +
             "</td><td align=\"right\" style=\"vertical-align: middle; white-space: nowrap;\">" +
             caps + "</td></tr></table>";
    return "<table width=\"" + QString::number(width) +
           "\" cellspacing=\"0\" cellpadding=\"0\"><tr><td>" + head + body + "</td></tr></table>";
  }

  void setTooltipPalette(const Palette& pal) {
    g_pal = pal;
    // The html carries LITERAL colours, so every enriched tooltip is rebuilt from its plain text.
    for (QWidget* w : QApplication::allWidgets()) {
      if (!w) continue;
      const QVariant plain = w->property(PLAIN_TIP_PROPERTY);
      if (!plain.isValid()) continue;
      const QString rich = renderTip(plain.toString(), g_pal);
      if (!rich.isEmpty()) w->setToolTip(rich);
    }
  }

  bool hasKeycaps(const QString& richText) {
    return richText.contains(QLatin1String(KEYCAP_CLASS));
  }

  QString blankKeycaps(const QString& richText) {
    static const QRegularExpression cap("(<img alt=\"[^\"]*\" class=\"" +
                                        QString::fromLatin1(KEYCAP_CLASS) +
                                        "\"[^>]*base64,)[^\"]*(\">)");
    static const QString empty = [] {  // 1x1 transparent, drawn to whatever the box asks
      QImage img(1, 1, QImage::Format_ARGB32);
      img.fill(Qt::transparent);
      QByteArray png;
      QBuffer buf(&png);
      buf.open(QIODevice::WriteOnly);
      img.save(&buf, "PNG");
      return QString::fromLatin1(png.toBase64());
    }();
    QString out = richText;
    out.replace(cap, "\\1" + empty + "\\2");
    return out == richText ? QString() : out;
  }

  namespace {
    void appendLines(QTextFrame::iterator it, const QTextFrame::iterator& end, QStringList& out);

    // A row is one line, its cells two spaces apart; a cell holding a block of its own keeps its lines.
    void appendTable(QTextTable* table, QStringList& out) {
      for (int r = 0; r < table->rows(); ++r) {
        QStringList cells;
        bool block = false;
        for (int c = 0; c < table->columns(); ++c) {
          const QTextTableCell cell = table->cellAt(r, c);
          if (cell.row() != r || cell.column() != c) continue;   // spanned from another slot
          QStringList lines;
          appendLines(cell.begin(), cell.end(), lines);
          block = block || lines.size() > 1;
          cells += lines;
        }
        if (block) out += cells;
        else if (!cells.isEmpty()) out << cells.join(QStringLiteral("  "));
      }
    }

    void appendLines(QTextFrame::iterator it, const QTextFrame::iterator& end, QStringList& out) {
      for (; it != end; ++it) {
        if (QTextFrame* frame = it.currentFrame()) {
          if (auto* table = qobject_cast<QTextTable*>(frame)) appendTable(table, out);
          else appendLines(frame->begin(), frame->end(), out);
          continue;
        }
        const QString text = it.currentBlock().text().remove(QChar::ObjectReplacementCharacter);
        for (const QString& part : text.split(QChar::LineSeparator))   // a <br> is a line too
          if (const QString line = part.simplified(); !line.isEmpty()) out << line;
      }
    }
  }  // namespace

  QString tipPlainText(const QString& richText) {
    if (!Qt::mightBeRichText(richText)) return richText;
    static const QRegularExpression img("<img\\b[^>]*?\\balt=\"([^\"]*)\"[^>]*>",
                                        QRegularExpression::CaseInsensitiveOption);
    QString html = richText;
    html.replace(img, QStringLiteral("\\1"));
    QTextDocument doc;
    doc.setHtml(html);
    QStringList lines;
    appendLines(doc.rootFrame()->begin(), doc.rootFrame()->end(), lines);
    return lines.join('\n');
  }

  void syncTipDescription(QWidget* w) {
    static constexpr const char* OWN = "stencilTipDescription";
    const QString mine = w->property(OWN).toString();
    if (w->accessibleDescription() != mine) return;
    const QString tip = w->toolTip();
    const QString plain = Qt::mightBeRichText(tip) ? tipPlainText(tip) : QString();
    w->setProperty(OWN, plain);
    w->setAccessibleDescription(plain);
  }

  QString enrichedToolTip(const QString& plain, const QFont* font) {
    const QString t = plain.trimmed();
    if (t.isEmpty() || t.startsWith('<')) return {};  // empty, or already someone's own HTML
    return renderTip(plain, g_pal, ON_MAC, font);
  }

  // Composed control tooltips (browser utils.js composeControlTitle)

  QString composeControlTitle(const QString& base, const QString& combo, bool disabled,
                              const QString& reason) {
    QString out = base;
    if (!combo.isEmpty()) out += (out.isEmpty() ? "" : " ") + QString("(%1)").arg(combo);
    if (disabled && !reason.isEmpty()) out += (out.isEmpty() ? "" : "\n") + QString("\u2014 ") + reason;
    return out;
  }
  Palette currentPalette() { return g_pal; }

}  // namespace stencil::gui
