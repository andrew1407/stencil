#include "InfoDialog.hpp"
#include "../support/filterFade.hpp"  // rows fade in/out with the search, never blink
#include "../support/KeycapChip.hpp"  // the term's key combos as the tooltips' caps
#include "../support/modalChrome.hpp"
#include "../support/ShimmerOverlay.hpp"   // row hover sweep
#include "tipContent.hpp"   // isKeyCombo, comboKeycapsHtml, currentPalette
#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    QByteArray readResource(const QString& path) {
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly)) return {};
      return f.readAll();
    }
    constexpr int KEY_COL_W = 188;   // .info-key flex-basis

    // The term column wears the tooltips' keycaps: every token that is a key combo
    // becomes caps, the rest stays mono accent prose (browser infoModal's keyTermHtml).
    QString keyTermHtml(const QString& term, const Palette& pal) {
      static const QRegularExpression plus("\\s*\\+\\s*");
      static const QRegularExpression split("(\\s+|[()/])");
      const QString s = QString(term).replace(plus, "+");
      QString out;
      int last = 0;
      const auto plain = [&](const QString& t) {
        return QString("<span style=\"color:%1;font-family:Menlo,Consolas,monospace;"
                       "font-size:12px;font-weight:bold;\">%2</span>")
            .arg(pal.textKey.name(), t.toHtmlEscaped());
      };
      const auto piece = [&](const QString& tok) {
        if (tok.isEmpty()) return;
        out += isKeyCombo(tok) ? comboKeycapsHtml(tok, pal, ON_MAC, 0.8) : plain(tok);   // browser .info-key 12px caps
      };
      for (auto it = split.globalMatch(s); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        piece(s.mid(last, m.capturedStart() - last));
        piece(m.captured(0));
        last = m.capturedEnd();
      }
      piece(s.mid(last));
      return out;
    }
  }

  InfoDialog::InfoDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Controls & Shortcuts Info");
    // Browser infoModal.js parity: the shared shell, a search box, a scrolling body.
    ModalChrome chrome = installModalChrome(this, "help", tr("Controls & Shortcuts Info"));
    search = addModalSearchBar(chrome, tr("Search controls…"));
    search->setToolTip("Filter the controls and tips by keyword");
    ModalScrollBody body = makeModalScrollBody(chrome);
    content = body.content;

    loadSections();
    build();
    body.layout->addStretch(1);
    connect(search, &QLineEdit::textChanged, this,
            [this](const QString& q) { applyFilter(q); });
    // The browser's max-height: 82vh shell with the list scrolling inside it.
    sizeModalTall(this, MODAL_WIDTH);
    // …and the caret lands in the search box, as the browser modal's onOpen does.
    QTimer::singleShot(0, search, [s = search] { s->setFocus(); });
  }

  // Parse the embedded config into sections once (infoConfig.json:
  // [section, [[term, desc]...]]).
  void InfoDialog::loadSections() {
    const auto info =
        QJsonDocument::fromJson(readResource(":/config/infoConfig.json")).array();
    for (const auto& sectionV : info) {
      const QJsonArray section = sectionV.toArray();
      if (section.size() < 2) continue;
      Section sec;
      sec.title = section[0].toString();
      for (const auto& rowV : section[1].toArray()) {
        const QJsonArray row = rowV.toArray();
        if (row.size() < 2) continue;
        sec.rows.push_back({row[0].toString(), row[1].toString(), nullptr});
      }
      sections.push_back(sec);
    }
  }

  // Build every group title and row once; the filter only shows/hides them.
  void InfoDialog::build() {
    auto* col = qobject_cast<QVBoxLayout*>(content->layout());
    // Caps with no face of their own — outline and glyph only, like the shortcut table's.
    Palette pal = currentPalette();
    pal.bgContainer = QColor(0, 0, 0, 0);
    bool first = true;
    for (Section& sec : sections) {
      sec.titleWidget = modalSectionLabel(sec.title, content, first);
      first = false;
      // .info-group-title sits 4px (not 6) over its rows.
      sec.titleWidget->setContentsMargins(0, sec.titleWidget->contentsMargins().top(), 0, 4);
      col->addWidget(sec.titleWidget);
      for (Row& r : sec.rows) {
        auto* row = new QWidget(content);
        row->setProperty("infoRow", true);
        row->setAttribute(Qt::WA_StyledBackground, true);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(4, 5, 4, 5);   // .info-item padding
        h->setSpacing(10);
        // The term, its key combos as caps that shake on hover (support/KeycapChip.hpp).
        auto* key = new KeycapChip(row);
        key->setObjectName(QStringLiteral("infoKey"));
        key->setWordWrap(true);
        key->setFixedWidth(KEY_COL_W);
        key->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        key->setCaps(keyTermHtml(r.left, pal));
        auto* desc = new QLabel(r.right, row);
        desc->setObjectName(QStringLiteral("infoDesc"));
        desc->setWordWrap(true);
        desc->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        desc->setTextInteractionFlags(Qt::TextSelectableByMouse);
        h->addWidget(key, 0, Qt::AlignTop);
        h->addWidget(desc, 1, Qt::AlignTop);
        col->addWidget(row);
        installHoverShimmer(row);   // the app-wide glass sweep, as on a list row
        r.widget = row;
      }
    }
    empty = modalEmptyLabel(tr("No matching controls."), content);
    empty->hide();
    col->addWidget(empty);
  }

  // Hide rows (and emptied groups) that don't match `filter`, case-insensitively on
  // either column — the browser render()'s rule.
  void InfoDialog::applyFilter(const QString& filter) {
    const QString q = filter.trimmed().toLower();
    bool any = false;
    for (Section& sec : sections) {
      bool secAny = false;
      for (Row& r : sec.rows) {
        const bool match = q.isEmpty() || r.left.toLower().contains(q) ||
                           r.right.toLower().contains(q);
        fadeFiltered(r.widget, match);
        secAny = secAny || match;
      }
      sec.titleWidget->setVisible(secAny);
      any = any || secAny;
    }
    empty->setVisible(!any);
  }

}
