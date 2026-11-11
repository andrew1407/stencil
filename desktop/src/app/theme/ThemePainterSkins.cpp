// The skins' words (browser js/ui/skin/session.js + adopt.js + scene.js): one skin at a time, on and off,
// and the picture an empty editor gets with its word. The word stores its look as the user's own (webcore:
// still and Gray, aqua: Sliding and Blue); a SkinShow is what one skin differs in.
#include "MainWindow.hpp"
#include "ThemePainter.hpp"
#include "ToolbarBuilder.hpp"
#include "CanvasWidget.hpp"
#include "ChatPlanTarget.hpp"
#include "ConnectDialog.hpp"
#include "fileStore.hpp"
#include "SiblingWindows.hpp"
#include "SharedState.hpp"
#include "../../support/aqua/look.hpp"
#include "../../support/motionPrefs.hpp"
#include "../../support/skin/skinAppliers.hpp"
#include "../../support/aqua/table.hpp"
#include "../../support/aqua/wallpaper.hpp"
#include "../../support/skinPrefs.hpp"
#include "../../support/webcore/image.hpp"
#include "../../support/webcore/rules.hpp"

#include <QApplication>
#include <QLabel>
#include <QToolButton>
#include <utility>

namespace stencil::gui {

  struct SkinShow {
    support::Skin skin;
    QLatin1String name;         // the key fileStore::saveSkin keeps
    QLatin1String motionMode;   // the look the word stores: motion key, drawing switch (unset = left alone), preset
    std::optional<bool> drawing;
    QLatin1String accent;
    void (*applyLook)(bool isOn);
    QString (*projectName)();
    QImage (*paint)();
    QVector<support::WordLine> (*words)(int w, int h);
    void (ThemePainter::*wear)(bool isOn);   // the window's own share, before the restyle
  };

  namespace {
    // A hovered autoRaise button draws QIcon::Active, which under webcore carries the menu
    // row's halo (iconSet.cpp); the browser's toolbar glyph never wears one.
    void holdAutoRaise(QWidget* root, bool isSkinOn) {
      for (QToolButton* b : root->findChildren<QToolButton*>()) {
        if (isSkinOn && b->autoRaise()) { b->setProperty("wcAutoRaise", true); b->setAutoRaise(false); }
        else if (!isSkinOn && b->property("wcAutoRaise").toBool()) { b->setAutoRaise(true); b->setProperty("wcAutoRaise", {}); }
      }
    }

    // The show on now, so the next one can shed it without a restyle of its own.
    const SkinShow*& wornShow() { static const SkinShow* show = nullptr; return show; }
    // What the word's look replaced, given back by an off in the same process (browser adopt.js).
    struct Replaced { const SkinShow* show; QString motionMode; bool drawing; QString accent; };
    std::optional<Replaced>& replaced() { static std::optional<Replaced> r; return r; }
  }  // namespace

  void ThemePainter::wearWebcore(bool isOn) { holdAutoRaise(&w, isOn); }

  bool ThemePainter::toggleWebcore(bool entered) {
    static const SkinShow show{support::Skin::WEBCORE, QLatin1String("webcore"), QLatin1String("none"), false,
                               QLatin1String("grey"),
                               &support::applyWebcoreLook,
                               [] { return support::webcoreConfig().projectName; },
                               &support::paintWebcoreImage, &support::wordLines, &ThemePainter::wearWebcore};
    return toggleSkin(show, entered);
  }

  bool ThemePainter::toggleAqua(bool entered) {
    static const SkinShow show{support::Skin::AQUA, QLatin1String("aqua"), QLatin1String("slide"), std::nullopt,
                               QLatin1String(support::AQUA_ACCENT),
                               &support::applyAquaLook,
                               [] { return support::aquaConfig().projectName; },
                               &support::paintAquaImage, &support::aquaWordLines, nullptr};
    return toggleSkin(show, entered);
  }

  void ThemePainter::restoreSkin() {
    if (std::exchange(support::skinRestored(), true)) return;   // a later window follows the process's skin
    const QString name = fileStore::loadSkin();
    if (name == QLatin1String("webcore")) toggleWebcore(/*entered=*/false);
    else if (name == QLatin1String("aqua")) toggleAqua(/*entered=*/false);
  }

  // One skin at a time: the one on is shed without a restyle of its own, the new look's covers both.
  // `entered` (the word) stores the skin's look as the user's and opens the scene; a relaunch wears what is stored.
  bool ThemePainter::toggleSkin(const SkinShow& show, bool entered) {
    const bool isOn = support::skin() != show.skin;
    if (!isOn) shedSkin(show);
    else {
      if (wornShow() && support::skin() == wornShow()->skin) shedSkin(*wornShow());
      support::setSkin(show.skin);
      show.applyLook(true);
      if (show.wear) (this->*show.wear)(true);
      wornShow() = &show;
      if (entered) adoptSkinLook(show);
      else replaced().reset();   // a relaunch has nothing to give back
    }
    dressWindows(isOn);
    fileStore::saveSkin(isOn ? QString(show.name) : QString());
    if (!isOn || !entered || w.canvas->hasImage()) return isOn;
    if (!openSkinProject(show.projectName())) skinScene(show);
    return isOn;
  }

  // The stored switches back in force, the look and the window's own share undone, what the word's
  // look replaced given back; no restyle.
  void ThemePainter::shedSkin(const SkinShow& show) {
    support::setSkin(support::Skin::DEFAULT);
    support::clearMotionOverride();
    show.applyLook(false);
    if (show.wear) (this->*show.wear)(false);
    wornShow() = nullptr;
    giveBackSkinLook(show);
  }

  // The skin is the process's: every open window dresses, this one last, as the app sheet ends on it.
  void ThemePainter::dressWindows(bool isOn) {
    for (MainWindow* other : SiblingWindows::windows())
      if (other != &w) other->parts.theme.dressWindow(isOn);
    dressWindow(isOn);
    restyleConnectionRows();
  }

  // One window's share: the faces re-measured, the full restyle, and every per-widget sheet rebuilt
  // with it (applyTheme re-issues them; off, the stored switches and theme are back in force).
  void ThemePainter::dressWindow(bool isOn) {
    unpinFaceWidths();
    for (QLabel* caption : w.findChildren<QLabel*>(QStringLiteral("sectionLabel"))) styleSectionCaption(caption);
    w.painted.done = false;
    if (isOn) w.applyTheme();
    else w.applySettings(w.settings, /*persist=*/false);
    w.refreshActions();
  }

  // The Start/Stop and Line/Rect faces are pinned to their widest word (MainWindowRefresh.cpp),
  // measured in the face that was on: a new one has to measure again or the pair sits off-centre.
  void ThemePainter::unpinFaceWidths() {
    for (QToolButton* b : {w.tools.startDrawBtn, w.tools.drawModeBtn})
      if (b) { b->setMinimumWidth(0); b->setMaximumWidth(QWIDGETSIZE_MAX); }
  }

  // The word's look made the user's settings, saved as any pick is; what it replaced is kept for an off.
  void ThemePainter::adoptSkinLook(const SkinShow& show) {
    Settings next = w.settings;
    replaced() = Replaced{&show, next.motionMode, next.drawingAnimations, next.accentColor};
    next.motionMode = show.motionMode;
    if (show.drawing) next.drawingAnimations = *show.drawing;
    next.accentColor = show.accent;
    w.applySettings(next, /*persist=*/true);
  }

  // Each part of the look the user left alone goes back to what it replaced; a pick of theirs stands.
  void ThemePainter::giveBackSkinLook(const SkinShow& show) {
    std::optional<Replaced> was = std::exchange(replaced(), std::nullopt);
    if (!was || was->show != &show) return;
    Settings next = w.settings;
    if (next.motionMode == show.motionMode) next.motionMode = was->motionMode;
    if (show.drawing && next.drawingAnimations == *show.drawing) next.drawingAnimations = was->drawing;
    if (next.accentColor == show.accent) next.accentColor = was->accent;
    w.settings = next;
    if (!w.projectHost.incognito) SharedState::instance().saveSettings(next, &w);
  }

  // The connections window's rows are built from the palette when it opens, outside applyTheme.
  void ThemePainter::restyleConnectionRows() {
    for (QWidget* top : QApplication::topLevelWidgets()) {
      QList<ConnectDialog*> open = top->findChildren<ConnectDialog*>();
      if (auto* d = qobject_cast<ConnectDialog*>(top)) open << d;
      for (ConnectDialog* d : open) d->restyleRows();
    }
  }

  // The local project the skin named before, reopened; false without one.
  bool ThemePainter::openSkinProject(const QString& name) {
    const QString want = name.trimmed().toLower();
    for (const Project& p : w.projectHost.projectList) {
      const QString id = QString::fromStdString(p.meta.id);
      if (QString::fromStdString(p.meta.name).trimmed().toLower() != want || w.projectHost.projectOpenInOtherWindow(id))
        continue;
      if (w.projectHost.incognito) w.acts.incognito->setChecked(false);
      if (w.projectHost.loadProjectIntoCanvas(id)) return true;
    }
    return false;
  }

  // Mirrors createBlankImage: a synthetic picture with no provenance, the word as one step on
  // the user's own history, and a local project under the skin's name.
  void ThemePainter::skinScene(const SkinShow& show) {
    if (w.projectHost.incognito) w.acts.incognito->setChecked(false);
    if (w.settings.imageFilter != QLatin1String("none")) w.applyImageFilter("none", /*asUndoStep=*/false);
    w.projectHost.activeProjectId.clear();
    w.canvas->loadFromImage(show.paint());
    w.projectHost.docSource.setBytes({}, {});
    w.projectHost.docSource.currentSource.clear();
    w.projectHost.docSource.currentResource.clear();
    w.projectHost.docSource.blankColor.clear();
    w.canvas->setBlankPage(false);
    const QSize picture = w.canvas->getImage().size();
    core::Lines lines = w.canvas->getLines();
    for (const support::WordLine& wl : show.words(picture.width(), picture.height())) {
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
    w.projectHost.createLocalProject(show.projectName(), /*announce=*/false);
  }

}  // namespace stencil::gui
