// The session skins' words (browser js/ui/skin/session.js + scene.js): one skin at a time, on and off,
// and the picture an empty editor gets with its word. webcore stills the interface, aqua slides it and
// wears the Blue preset; a SkinShow is what one skin differs in.
#include "MainWindow.hpp"
#include "ThemePainter.hpp"
#include "ToolbarBuilder.hpp"
#include "CanvasWidget.hpp"
#include "ChatPlanTarget.hpp"
#include "ConnectDialog.hpp"
#include "SiblingWindows.hpp"
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

namespace stencil::gui {

  struct SkinShow {
    support::Skin skin;
    support::MotionSwitches (*motion)();   // the session switches laid over the stored ones
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
  }  // namespace

  void ThemePainter::wearWebcore(bool isOn) { holdAutoRaise(&w, isOn); }
  void ThemePainter::wearAqua(bool isOn) { wearSessionAccent(isOn); }

  bool ThemePainter::toggleWebcore() {
    static const SkinShow show{support::Skin::WEBCORE,
                               [] { return support::MotionSwitches{support::MotionMode::NONE, false}; },
                               &support::applyWebcoreLook,
                               [] { return support::webcoreConfig().projectName; },
                               &support::paintWebcoreImage, &support::wordLines, &ThemePainter::wearWebcore};
    return toggleSkin(show);
  }

  bool ThemePainter::toggleAqua() {
    static const SkinShow show{support::Skin::AQUA,
                               [] { return support::MotionSwitches{support::MotionMode::SLIDE, support::drawingAnimations()}; },
                               &support::applyAquaLook,
                               [] { return support::aquaConfig().projectName; },
                               &support::paintAquaImage, &support::aquaWordLines, &ThemePainter::wearAqua};
    return toggleSkin(show);
  }

  // One skin at a time: the one on is shed without a restyle of its own, the new look's covers both.
  bool ThemePainter::toggleSkin(const SkinShow& show) {
    const bool isOn = support::skin() != show.skin;
    if (!isOn) shedSkin(show);
    else {
      if (wornShow() && support::skin() == wornShow()->skin) shedSkin(*wornShow());
      support::setSkin(show.skin);
      support::setMotionOverride(show.motion());
      show.applyLook(true);
      (this->*show.wear)(true);
      wornShow() = &show;
    }
    dressWindows(isOn);
    if (!isOn || w.canvas->hasImage()) return isOn;
    if (!openSkinProject(show.projectName())) skinScene(show);
    return isOn;
  }

  // The stored switches back in force, the look and the window's own share undone; no restyle.
  void ThemePainter::shedSkin(const SkinShow& show) {
    support::setSkin(support::Skin::DEFAULT);
    support::clearMotionOverride();
    show.applyLook(false);
    (this->*show.wear)(false);
    wornShow() = nullptr;
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

  // On, the Blue preset for the session in every window and the user's own set aside; off, theirs
  // back unless they picked one meanwhile. Neither writes: the file holds their own (browser wearBlue/giveBack).
  void ThemePainter::wearSessionAccent(bool isOn) {
    std::optional<QString>& aside = support::setAsideAccent();
    static const QString BLUE = QString::fromLatin1(support::AQUA_ACCENT);
    QList<MainWindow*> all = SiblingWindows::windows();
    if (!all.contains(&w)) all << &w;
    if (!isOn) {
      if (aside) for (MainWindow* m : all) m->settings.accentColor = *aside;
      aside.reset();
      return;
    }
    aside.reset();
    if (w.settings.accentColor != BLUE) aside = w.settings.accentColor;
    for (MainWindow* m : all) m->settings.accentColor = BLUE;
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
