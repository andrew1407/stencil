#pragma once
// A project's "Copy entries ›" — its name, stencil:// links, layout and whole project — and "Copy image ›"
// — Current / No lines / Original — (browser ui/share/projectMenu.js copyMenuItems). The projects
// row menu and the name chip's menu build their rows here, so labels, icons and order agree.
#include "deepLink.hpp"
#include "iconSet.hpp"
#include "menuReveal.hpp"

#include <QAction>
#include <QColor>
#include <QHash>
#include <QMenu>
#include <QString>

#include <cstddef>
#include <functional>
#include <iterator>

namespace stencil::support {

  enum class ShareItem {
    NAME, LINK_NAME, LINK_SOURCE, LINK_SERVER, IMAGE_CURRENT, IMAGE_TINT, IMAGE_ORIGINAL, LAYOUT, PROJECT,
  };

  struct ShareRow {
    ShareItem item;
    const char* icon;
    const char* label;
    const char* variant = "";   // a picture row's CanvasScene::renderToImage variant; "" = not a picture
  };
  // In ShareItem order: shareRow indexes it.
  inline constexpr ShareRow SHARE_ROWS[] = {
      {ShareItem::NAME, "clipboard", "Project name"},
      {ShareItem::LINK_NAME, "link", "Project link"},
      {ShareItem::LINK_SOURCE, "globe", "Image link (source URL)"},
      {ShareItem::LINK_SERVER, "server", "Server project link"},
      {ShareItem::IMAGE_CURRENT, "image", "Current", "current"},
      {ShareItem::IMAGE_TINT, "palette", "No lines", "tint"},
      {ShareItem::IMAGE_ORIGINAL, "layers", "Original", "original"},
      {ShareItem::LAYOUT, "file-text", "Layout (JSON)"},
      {ShareItem::PROJECT, "save", "Project (.stencil)"},
  };

  constexpr const ShareRow& shareRow(ShareItem item) { return SHARE_ROWS[static_cast<std::size_t>(item)]; }
  constexpr bool isImageShare(ShareItem item) { return shareRow(item).variant[0] != '\0'; }
  constexpr bool shareRowsInOrder() {
    for (std::size_t i = 0; i < std::size(SHARE_ROWS); ++i)
      if (static_cast<std::size_t>(SHARE_ROWS[i].item) != i) return false;
    return true;
  }
  static_assert(shareRowsInOrder());

  // A project's name and each stencil:// link it has, by item. A server project's link opens it there,
  // so its name link — which would look for a local project of that name — is left out.
  inline QHash<ShareItem, QString> projectShareTexts(const QString& name, const QString& source,
                                                     const QString& serverUrl, const QString& remoteId) {
    static constexpr ShareItem LINK_ITEMS[] = {ShareItem::LINK_NAME, ShareItem::LINK_SOURCE, ShareItem::LINK_SERVER};
    const bool onServer = !serverUrl.isEmpty() && !remoteId.isEmpty();
    QHash<ShareItem, QString> text;
    if (!name.trimmed().isEmpty()) text.insert(ShareItem::NAME, name.trimmed());
    for (const auto& link : gui::deepLink::projectShareLinks(onServer ? QString() : name, source, serverUrl, remoteId))
      text.insert(LINK_ITEMS[static_cast<std::size_t>(link.kind)], link.url);
    return text;
  }

  // The offered rows into `menu` in table order: the image variants when `images`, else the rest.
  inline void addShareRows(QMenu& menu, const QColor& ink, int iconSize, bool images,
                           const std::function<bool(ShareItem)>& offered,
                           const std::function<void(ShareItem)>& pick) {
    for (const ShareRow& row : SHARE_ROWS) {
      if (isImageShare(row.item) != images || !offered(row.item)) continue;
      QAction* a = menu.addAction(gui::themedIcon(QString::fromLatin1(row.icon), ink, iconSize),
                                  QString::fromUtf8(row.label));
      QObject::connect(a, &QAction::triggered, &menu, [pick, item = row.item] { pick(item); });
    }
  }

  // A flyout under `parent` growing its dust from its own row on `dustMs`: "Copy entries" holds the offered
  // name, links, layout and project; "Copy image" the three picture variants.
  inline QMenu* addShareRowsMenu(QMenu& parent, const QColor& ink, int iconSize, int dustMs, bool images,
                                 const std::function<bool(ShareItem)>& offered,
                                 const std::function<void(ShareItem)>& pick) {
    auto* sub = new QMenu(images ? QStringLiteral("Copy image") : QStringLiteral("Copy entries"), &parent);
    QAction* opener = parent.addMenu(sub);
    opener->setIcon(gui::themedIcon(images ? QStringLiteral("image") : QStringLiteral("copy"), ink, iconSize));
    revealSubmenu(*sub, parent, *opener, dustMs);
    addShareRows(*sub, ink, iconSize, images, offered, pick);
    return sub;
  }

}  // namespace stencil::support
