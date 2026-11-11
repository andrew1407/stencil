#include "ProjectCopy.hpp"
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "CanvasScene.hpp"
#include "mainWindowShared.hpp"
#include "Notifications.hpp"
#include "MenuShimmer.hpp"
#include "menuReveal.hpp"
#include "menuIconColumn.hpp"
#include "menuDoubleClick.hpp"
#include "shareImage.hpp"
#include "iconSet.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QHash>
#include <QJsonDocument>
#include <QMenu>
#include <QPalette>
#include <QPointer>

#include <optional>

// "Copy ›" onto the clipboard (browser ui/share/projectMenu.js): a name or a stencil:// link as it
// is; the open editor's picture as rendered; another row's original, a server row's saved render,
// its layout or its whole .stencil read only once picked. Each pick says what it copied.

namespace stencil::gui {

  namespace {
    QString variantOf(support::ShareItem item) { return QString::fromLatin1(support::shareRow(item).variant); }

    fileStore::ProjectFileData fileOf(const CopySource& src, const CopyUpload& up, const QJsonObject& layout) {
      fileStore::ProjectFileData pf;
      pf.name = QString::fromStdString(src.meta.name);
      pf.color = QString::fromStdString(src.meta.color);
      pf.description = QString::fromStdString(src.meta.description);
      for (const auto& k : src.meta.keywords) pf.keywords << QString::fromStdString(k);
      pf.source = QString::fromStdString(src.meta.source);
      pf.resource = QString::fromStdString(src.meta.resource);
      pf.blank = src.meta.blank;
      pf.blankColor = QString::fromStdString(src.meta.blankColor);
      pf.imageBytes = up.bytes;
      pf.imageExt = up.ext;
      pf.imageWidth = up.size.width();
      pf.imageHeight = up.size.height();
      pf.layout = layout;
      pf.chat = src.chat;
      return pf;
    }
  }  // namespace

  void ProjectCopy::copyToClipboard(const CopyRequest& at, support::ShareItem item, const QString& text) {
    const QString said = QString::fromUtf8(support::shareRow(item).label) + QStringLiteral(" copied");
    QPointer<QWidget> self(w.window);
    const auto put = [this, self, said](const std::function<void(QClipboard*)>& write) {
      if (!self) return;
      write(QGuiApplication::clipboard());
      if (w.notify) w.notify->success(said);
    };
    const auto fail = [this, self](const QString& why) {
      if (self && w.notify) w.notify->error(QStringLiteral("Could not copy — ") + why);
    };
    if (!text.isEmpty()) return put([text](QClipboard* cb) { cb->setText(text); });
    const bool live = at.serverUrl.isEmpty() && (at.id.isEmpty() || at.id == host.activeProjectId);
    if (live && support::isImageShare(item)) {
      const QImage img = w.canvas->renderToImage(variantOf(item));
      if (img.isNull()) return fail(QStringLiteral("there is no image"));
      return put([img](QClipboard* cb) { cb->setImage(img); });
    }
    readSource(at, [this, item, put, fail](std::optional<CopySource> src, QString why) {
      if (!src) return fail(why);
      const CopyUpload up = uploadOf(*src);
      if (support::isImageShare(item)) {
        const QImage img = renderSource(*src, QImage::fromData(up.bytes), variantOf(item));
        if (img.isNull()) return fail(QStringLiteral("the original could not be read"));
        return put([img](QClipboard* cb) { cb->setImage(img); });
      }
      const QJsonObject layout = layoutOf(*src, support::CopyScope::PROJECT, up.size.width(), up.size.height());
      const QByteArray out = item == support::ShareItem::LAYOUT ? QJsonDocument(layout).toJson(QJsonDocument::Indented)
                                                           : fileStore::buildProjectFile(fileOf(*src, up, layout));
      put([out](QClipboard* cb) { cb->setText(QString::fromUtf8(out)); });
    });
  }

  // A project that is not open, drawn off-screen by the editor's own scene: turned, mirrored, cropped, under
  // its filter (a server row's own, else this window's) and, for "current", this window's show settings.
  QImage ProjectCopy::renderSource(const CopySource& src, const QImage& original, const QString& variant) const {
    if (original.isNull()) return QImage();
    QString filter = w.settings.imageFilter, tint = w.settings.filterColor;
    if (!src.layout.isEmpty()) parseLayoutFilter(src.layout, tint, filter, tint);
    CanvasScene scene;
    scene.loadFromImage(original, src.crop, src.quarters, src.mirrored);
    scene.setLines(src.lines);
    scene.setShowPoints(w.settings.showPoints);
    scene.setShowLines(w.settings.showLines);
    scene.setImageFilter(filter, QColor(tint));
    return scene.renderToImage(variant);
  }

  QHash<support::ShareItem, QString> ProjectCopy::liveText() {
    const auto& link = w.remote.session->getLink();
    return support::projectShareTexts(liveName(), host.docSource.currentSource, link.address, link.id);
  }

  // The share button's menu (browser ui/share/projectMenu.js shareMenuItems): Image › Current / No
  // lines / Original, then each link; a click shares (`shareImage` for a picture), a double click copies.
  void ProjectCopy::showShareMenu(QWidget* anchor, const QPoint& globalPos,
                                  const std::function<void(const QString&)>& shareImage) {
    if (!w.canvas->hasImage()) return;
    const QHash<support::ShareItem, QString> text = liveText();
    const QColor ink = w.window->palette().color(QPalette::WindowText);
    QMenu menu(w.window);
    auto* twice = new support::MenuDoubleClick(&menu);
    QMenu* images = menu.addMenu(themedIcon(QStringLiteral("image"), ink, 15), QStringLiteral("Image"));
    support::revealSubmenu(*images, menu, *images->menuAction(), support::CONTEXT_MENU_DUST_MS);
    twice->watch(images);
    QPointer<QWidget> at(anchor);
    for (const support::ShareRow& row : support::SHARE_ROWS) {
      const support::ShareItem item = row.item;
      const bool image = support::isImageShare(item);
      if (!image && (item == support::ShareItem::NAME || !text.contains(item))) continue;
      QAction* a = (image ? images : &menu)->addAction(themedIcon(QString::fromLatin1(row.icon), ink, 15),
                                                       QString::fromUtf8(row.label));
      const QString url = text.value(item);
      const auto copy = [this, item, url] { copyToClipboard(CopyRequest{}, item, url); };
      if (image) twice->add(a, [shareImage, v = variantOf(item)] { shareImage(v); }, copy);
      else twice->add(a, [this, at, url, copy] { if (!support::showShareLinkSheet(at, url, liveName())) copy(); }, copy);
    }
    support::MenuShimmer shimmer(&menu);
    gui::compactIconMenu(menu);
    gui::compactIconMenu(*images);
    if (anchor) support::revealMenuFrom(menu, anchor);
    menu.exec(globalPos);
  }

  void ProjectCopy::showNameMenu(QWidget* anchor, const QPoint& globalPos) {
    const QHash<support::ShareItem, QString> text = liveText();
    if (text.isEmpty()) return;
    QMenu menu(w.window);
    std::optional<support::ShareItem> picked;
    support::addShareRows(menu, w.window->palette().color(QPalette::WindowText), 15, /*images=*/false,
                          [&text](support::ShareItem item) { return text.contains(item); },
                          [&picked](support::ShareItem item) { picked = item; });
    support::MenuShimmer shimmer(&menu);
    gui::compactIconMenu(menu);
    if (anchor) support::revealMenuFrom(menu, anchor);
    menu.exec(globalPos);
    if (picked) copyToClipboard(CopyRequest{}, *picked, text.value(*picked));
  }

}  // namespace stencil::gui
