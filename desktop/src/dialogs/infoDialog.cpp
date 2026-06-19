#include "infoDialog.hpp"
#include "guiHelpers.hpp"
#include "hotkeyFormat.hpp"
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace stencil::gui {

  InfoDialog::InfoDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Info & Shortcuts");
    setMinimumSize(560, 560);

    auto* browser = new QTextBrowser(this);
    browser->setOpenExternalLinks(true);
    browser->setHtml(buildHtml());

    auto* buttons = makeButtonBox(this, QDialogButtonBox::Close);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(browser, 1);
    layout->addWidget(buttons);
  }

  namespace {
    QByteArray readResource(const QString& path) {
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly)) return {};
      return f.readAll();
    }
  }

  QString InfoDialog::buildHtml() {
    QString html = "<h2>Stencil — Info &amp; Shortcuts</h2>";

    // Keyboard shortcuts table (hotkeysConfig.json).
    const auto keys =
        QJsonDocument::fromJson(readResource(":/config/hotkeysConfig.json"))
            .array();
    if (!keys.isEmpty()) {
      html += "<h3>Keyboard shortcuts</h3><table cellpadding='4'>";
      // Render the shortcut in the platform's native form: ⌘C / ⌥A on macOS,
      // Ctrl+C / Alt+A on Windows/Linux. Storage stays the portable config
      // string; only this human-facing column is converted.
      const bool isMac = core::hotkeyFormat::isMacBuild();
      for (const auto& v : keys) {
        const QJsonObject o = v.toObject();
        const QString seq = QString::fromStdString(core::hotkeyFormat::toNative(
            o.value("default").toString().toStdString(), isMac));
        html += QString("<tr><td><b>%1</b></td><td>&nbsp;&nbsp;</td>"
                        "<td>%2</td></tr>")
                    .arg(seq.toHtmlEscaped(), o.value("label").toString());
      }
      html += "</table>";
    }

    // Usage tips, grouped by section (infoConfig.json: [section, [[term, desc]...]]).
    const auto info =
        QJsonDocument::fromJson(readResource(":/config/infoConfig.json")).array();
    for (const auto& sectionV : info) {
      const QJsonArray section = sectionV.toArray();
      if (section.size() < 2) continue;
      html += QString("<h3>%1</h3><table cellpadding='4'>")
                  .arg(section[0].toString());
      for (const auto& rowV : section[1].toArray()) {
        const QJsonArray row = rowV.toArray();
        if (row.size() < 2) continue;
        html += QString("<tr><td valign='top'><b>%1</b></td><td>&nbsp;&nbsp;</td>"
                        "<td>%2</td></tr>")
                    .arg(row[0].toString(), row[1].toString());
      }
      html += "</table>";
    }
    return html;
  }

}
