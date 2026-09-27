#pragma once
#include "BlockedCursor.hpp"
#include "ContextMenuParts.hpp"
#include "DocumentSource.hpp"
#include "FullscreenController.hpp"
#include "HeldKeys.hpp"
#include "HotkeyTable.hpp"
#include "PaintedTheme.hpp"
#include "PanelSlide.hpp"
#include "PopoverHost.hpp"
#include "ProjectNameBar.hpp"
#include "RemoteState.hpp"
#include "SessionController.hpp"
#include "ToolbarControls.hpp"
#include "UnitsController.hpp"
#include "WindowActions.hpp"
#include "WindowOverlays.hpp"
#include "WindowParts.hpp"
#include "fileStore.hpp"
#include "formulaParser.hpp"
#include "pageMetrics.hpp"
#include "ProjectsStore.hpp"
#include "tooltipRows.hpp"
#include <QByteArray>
#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <functional>
#include <memory>
#include <vector>

class QColor;
class QComboBox;
class QDockWidget;
class QImage;
class QJsonObject;
class QLabel;
class QScrollArea;
class QToolButton;
class QUrl;
class QVBoxLayout;
class MainWindowGuiTest;

// Top-level window; browser twin js/ui/layout.js + toolbar.js + the DrawingApp wiring.
namespace stencil::gui {

  class ArrowPanner;
  class CanvasWidget;
  class ChatDock;
  class ChatSessionController;
  class DataExportController;
  class MediaLoader;
  class Notifications;
  class ProjectTitleController;
  class ProjectTransferController;
  class RemoteSyncController;
  class SelectedLineBar;
  class SelectionPanel;
  class StencilFileSync;
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

   private:
    void newBlankImage();
    // Browser cropModal.js. Confirms before discarding lines when the orientation flips.
    void openCropDialog();
    void refreshActions();
    void onCanvasChanged();
    void onSelectionChanged();
    void applyImageFilter(const QString& mode, bool asUndoStep = true);
    void applyTintColor(const QColor& color, bool asUndoStep = true);
    void updateColorSwatch(QToolButton* btn, const QColor& color);
    // Filter chain in THIS order: a void handler observes, an answering one ends it (composition.gui pins it).
    bool eventFilter(QObject* obj, QEvent* event) override;
    void updateImageSizeInfo();
    QString incognitoTagHtml() const;
    // Pinned to the taller state so the incognito indicator never reflows the window.
    void reserveImageInfoHeight();
    QString pageSizeValue() const;
    core::PageSize currentPageDimensions() const;
    core::Point pageCoords(double imageX, double imageY) const;
    core::UnitFormat unitFormat() const;
    void setZoom(double scale, bool syncCombo = true);
    void fitToWindow();
    void spinControlsPill(bool animate);
    void setPanelShown(bool show, bool animate);
    void openChatCompact(QWidget* anchor);
    void setZoomAnchored(double newScale, const QPoint& cursorInViewport);
    QWidget* buttonForAction(QAction* act) const;
    void applySettings(const Settings& s, bool persist);
    void scheduleAutosave();
    void saveSessionNow();
    stencil::net::ConnectionManager* ensureConnections();
    // Plaintext http to a remote host sends the bearer token + image bytes in the clear.
    void warnInsecureConnections();
    // Browser switchToProject(); animate=false for a REBIND, where a dust arrival would be a lie.
    // False without such a project; `landed` hears whether its off-thread decode went in.
    bool loadProjectIntoCanvas(const QString& id, bool animate = true,
                               std::function<void(bool)> landed = {});
    // `decode` runs on the pool and touches no GUI object; `adopt` gets its pixels (null: the decode
    // failed) on the GUI thread, or `dropped` does when a newer picture load began meanwhile.
    void decodeForCanvas(std::function<QImage()> decode, std::function<void(const QImage&)> adopt,
                         std::function<void()> dropped = {});
    bool projectOpenInOtherWindow(const QString& id) const;
    void createLocalProject(const QString& name, bool announce = true, bool fromFile = false);
    void adoptCanvasAsLocalProject();
    void saveToActiveProject();
    void loadImageWithLayout(const QImage& img, const QJsonObject& layout,
                             const QByteArray& bytes = {}, const QString& ext = {});
    // The project StencilFileSync writes out; the live sync itself is that controller's.
    QByteArray buildStencilBytes();
    fileStore::LayoutMeta currentLayoutMeta() const;
    QColor toolButtonIconColor(QAction* act, const QColor& normal) const;
    void applyTheme();
    void positionOverlayArrows();
    // A file dialog closes every open popup; Upload/Download re-open the flyout on the script row.
    void reopenScriptFlyout();
    QString promoteIncognitoToLocal(const QString& name = QString());
    Project* findProject(const std::string& id);
    void persistSettings();
    // Browser twin: validateName/nameExists.
    QString activeProjectName() const;
    QString projectBaseName() const;
    core::ProjectsStore::NameCheck checkProjectName(const QString& name, const QString& exceptId) const;
    void setActionTip(QAction* a, const QString& desc);
    void updateStatusIdle();
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    int execMaybePopover(QDialog& dlg, QAction* opener = nullptr);
    void dismissPopover();
    bool chatCompactShowing() const;
    void playImageArrival();
    // First-show window-opacity ramp (browser appReveal). Runs once; later shows are instant.
    void showEvent(QShowEvent* event) override;
    // Closing is immediate on every path — no confirmation modal, a deliberate decision.
    void closeEvent(QCloseEvent* event) override;

    // The parts (WindowParts.hpp), ChatPlanTarget and the window factory reach the private hub.
    friend class ChatPlanTarget;
    friend class StyleControls;
    friend class WindowEvents;
    friend class WindowAssembly;
    friend class ExportMenus;
    friend class ScriptHost;
    friend class SettingsDialogs;
    friend class HoverTip;
    friend class PopoverGestures;
    friend class DocumentPersistence;
    friend class ChatAppliers;
    friend class ProjectFlows;
    friend class ThemePainter;
    friend class EditorView;
    friend class DockChrome;
    friend class SourceOpener;
    friend class CanvasContextMenu;
    friend class MenuBuilder;
    friend class ActionsBuilder;
    friend class ToolbarBuilder;
    friend struct SiblingWindows;
    // The GUI e2e drives private completion paths (mocked chat replies → toast).
    friend class ::MainWindowGuiTest;

    HeldKeys held;
    CanvasWidget* canvas = nullptr;
    QScrollArea* scroll = nullptr;
    SelectionPanel* selPanel = nullptr;
    SelectedLineBar* selectedLineBar = nullptr;
    // A dock area, not a QToolBar row: it stretches to the full width the FlowLayout wraps against.
    class QDockWidget* selectedLineDock = nullptr;
    class QVBoxLayout* centralLayout = nullptr;
    // Toolbars, docks and canvas; the chat docks AROUND it, as the browser panel insets the page.
    class QMainWindow* editor = nullptr;
    ChatDock* chatDock = nullptr;
    // QPointer: notify dies with the scroll viewport while dock signals can still fire in teardown.
    QPointer<Notifications> notify;
    WindowOverlays overlays;
    QLabel* status = nullptr;
    QComboBox* zoom = nullptr;
    SessionController session;
    bool filterDirty = false;
    ProjectNameBar nameBar;
    bool toolbarsShown = true;
    bool tearingDown = false;
    UnitsController units;
    WindowActions acts;
    ToolbarControls tools;
    ContextMenuParts ctxMenu;
    WindowParts parts{*this};
    bool showCovered = false;
    PopoverHost pop;
    FullscreenController fs;
    BlockedCursor blocked;
    PaintedTheme painted;
    PanelSlide panelSlide;
    HotkeyTable keys;
    Settings settings;
    core::ProjectsStore projectsStore;
    std::vector<Project> projectList;
    QString activeProjectId;
    RemoteState remote;
    DocumentSource docSource;
    bool incognito = false;
    bool firstShow = true;
    MediaLoader* mediaLoader = nullptr;
    std::unique_ptr<DataExportController> dataExport;
    std::unique_ptr<RemoteSyncController> remoteSync;
    std::unique_ptr<ProjectTransferController> projectTransfer;
    std::unique_ptr<StencilFileSync> stencilSync;
    std::unique_ptr<ChatSessionController> chatSession;
    std::unique_ptr<ProjectTitleController> projectTitle;
    std::unique_ptr<ArrowPanner> arrowPan;
  };
}  // namespace stencil::gui
