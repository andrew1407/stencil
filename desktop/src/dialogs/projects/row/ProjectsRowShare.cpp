#include "ProjectsDialog.hpp"
#include "menuIconColumn.hpp"
#include "../../../support/share/shareRows.hpp"
#include "../../../support/menu/menuReveal.hpp"

#include <QMenu>

#include <algorithm>

// A row's "Copy entries ›" and "Copy image ›" (browser ui/share/projectMenu.js copyMenuItems): the name and
// its stencil:// links are made here from the row; a picture, the layout and the whole project are
// read — and a picture drawn off-screen — by the owner only once picked.

namespace stencil::gui {

  void ProjectsDialog::addRowCopyMenu(QMenu& menu, const QString& rowId, const QString& rowServer, const QColor& ico) {
    QString name, source;
    if (rowServer.isEmpty()) {
      const auto it = std::find_if(projects.begin(), projects.end(),
                                   [&rowId](const Project& p) { return QString::fromStdString(p.meta.id) == rowId; });
      if (it != projects.end()) {
        name = QString::fromStdString(it->meta.name);
        source = QString::fromStdString(it->meta.source);
      }
    } else {
      const auto it = std::find_if(remote.begin(), remote.end(), [&](const stencil::net::ServerProject& sp) {
        return sp.id == rowId && sp.serverUrl == rowServer;
      });
      if (it != remote.end()) {
        name = it->name;
        source = it->source;
      }
    }
    const QHash<support::ShareItem, QString> text = support::projectShareTexts(name, source, rowServer, rowId);
    const auto offered = [&text](support::ShareItem item) {
      return support::isImageShare(item) || item == support::ShareItem::LAYOUT || item == support::ShareItem::PROJECT
          || text.contains(item);
    };
    const auto pick = [this, rowId, rowServer, text](support::ShareItem item) {
      emit clipboardRequested(rowId, rowServer, static_cast<int>(item), text.value(item));
    };
    for (const bool images : {false, true})
      gui::compactIconMenu(*support::addShareRowsMenu(menu, ico, 16, support::CONTEXT_MENU_DUST_MS, images, offered, pick));
  }

}  // namespace stencil::gui
