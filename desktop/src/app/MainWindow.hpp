#pragma once
#include "accentDefaults.hpp"
#include "ChatDock.hpp"
#include "FullscreenController.hpp"
#include "PopoverHost.hpp"
#include "ProjectNameBar.hpp"
#include "SessionController.hpp"
#include "UnitsController.hpp"
#include "formulaParser.hpp"
#include "LlmClient.hpp"
#include "pageMetrics.hpp"
#include "ProjectsStore.hpp"
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
    // Filter chain in THIS order: a void handler observes, an answering one ends it (composition.gui pins it).
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
    // Qt reads settings.nativeMenuBar only on (re)creation, so a runtime switch rebuilds.
    void applyMenuBarPlacement();
    void buildToolbar();
    // Ctor phases in exactly this order: it is observable (tab order, stacking) and composition.gui pins it.
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
    void buildLogoStage();
    bool toggleWebcore();
    void unpinFaceWidths();
    void webcoreScene();
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
    core::FormulaContext formulaContext() const;
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
    // Browser mainContent.js surface flight; panelVeil hides the panel so the motes ARE it.
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
    void dropChatVeil();
    void stopChatAnim();
    void setToolbarsShown(bool show, bool animate);
    void refreshStatusHintVisibility();
    void animateBarsHeight(const QList<class QToolBar*>& bars, bool show);
    void scrollTo(int x, int y);
    void setZoomAnchored(double newScale, const QPoint& cursorInViewport);
    void applyTheme();
    void refreshDropHint();
    void restyleImageSizeInfo();
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
    QHash<QAction*, QString> actionIconNames;
    void restyleContextToggles(const QColor& textColor);
    void toggleTheme();
    void applySettings(const Settings& s, bool persist);
    void openSettings();
    void openAssistantSettings();   // a named pair, NOT an overload: taken by address in connect()s
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
    void chatMirrorProviderStatus(const QString& richTooltip, ChatDock::ProviderStatus status);
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
    // Browser modal.js.
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
    // `fallbacks`: the ranked candidates to try after `src` (a linked image's other urls).
    void openSourceHere(const QString& src, int frame, bool incognito,
                        const QStringList& fallbacks = {});
    // `cropRect` is what the Open-Image stage was left on; empty ⇒ the new window centres it.
    void openSourceInNewWindow(const QString& src, int frame, bool incognito,
                               const QStringList& fallbacks = {}, bool hasPreview = false,
                               bool cropToPage = false, bool cropAlbum = false,
                               const QString& cropPage = QString(),
                               const core::CropRect& cropRect = {});
    void openPreviewedImageHere(const QImage& image, const QString& localPath,
                                const QString& provSource, bool incognito, bool cropToPage,
                                bool cropAlbum, const QString& cropPage,
                                const core::CropRect& cropRect = {});
    bool canReplaceActive() const;
    void replaceProjectImage(const QString& path, bool rename, bool keepAnnotations);
    void replaceServerOriginal(std::function<void()> done = {});
    void publishIncognitoToServer(const QString& serverUrl);
    bool loadLocalImageReset(const QString& path);
    bool openProjectByName(const QString& name);
    void onLaunchImageLoaded(const QImage& image, const QString& localPath);
    void applyQuickCrop();
    void applyLayoutFromSource(const QString& src);
    void openImageSource(const QString& src, int frame, const QStringList& fallbacks = {});
    void ensureMediaLoader();
    // Browser linksModal.js.
    void openLinks();
    void openDescription();
    void openScript();
    // The QWidgetAction owns the panel, so the typed script survives the menu closing.
    void ensureScriptMenuPanel();
    /* A file dialog closes every popup Qt has open, so the flyout's Upload and Download put
     * the chain back afterwards: the menu re-opens where it was, on the script row. */
    void reopenScriptFlyout();
    void refreshAfterScript();
    void runScriptFromFile(const QString& path);
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
    void openInAnotherAppFor(const QString& id, const QString& serverUrl, const QRect& closeRect);
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

    // Opt-in .stencil live sync (browser twin StencilSync); `stencilLink` empty ⇒ not linked.
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
    void postChatReplyBubble(const stencil::llm::OpPlan& plan, bool hasWork, bool adoptAttachment);
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
    bool chatLoadSource(const QString& src, bool incognito, QString* why, int frame = 0);
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

    bool rKeyHeld = false;
    // Two held keys arrive as independent native auto-repeat streams; combined on our own tick.
    bool panLeftHeld = false;
    bool panRightHeld = false;
    bool panUpHeld = false;
    bool panDownHeld = false;
    bool panShiftHeld = false;
    QTimer* arrowPanTimer = nullptr;
    CanvasWidget* canvas = nullptr;
    QScrollArea* scroll = nullptr;
    SelectionPanel* selPanel = nullptr;
    SelectedLineBar* selectedLineBar = nullptr;
    // A dock area, not a QToolBar row: it stretches to the full width the FlowLayout wraps against.
    class QDockWidget* selectedLineDock = nullptr;
    class QVBoxLayout* centralLayout = nullptr;
    ChatDock* chatDock = nullptr;
    // QPointer: notify dies with the scroll viewport while dock signals can still fire in teardown.
    QPointer<Notifications> notify;
    CanvasTooltip* tooltip = nullptr;
    QTimer* hoverTooltipTimer = nullptr;
    QString hoverPendingKey;
    QString hoverShownKey;
    std::function<void()> hoverPendingReveal;
    IncognitoOverlay* incognitoOverlay = nullptr;
    DropZonesOverlay* dropZones = nullptr;
    ProjectDragZones* projectZones = nullptr;
    QLabel* status = nullptr;
    QComboBox* zoom = nullptr;
    SessionController session;
    // QPointer: QWidget::setGraphicsEffect DELETES the previous effect, so a re-install dangles.
    QPointer<QGraphicsOpacityEffect> vScrollOpacity;
    QPointer<QGraphicsOpacityEffect> hScrollOpacity;
    QTimer* scrollbarHideTimer = nullptr;
    bool scrollbarHovered = false;
    // Reentrancy flags, read by RemoteSyncController as const bool*.
    bool remotePushing = false;
    bool remoteReloading = false;
    // True for the whole of an op plan, whose appliers await async work in nested event loops.
    bool planRunning = false;
    bool filterDirty = false;

    ProjectNameBar nameBar;
    class QLabel* statusHint = nullptr;
    QAction* statusHintAction = nullptr;
    bool toolbarsShown = true;

    bool tearingDown = false;

    UnitsController units;
    QCheckBox* allowFormulas = nullptr;
    QWidget* formulaGroup = nullptr;
    QLineEdit* formulaX = nullptr;
    QLineEdit* formulaY = nullptr;
    QLabel* formulaError = nullptr;
    // Commits when typing settles, never per keystroke; browser twin settingsController.wireFormulaInputs, same delay.
    QTimer* formulaCommitTimer = nullptr;
    QWidgetAction* ctxAllowFormulasAct = nullptr;
    QCheckBox* ctxAllowFormulas = nullptr;
    QWidgetAction* ctxFormulaXAct = nullptr;
    QWidgetAction* ctxFormulaYAct = nullptr;
    QLineEdit* ctxFormulaX = nullptr;
    QLineEdit* ctxFormulaY = nullptr;

    // Browser toolbar.js sections; these set canvas DEFAULTS only — selected-line editing is SelectionPanel's.
    QToolButton* drawModeBtn = nullptr;
    QToolButton* zoomFitBtn = nullptr;
    QCheckBox* showPointsCheck = nullptr;
    QCheckBox* showLinesCheck = nullptr;
    QToolButton* startDrawBtn = nullptr;
    QToolButton* lineColorBtn = nullptr;
    QToolButton* pointColorBtn = nullptr;
    QSpinBox* lineThickness = nullptr;
    QSpinBox* pointSize = nullptr;
    QComboBox* lineStyle = nullptr;
    QComboBox* imageFilter = nullptr;
    QComboBox* compareCombo = nullptr;
    QToolButton* filterColorBtn = nullptr;
    class QToolBar* styleToolbar = nullptr;
    class QWidget* imageSection = nullptr;
    QSet<class QAction*> dangerIcons;
    class QToolButton* openImageBtn = nullptr;
    QColor lineColorValue{"#FFFF00"}, filterColorValue{DEFAULT_ACCENT_HEX};

    QAction* actOpen = nullptr;
    QAction* actOpenAnother = nullptr;
    QAction* actCrop = nullptr;
    QAction* actRotateLeft = nullptr;
    QAction* actRotateRight = nullptr;
    QAction* actCycleFilter = nullptr;
    QAction* actCycleCompare = nullptr;
    QAction* actStartDraw = nullptr;
    QAction* actStopDraw = nullptr;
    QAction* actNewLine = nullptr;
    QAction* actUndo = nullptr;
    QAction* actRedo = nullptr;
    QAction* actDeleteLast = nullptr;
    QAction* actDeleteLine = nullptr;
    QAction* actDeletePoint = nullptr;
    QAction* actClearAll = nullptr;
    QAction* actDeselect = nullptr;
    QAction* actZoomIn = nullptr;
    QAction* actZoomOut = nullptr;
    QAction* actFit = nullptr;
    QAction* actShowPoints = nullptr;
    QAction* actShowLines = nullptr;
    QAction* actAllowFormulas = nullptr;
    QAction* actTooltip = nullptr;
    QAction* actTheme = nullptr;
    QAction* actPanel = nullptr;
    QAction* actToolbars = nullptr;
    QAction* actFullscreen = nullptr;
    class QToolButton* controlsPill = nullptr;
    qreal pillChevronDeg = 0;
    QVariantAnimation* pillSpinAnim = nullptr;
    class QToolBar* headerToolbar = nullptr;
    QWidget* settingsSection = nullptr;
    QWidget* connectionsSection = nullptr;
    class QToolButton* panelReopenBtn = nullptr;
    class DockGripOverlay* panelGrip = nullptr;
    bool panelGripDrag = false, showCovered = false;
    // chatEdgeHit is the separator's real rect: the band paints thicker but lights only where Qt resizes.
    class DockEdgeOverlay* chatEdge = nullptr;
    QRect chatEdgeHit;
    bool chatEdgeDrag = false;
    class QLabel *imageSizeInfo = nullptr, *incognitoTag = nullptr;
    QWidget* imageInfoBar = nullptr;
    QWidget* imageInfoHost = nullptr;
    class QDockWidget* imageInfoDock = nullptr;
    QString imageInfoHeightKey;
    QWidget* dropHint = nullptr;
    class QLabel* dropHintIcon = nullptr;
    class QLabel* dropHintText = nullptr;
    class QToolButton* logoBtn = nullptr;
    QTimer* logoClickTimer = nullptr;
    QAction* actAccent = nullptr;
    bool altHeldForTest = false;
    QWidget* logoFx = nullptr;

    PopoverHost pop;
    int execMaybePopover(QDialog& dlg, QAction* opener = nullptr);
    void dismissPopover();
    // The popover is this window's child, so its frameGeometry() is not a screen rect.
    QRect popoverRectGlobal() const;
    bool handlePopoverPress(class QWidget* target, const QPoint& globalPos,
                            Qt::MouseButton button);
    FullscreenController fs;
    bool themePainted = false;
    bool paintedDark = false;
    QString paintedAccent;
    QString accentPreviewSaved;
    bool accentPreviewActive = false;
    bool blockedCursorOn = false;
    QPointer<QWidget> blockedRow;
    void setBlockedCursor(bool on);
    // The wipe in flight; a second toggle is ignored. QPointer — it deleteLater()s itself.
    QPointer<QWidget> themeWipe;
    bool themeSwapping() const { return !themeWipe.isNull(); }

    // FLIP out of / into the viewport box (js/ui/motion.js); the chosen zoom is preserved.
    void beginFullscreenZoom();
    void startFullscreenZoom();
    // The browser's --coord-panel-default (css/layout.css).
    static constexpr int PANEL_DEFAULT_WIDTH = 405;
    // The coordinate columns elide below this; browser .coordinates-panel has the same floor.
    static constexpr int PANEL_MIN_WIDTH = 240;
    int panelRestoreWidth = PANEL_DEFAULT_WIDTH;
    QVariantAnimation* panelAnim = nullptr;
    QVariantAnimation* chatAnim = nullptr;
    int chatRestoreExtent = 0;
    QSize chatNaturalMin;
    QPointer<QGraphicsOpacityEffect> chatVeil;
    QPointer<QGraphicsOpacityEffect> panelVeil;
    QString chatTextOnlyKey;
    bool chatClearPending = false;
    // Always through setChatCompactPopover: the dock must hear it too, or it stays undraggable.
    bool chatCompactPopover = false;
    void setChatCompactPopover(bool on);
    bool chatClosing = false;
    struct MirrorRow {
      QString role;
      QString text;
      QString retryText;
      QStringList notes;
      bool muted = false;
    };
    QVector<MirrorRow> chatMirrorLog;
    Qt::DockWidgetArea chatCompactPrevArea = Qt::LeftDockWidgetArea;
    QRect chatFloatRect;
    // Browser FLOAT_DEFAULT, ported to this window's top-left; clamped to its own screen.
    QRect defaultChatFloatRect() const;
    bool chatCompactShowing() const;
    QVariantAnimation* barsAnim = nullptr;
    QAction* actSettings = nullptr;
    QAction* actProjects = nullptr;
    QAction* actConnect = nullptr;
    QAction* actLinks = nullptr;
    QAction* actDescription = nullptr;
    QAction* actKeywords = nullptr;
    QAction* actNewProject = nullptr;
    QAction* actSaveProject = nullptr;
    QAction* actClearProject = nullptr;
    QAction* actRenameProject = nullptr;
    QAction* actProjectColor = nullptr;
    QAction* actProjectColorClear = nullptr;
    QAction* actSaveSession = nullptr;
    QAction* actInfo = nullptr;
    QAction* actIncognito = nullptr;
    QAction* actShortcuts = nullptr;
    QAction* actContextMenu = nullptr;
    QAction* actOpenIn = nullptr;
    QAction* actChat = nullptr;
    QAction* actAssistantSettings = nullptr;
    QAction* actQuit = nullptr;

    // Browser toolbar.js Image/Layout buttons and the paste listener.
    QAction* actDownloadJson = nullptr;
    QAction* actUploadJson = nullptr;
    QAction* actScript = nullptr;
    QAction* actSaveProjectFile = nullptr;
    QAction* actOpenProjectFile = nullptr;
    QAction* actDeleteProjectFile = nullptr;
    QAction* actCopyLayout = nullptr;
    QAction* actPasteLayout = nullptr;
    // Only ONE of a pair holds the real shortcut at a time — Qt disallows ambiguous shortcuts.
    QAction* actSaveImage = nullptr;
    QAction* actSaveImageSplit = nullptr;
    QAction* actSaveImageOriginal = nullptr;
    QAction* actSaveImageTint = nullptr;
    QAction* actCopyImage = nullptr;
    QAction* actCopyImageSplit = nullptr;
    QAction* actCopyImageOriginal = nullptr;
    QAction* actCopyImageTint = nullptr;
    QAction* actSaveImageCurrentRow = nullptr;
    QAction* actCopyImageCurrentRow = nullptr;
    QAction* actShareImage = nullptr;
    QAction* actPasteImage = nullptr;
    QMenu* copyImageOptionsMenu = nullptr;
    QMenu* saveImageOptionsMenu = nullptr;

    // Browser js/ui/contextMenu.js; owned by `this` and reused on every right-click.

    QAction* actDrawLineNow = nullptr;
    QAction* actDrawRectNow = nullptr;

    QActionGroup* lineStyleGroup = nullptr;
    QAction* actStyleSolid = nullptr;
    QAction* actStyleDashed = nullptr;
    QAction* actStyleDotted = nullptr;
    QWidgetAction* pointSizeAction = nullptr;
    QWidgetAction* thicknessAction = nullptr;
    QSpinBox* pointSpin = nullptr;
    QSpinBox* thickSpin = nullptr;

    // Plain muted text, NOT QMenu::addSection(): that QAction isSeparator(), so the QSS paints a line.
    QWidgetAction* secImageAct = nullptr;
    QWidgetAction* secLayoutJsonAct = nullptr;
    QWidgetAction* secLineStyleAct = nullptr;
    QWidgetAction* secFilterAct = nullptr;
    QWidgetAction* secCoordFormulasAct = nullptr;
    QWidgetAction* secShowInTooltipAct = nullptr;

    // Hosted radios in an exclusive QButtonGroup keep the menu open, like the browser's inline radios.
    QButtonGroup* filterButtons = nullptr;
    QActionGroup* compareGroup = nullptr;
    QWidgetAction* actFilterNone = nullptr;
    QWidgetAction* actFilterBW = nullptr;
    QWidgetAction* actFilterSepia = nullptr;
    QWidgetAction* actFilterInvert = nullptr;
    QWidgetAction* actFilterContour = nullptr;
    QWidgetAction* actFilterCustom = nullptr;
    QAction* tintColorAction = nullptr;

    // Real QCheckBoxes in QWidgetActions, so a click flips them WITHOUT closing the menu.
    QWidgetAction* actTooltipEnable = nullptr;
    QCheckBox* tooltipEnableCheck = nullptr;
    QWidgetAction* actTtPage = nullptr;
    QWidgetAction* actTtScreen = nullptr;
    QWidgetAction* actTtCoords = nullptr;
    QCheckBox* ttPageCheck = nullptr;
    QCheckBox* ttScreenCheck = nullptr;
    QCheckBox* ttCoordsCheck = nullptr;

    QHash<QString, QString> hotkeys;
    QHash<QString, QString> hotkeyDefaults;
    QHash<QString, QString> hotkeyLabels;
    QStringList hotkeyOrder;
    QHash<QString, QAction*> hotkeyActions;

    Settings settings;
    core::ProjectsStore projectsStore;
    std::vector<Project> projectList;
    QString activeProjectId;
    stencil::net::ConnectionManager* connections = nullptr;
    RemoteSession* remoteSession = nullptr;
    QString currentSource;
    QString currentResource;

    QByteArray sourceBytes;
    QString sourceExt;
    void setSourceBytes(const QByteArray& bytes, const QString& ext);
    void retainSourceFromFile(const QString& path);

    QString stencilLink;
    QByteArray stencilBaseline;
    bool stencilLiveSync = false;
    bool stencilApplying = false;
    QFileSystemWatcher* stencilWatcher = nullptr;
    QTimer* stencilAutosaveTimer = nullptr;
    QAction* actStencilLiveSync = nullptr;
    QString blankColor;
    QString pendingProvSource;
    QString pendingProvResource;
    QString pendingServerTarget;
    struct QuickCropOpts {
      enum class Mode { AUTO, PAGE, NONE };
      Mode mode = Mode::AUTO;
      bool album = false;
      QString page;
      // The Open-Image crop stage's rect in original-image px; width 0 = none dragged, so the page-aspect crop centres.
      core::CropRect rect;
    };
    QuickCropOpts pendingCrop;
    bool incognito = false;
    // NaN until the cursor really hovers the canvas; the refreshers replay onHovered with these.
    double lastHoverX = std::numeric_limits<double>::quiet_NaN();
    double lastHoverY = std::numeric_limits<double>::quiet_NaN();
    QColor iconColor{Qt::black};
    bool firstShow = true;

    stencil::llm::QtLlmTransport* llmTransport = nullptr;
    std::unique_ptr<stencil::llm::LlmClient> llmClient;
    // Short-TTL probe cache (browser chatSession cacheProbe), keyed by the effective settings.
    QString llmProbeKey;
    qint64 llmProbeAt = 0;
    stencil::llm::LlmProbeResult llmProbeCache;
    QString chatLastPrompt;
    QVector<stencil::llm::ChatMessage> chatHistory;
    QList<QImage> chatTurnAttachments;
    QStringList chatTurnAttachmentNames;
    int chatActiveAttachment = 0;
    QString chatVideoPath;
    int chatVideoFrames = 0;
    MediaLoader* chatMedia = nullptr;
    QByteArray chatImageDigest;
    stencil::llm::ChatImage chatImageEncoded;
    stencil::llm::ChatImage chatEdgeMapEncoded;
    QWidget* chatToast = nullptr;
    // The ACTION is parented to the window, not the menu, so it outlives the per-right-click rebuild.
    QWidgetAction* chatMenuAction = nullptr;
    QWidget* chatMenuPanel = nullptr;
    QWidget* chatMenuInput = nullptr;
    QWidgetAction* scriptMenuAction = nullptr;   // the script flyout, same arrangement
    QWidget* scriptMenuPanel = nullptr;
    QWidget* scriptMenuEditor = nullptr;
    QPoint contextMenuAt;            // where the last canvas menu was raised
    bool reopenScriptFlyoutPending = false; // …and whether to land on the script row this time
    QWidget* dockZones = nullptr;
    bool chatStopRequested = false;
    bool chatContinued = false;
    bool chatReplyHeld = false;
    QString chatHeldReply;
    QStringList chatHeldWarnings;
    QStringList chatHeldNotes;

    MediaLoader* mediaLoader = nullptr;
    std::unique_ptr<DataExportController> dataExport;
    std::unique_ptr<RemoteSyncController> remoteSync;
    std::unique_ptr<ProjectTransferController> projectTransfer;
    QString pendingLaunchLayout;
    QString pendingLaunchLayoutJson;

    // Last setAsDockMenu wins; owned by the app, not any window, so a closing window never dangles it.
    static QMenu* sDockMenu;
  };

}
