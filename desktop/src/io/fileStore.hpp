#pragma once
#include "cropGeometry.hpp"
#include "models.hpp"
#include "projectsStore.hpp"
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <optional>
#include <vector>

// File persistence adapter — the desktop counterpart of browser/js/core/
// storage.js + projectsStore.js (which use localStorage / IndexedDB). All state
// lives in a single repo-local, gitignored directory (desktop/.stencil/), as the
// user requested: settings.json, session.autosave (the "last edited" blob), and
// projects.json. JSON parsing uses Qt here so the core/ library stays STL-only.
namespace stencil::gui {

  // Persisted user settings + default visuals (mirrors the browser settings /
  // DEFAULT_VISUALS the modals edit).
  struct Settings {
    // themeMode follows the browser's tri-state (system|light|dark); "system"
    // tracks the OS color scheme (S14). The legacy `theme` key is migrated on
    // load. Default "system" (per the confirmed decision).
    QString themeMode = "system";     // "system" | "light" | "dark"
    bool autosave = true;
    bool showPoints = true;
    bool showLines = true;
    QString defaultColor = "#FFFF00";
    double defaultThickness = 2.0;
    double defaultMarkerSize = 4.0;
    QString defaultStyle = "solid";   // solid | dashed | dotted
    QString pageSize = "A3";          // A3 | A4 | custom
    // Custom page dimensions in cm (browser DEFAULT_PAGE 21 x 29.7).
    double customPageWidth = 21.0;
    double customPageHeight = 29.7;
    // Display unit for page/length readouts: "cm" (default) | "in". Lengths are
    // always stored in cm; this only changes how they are shown and entered.
    QString units = "cm";
    // Formula transform of page (cm) coords (browser allowFormulas/formulaX/Y).
    bool allowFormulas = false;
    QString formulaX;
    QString formulaY;
    // Hover tooltip over the canvas (browser tooltipEnabled, default true).
    bool tooltipEnabled = true;
    // Image filter + custom tint (browser drawingApp.js:83-84). "none" | "bw" |
    // "sepia" | "custom"; filterColor is the custom duotone tint.
    QString imageFilter = "none";
    QString filterColor = "#7c3aed";
  };

  // The autosaved in-progress drawing ("last edited points"), restored on launch.
  // Mirrors the browser localStorage layout blob.
  struct Session {
    QString imagePath;
    QString pageSize = "A3";
    double scale = 1.0;
    core::Lines lines;
    // Carry a custom page across restart (A3/A4 ride along in pageSize).
    double customPageWidth = 21.0;
    double customPageHeight = 29.7;
    // Image filter / tint / draw mode ride along in the layout blob (browser
    // storage.js:40-41,54). drawMode is "line" | "rect".
    QString imageFilter = "none";
    QString filterColor = "#7c3aed";
    QString drawMode = "line";
    // Crop window in rotated-image pixels (width 0 = no crop stored → default
    // centered crop is applied on load). The original image is never modified.
    core::CropRect cropRect;
    // 90° quarter-turns (0..3, clockwise) applied to the original before the crop.
    int rotationQuarters = 0;
  };

  // One saved project: registry metadata (handled by core::ProjectsStore) plus
  // its layout payload.
  struct Project {
    core::ProjectMeta meta;
    QString imagePath;
    core::Lines lines;
    // Crop window (rotated-image pixels); width 0 = default crop on load.
    core::CropRect cropRect;
    // 90° quarter-turns (0..3, clockwise) applied to the original before the crop.
    int rotationQuarters = 0;
  };

  namespace fileStore {
    // The gitignored state directory (created on first use). Returns its path.
    QString stateDir();
    QString settingsPath();
    QString sessionPath();
    QString projectsPath();

    Settings loadSettings();
    void saveSettings(const Settings& s);

    std::optional<Session> loadSession();
    void saveSession(const Session& s);
    void clearSession();

    std::vector<Project> loadProjects();
    void saveProjects(const std::vector<Project>& projects);

    // ── Line <-> JSON helpers (promoted from the .cpp anon namespace so the
    // layout-import/export data actions can reuse them). Mirror the browser
    // line object fields (storage.js).
    QJsonObject lineToJson(const core::Line& line);
    core::Line lineFromJson(const QJsonObject& o);
    QJsonArray linesToJson(const core::Lines& lines);
    core::Lines linesFromJson(const QJsonArray& arr);

    // ── Layout-JSON envelope (browser drawingApp.js:2078-2079 download/upload
    // layout). buildLayoutJson emits {imageWidth,imageHeight,lines};
    // parseLayoutJson reads them back, reporting the stored image dimensions.
    QJsonObject buildLayoutJson(int w, int h, const core::Lines& lines);
    core::Lines parseLayoutJson(const QJsonObject& o, int& wOut, int& hOut);

    QString hotkeysPath();
    // Shortcut overrides (id -> key sequence), layered over hotkeysConfig.json
    // defaults. Mirrors the browser STORAGE_KEYS.hotkeys blob (S13).
    QHash<QString, QString> loadHotkeys();
    void saveHotkeys(const QHash<QString, QString>& overrides);
  }

}
