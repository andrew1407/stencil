#include "infoDialog.hpp"
#include "guiHelpers.hpp"
#include "hotkeyFormat.hpp"
#include "iconSet.hpp"
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPalette>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    QByteArray readResource(const QString& path) {
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly)) return {};
      return f.readAll();
    }
  }

  InfoDialog::InfoDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Info & Shortcuts");
    setMinimumSize(560, 600);

    // Live search box (mirrors the browser info modal's filter).
    search_ = new QLineEdit(this);
    search_->setPlaceholderText("Search shortcuts and tips…");
    search_->setToolTip("Filter the shortcuts and tips by keyword");
    search_->setClearButtonEnabled(true);
    search_->addAction(themedIcon("search", palette().color(QPalette::PlaceholderText), 16),
                       QLineEdit::LeadingPosition);

    browser_ = new QTextBrowser(this);
    browser_->setOpenExternalLinks(true);

    auto* buttons = makeButtonBox(this, QDialogButtonBox::Close);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(search_);
    layout->addWidget(browser_, 1);
    layout->addWidget(buttons);

    loadSections();
    connect(search_, &QLineEdit::textChanged, this,
            [this](const QString& q) { render(q); });
    render(QString());
  }

  // Parse the two embedded configs into normalized sections once.
  void InfoDialog::loadSections() {
    // Keyboard shortcuts (hotkeysConfig.json) — left = native key combo, right = label.
    const auto keys =
        QJsonDocument::fromJson(readResource(":/config/hotkeysConfig.json")).array();
    if (!keys.isEmpty()) {
      Section sec;
      sec.title = "Keyboard shortcuts";
      const bool isMac = core::hotkeyFormat::isMacBuild();
      for (const auto& v : keys) {
        const QJsonObject o = v.toObject();
        const QString seq = QString::fromStdString(core::hotkeyFormat::toNative(
            o.value("default").toString().toStdString(), isMac));
        sec.rows.push_back({seq, o.value("label").toString()});
      }
      sections_.push_back(sec);
    }

    // Usage tips (infoConfig.json: [section, [[term, desc]...]]).
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
        sec.rows.push_back({row[0].toString(), row[1].toString()});
      }
      sections_.push_back(sec);
    }
  }

  // Rebuild the themed HTML, dropping rows (and emptied sections) that don't match
  // `filter`. Colors come from the live QPalette so it tracks light/dark/accent.
  void InfoDialog::render(const QString& filter) {
    const QString q = filter.trimmed().toLower();
    auto col = [this](QPalette::ColorRole r) {
      return palette().color(r).name();
    };
    const QString text = col(QPalette::WindowText);
    const QString muted = col(QPalette::PlaceholderText);
    const QString keyCol = col(QPalette::Link);  // accent-2 (browser --text-key)

    // Mirror the browser info modal: muted uppercase group titles, a fixed-width
    // left column so keys/terms align, monospace accent-2 keys (no filled chip).
    QString html = QString(
                       "<style>"
                       "h3 { color:%1; font-size:12px; font-weight:bold;"
                       "     text-transform:uppercase; letter-spacing:1px;"
                       "     margin:18px 0 2px 0; border-bottom:1px solid %1;"
                       "     padding-bottom:4px; }"
                       "td { padding:5px 10px 5px 0; color:%2; font-size:13px; }"
                       "td.k { font-family:'Menlo','Consolas',monospace;"
                       "       font-weight:bold; color:%3; }"
                       ".muted { color:%1; }"
                       "</style>")
                       .arg(muted, text, keyCol);

    int shown = 0;
    for (const Section& sec : sections_) {
      QString rows;
      for (const Row& r : sec.rows) {
        if (!q.isEmpty() &&
            !r.left.toLower().contains(q) && !r.right.toLower().contains(q))
          continue;
        rows += QString("<tr><td class='k' width='190' valign='top'>%1</td>"
                        "<td valign='top'>%2</td></tr>")
                    .arg(r.left.toHtmlEscaped(), r.right.toHtmlEscaped());
      }
      if (rows.isEmpty()) continue;  // hide a section with no matches
      html += QString("<h3>%1</h3>"
                      "<table width='100%' cellspacing='0' cellpadding='0'>%2</table>")
                  .arg(sec.title.toHtmlEscaped(), rows);
      ++shown;
    }
    if (shown == 0)
      html += QString("<p class='muted'>No matches for “%1”.</p>")
                  .arg(q.toHtmlEscaped());
    browser_->setHtml(html);
  }

}
