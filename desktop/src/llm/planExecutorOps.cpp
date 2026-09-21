#include "planExecutor.hpp"

#include "CanvasWidget.hpp"
#include "opRegistry.hpp"

#include <QColor>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace stencil::llm {

  bool PlanTarget::extractFrames(const QVector<int>&, QString* err) {
    if (err) *err = QStringLiteral("frame: video frames are not available here");
    return false;
  }

  bool PlanTarget::openSourceFrame(const QString&, int, QString* err) {
    if (err) *err = QStringLiteral("frame: video frames are not available here");
    return false;
  }

  /* §7 revert: a crop rect is absolute in rotated-original px, so re-applying the earlier one
   * IS the revert; an empty rect means "was never cropped", i.e. the whole original. */
  void PlanTarget::restoreEdit(const EditState& state) {
    if (!state.valid) return;
    EditState now;
    const bool known = captureEdit(now);
    core::CropRect crop = state.crop;
    if (crop.width <= 0 || crop.height <= 0) {
      const QSize full = effectiveOriginalSize();
      crop = {0, 0, static_cast<double>(full.width()), static_cast<double>(full.height())};
    }
    if (!known || now.crop.x != crop.x || now.crop.y != crop.y ||
        now.crop.width != crop.width || now.crop.height != crop.height)
      applyCropRect(crop);
    if (!known || now.filterMode != state.filterMode || now.filterTint != state.filterTint)
      setImageFilter(state.filterMode, state.filterTint);
    commitLayoutLines(state.lines);
  }

  void PlanTarget::setPageCustom(double, double) {}
  void PlanTarget::setDefaultLineStyle(const Action&) {}
  void PlanTarget::setViewVisibility(int, int) {}

  int PlanTarget::stepHistory(bool, int) { return -1; }

  void PlanTarget::setAccentPreset(const QString&, QString* note) {
    if (note) *note = QStringLiteral("accent presets are not available here");
  }

  bool PlanTarget::copyLayout(QString* err) {
    if (err) *err = QStringLiteral("copy: the layout clipboard is not available here");
    return false;
  }

  bool PlanTarget::setCompare(const QString&, double, QString* err) {
    if (err) *err = QStringLiteral("compare: the compare view is not available here");
    return false;
  }

  bool PlanTarget::setZoom(int, bool, QString* err) {
    if (err) *err = QStringLiteral("zoom: the view zoom is not available here");
    return false;
  }

  bool PlanTarget::renameActiveProject(const QString&, QString* note) {
    if (note) *note = QStringLiteral("renameProject: managing projects is not available here");
    return false;
  }

  bool PlanTarget::setProjectColor(const QString&, QString* note) {
    if (note) *note = QStringLiteral("projectColor: managing projects is not available here");
    return false;
  }

  bool PlanTarget::openProjectNamed(const QString&, bool, QString* note) {
    if (note) *note = QStringLiteral("openProject: managing projects is not available here");
    return false;
  }

  bool PlanTarget::setIncognito(bool, QString* note) {
    if (note) *note = QStringLiteral("incognito: not available here");
    return false;
  }

  bool PlanTarget::setChatPlacement(int, const QString&, QString* note) {
    if (note) *note = QStringLiteral("chatPanel: there is no assistant panel here");
    return false;
  }

  bool PlanTarget::openDialog(const QString&, QString* note) {
    if (note) *note = QStringLiteral("dialog: there are no windows to open here");
    return false;
  }

  bool PlanTarget::connectServer(const QString&, QString* err) {
    if (err) *err = QStringLiteral("connect: not available here");
    return false;
  }

  bool PlanTarget::copyImage(QString* err) {
    if (err) *err = QStringLiteral("copy: not available here");
    return false;
  }

  bool PlanTarget::disconnectServer(const QString&, QString* err) {
    if (err) *err = QStringLiteral("disconnect: not available here");
    return false;
  }

  bool PlanTarget::removeProjectNamed(const QString&, bool, QString* note) {
    if (note) *note = QStringLiteral("removeProject: managing projects is not available here");
    return false;
  }

  bool PlanTarget::clearProjects(bool, QString* note) {
    if (note) *note = QStringLiteral("clearProjects: managing projects is not available here");
    return false;
  }

  bool PlanTarget::openUrl(const QString&, bool, QString* err) {
    if (err) *err = QStringLiteral("openUrl: not available here");
    return false;
  }

  bool PlanTarget::openFile(const QString&, QString* err) {
    if (err) *err = QStringLiteral("openFile: reading local files is not available here");
    return false;
  }

  bool PlanTarget::clearChat(QString* note) {
    if (note) *note = QStringLiteral("clearChat: there is no conversation to clear here");
    return false;
  }

  bool PlanTarget::loadAttachment(int, QString* err) {
    if (err) *err = QStringLiteral("this surface cannot switch to attached images");
    return false;
  }

  bool PlanTarget::saveProject(const QString&, const QString&, QString* err) {
    if (err) *err = QStringLiteral("save: saving projects is not available here");
    return false;
  }

  QString resolveServerRef(const QString& ref, const QStringList& saved) {
    const QString want = ref.trimmed();
    if (want.isEmpty()) return QString();
    for (const QString& s : saved)  // exact URL first
      if (QString::compare(s, want, Qt::CaseInsensitive) == 0) return s;
    const QString host = QUrl::fromUserInput(want).host();
    if (host.isEmpty()) return QString();
    QString found;
    for (const QString& s : saved) {
      if (QUrl(s).host().compare(host, Qt::CaseInsensitive) == 0) {
        if (!found.isEmpty()) return QString();  // ambiguous host — refuse
        found = s;
      }
    }
    return found;
  }

  void CanvasPlanTarget::setPageCustom(double widthCm, double heightCm) {
    pageCustomW = widthCm;
    pageCustomH = heightCm;
    page = {widthCm, heightCm};
    canvas->setPageCm(page.width, page.height);
  }

  int CanvasPlanTarget::stepHistory(bool redo, int steps) {
    int done = 0;
    for (; done < steps; ++done) {
      if (redo ? !canvas->canRedo() : !canvas->canUndo()) break;
      if (redo) canvas->redo();
      else canvas->undo();
    }
    return done;
  }

  void CanvasPlanTarget::setDefaultLineStyle(const Action& a) {
    if (!a.color.isEmpty()) lsColor = a.color;
    if (a.thickness > 0) lsThickness = a.thickness;
    if (a.pointSize > 0) lsPointSize = a.pointSize;
    if (!a.style.isEmpty()) lsStyle = a.style;
    if (a.pointColorSet) {
      lsPointColor = a.pointColor;
      lsPointColorSet = true;
    }
    if (!a.drawMode.isEmpty()) lsDrawMode = a.drawMode;
  }

  void CanvasPlanTarget::setViewVisibility(int points, int lines) {
    if (points >= 0) viewPoints = points;
    if (lines >= 0) viewLines = lines;
  }

  bool CanvasPlanTarget::setCompare(const QString& mode, double split, QString*) {
    compareMode = mode;
    canvas->setCompareMode(mode);
    if (split > 0) {
      compareSplit = split;
      canvas->setCompareSplit(split);
    }
    return true;
  }

  bool CanvasPlanTarget::setZoom(int percent, bool fit, QString*) {
    zoomPercent = percent;
    zoomFit = fit;
    if (!fit && percent > 0) canvas->setScale(percent / 100.0);
    return true;
  }
}  // namespace stencil::llm

