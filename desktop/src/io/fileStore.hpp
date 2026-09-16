#pragma once
#include "../llm/llmSettings.hpp"
#include "cropGeometry.hpp"
#include "models.hpp"
#include "ProjectsStore.hpp"
#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

// File persistence — browser twin js/core/storage.js + projectsStore.js. All state lives in
// one gitignored directory (desktop/.stencil/); JSON is Qt's here, so core/ stays STL-only.
namespace stencil::gui {

  // Persisted settings + default visuals (browser DEFAULT_VISUALS).
  struct Settings {
    // Tri-state system|light|dark; the legacy `theme` key is migrated on load.
    QString themeMode = "system";     // "system" | "light" | "dark"
    // Accent preset key (theme.hpp accentPresets); browser data-accent twin.
    QString accentColor = "violet";
    bool autosave = true;
    // Off: edits to a fetched server project stay in this session only — never pushed nor autosaved.
    bool syncToServer = true;
    bool showPoints = true;
    bool showLines = true;
    QString defaultColor = "#FFFF00";
    // Empty = follow defaultColor (core::Line::pointColor), so an old settings file keeps its behaviour.
    QString defaultPointColor = "";
    double defaultThickness = 2.0;
    double defaultPointSize = 4.0;
    QString defaultStyle = "solid";   // solid | dashed | dotted
    // Fill for a newly LOCKED area (browser DEFAULT_VISUALS.defaultFillColor).
    QString defaultFillColor = "#ffffff";
    QString selGlowColor = "#ffc800";    // selection highlight glow (lines + points)
    QString hoverRingColor = "#7c3aed";  // hover ring around points
    QString focusRingColor = "#7c3aed";  // focused/clicked point ring
    QString pageSize = "A3";          // a named ISO format ("A3", "B5", …) | custom
    double customPageWidth = 21.0;
    double customPageHeight = 29.7;
    // "cm" | "in"; lengths are always STORED in cm.
    QString units = "cm";
    bool allowFormulas = false;
    QString formulaX;
    QString formulaY;
    bool tooltipEnabled = true;
    // Browser tooltipShowPage/Screen/Coords, all default true.
    bool tooltipShowPage = true;
    bool tooltipShowScreen = true;
    bool tooltipShowCoords = true;
    // "none" | "bw" | "sepia" | "invert" | "contour" | "custom"; filterColor is the custom tint.
    QString imageFilter = "none";
    QString filterColor = "#7c3aed";
    int holdDrawDelay = 500;
    // Motion (browser js/ui/motionPrefs.js; support/modalReveal.hpp drives them).
    bool drawingAnimations = true;
    bool modalBackdrop = true;
    // "particles" | "water" | "fire" | "slide" | "none"; unknown reads as "particles".
    QString motionMode = "particles";
    // Desktop-only "Open in…" targets; the browser keeps its own in js/config/openInConfig.json.
    QString browserBaseUrl = "http://localhost:8080";
    QString telegramBotUsername;
    // AI assistant (llm-contract.md §5). An empty llmServerUrl resolves at use time to the first
    // saved connection (net/connectionStore).
    QString llmProvider = "ollama";     // "ollama" | "openai-compat" | "stencil-server"
    QString llmBaseUrl = stencil::llm::defaultLlmBaseUrl(QStringLiteral("ollama"));
    QString llmModel;
    QString llmApiKey;                  // openai-compat only (Bearer)
    QString llmServerUrl;               // stencil-server only ("" = first saved connection)
    // Chat persistence opt-in (llm-contract.md §12): OFF by default; incognito never persists.
    bool saveChatsWithProject = false;
    // false = user right, assistant left (browser chatLayoutPrefs.js CHAT_SIDE_SWAPPED twin).
    bool chatSwapSides = false;
    // Platform menu bar (macOS/Unity global bar); inert on Windows.
    bool nativeMenuBar = true;
    // QMainWindow::saveState() bytes, base64; "" = never saved.
    QString windowState;
  };

  // The autosaved in-progress drawing (browser localStorage layout blob).
  struct Session {
    QString imagePath;
    QString pageSize = "A3";
    double scale = 1.0;
    core::Lines lines;
    double customPageWidth = 21.0;
    double customPageHeight = 29.7;
    // drawMode is "line" | "rect" (browser storage.js).
    QString imageFilter = "none";
    QString filterColor = "#7c3aed";
    QString drawMode = "line";
    // Rotated-image pixels; width 0 = no crop stored → default centered crop on load.
    core::CropRect cropRect;
    int rotationQuarters = 0;
    // The project being edited (empty = unsaved), so a relaunch knows WHOSE pixels these are.
    QString activeProjectId;
  };

  // One saved project: registry metadata (core::ProjectsStore) + layout payload.
  struct Project {
    core::ProjectMeta meta;
    QString imagePath;
    core::Lines lines;
    core::CropRect cropRect;
    int rotationQuarters = 0;
    // llm-contract.md §12.1; empty = no saved chat.
    QJsonObject chat;
    // 0 = never saved → MainWindow falls back to fitToWindow() (browser `if (layout.zoom)` gate).
    double zoomScale = 0.0;
    int scrollLeft = 0;
    int scrollTop = 0;
  };

  namespace fileStore {
    QString stateDir();
    QString settingsPath();
    QString sessionPath();
    QString projectsPath();
    // Owner-only (0600) sidecar for SECRETS (saved connection tokens) — never QSettings plaintext.
    QString secretsPath();
    QJsonObject loadSecrets();
    void saveSecrets(const QJsonObject& o);

    Settings loadSettings();
    void saveSettings(const Settings& s);

    // Exposed so the round-trip is unit-testable without disk; `base` supplies defaults for absent keys.
    QJsonObject settingsToJson(const Settings& s);
    Settings settingsFromJson(const QJsonObject& o, const Settings& base = Settings());

    std::optional<Session> loadSession();
    void saveSession(const Session& s);
    void clearSession();

    std::vector<Project> loadProjects();   // flushes any pending saveProjects first
    void saveProjects(const std::vector<Project>& projects);   // io/deferredWrite.hpp
    void flushWrites();   // …force those out: on the way out of the app

    // Exposed so the `color` round-trip is unit-testable without disk (browser projectsStore.js twin).
    QJsonObject projectToJson(const Project& pr);
    Project projectFromJson(const QJsonObject& o);

    // Line <-> JSON (browser storage.js line fields); reused by the layout data actions.
    QJsonObject lineToJson(const core::Line& line);
    core::Line lineFromJson(const QJsonObject& o);
    QJsonArray linesToJson(const core::Lines& lines);
    core::Lines linesFromJson(const QJsonArray& arr);

    struct LayoutMeta {
      QString pageSize;             // "" = omit; else a named ISO format | "custom"
      double customPageWidth = 0;   // cm; 0 = unset
      double customPageHeight = 0;
      bool allowFormulas = false;
      QString formulaX;             // "" = identity transform
      QString formulaY;
    };

    // Browser layout.js buildLayoutPayload twin: cropRect and rotationQuarters are omitted when
    // empty/0 so old exports stay byte-identical; parseLayoutJson leaves an absent out-pointer untouched.
    QJsonObject buildLayoutJson(int w, int h, const core::Lines& lines,
                                const QString& imageFilter = "none",
                                const QString& filterColor = "#7c3aed",
                                const core::CropRect& cropRect = {},
                                int rotationQuarters = 0,
                                const LayoutMeta& meta = {});
    LayoutMeta parseLayoutMeta(const QJsonObject& o);
    core::Lines parseLayoutJson(const QJsonObject& o, int& wOut, int& hOut,
                                core::CropRect* cropOut = nullptr, int* rotOut = nullptr);

    // .stencil portable project file — browser/js/core/projectFile.js twin; QtCore-only (image as base64).
    inline constexpr int STENCIL_FILE_VERSION = 1;
    struct ProjectFileData {
      QString name = "Untitled";
      QString color;              // "#rrggbb" or "" (omitted from the file when empty)
      QString description;        // free-text description or "" (omitted from the file when empty)
      QStringList keywords;
      QString source, resource;
      bool blank = false;
      QString blankColor;
      QByteArray imageBytes;      // ENCODED original image (PNG/JPEG…), NOT base64
      QString imageExt = "png";
      int imageWidth = 0, imageHeight = 0;
      QJsonObject layout;         // the export layout (fileStore::buildLayoutJson)
      bool hasTheme = false;
      QString themeMode;          // "light" | "dark"
      QString themeAccent;        // accent preset key or "#rrggbb"
      // llm-contract.md §12.3; written only when the save-chats toggle is on.
      QJsonObject chat;
    };
    QByteArray buildProjectFile(const ProjectFileData& pf);
    // On success `out.imageBytes` holds the DECODED image.
    bool parseProjectFile(const QByteArray& bytes, ProjectFileData& out, QString* err = nullptr);

    // llm-contract.md §12.1: text-only, most recent 32 turns; unknown roles/fields/versions are dropped.
    inline constexpr int CHAT_DOC_VERSION = 1;
    inline constexpr int CHAT_DOC_MESSAGE_LIMIT = 32;
    QJsonObject buildChatDoc(const QJsonArray& messages, qint64 savedAt);
    QJsonArray parseChatDoc(const QJsonObject& doc);

    QString hotkeysPath();
    // Shortcut overrides over hotkeysConfig.json (browser STORAGE_KEYS.hotkeys twin).
    QHash<QString, QString> loadHotkeys();
    void saveHotkeys(const QHash<QString, QString>& overrides);
  }

}
