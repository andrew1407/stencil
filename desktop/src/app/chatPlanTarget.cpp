#include "chatPlanTarget.hpp"

#include "mainWindow.hpp"
#include "../support/modalChrome.hpp"  // confirmModal — the browser-styled question
#include "mainWindowHelpers.hpp"
#include "dataExportController.hpp"
#include "remoteSession.hpp"
#include "../canvas/canvasWidget.hpp"
#include "../net/connectionStore.hpp"
#include "../net/serverClient.hpp"
#include "../support/displayName.hpp"
#include "../support/notifications.hpp"
#include "../support/theme.hpp"
#include "colorNames.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QJsonObject>
#include <QLineEdit>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>

namespace stencil::gui {

  bool ChatPlanTarget::hasImage() const { return w_.canvas_->hasImage(); }
  bool ChatPlanTarget::isVideoInput() const { return !w_.chatVideoPath_.isEmpty(); }
  QSize ChatPlanTarget::effectiveOriginalSize() const {
    return w_.canvas_->effectiveOriginalImage().size();
  }
  QSize ChatPlanTarget::workingSize() const { return w_.canvas_->image().size(); }
  core::PageSize ChatPlanTarget::pageCm() const {
    return naturalPageCm(w_.pageSizeValue(), w_.settings_.customPageWidth,
                         w_.settings_.customPageHeight);
  }
  bool ChatPlanTarget::applyCropRect(const core::CropRect& rect) {
    const core::PageSize page = pageCm();
    w_.canvas_->setPageCm(page.width, page.height);
    w_.canvas_->applyCrop(rect, /*recalc=*/true);
    return true;
  }
  void ChatPlanTarget::rotateQuarter(bool clockwise) { w_.canvas_->rotateImage(clockwise); }
  void ChatPlanTarget::setImageFilter(const QString& mode, const QString& tintHex) {
    if (!tintHex.isEmpty()) w_.applyTintColor(QColor(tintHex));
    w_.applyImageFilter(mode);  // syncs toolbar combo + context radios + persists
  }
  void ChatPlanTarget::setLayoutLines(const core::Lines& lines) {
    w_.canvas_->setLines(lines);
  }
  void ChatPlanTarget::setFormula(QChar axis, const QString& expr) {
    // Clearing an axis ("" = identity) must not switch formulas ON.
    if (!expr.isEmpty() && w_.allowFormulas_ && !w_.allowFormulas_->isChecked())
      w_.allowFormulas_->setChecked(true);  // shows the inputs + persists
    QLineEdit* edit = axis == QLatin1Char('x') ? w_.formulaX_ : w_.formulaY_;
    if (!edit) return;
    edit->setText(expr);
    // An op-plan applies NOW: the typing-pause debounce is for a human at the
    // keyboard, and the caller expects the formula live when this returns.
    w_.validateAndApplyFormulas();
  }
  // §2 formula enabled:false/true — the allow-formulas toggle itself (the
  // toolbar checkbox is the source of truth; actAllowFormulas_ follows it).
  void ChatPlanTarget::setFormulasEnabled(bool on) {
    if (w_.allowFormulas_ && w_.allowFormulas_->isChecked() != on)
      w_.allowFormulas_->setChecked(on);  // applies + persists via its handler
  }
  void ChatPlanTarget::setPageFormat(const QString& isoName) {
    const int idx = w_.units_.pageSize->findData(isoName);
    if (idx >= 0) w_.units_.pageSize->setCurrentIndex(idx);  // fires onPageSizeChanged
  }
  // §2 page custom dims: the SAME controls the toolbar drives — the custom
  // spinboxes (edited in the active unit) + the "custom" combo entry.
  void ChatPlanTarget::setPageCustom(double widthCm, double heightCm) {
    const double f = w_.unitFormat().factor;
    if (w_.units_.customW) w_.units_.customW->setValue(widthCm * f);
    if (w_.units_.customH) w_.units_.customH->setValue(heightCm * f);
    // The spinboxes round to their display precision; keep the model exact.
    w_.settings_.customPageWidth = widthCm;
    w_.settings_.customPageHeight = heightCm;
    const int idx = w_.units_.pageSize->findData(QStringLiteral("custom"));
    if (idx >= 0) w_.units_.pageSize->setCurrentIndex(idx);
    w_.onPageSizeChanged();  // idempotent when the combo change already fired
  }
  bool ChatPlanTarget::newBlank(const QString& color, const QString& isoName,
                                double widthCm, double heightCm, QString* err) {
    // §2: explicit cm dims override the format (they become the custom page).
    if (widthCm > 0 && heightCm > 0) setPageCustom(widthCm, heightCm);
    else if (!isoName.isEmpty()) setPageFormat(isoName);
    const auto rgba = core::parseColor(color.toStdString());
    if (!rgba) {
      if (err) *err = QStringLiteral("blank: unknown colour \"%1\"").arg(color);
      return false;
    }
    const core::SizePx px = core::defaultBlankSizePx(pageCm(), 96.0);
    w_.createBlankImage(QColor(rgba->r, rgba->g, rgba->b), px.width, px.height);
    return true;
  }
  // §2 undo/redo: the canvas's own history stack (the toolbar's Undo/Redo).
  int ChatPlanTarget::stepHistory(bool redo, int steps) {
    int done = 0;
    for (; done < steps; ++done) {
      if (redo ? !w_.canvas_->canRedo() : !w_.canvas_->canUndo()) break;
      if (redo) w_.canvas_->redo();
      else w_.canvas_->undo();
    }
    if (done > 0) w_.refreshActions();
    return done;
  }
  bool ChatPlanTarget::extractFrames(const QVector<int>& indices, QString* err) {
    return w_.chatExtractFrames(indices, err);
  }

  void ChatPlanTarget::setTheme(const QString& mode) {
    Settings s = w_.settings_;
    s.themeMode = mode;
    w_.applySettings(s, true);  // the settings-dialog apply path
  }
  void ChatPlanTarget::setAccent(const QString& hex) {
    Settings s = w_.settings_;
    s.accentColor = hex;
    w_.applySettings(s, true);  // same as the logo colour picker
  }
  // §10 accent preset: the accentPresets() apply path (the preset KEY is what
  // the Settings dropdown / logo-click cycle store). Unknown name = note+skip.
  void ChatPlanTarget::setAccentPreset(const QString& preset, QString* note) {
    const QString want = preset.trimmed().toLower();
    for (const auto& p : accentPresets()) {
      if (p.key == want) {
        Settings s = w_.settings_;
        s.accentColor = p.key;
        w_.applySettings(s, true);
        return;
      }
    }
    if (note) *note = QStringLiteral("unknown accent preset \"%1\"").arg(preset);
  }
  void ChatPlanTarget::setDefaultLineStyle(const llm::Action& a) {
    if (!a.color.isEmpty()) {
      QColor c(a.color);  // hex or CSS name (validated upstream)
      if (!c.isValid())
        if (const auto rgba = core::parseColor(a.color.toStdString()))
          c = QColor(rgba->r, rgba->g, rgba->b);
      if (c.isValid()) {
        // The lineColorBtn_ click handler's body, minus the colour dialog.
        w_.lineColorValue_ = c;
        w_.updateColorSwatch(w_.lineColorBtn_, c);
        w_.settings_.defaultColor = c.name(QColor::HexRgb);
        w_.onLineStyleControlChanged();
      }
    }
    // §10 widening: pointColor ("" = follow the stroke) — the pointColorBtn_
    // handler's body, minus the colour dialog.
    if (a.pointColorSet) {
      w_.settings_.defaultPointColor = a.pointColor;
      if (w_.pointColorBtn_)
        w_.updateColorSwatch(w_.pointColorBtn_, w_.effectiveDefaultPointColor());
      w_.onLineStyleControlChanged();
    }
    // §10 widening: drawMode — the context menu's line<->rect bridge.
    if (!a.drawMode.isEmpty()) {
      w_.canvas_->setDrawMode(a.drawMode == QLatin1String("rect")
                                  ? CanvasWidget::DrawMode::Rect
                                  : CanvasWidget::DrawMode::Line);
      w_.persistSettings();
    }
    // The toolbar inputs — their valueChanged handlers persist + apply.
    if (a.thickness > 0 && w_.lineThickness_) w_.lineThickness_->setValue(a.thickness);
    if (a.pointSize > 0 && w_.pointSize_) w_.pointSize_->setValue(a.pointSize);
    if (!a.style.isEmpty() && w_.lineStyle_) {
      const int idx = w_.lineStyle_->findData(a.style);
      if (idx >= 0) w_.lineStyle_->setCurrentIndex(idx);
    }
  }
  void ChatPlanTarget::setUnits(const QString& value) { w_.applyUnits(value); }
  void ChatPlanTarget::setViewVisibility(int points, int lines) {
    if (points >= 0 && w_.actShowPoints_) w_.actShowPoints_->setChecked(points == 1);
    if (lines >= 0 && w_.actShowLines_) w_.actShowLines_->setChecked(lines == 1);
  }
  // §10 clear: the empty "Open an image" canvas the trash button leaves behind.
  // No confirmation — the plan already said so, and a modal would stall the turn.
  void ChatPlanTarget::clearImage() { w_.resetToBlankEditor(); }
  bool ChatPlanTarget::connectServer(const QString& server, QString* err) {
    // Resolve ONLY against the user's SAVED servers (exact URL, else unique host); their
    // stored token authenticates — plans never carry tokens or hosts (contract §10).
    const auto saved = stencil::net::connectionStore::loadSavedServers();
    QStringList urls;
    for (const auto& s : saved) urls << s.url;
    const QString url = llm::resolveServerRef(server, urls);
    if (url.isEmpty()) {
      if (err)
        *err = QStringLiteral(
                   "connect: unknown server \"%1\" — only a server you have "
                   "already saved can be used")
                   .arg(server);
      return false;
    }
    QString token;
    auto kind = stencil::net::ServerClient::CredentialKind::None;
    for (const auto& s : saved)
      if (s.url == url) { token = s.token; kind = stencil::net::ServerClient::kindFromTag(s.kind); break; }
    // The executor runs its ops in order, so this one waits out the handshake — the same
    // bounded local loop chatLoadSource uses, not a blocking call inside the client.
    QString cerr;
    bool done = false, ok = false;
    QEventLoop loop;
    QTimer::singleShot(20000, &loop, [&loop] { loop.quit(); });   // never hang the plan
    w_.ensureConnections()->connectToAsync(url, token, [&](bool o, QString e) {
      ok = o; cerr = std::move(e); done = true; loop.quit();
    }, kind);
    if (!done) loop.exec();
    if (!ok) {
      if (err) *err = QStringLiteral("connect: %1").arg(cerr.isEmpty() ? QStringLiteral("timed out") : cerr);
      return false;
    }
    w_.notify_->success(QStringLiteral("Connected to %1").arg(url));
    return true;
  }
  bool ChatPlanTarget::disconnectServer(const QString& server, QString* err) {
    const QStringList live = w_.connections_ ? w_.connections_->urls() : QStringList();
    const QString url = llm::resolveServerRef(server, live);
    if (url.isEmpty()) {
      if (err)
        *err = QStringLiteral("disconnect: unknown server \"%1\" — not a live connection")
                   .arg(server);
      return false;
    }
    w_.connections_->disconnectFrom(url);
    w_.notify_->info(QStringLiteral("Disconnected from %1").arg(url));
    return true;
  }

  // §10 openUrl: the user-echo guard already ran in the executor; loading rides the SAME
  // async path as the dialog's "open here", and the executor awaits it (chatLoadSource).
  bool ChatPlanTarget::openUrl(const QString& url, bool incognito, QString* err) {
    // Say what happened in OUR words — a silent download plus a vague model
    // reply reads as "nothing happened".
    w_.notify_->info(QStringLiteral("Opening %1%2")
                         .arg(url, incognito ? QStringLiteral(" (incognito)") : QString()));
    QString why;
    if (w_.chatLoadSource(url, incognito, &why)) return true;
    if (err) *err = QStringLiteral("openUrl: %1").arg(why);
    return false;
  }
  // §10 openFile: the same await, pointed at a LOCAL path the user named (the
  // executor checked the echo rule); a .stencil or .json takes its own path.
  bool ChatPlanTarget::openFile(const QString& path, QString* err) {
    return w_.chatOpenFile(path, err);
  }
  // §10 copy: the SAME path as the toolbar's "Copy Image to Clipboard"
  // (DataExportController — image + filter, no overlay; it notifies too).
  bool ChatPlanTarget::copyImage(QString*) {
    w_.dataExport_->copyImageToClipboard();
    return true;
  }
  // §10 copy what:"layout": actCopyLayout_'s DataExportController path.
  bool ChatPlanTarget::copyLayout(QString*) {
    w_.dataExport_->copyLayout();
    return true;
  }
  bool ChatPlanTarget::hasDrawnLines() const { return !w_.canvas_->lines().empty(); }
  // §10 compare: the shared setter the toolbar combo and the View submenu
  // drive (syncs canvas + both UIs); the divider fraction goes to the canvas.
  bool ChatPlanTarget::setCompare(const QString& mode, double split, QString*) {
    w_.setCompareModeUi(mode);
    if (split > 0) w_.canvas_->setCompareSplit(split);
    return true;
  }
  // §10 zoom: the zoom_ combo / Fit-to-Window paths (view-only).
  bool ChatPlanTarget::setZoom(int percent, bool fit, QString*) {
    if (fit) w_.fitToWindow();
    else w_.setZoom(percent / 100.0);
    return true;
  }
  // Shared §10 resolution: a saved LOCAL project by exact name, else unique
  // case-insensitive prefix. nullptr + *note set on a miss/ambiguity.
  const Project* ChatPlanTarget::resolveLocalProject(const QString& name,
                                                     QString* note) const {
    std::vector<const Project*> exact, prefixed;
    const std::string want = name.toStdString();
    for (const auto& p : w_.projectList_) {
      if (p.meta.name == want) exact.push_back(&p);
      else if (QString::fromStdString(p.meta.name)
                   .startsWith(name, Qt::CaseInsensitive))
        prefixed.push_back(&p);
    }
    const auto& picks = exact.empty() ? prefixed : exact;
    if (picks.empty()) {
      *note = QStringLiteral("no saved project named \"%1\"").arg(name);
      return nullptr;
    }
    if (picks.size() > 1) {
      *note = QStringLiteral("\"%1\" matches %2 projects — use the full name")
                  .arg(name).arg(picks.size());
      return nullptr;
    }
    return picks.front();
  }
  // §10 removeProject: resolve among the saved LOCAL projects (or the ACTIVE one for
  // current:true, falling back to `clear` when nothing is saved but an image is open),
  // then the projects dialog's Delete flow. A miss/decline is a note, not a failure.
  bool ChatPlanTarget::removeProjectNamed(const QString& name, bool current,
                                          QString* note) {
    QString id, nm;
    if (current) {
      if (w_.activeProjectId_.isEmpty()) {
        if (!w_.canvas_->hasImage()) {
          *note = QStringLiteral("no saved project is open right now");
          return true;
        }
        ConfirmSpec spec;
        spec.title = "Remove image";
        spec.message = QString("Nothing is saved here — remove this editor's image and "
                               "its lines?");
        spec.confirmIcon = QStringLiteral("trash");
        spec.danger = true;
        if (!confirmModal(&w_, spec)) {
          *note = QStringLiteral("removal canceled");
          return true;
        }
        w_.resetToBlankEditor();
        w_.notify_->success("Editor cleared");
        return true;
      }
      id = w_.activeProjectId_;
      nm = support::shortName(w_.activeProjectName());
    } else {
      const Project* pick = resolveLocalProject(name, note);
      if (!pick) return true;  // *note says why
      id = QString::fromStdString(pick->meta.id);
      nm = support::shortName(QString::fromStdString(pick->meta.name));
    }
    ConfirmSpec spec;
    spec.title = "Remove project";
    spec.message = QString("Remove \"%1\"? This cannot be undone.").arg(nm);
    spec.confirmIcon = QStringLiteral("trash");
    spec.danger = true;
    if (!confirmModal(&w_, spec)) {
      *note = QStringLiteral("removal canceled");
      return true;
    }
    w_.eraseLocalProject(id);
    fileStore::saveProjects(w_.projectList_);
    w_.refreshActions();
    w_.refreshDockMenu();
    w_.notify_->info("Project deleted");
    return true;
  }
  // §10 renameProject: the commitProjectName path (local rename or the server-linked live
  // push), pre-validated so a duplicate name surfaces the store's own reason as a note.
  bool ChatPlanTarget::renameActiveProject(const QString& name, QString* note) {
    const bool remote = !w_.remoteSession_->link().id.isEmpty();
    if (!remote && w_.activeProjectId_.isEmpty()) {
      *note = QStringLiteral("no active saved project to rename");
      return true;
    }
    if (!remote) {
      const auto check = w_.checkProjectName(name, w_.activeProjectId_);
      if (!check.ok) {
        *note = QString::fromStdString(check.reason);
        return true;
      }
    }
    w_.nameBar_.field->setText(name);
    w_.commitProjectName();
    return true;
  }
  // §10 projectColor: the project name-colour control ("" = theme accent).
  bool ChatPlanTarget::setProjectColor(const QString& color, QString* note) {
    if (w_.remoteSession_->link().id.isEmpty() && w_.activeProjectId_.isEmpty()) {
      *note = QStringLiteral("no active project — open or save one first");
      return true;
    }
    w_.setActiveProjectColor(color);
    return true;
  }
  // §10 blankColor: the nameBar_.blankColorBtn path minus its dialog — blanks only
  // (note+skip otherwise), keeps every drawn line.
  bool ChatPlanTarget::setBlankColor(const QString& color, QString* note) {
    if (w_.blankColor_.isEmpty() || !w_.canvas_->hasImage()) {
      *note = QStringLiteral("only a blank project's background can be recoloured");
      return true;
    }
    QColor c(color);  // hex or CSS name (validated upstream)
    if (!c.isValid())
      if (const auto rgba = core::parseColor(color.toStdString()))
        c = QColor(rgba->r, rgba->g, rgba->b);
    if (!c.isValid()) {
      *note = QStringLiteral("unknown colour \"%1\"").arg(color);
      return true;
    }
    w_.applyBlankColor(c);
    return true;
  }
  // §10 openProject: removeProject's resolution, then the projects dialog's open path —
  // including its unsaved-replace confirm when no saved project backs the editor's work.
  bool ChatPlanTarget::openProjectNamed(const QString& name, bool last, QString* note) {
    // "the last project I worked on": the most recently edited one, resolved HERE off the
    // store's updatedAt — the model never sees the project list (chatSession.js parity).
    const Project* pick = nullptr;
    if (last) {
      for (const auto& p : w_.projectList_)
        if (!pick || p.meta.updatedAt > pick->meta.updatedAt) pick = &p;
      if (!pick) {
        *note = QStringLiteral("there are no saved projects yet");
        return true;
      }
    } else {
      pick = resolveLocalProject(name, note);
    }
    if (!pick) return true;  // *note says why
    const QString id = QString::fromStdString(pick->meta.id);
    const QString nm = support::shortName(QString::fromStdString(pick->meta.name));
    const bool unsaved = w_.canvas_->hasImage() && w_.activeProjectId_.isEmpty() &&
                         w_.remoteSession_->link().id.isEmpty();
    ConfirmSpec openSpec;
    openSpec.title = "Open project";
    openSpec.message = QString("Open \"%1\"? Any unsaved changes in the current window will be "
                               "replaced.")
                           .arg(nm);
    openSpec.confirmLabel = "Open";
    openSpec.confirmIcon = QStringLiteral("folder");
    if (unsaved && !confirmModal(&w_, openSpec)) {
      *note = QStringLiteral("open canceled");
      return true;
    }
    if (!w_.loadProjectIntoCanvas(id)) {
      *note = QStringLiteral("could not open \"%1\"").arg(nm);
      return true;
    }
    return true;
  }
  // §10 incognito: the actIncognito_ toggle — only togglable on a blank
  // (imageless) editor, exactly like the action's own enabled gate.
  bool ChatPlanTarget::setIncognito(bool on, QString* note) {
    if (!on && w_.incognito_ && w_.canvas_->hasImage()) {
      // "make this not incognito" with work on screen: keep the work — leave
      // incognito and save it as a local project (publishIncognitoToServer's move).
      const QString promoted = w_.promoteIncognitoToLocal();
      *note = QStringLiteral("left incognito — saved as the local project \"%1\"")
                  .arg(support::shortName(promoted));
      return true;
    }
    if (w_.canvas_->hasImage()) {
      *note = QStringLiteral("incognito can only be turned ON from a blank editor");
      return true;
    }
    if (w_.actIncognito_ && w_.actIncognito_->isChecked() != on)
      w_.actIncognito_->setChecked(on);  // its toggled handler applies + notifies
    return true;
  }
  // §10 chatPanel: the panel's OWN placement, through the very calls its title-bar buttons
  // make, so a spoken "put the chat on the right" lands where a click would have. A dock
  // with no "open" shows the panel too (browser chatSession.js setChatPlacement parity).
  bool ChatPlanTarget::setChatPlacement(int open, const QString& dock, QString* note) {
    if (!w_.chatDock_ || !w_.actChat_) {
      *note = QStringLiteral("there is no assistant panel here");
      return true;
    }
    // Show FIRST, then place: the placement paths animate the panel on screen (and
    // toggleChatFloat only has a window to lift once it is showing), so a hidden dock
    // asked to float would have stayed docked. Closing comes last, for the same reason.
    const bool show = open < 0 ? !dock.isEmpty() : open == 1;
    if (show && !w_.actChat_->isChecked()) w_.actChat_->setChecked(true);
    if (!dock.isEmpty()) {
      if (dock == QLatin1String("float")) {
        if (!w_.chatDock_->isFloating()) w_.toggleChatFloat();
      } else {
        const Qt::DockWidgetArea area = dock == QLatin1String("left")    ? Qt::LeftDockWidgetArea
                                        : dock == QLatin1String("right")  ? Qt::RightDockWidgetArea
                                        : dock == QLatin1String("top")    ? Qt::TopDockWidgetArea
                                                                          : Qt::BottomDockWidgetArea;
        w_.dockChatTo(area);
      }
    }
    // The toggle's own handler runs the open/close flight, so this IS the click.
    if (!show && w_.actChat_->isChecked()) w_.actChat_->setChecked(false);
    return true;
  }

  // §10 dialog: the editor's own windows, through the very QActions their toolbar buttons
  // drive. Opened on the NEXT event-loop turn, never inline: they exec() modally, which
  // would park the whole plan behind a window the user has not been told about yet.
  bool ChatPlanTarget::openDialog(const QString& name, QString* note) {
    if (name.isEmpty()) {
      QWidget* open = QApplication::activeModalWidget();
      if (!open) {
        *note = QStringLiteral("no window is open");
        return true;
      }
      QTimer::singleShot(0, open, [open] { open->close(); });
      return true;
    }
    QAction* act = name == QLatin1String("projects")    ? w_.actProjects_
                   : name == QLatin1String("servers")   ? w_.actConnect_
                   : name == QLatin1String("shortcuts") ? w_.actShortcuts_
                   : name == QLatin1String("visuals")   ? w_.actSettings_
                                                        : w_.actInfo_;
    if (!act || !act->isEnabled()) {
      *note = QStringLiteral("the %1 window is not available right now").arg(name);
      return true;
    }
    QTimer::singleShot(0, &w_, [act] { act->trigger(); });
    return true;
  }

  // §10 clearProjects: every LOCAL project through the same machinery as the
  // dialog's "Clear All (Local)" — server projects are never touched from chat.
  bool ChatPlanTarget::clearProjects(bool keepCurrent, QString* note) {
    // `keepCurrent` = "delete the others": the open project stays where it is. Without it
    // the model clears the lot, then loses the project outright when no working image is
    // left to re-save.
    const QString keepId = keepCurrent ? w_.activeProjectId_ : QString();
    const bool keeping = !keepId.isEmpty();
    const int total = static_cast<int>(w_.projectList_.size());
    QString keptName;
    int n = 0;
    for (const Project& p : w_.projectList_) {
      if (keeping && QString::fromStdString(p.meta.id) == keepId) {
        keptName = support::shortName(QString::fromStdString(p.meta.name));
        continue;
      }
      ++n;
    }
    if (n == 0) {
      *note = total ? QStringLiteral("no other saved projects to clear")
                    : QStringLiteral("no saved projects to clear");
      return true;
    }
    ConfirmSpec spec;
    spec.title = keeping ? "Clear other projects" : "Clear all projects";
    spec.message = keeping
        ? QString("Are you sure? This removes the %1 other local project(s), keeping "
                  "\"%2\", and cannot be undone. Server projects are not affected.")
              .arg(n)
              .arg(keptName)
        : QString("Are you sure? This removes all %1 local project(s) and cannot be undone. "
                  "Server projects are not affected.")
              .arg(n);
    spec.confirmIcon = QStringLiteral("trash");
    spec.danger = true;
    if (!confirmModal(&w_, spec)) {
      *note = QStringLiteral("clear canceled");
      return true;
    }
    const bool hadActive = !w_.activeProjectId_.isEmpty();
    if (keeping) {
      std::vector<Project> kept;
      for (const Project& p : w_.projectList_)
        if (QString::fromStdString(p.meta.id) == keepId) kept.push_back(p);
      w_.projectList_ = std::move(kept);
    } else {
      w_.projectList_.clear();
      if (hadActive) w_.resetToBlankEditor();   // the open one went with them
    }
    fileStore::saveProjects(w_.projectList_);
    w_.refreshActions();
    w_.refreshDockMenu();
    w_.notify_->success(QString("Cleared %1 local project(s)").arg(n));
    return true;
  }
  // §10 clearChat: only FLAG the request — the confirm + clear run once the
  // whole turn settles (chatTurnSettled); a modal here would stall the plan.
  bool ChatPlanTarget::clearChat(QString*) {
    w_.chatClearPending_ = true;
    return true;
  }
  // `image`: the turn's Nth attachment becomes the working image, through the
  // same bare-QImage adoption the empty-canvas case uses. An index this turn
  // cannot satisfy is reported back (the executor's skipped-action note).
  bool ChatPlanTarget::loadAttachment(int index, QString* err) {
    if (index < 1 || index > w_.chatTurnAttachments_.size()) {
      if (err)
        *err = QStringLiteral("this message attached %1 image(s)")
                   .arg(w_.chatTurnAttachments_.size());
      return false;
    }
    w_.loadImageWithLayout(w_.chatTurnAttachments_.at(index - 1), QJsonObject());
    w_.chatActiveAttachment_ = index;   // it names an unnamed `save`
    w_.refreshActions();
    w_.onSelectionChanged();
    w_.updateImageSizeInfo();
    return true;
  }
  // `save`: a LOCAL project, never a server publish (that stays a user action).
  // `dest` (already echo-checked) writes to that folder/file instead.
  bool ChatPlanTarget::saveProject(const QString& name, const QString& dest,
                                   QString* err) {
    return w_.chatSaveProject(name, dest, err);
  }

  // The pool the executor's echo-guard checks: the user's OWN turns.
  QString ChatPlanTarget::userTypedText() const {
    QStringList parts;
    for (const auto& m : w_.chatHistory_)
      if (m.role == QLatin1String("user")) parts << m.text;
    return parts.join(QLatin1Char('\n'));
  }

  QImage ChatPlanTarget::renderResult() const { return w_.canvas_->renderToImage(true); }

}  // namespace stencil::gui
