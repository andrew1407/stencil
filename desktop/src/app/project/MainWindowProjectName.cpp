#include "MainWindow.hpp"
#include <QLabel>
#include "../../support/skinPrefs.hpp"
#include "CanvasWidget.hpp"
#include "guiHelpers.hpp"
#include "theme.hpp"

#include <algorithm>

// The project's name: what it reads, what a rename accepts, and the incognito tag the image bar
// shows. The window title is ProjectTitleController's.

namespace stencil::gui {


  QString MainWindow::activeProjectName() const {
    if (activeProjectId.isEmpty()) return {};
    for (const auto& p : projectList)
      if (QString::fromStdString(p.meta.id) == activeProjectId)
        return QString::fromStdString(p.meta.name);
    return {};
  }

  QString MainWindow::projectBaseName() const {
    const QString n = activeProjectName();
    if (!n.isEmpty()) return n;   // the project name IS the download name
    return canvas ? canvas->imageBaseName() : QStringLiteral("image");
  }

  core::ProjectsStore::NameCheck MainWindow::checkProjectName(
      const QString& name, const QString& exceptId) const {
    std::vector<core::ProjectMeta> metas;
    for (const auto& p : projectList) metas.push_back(p.meta);
    core::ProjectsStore store;   // local; never disturbs projectsStore
    store.load(metas);
    return store.validateName(name.toStdString(), exceptId.toStdString());
  }


  // Mirrors the browser's #image-info bar. The incognito half is the toolbar's own glyph (an emoji
  // took the font's colour); divider and tag are one unit, so no dangling separator.
  QString MainWindow::incognitoTagHtml() const {
    const Palette pal = themePalette(resolveDark(settings.themeMode), settings.accentColor);
    const int glyphPx = std::max(12, QFontMetrics(tools.imageSizeInfo->font()).height() - 2);
    // The skin's accent is its navy, lost on the dark face: there the tag takes the bar's ink.
    const QColor tagInk = support::isWebcore() ? pal.textMain : pal.accent;
    return QStringLiteral("<span style=\"color:%1;\">|</span>&nbsp;&nbsp;"
                          "%2<span style=\"color:%3;font-weight:700;vertical-align:middle;\">"
                          "&nbsp;Incognito &mdash; not saved</span>")
        .arg(pal.textMuted.name(),
             inlineIconHtml("incognito", tagInk, glyphPx,
                            QStringLiteral("vertical-align:middle")),
             tagInk.name());
  }

}  // namespace stencil::gui
