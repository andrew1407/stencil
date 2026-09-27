// The webcore word (browser js/ui/webcore/toggle.js + scene.js): the session skin on and off,
// and the picture an empty editor gets with it.
#include "MainWindow.hpp"
#include "ThemePainter.hpp"
#include "CanvasWidget.hpp"
#include "ChatPlanTarget.hpp"
#include "../../support/webcore/image.hpp"
#include "../../support/webcore/look.hpp"
#include "../../support/webcore/rules.hpp"
#include "../../support/skinPrefs.hpp"

#include <QToolButton>

namespace stencil::gui {

  namespace {
    // A hovered autoRaise button draws QIcon::Active, which under the skin carries the menu
    // row's halo (iconSet.cpp); the browser's toolbar glyph never wears one.
    void holdAutoRaise(QWidget* root, bool skin) {
      for (QToolButton* b : root->findChildren<QToolButton*>()) {
        if (skin && b->autoRaise()) { b->setProperty("wcAutoRaise", true); b->setAutoRaise(false); }
        else if (!skin && b->property("wcAutoRaise").toBool()) { b->setAutoRaise(true); b->setProperty("wcAutoRaise", {}); }
      }
    }
  }  // namespace

  // The Start/Stop and Line/Rect faces are pinned to their widest word (MainWindowRefresh.cpp),
  // measured in the face that was on: a new one has to measure again or the pair sits off-centre.
  void ThemePainter::unpinFaceWidths() {
    for (QToolButton* b : {w.tools.startDrawBtn, w.tools.drawModeBtn})
      if (b) { b->setMinimumWidth(0); b->setMaximumWidth(QWIDGETSIZE_MAX); }
  }

  bool ThemePainter::toggleWebcore() {
    const bool on = !support::isWebcore();
    support::setSkin(on ? support::Skin::WEBCORE : support::Skin::DEFAULT);
    // Still lines and a still interface for the session, written to no file (browser toggle.js).
    if (on) support::setMotionOverride({support::MotionMode::NONE, false, support::modalBackdrop()});
    else support::clearMotionOverride();
    support::applyWebcoreLook(on);
    holdAutoRaise(&w, on);
    unpinFaceWidths();
    w.painted.done = false;   // the full restyle, palette and icons included
    if (on) w.applyTheme();
    else w.applySettings(w.settings, /*persist=*/false);   // the stored switches and theme, back in force
    w.refreshActions();   // …and the faces re-pin to the skin's own type
    if (!on || w.canvas->hasImage()) return on;
    const QString want = support::webcoreConfig().projectName.trimmed().toLower();
    for (const Project& p : w.projectList) {
      const QString id = QString::fromStdString(p.meta.id);
      if (QString::fromStdString(p.meta.name).trimmed().toLower() != want || w.projectOpenInOtherWindow(id))
        continue;
      if (w.incognito) w.acts.incognito->setChecked(false);
      if (w.loadProjectIntoCanvas(id)) return on;
    }
    webcoreScene();
    return on;
  }

  // Mirrors createBlankImage: a synthetic picture with no provenance, the word as one step on
  // the user's own history, and a local project under the skin's name.
  void ThemePainter::webcoreScene() {
    const support::WebcoreConfig& cfg = support::webcoreConfig();
    if (w.incognito) w.acts.incognito->setChecked(false);
    if (w.settings.imageFilter != QLatin1String("none")) w.applyImageFilter("none", /*asUndoStep=*/false);
    w.activeProjectId.clear();
    w.canvas->loadFromImage(support::paintWebcoreImage());
    w.docSource.setBytes({}, {});
    w.docSource.currentSource.clear();
    w.docSource.currentResource.clear();
    w.docSource.blankColor.clear();
    w.canvas->setBlankPage(false);
    const QSize picture = w.canvas->getImage().size();
    core::Lines lines = w.canvas->getLines();
    for (const support::WordLine& wl : support::wordLines(picture.width(), picture.height())) {
      core::Line line;
      line.locked = true;
      line.color = wl.color.toStdString();
      line.fillColor = wl.fillColor.toStdString();
      line.thickness = wl.thickness;
      for (const QPointF& p : wl.points) line.points.push_back(core::Point{p.x(), p.y()});
      lines.push_back(line);
    }
    ChatPlanTarget(w).commitLayoutLines(lines);
    w.refreshActions();
    w.fitToWindow();
    w.createLocalProject(cfg.projectName, /*announce=*/false);
  }

}  // namespace stencil::gui
