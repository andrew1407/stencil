#pragma once
#include "chatDock.hpp"
#include "fullscreenController.hpp"
#include "popoverHost.hpp"
#include "projectNameBar.hpp"
#include "sessionController.hpp"
#include "unitsController.hpp"
#include "formulaParser.hpp"
#include "llmClient.hpp"
#include "pageMetrics.hpp"
#include "projectsStore.hpp"
#include "tipContent.hpp"
#include "tooltipRows.hpp"
#include "fileStore.hpp"
#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QGraphicsOpacityEffect>
#include <QPointer>
#include <QSet>
#include <QMainWindow>
#include <QString>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

class QAction;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class QTimer;
class QFileSystemWatcher;
class QDoubleSpinBox;
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QToolButton;
class QVariantAnimation;
class QJsonObject;
class QActionGroup;
class QWidgetAction;
class QCheckBox;
class QRadioButton;
class QButtonGroup;
class QImage;
class QPixmap;
class QMenu;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QUrl;
class QGraphicsOpacityEffect;

namespace stencil::net {
  class ConnectionManager;
  class ServerClient;
  class LiveFeed;
}

namespace stencil::llm {
  class QtLlmTransport;
  class PlanTarget;
  struct ExecResult;
  struct OpPlan;
}

class MainWindowGuiTest;
class QScrollBar;

// Top-level window; browser twin js/ui/layout.js + toolbar.js + the DrawingApp wiring.
namespace stencil::gui {

  class CanvasWidget;
  class OpenInDialog;
  class SelectionPanel;
  class SelectedLineBar;
  class Notifications;
  class CanvasTooltip;
  class IncognitoOverlay;
  class DropZonesOverlay;
  class ProjectDragZones;
  class MediaLoader;
  class DataExportController;
  class RemoteSyncController;
  class RemoteSession;
  class ProjectTransferController;
  class DisintegrateOverlay;
  struct LaunchOptions;

  class MainWindow : public QMainWindow {
    Q_OBJECT
   public:
    // restoreLast=false begins blank (a "New Incognito Editor" window, an incognito launch).
    explicit MainWindow(QWidget* parent = nullptr, bool restoreLast = true);
    // Out-of-line, so unique_ptr members of forward-declared types see a complete type.
    ~MainWindow() override;

    // Called from main() AFTER show(), so async image/video/network resolution runs on the loop.
    void applyLaunchOptions(const LaunchOptions& opts);

    void openPathFromOS(const QString& path, int frame = 0);

    void openStencilUrl(const QUrl& url);

   private slots:
    void openImage();
    void newBlankImage();
    // Browser cropModal.js. Confirms before discarding lines when the orientation flips.
    void openCropDialog();
    void onHovered(double imageX, double imageY);
    void refreshActions();
    void onCanvasChanged();
    void onSelectionChanged();
    void onPageSizeChanged();
    void validateAndApplyFormulas();
    // Browser twin: drawingApp.js lineColor/lineThickness/pointSize/lineStyle handlers.
    void onLineStyleControlChanged();
    void applyImageFilter(const QString& mode);
    void setCompareModeUi(const QString& mode);
    void applyTintColor(const QColor& color);
    void applyLineStyle(const QString& style);
    void updateColorSwatch(QToolButton* btn, const QColor& color);
    QColor effectiveDefaultPointColor() const;
    void showContextMenu(const QPoint& globalPos);
    // Must run after applyTheme too — styleActionIcons() resets the split action's icon.
    void syncSplitCopyDownloadSlot();
    void syncContextActions();
    void onHoverDetail(double imageX, double imageY, const QPoint& globalPos,
                       Qt::KeyboardModifiers mods, bool immediate = false);
    // Browser tooltip.js scheduleShow; `immediate` skips the wait outright.
    void scheduleHoverShow(const QString& key, std::function<void()> revealFn, bool immediate);
    void hideHoverTooltip();
    void pasteImage();

   private:
    // Filter chain in THIS order: a void handler only observes, an optional-returning one that
    // answers ends the chain. tests/mainWindow.composition.gui.cpp pins the verdicts.
    bool eventFilter(QObject* obj, QEvent* event) override;
    void filterPointerChrome(QObject* obj, QEvent* event);
    void filterDockChrome(QObject* obj, QEvent* event);
    std::optional<bool> filterKeyClaims(QObject* obj, QEvent* event);
    std::optional<bool> filterPopoverGestures(QObject* obj, QEvent* event);
    std::optional<bool> filterPopoverButton(QObject* obj, QEvent* event);
    std::optional<bool> filterZoomAndLogo(QObject* obj, QEvent* event);
    std::optional<bool> filterCanvasViewport(QObject* obj, QEvent* event);
    std::optional<bool> filterProjectNameBar(QObject* obj, QEvent* event);
    QAction* newAction(const QString& text, const QString& seq);
    // Phases in call order — menus and toolbar iterate the actions in CREATION order; never reorder.
    void buildActions();
    void createCoreActions();
    void createDataActions();
    void wireActionHandlers();
    void mapHotkeyActions();
    void setActionTooltips();
    void buildContextActions();
    void buildDrawNowActions();
    void buildContextStyleActions();
    void buildContextTooltipActions();
    void buildUnitActions();
    QHBoxLayout* makeContextMenuRow(QWidgetAction*& act, int topM = 4, int botM = 4);
    void addContextCheckRow(const QString& text, bool checked, QCheckBox*& box,
                            QWidgetAction*& act);
    // After buildToolbar() — needs buttonForAction.
    void wireExportOptionsPopups();
    void wireExportPreviewHover(QMenu* menu);
    void populateExportVariantMenu(QMenu* menu, bool copy);
    void syncExportActions();
    QImage exportVariantPreviewImage(QAction* act) const;
    void buildMenus();
    // Qt reads settings_.nativeMenuBar only on (re)creation, so a runtime switch rebuilds.
    void applyMenuBarPlacement();
    void buildToolbar();
    // Ctor phases in exactly this order: construction order is observable (tab order, stacking,
    // dock placement) and tests/mainWindow.composition.gui.cpp pins it; wireSignals()'s connect ORDER too.
    void loadHotkeys();
    void setupCanvasArea();
    void installWindowFilters();
    void setupDocks();
    void setupChatDock();
    void installPanelShimmers();
    void setupOverlaysAndStatus();
    void setupPageAndZoomControls();
    void setupSyncControllers();
    void wireSignals();
    void restorePersistedState(bool restoreLast);
    // Call order preserves the addToolBar/addToolBarBreak row sequencing.
    void buildMainToolbar();
    void buildHeaderRow();
    void buildToolSectionsRow();
    void buildFormulaFields();

    class QToolBar* toolRow() const;
    class QWidget* makeToolSection(const QString& title, const QList<class QAction*>& actions,
                                   const QList<class QWidget*>& extras = {},
                                   const QList<class QWidget*>& leading = {});
    void buildProjectNameGroup(class QToolBar* bar);
    void updateImageSizeInfo();
    QString incognitoTagHtml() const;
    // Pinned to the taller state so the incognito indicator never reflows the window.
    void reserveImageInfoHeight();
    void syncImageInfoDockHeight();
    void syncFullscreenGlyph();
    void markFullscreenBars(bool on);
    QPixmap makeLogoPixmap(int size) const;
    void openAccentPicker();
    void remarkAccentPopover();
    // Browser twin accentController.previewAccent: instant repaint, no wipe, no persist.
    void previewAccent(const QString& key);
    void endAccentPreview();
    void showContextMenuFromKeyboard();
    void buildPageFormulaToolbar();
    void buildStyleToolbar();
    void buildDrawViewToolbar();
    void buildImageInfoBar();
    QString hotkey(const QString& id, const QString& fallback) const;
    QString pageSizeValue() const;
    core::PageSize currentPageDimensions() const;
    core::Point pageCoords(double imageX, double imageY) const;
    core::UnitFormat unitFormat() const;
    double currentLineLengthCm() const;
    void stampCanvasMeta(core::ProjectMeta& meta) const;
    void applyUnitToPageInputs();
    void applyUnitToPageCombo();
    void applyUnits(const QString& code);
    void syncUnitControls();
    void zoomIn();
    void zoomOut();
    void setZoom(double scale, bool syncCombo = true);
    void fitToWindow();
    void toggleFullscreen();
    void setToolbarsVisible(bool on);
    void fsHoverTick();
    // Browser parity: each chevron lives where its menu is, NOT in the top toolbar.
    void buildOverlayArrows();
    void positionOverlayArrows();
    void spinControlsPill(bool animate);
    void sizeViewToggles();
    void syncToastInset();
    void updatePanelReopenButton();
    void positionPanelReopenButton();
    void positionPanelGrip();
    void positionChatEdge();
    void setPanelShown(bool show, bool animate);
    // Browser mainContent.js surface flight; panelVeil_ hides the panel so the motes ARE it.
    QPointer<gui::DisintegrateOverlay> panelSurfaceFlight(bool gather, int ms, int full);
    void releasePanelVeil();
    QPointer<gui::DisintegrateOverlay> barsSurfaceFlight(const QList<class QToolBar*>& bars,
                                                         bool gather, int ms);
    // Browser selectionPanel.js surfaceIn/Out.
    void dustSelectedLineBarIn();
    void dustSelectedLineBarOut();
    QPoint selectedLineBarDustPoint(const QRect& barPicture, bool closing);
    void setChatShown(bool show, bool animate);
    // Pins the panel width so a shared-area chat slide eats into the CANVAS column only.
    void pinPanelWhileSharing(Qt::DockWidgetArea chatArea);
    // Splitting against a hidden dock can park it off-screen; idempotent.
    void ensurePanelChatSplit();
    void dockChatTo(Qt::DockWidgetArea area);
    QPointer<gui::DisintegrateOverlay> chatSurfaceFlight(Qt::DockWidgetArea area, bool gather, int ms,
                                                         const std::function<void(int)>& pin, int full);
    std::function<void(int)> chatExtentPin(bool horiz);
    void toggleChatFloat();
    void openChatCompact(QWidget* anchor);
    void openChatCompactNow(QWidget* anchor);
    QRect compactChatRect(QWidget* anchor) const;
    // Browser popover.js altHover parity. Closes a compact chat the glide moved off first.
    void altPeekOpen(class QToolButton* btn, QAction* act);
    void startLingerPoll();
    void stopLingerPoll();
    void stopChatAnim();
    void setToolbarsShown(bool show, bool animate);
    void refreshStatusHintVisibility();
    void animateBarsHeight(const QList<class QToolBar*>& bars, bool show);
    void scrollTo(int x, int y);
    void setZoomAnchored(double newScale, const QPoint& cursorInViewport);
    void applyTheme();
    void styleActionIcons(bool dark, const QColor& iconColor);
    void styleDangerToolButtons();
    bool sectionButtonVisible(QAction* act, QToolButton* btn) const;
    void syncDrawToggleFace(bool drawing, bool animate);
    void syncDrawModeFace(bool rect, bool animate);
    void bindRevealAnchors();
    void bindRevealAnchor(QAction* a);
    QColor toolButtonIconColor(QAction* act, const QColor& normal) const;
    // macOS menu-bar icons follow the SYSTEM appearance, not our theme.
    void retintMenuIconsForSystem(bool appDark, const QColor& appIconColor);
    QWidget* buttonForAction(QAction* act) const;
    QHash<QAction*, QString> actionIconNames_;
    void restyleContextToggles(const QColor& textColor);
    void toggleTheme();
    void applySettings(const Settings& s, bool persist);
    void openSettings();
    // A named pair, NOT an overload: taken by address in connect()s.
    void openAssistantSettings();
    void openAssistantSettingsFrom(QWidget* anchor, const QRect& anchorRect = QRect());
    // Parented to the WINDOW so the transcript survives the per-right-click menu rebuild.
    void ensureChatMenuPanel();
    void chatMirror(const QString& role, const QString& text, bool muted,
                    const QString& retryText = QString(), const QStringList& notes = {},
                    bool configure = false);
    void chatError(const QString& text, const QString& toastError = QString());
    void chatUnreachable(const QString& text, const QString& toastError = QString());
    void chatMirrorPending(bool show);
    void chatMirrorStopped(const QString& retryText = QString());
    // A dock mid-close counts as hidden: its slide keeps isVisible() true for 260 ms.
    bool chatSurfaceHidden() const;
    void chatRetryTurn(const QString& text);
    void chatMirrorBusy(bool on);
    void chatMirrorClear();
    static QString withChatWarnings(const QString& text, const QStringList& warnings);
    void chatMirrorLateNote(const QString& text);
    void chatLateNote(const QString& text);
    void chatNote(const QString& text);
    void chatMirrorProviderStatus(const QString& richTooltip,
                                  ChatDock::ProviderStatus status);
    SessionController::Gates sessionGates() const;
    void scheduleAutosave();
    void saveSessionNow();
    void restoreSession();
    // Browser storage.js parity; touches only the active project's view fields.
    void scheduleViewSave();
    void saveActiveProjectView();
    // A QGraphicsOpacityEffect per bar: QSS cannot express hidden-until-pan-then-fade.
    void revealCanvasScrollbars();
    void scheduleScrollbarHide();
    QScrollBar* canvasScrollBar(Qt::Orientation o) const;
    void openProjects();
    QHash<QString, QPixmap> buildProjectThumbs() const;
    // Browser connectModal.js.
    void openConnections();
    stencil::net::ConnectionManager* ensureConnections();
    void autoConnectServers();
    // Plaintext http to a remote host sends the bearer token + image bytes in the clear.
    void warnInsecureConnections();
    // Browser switchToProject(); animate=false for a REBIND, where a dust arrival would be a lie.
    bool loadProjectIntoCanvas(const QString& id, bool animate = true);
    void openProjectInNewWindow(const QString& id);
    bool projectOpenInOtherWindow(const QString& id) const;
    void openImageDialog(bool startBlank);
    void createBlankImageFromDialog(const QColor& color, int w, int h);
    void createBlankImage(const QColor& color, int w, int h);
    void openImageHere(const QString& path, bool incognito);
    void openImageInNewWindow(const QString& path, bool incognito);
    void openSourceHere(const QString& src, int frame, bool incognito);
    void openSourceInNewWindow(const QString& src, int frame, bool incognito,
                               bool hasPreview = false, bool cropToPage = false,
                               bool cropAlbum = false,
                               const QString& cropPage = QString());
    void openPreviewedImageHere(const QImage& image, const QString& localPath,
                                const QString& provSource, bool incognito,
                                bool cropToPage, bool cropAlbum,
                                const QString& cropPage);
    bool canReplaceActive() const;
    void replaceProjectImage(const QString& path, bool rename, bool keepAnnotations);
    void replaceServerOriginal(std::function<void()> done = {});
    void publishIncognitoToServer(const QString& serverUrl);
    bool loadLocalImageReset(const QString& path);
    bool openProjectByName(const QString& name);
    void onLaunchImageLoaded(const QImage& image, const QString& localPath);
    void applyQuickCrop();
    void applyLayoutFromSource(const QString& src);
    void openImageSource(const QString& src, int frame);
    void ensureMediaLoader();
    // Browser linksModal.js.
    void openLinks();
    void openDescription();
    void openKeywords();
    void loadImageByUrl(const QString& source, const QString& resource, int frame);

    // Self-owned (WA_DeleteOnClose), so a long-lived application Dock menu never dangles them.
    static void openIncognitoWindow();
    static void openProjectsWindow();
    static void openProjectWindowById(const QString& id);
    void refreshDockMenu();
    void newProjectFromCanvas();
    void createProject(const QString& name);
    void createLocalProject(const QString& name, bool announce = true, bool fromFile = false);
    void adoptCanvasAsLocalProject();
    // Browser twin: remoteSync.js createRemoteProject.
    void createServerProject(const QString& serverUrl, const QString& name,
                             std::function<void()> onLinked = {});
    void saveToActiveProject();
    void clearCurrentProject();
    void resetToBlankEditor();
    // Version-guarded PUT; a 409 surfaces "edited elsewhere" and leaves the link untouched.
    void saveToServer();
    void openServerProject(const QString& serverUrl, const QString& id, bool silent = false,
                           bool link = true);
    void openServerLaunch(const QString& serverUrl, const QString& id, bool incognito);
    // Browser openInModal.js, for the current session.
    void openInAnotherApp();
    void openInAnotherAppFor(const QString& id, const QString& serverUrl,
                             const QRect& closeRect);
    struct OpenInSource {
      QString serverUrl, serverId;
      qint64 version = 0;
      QImage image;
      QString name, source, resource;
      QJsonObject layout;
      bool startIncognito = false;
    };
    void dispatchOpenIn(const OpenInSource& src, bool browserAvailable, bool telegramAvailable,
                        const std::function<int(OpenInDialog&)>& run);
    void loadImageWithLayout(const QImage& img, const QJsonObject& layout,
                             const QByteArray& sourceBytes = {}, const QString& sourceExt = {});

    void openProjectFile(const QString& path);
    void saveProjectFileAs();
    void deleteProjectFile();   // delete the linked .stencil file from disk (confirm), then unlink

    // Opt-in .stencil live sync (browser twin StencilSync); `stencilLink_` empty ⇒ not linked.
    QByteArray buildStencilBytes();
    void linkStencilFile(const QString& path, const QByteArray& baseline);
    void unlinkStencilFile();
    void writeStencilNow(const QByteArray& prebuilt = {});
    void scheduleStencilAutosave();
    void flushStencilAutosave();
    void onStencilFileChanged(const QByteArray& prebuilt = {});
    void applyStencilExternal(const QByteArray& text, bool merge = false);
    void toggleStencilLiveSync(bool on);
    fileStore::LayoutMeta currentLayoutMeta() const;
    void adoptServerLayoutMeta(const QJsonObject& layout);

    // ChatPlanTarget adapts the live editor to PlanTarget, so it needs the private appliers.
    friend class ChatPlanTarget;
    // The GUI e2e drives private completion paths (mocked chat replies → toast).
    friend class ::MainWindowGuiTest;
    void ensureLlmClient();
    stencil::llm::LlmSettings currentLlmSettings() const;
    void refreshLlmStatus();
    QString chatSystemSuffix() const;
    void onChatSend(const QString& text);
    void onChatReply(const stencil::llm::LlmReply& reply);
    bool settleFailedChatReply(const stencil::llm::LlmReply& reply, bool toastWanted);
    void postChatReplyBubble(const stencil::llm::OpPlan& plan, bool hasWork,
                             bool adoptAttachment);
    void renderChatAskCard(const stencil::llm::OpPlan& plan, stencil::llm::PlanTarget& target);
    void renderChatVariantCards(const stencil::llm::ExecResult& res, const QString& reply);
    bool maybeContinueChat(const stencil::llm::OpPlan& plan);
    bool chatPlanLoadsWithoutTracing(const stencil::llm::OpPlan& plan) const;
    void flushHeldChatReply();
    void onChatStop();
    void onChatClear();
    // §10 clearChat is deferred to chatTurnSettled.
    void chatTurnSettled();
    void runDeferredChatClear();
    void resetChatState();
    QJsonObject buildActiveChatDoc() const;
    void persistActiveChat();
    void restoreChatFromDoc(const QJsonObject& doc);
    void clearPersistedChat();
    // Browser closedToast parity: ~90 chars, click opens the chat.
    void showChatToast(const QString& text, bool success);
    void pushChatHistory(const stencil::llm::ChatMessage& m);
    // §7: last 32 messages; only this turn's images and the one before them ride along.
    QVector<stencil::llm::ChatMessage> wireChatMessages() const;
    void onChatVideoAttached(const QString& path);
    void offerChatVideoUpload(const QString& path);
    // Sequential MediaLoader seeks, blocking on a local event loop.
    bool chatExtractFrames(const QVector<int>& indices, QString* err);
    bool chatSaveProject(const QString& name, const QString& dest, QString* err);
    QString promoteIncognitoToLocal(const QString& name = QString());
    bool chatOpenFile(const QString& path, QString* err);
    // Blocks until MediaLoader resolves, so the plan's next action edits the loaded picture.
    bool chatLoadSource(const QString& src, bool incognito, QString* why);
    QString chatSaveBaseName(const QString& requested) const;
    QString uniqueLocalProjectName(const QString& wanted) const;
    QString addImageProjectEntry(const QImage& img, const QString& baseName,
                                 bool deferRegistrySave = false);

    Project* findProject(const std::string& id);

    void eraseLocalProject(const QString& id);
    void persistSettings();

    // Browser twin: updateProjectTitle + validateName/nameExists.
    void updateProjectTitle();
    QString activeProjectName() const;
    QString projectBaseName() const;
    core::ProjectsStore::NameCheck checkProjectName(const QString& name,
                                                    const QString& exceptId) const;
    bool renameProjectById(const QString& id, const QString& name);
    QString activeProjectColor() const;
    QString currentProjectColor() const;
    void chooseProjectColor();
    void showProjectColorMenu();
    void setActiveProjectColor(const QString& color);
    void setActiveBlankColor();
    void applyBlankColor(const QColor& c);
    void setProjectColorById(const QString& id, const QString& serverUrl, const QString& color,
                             std::function<void(bool ok)> done = {});
    std::optional<QString> normalizeProjectColor(const QString& color) const;
    void refreshProjectNameButtons();   // ✓/✗ visibility + ✓ enabled state, as the field is edited
    void setPaintedOut(QWidget* w, bool out);   // browser `visibility: hidden` — keeps the slot
    void updateNameHover();   // is the cursor over the name group (hover-reveal ✎/🎨)
    void applyProjectNameStyle(bool editing);
    void setActionTip(QAction* a, const QString& desc);
    void enterNameEdit();
    void commitProjectName();
    void cancelProjectName();
    void openInfo();
    void openShortcuts();
    void applyHotkeyOverrides(const QHash<QString, QString>& overrides);
    void updateStatusIdle();
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void playImageArrival();
    // First-show window-opacity ramp (browser appReveal). Runs once; later shows are instant.
    void showEvent(QShowEvent* event) override;
    // Closing is immediate on every path — no confirmation modal, a deliberate decision.
    void closeEvent(QCloseEvent* event) override;

    bool rKeyHeld_ = false;
    // Two held keys arrive as independent native auto-repeat streams; combined on our own tick.
    bool panLeftHeld_ = false;
    bool panRightHeld_ = false;
    bool panUpHeld_ = false;
    bool panDownHeld_ = false;
    bool panShiftHeld_ = false;
    QTimer* arrowPanTimer_ = nullptr;
    CanvasWidget* canvas_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    SelectionPanel* selPanel_ = nullptr;
    SelectedLineBar* selectedLineBar_ = nullptr;
    // A dock area, not a QToolBar row: it stretches to the full width the FlowLayout wraps against.
    class QDockWidget* selectedLineDock_ = nullptr;
    class QVBoxLayout* centralLayout_ = nullptr;
    ChatDock* chatDock_ = nullptr;
    // QPointer: notify_ dies with the scroll viewport while dock signals can still fire in teardown.
    QPointer<Notifications> notify_;
    CanvasTooltip* tooltip_ = nullptr;
    QTimer* hoverTooltipTimer_ = nullptr;
    QString hoverPendingKey_;
    QString hoverShownKey_;
    std::function<void()> hoverPendingReveal_;
    IncognitoOverlay* incognitoOverlay_ = nullptr;
    DropZonesOverlay* dropZones_ = nullptr;
    ProjectDragZones* projectZones_ = nullptr;
    QLabel* status_ = nullptr;
    QComboBox* zoom_ = nullptr;
    SessionController session_;
    // QPointer: QWidget::setGraphicsEffect DELETES the previous effect, so a re-install dangles.
    QPointer<QGraphicsOpacityEffect> vScrollOpacity_;
    QPointer<QGraphicsOpacityEffect> hScrollOpacity_;
    QTimer* scrollbarHideTimer_ = nullptr;
    bool scrollbarHovered_ = false;
    // Reentrancy flags, read by RemoteSyncController as const bool*.
    bool remotePushing_ = false;
    bool remoteReloading_ = false;
    bool filterDirty_ = false;

    ProjectNameBar nameBar_;
    class QLabel* statusHint_ = nullptr;
    QAction* statusHintAction_ = nullptr;
    bool toolbarsShown_ = true;

    bool tearingDown_ = false;

    UnitsController units_;
    QCheckBox* allowFormulas_ = nullptr;
    QWidget* formulaGroup_ = nullptr;
    QLineEdit* formulaX_ = nullptr;
    QLineEdit* formulaY_ = nullptr;
    QLabel* formulaError_ = nullptr;
    // Commits when typing settles, never per keystroke; browser twin settingsController.wireFormulaInputs, same delay.
    QTimer* formulaCommitTimer_ = nullptr;
    QWidgetAction* ctxAllowFormulasAct_ = nullptr;
    QCheckBox* ctxAllowFormulas_ = nullptr;
    QWidgetAction* ctxFormulaXAct_ = nullptr;
    QWidgetAction* ctxFormulaYAct_ = nullptr;
    QLineEdit* ctxFormulaX_ = nullptr;
    QLineEdit* ctxFormulaY_ = nullptr;

    // Browser toolbar.js sections; these set canvas DEFAULTS only — selected-line editing is SelectionPanel's.
    QToolButton* drawModeBtn_ = nullptr;
    QToolButton* zoomFitBtn_ = nullptr;
    QCheckBox* showPointsCheck_ = nullptr;
    QCheckBox* showLinesCheck_ = nullptr;
    QToolButton* startDrawBtn_ = nullptr;
    QToolButton* lineColorBtn_ = nullptr;
    QToolButton* pointColorBtn_ = nullptr;
    QSpinBox* lineThickness_ = nullptr;
    QSpinBox* pointSize_ = nullptr;
    QComboBox* lineStyle_ = nullptr;
    QComboBox* imageFilter_ = nullptr;
    QComboBox* compareCombo_ = nullptr;
    QToolButton* filterColorBtn_ = nullptr;
    class QToolBar* styleToolbar_ = nullptr;
    class QWidget* imageSection_ = nullptr;
    QSet<class QAction*> dangerIcons_;
    class QToolButton* openImageBtn_ = nullptr;
    QColor lineColorValue_{"#FFFF00"};
    QColor filterColorValue_{"#7c3aed"};

    QAction* actOpen_ = nullptr;
    QAction* actOpenAnother_ = nullptr;
    QAction* actCrop_ = nullptr;
    QAction* actRotateLeft_ = nullptr;
    QAction* actRotateRight_ = nullptr;
    QAction* actCycleFilter_ = nullptr;
    QAction* actCycleCompare_ = nullptr;
    QAction* actStartDraw_ = nullptr;
    QAction* actStopDraw_ = nullptr;
    QAction* actNewLine_ = nullptr;
    QAction* actUndo_ = nullptr;
    QAction* actRedo_ = nullptr;
    QAction* actDeleteLast_ = nullptr;
    QAction* actDeleteLine_ = nullptr;
    QAction* actDeletePoint_ = nullptr;
    QAction* actClearAll_ = nullptr;
    QAction* actDeselect_ = nullptr;
    QAction* actZoomIn_ = nullptr;
    QAction* actZoomOut_ = nullptr;
    QAction* actFit_ = nullptr;
    QAction* actShowPoints_ = nullptr;
    QAction* actShowLines_ = nullptr;
    QAction* actAllowFormulas_ = nullptr;
    QAction* actTooltip_ = nullptr;
    QAction* actTheme_ = nullptr;
    QAction* actPanel_ = nullptr;
    QAction* actToolbars_ = nullptr;
    QAction* actFullscreen_ = nullptr;
    class QToolButton* controlsPill_ = nullptr;
    qreal pillChevronDeg_ = 0;
    QVariantAnimation* pillSpinAnim_ = nullptr;
    class QToolBar* headerToolbar_ = nullptr;
    QWidget* settingsSection_ = nullptr;
    QWidget* connectionsSection_ = nullptr;
    class QToolButton* panelReopenBtn_ = nullptr;
    class DockGripOverlay* panelGrip_ = nullptr;
    bool panelGripDrag_ = false;
    // chatEdgeHit_ is the separator's real rect: the band paints thicker but lights only where Qt resizes.
    class DockEdgeOverlay* chatEdge_ = nullptr;
    QRect chatEdgeHit_;
    bool chatEdgeDrag_ = false;
    class QLabel* imageSizeInfo_ = nullptr;
    QWidget* imageInfoBar_ = nullptr;
    QWidget* imageInfoHost_ = nullptr;
    class QDockWidget* imageInfoDock_ = nullptr;
    QString imageInfoHeightKey_;
    QWidget* dropHint_ = nullptr;
    class QLabel* dropHintIcon_ = nullptr;
    class QLabel* dropHintText_ = nullptr;
    class QToolButton* logoBtn_ = nullptr;
    QTimer* logoClickTimer_ = nullptr;
    QAction* actAccent_ = nullptr;
    bool altHeldForTest_ = false;
    QWidget* logoFx_ = nullptr;

    PopoverHost pop_;
    int execMaybePopover(QDialog& dlg, QAction* opener = nullptr);
    void dismissPopover();
    // The popover is this window's child, so its frameGeometry() is not a screen rect.
    QRect popoverRectGlobal() const;
    bool handlePopoverPress(class QWidget* target, const QPoint& globalPos,
                            Qt::MouseButton button);
    FullscreenController fs_;
    bool themePainted_ = false;
    bool paintedDark_ = false;
    QString paintedAccent_;
    QString accentPreviewSaved_;
    bool accentPreviewActive_ = false;
    bool blockedCursorOn_ = false;
    QPointer<QWidget> blockedRow_;
    void setBlockedCursor(bool on);
    // The wipe in flight; a second toggle is ignored. QPointer — it deleteLater()s itself.
    QPointer<QWidget> themeWipe_;
    bool themeSwapping() const { return !themeWipe_.isNull(); }

    // FLIP out of / into the viewport box (js/ui/motion.js); the chosen zoom is preserved.
    void beginFullscreenZoom();
    void startFullscreenZoom();
    // The browser's --coord-panel-default (css/layout.css).
    static constexpr int PANEL_DEFAULT_WIDTH = 405;
    // The coordinate columns elide below this; browser .coordinates-panel has the same floor.
    static constexpr int PANEL_MIN_WIDTH = 240;
    int panelRestoreWidth_ = PANEL_DEFAULT_WIDTH;
    QVariantAnimation* panelAnim_ = nullptr;
    QVariantAnimation* chatAnim_ = nullptr;
    int chatRestoreExtent_ = 0;
    QSize chatNaturalMin_;
    QPointer<QGraphicsOpacityEffect> chatVeil_;
    QPointer<QGraphicsOpacityEffect> panelVeil_;
    QString chatTextOnlyKey_;
    bool chatClearPending_ = false;
    bool chatCompactPopover_ = false;
    bool chatClosing_ = false;
    struct MirrorRow {
      QString role;
      QString text;
      QString retryText;
      QStringList notes;
      bool muted = false;
    };
    QVector<MirrorRow> chatMirrorLog_;
    Qt::DockWidgetArea chatCompactPrevArea_ = Qt::LeftDockWidgetArea;
    QRect chatFloatRect_;
    // Browser FLOAT_DEFAULT, ported to this window's top-left; clamped to its own screen.
    QRect defaultChatFloatRect() const;
    bool chatCompactShowing() const;
    QVariantAnimation* barsAnim_ = nullptr;
    QAction* actSettings_ = nullptr;
    QAction* actProjects_ = nullptr;
    QAction* actConnect_ = nullptr;
    QAction* actLinks_ = nullptr;
    QAction* actDescription_ = nullptr;
    QAction* actKeywords_ = nullptr;
    QAction* actNewProject_ = nullptr;
    QAction* actSaveProject_ = nullptr;
    QAction* actClearProject_ = nullptr;
    QAction* actRenameProject_ = nullptr;
    QAction* actProjectColor_ = nullptr;
    QAction* actProjectColorClear_ = nullptr;
    QAction* actSaveSession_ = nullptr;
    QAction* actInfo_ = nullptr;
    QAction* actIncognito_ = nullptr;
    QAction* actShortcuts_ = nullptr;
    QAction* actContextMenu_ = nullptr;
    QAction* actOpenIn_ = nullptr;
    QAction* actChat_ = nullptr;
    QAction* actAssistantSettings_ = nullptr;
    QAction* actQuit_ = nullptr;

    // Browser toolbar.js Image/Layout buttons and the paste listener.
    QAction* actDownloadJson_ = nullptr;
    QAction* actUploadJson_ = nullptr;
    QAction* actSaveProjectFile_ = nullptr;
    QAction* actOpenProjectFile_ = nullptr;
    QAction* actDeleteProjectFile_ = nullptr;
    QAction* actCopyLayout_ = nullptr;
    QAction* actPasteLayout_ = nullptr;
    // Only ONE of a pair holds the real shortcut at a time — Qt disallows ambiguous shortcuts.
    QAction* actSaveImage_ = nullptr;
    QAction* actSaveImageSplit_ = nullptr;
    QAction* actSaveImageOriginal_ = nullptr;
    QAction* actSaveImageTint_ = nullptr;
    QAction* actCopyImage_ = nullptr;
    QAction* actCopyImageSplit_ = nullptr;
    QAction* actCopyImageOriginal_ = nullptr;
    QAction* actCopyImageTint_ = nullptr;
    QAction* actSaveImageCurrentRow_ = nullptr;
    QAction* actCopyImageCurrentRow_ = nullptr;
    QAction* actShareImage_ = nullptr;
    QAction* actPasteImage_ = nullptr;
    QMenu* copyImageOptionsMenu_ = nullptr;
    QMenu* saveImageOptionsMenu_ = nullptr;

    // Browser js/ui/contextMenu.js; owned by `this` and reused on every right-click.

    QAction* actDrawLineNow_ = nullptr;
    QAction* actDrawRectNow_ = nullptr;

    QActionGroup* lineStyleGroup_ = nullptr;
    QAction* actStyleSolid_ = nullptr;
    QAction* actStyleDashed_ = nullptr;
    QAction* actStyleDotted_ = nullptr;
    QWidgetAction* pointSizeAction_ = nullptr;
    QWidgetAction* thicknessAction_ = nullptr;
    QSpinBox* pointSpin_ = nullptr;
    QSpinBox* thickSpin_ = nullptr;

    // Plain muted text, NOT QMenu::addSection(): that QAction isSeparator(), so the QSS paints a line.
    QWidgetAction* secImageAct_ = nullptr;
    QWidgetAction* secLayoutJsonAct_ = nullptr;
    QWidgetAction* secLineStyleAct_ = nullptr;
    QWidgetAction* secFilterAct_ = nullptr;
    QWidgetAction* secCoordFormulasAct_ = nullptr;
    QWidgetAction* secShowInTooltipAct_ = nullptr;

    // Hosted radios in an exclusive QButtonGroup keep the menu open, like the browser's inline radios.
    QButtonGroup* filterButtons_ = nullptr;
    QActionGroup* compareGroup_ = nullptr;
    QWidgetAction* actFilterNone_ = nullptr;
    QWidgetAction* actFilterBW_ = nullptr;
    QWidgetAction* actFilterSepia_ = nullptr;
    QWidgetAction* actFilterInvert_ = nullptr;
    QWidgetAction* actFilterContour_ = nullptr;
    QWidgetAction* actFilterCustom_ = nullptr;
    QAction* tintColorAction_ = nullptr;

    // Real QCheckBoxes in QWidgetActions, so a click flips them WITHOUT closing the menu.
    QWidgetAction* actTooltipEnable_ = nullptr;
    QCheckBox* tooltipEnableCheck_ = nullptr;
    QWidgetAction* actTtPage_ = nullptr;
    QWidgetAction* actTtScreen_ = nullptr;
    QWidgetAction* actTtCoords_ = nullptr;
    QCheckBox* ttPageCheck_ = nullptr;
    QCheckBox* ttScreenCheck_ = nullptr;
    QCheckBox* ttCoordsCheck_ = nullptr;

    QHash<QString, QString> hotkeys_;
    QHash<QString, QString> hotkeyDefaults_;
    QHash<QString, QString> hotkeyLabels_;
    QStringList hotkeyOrder_;
    QHash<QString, QAction*> hotkeyActions_;

    Settings settings_;
    core::ProjectsStore projectsStore_;
    std::vector<Project> projectList_;
    QString activeProjectId_;
    stencil::net::ConnectionManager* connections_ = nullptr;
    RemoteSession* remoteSession_ = nullptr;
    QString currentSource_;
    QString currentResource_;

    QByteArray sourceBytes_;
    QString sourceExt_;
    void setSourceBytes(const QByteArray& bytes, const QString& ext);
    void retainSourceFromFile(const QString& path);

    QString stencilLink_;
    QByteArray stencilBaseline_;
    bool stencilLiveSync_ = false;
    bool stencilApplying_ = false;
    QFileSystemWatcher* stencilWatcher_ = nullptr;
    QTimer* stencilAutosaveTimer_ = nullptr;
    QAction* actStencilLiveSync_ = nullptr;
    QString blankColor_;
    QString pendingProvSource_;
    QString pendingProvResource_;
    QString pendingServerTarget_;
    struct QuickCropOpts {
      enum class Mode { AUTO, PAGE, NONE };
      Mode mode = Mode::AUTO;
      bool album = false;
      QString page;
    };
    QuickCropOpts pendingCrop_;
    bool incognito_ = false;
    // NaN until the cursor really hovers the canvas; the refreshers replay onHovered with these.
    double lastHoverX_ = std::numeric_limits<double>::quiet_NaN();
    double lastHoverY_ = std::numeric_limits<double>::quiet_NaN();
    QColor iconColor_{Qt::black};
    bool firstShow_ = true;

    stencil::llm::QtLlmTransport* llmTransport_ = nullptr;
    std::unique_ptr<stencil::llm::LlmClient> llmClient_;
    // Short-TTL probe cache (browser chatSession cacheProbe), keyed by the effective settings.
    QString llmProbeKey_;
    qint64 llmProbeAt_ = 0;
    stencil::llm::LlmProbeResult llmProbeCache_;
    QString chatLastPrompt_;
    QVector<stencil::llm::ChatMessage> chatHistory_;
    QList<QImage> chatTurnAttachments_;
    QStringList chatTurnAttachmentNames_;
    int chatActiveAttachment_ = 0;
    QString chatVideoPath_;
    int chatVideoFrames_ = 0;
    MediaLoader* chatMedia_ = nullptr;
    QByteArray chatImageDigest_;
    stencil::llm::ChatImage chatImageEncoded_;
    stencil::llm::ChatImage chatEdgeMapEncoded_;
    QWidget* chatToast_ = nullptr;
    // The ACTION is parented to the window, not the menu, so it outlives the per-right-click rebuild.
    QWidgetAction* chatMenuAction_ = nullptr;
    QWidget* chatMenuPanel_ = nullptr;
    QWidget* chatMenuInput_ = nullptr;
    QWidget* dockZones_ = nullptr;
    bool chatStopRequested_ = false;
    bool chatContinued_ = false;
    bool chatReplyHeld_ = false;
    QString chatHeldReply_;
    QStringList chatHeldWarnings_;
    QStringList chatHeldNotes_;

    MediaLoader* mediaLoader_ = nullptr;
    std::unique_ptr<DataExportController> dataExport_;
    std::unique_ptr<RemoteSyncController> remoteSync_;
    std::unique_ptr<ProjectTransferController> projectTransfer_;
    QString pendingLaunchLayout_;
    QString pendingLaunchLayoutJson_;

    // Last setAsDockMenu wins; owned by the app, not any window, so a closing window never dangles it.
    static QMenu* sDockMenu_;
  };

}
