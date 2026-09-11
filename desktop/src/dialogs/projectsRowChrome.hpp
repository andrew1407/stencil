#pragma once

// The projects dialog's row furniture: the per-session sort state, the row text
// helpers, the name-prompt plumbing, the kebab strip's geometry and the paint
// constants its delegate and its event filter both read. Declarations only.

#include "fileStore.hpp"      // gui::Project
#include "projectsStore.hpp"

#include <QColor>
#include <QCursor>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QStringList>
#include <Qt>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QAbstractButton;
class QLineEdit;
class QWidget;

namespace stencil::gui {

  // Longest edge of the floating hover-magnify preview. Matches the browser modal's
  // PREVIEW_ZOOM (1.67) applied to its 160px stored thumbnails, so the popped preview
  // is the same size on both surfaces.
  inline constexpr int kHoverPreviewPx = 178;
  inline constexpr int kHoverPreviewAltPx = 324;   // Alt glance
  // The preview is sand too, on the SHARED floating-tip clock (disintegrateOverlay.hpp
  // kTipDust*/kDustHold/kDustHandOverMs — browser surfaceIn/surfaceOut).
  inline constexpr int kHoverFadeMs = 90;         // plain ramp when the dust can't play

  // Per-session project sort mode + manual drag order — shared across dialog
  // re-opens, reset on app restart (the desktop analogue of the browser modal's
  // sessionStorage; deliberately NOT persisted). Modes mirror the browser.
  inline QString g_projectsSortMode = QStringLiteral("name");
  inline QStringList g_projectsManualOrder;   // (serverUrl|id) keys in manual order

  // What the "⋯" says on hover — its own tip, not the row's (browser projectsModal.js
  // menuBtn.title = 'More actions').
  inline const QString kKebabTip = QStringLiteral("More actions");

  // Delegate paint colours, parsed once — paintRow runs per row per frame, and a
  // QColor("#…") parse there is measurable churn.
  inline const QColor kGoldEdge("#d4a017");    // server rows (browser .project-remote)
  inline const QColor kBronzeEdge("#c1783c");  // .stencil-file rows
  inline const QColor kGreyOrigin("#9aa4b2");  // the "computer" origin line

  // UserRole+7: "doomed" — this row's removal scatter is playing. The delegate
  // paints NOTHING for it (the overlay animates a snapshot; the row must not keep
  // painting underneath); the slot stays until retireRow drops the item.
  inline constexpr int kDoomedRole = Qt::UserRole + 7;
  // UserRole+8: this row is the project open in THIS editor right now (browser parity:
  // projectsModal.js's "(Current)" — the word itself, in the accent, right after the
  // origin badge). The accent itself comes from the installed palette (Highlight /
  // Link — theme.cpp buildQPalette publishes accent + accent-2 there).
  inline constexpr int kActiveRole = Qt::UserRole + 8;
  // UserRole+10: the row's muted meta line ("Created … · expires …"), drawn under the
  // bold name (browser projectsModal row parity: name / dates / origin, stacked).
  inline constexpr int kMetaRole = Qt::UserRole + 10;
  // UserRole+11: the synthetic "Temporary (unsaved)" row — THIS window's session while
  // no project is open (browser parity: projectsModal.js's pinned `project-temp` row).
  // Its UserRole stays null, so every project action already ignores it.
  inline constexpr int kTempRole = Qt::UserRole + 11;

  // Ctrl on Windows/Linux; Qt maps macOS ⌘ to ControlModifier, and Meta is
  // accepted too so a remapped keyboard still works. Used by the row-open
  // gestures to pick "new window" over "current window".
  bool isNewWindowMod(Qt::KeyboardModifiers m);

  // The browser thumb advertises the glance with cursor:zoom-in; Qt ships no stock
  // magnifier cursor, so paint the classic lens-with-plus (dark glyph under a white
  // halo, like the system cursors, so it reads on any row colour). Hotspot on the
  // lens centre. Built once, after QGuiApplication exists.
  const QCursor& zoomInCursor();

  // Human expiry label for one project, mirroring the browser modal's
  // expiryLabel(): "EXPIRED", "expires in 1 day", or "expires in N days".
  QString expiryText(const core::ProjectsStore& store,
                     const core::ProjectMeta& meta, long long now);

  // "Created <localized short date>" for a project's createdAt (epoch ms), or
  // empty when unset. Shown on local + server rows so the create date is visible.
  QString createdText(long long createdAt);

  // "Expires <localized short date>" for a server project's expiresAt (epoch ms),
  // or empty when 0/unset (keep forever). Shown next to the created date on server
  // rows; local rows use expiryText() (a relative "expires in N days") instead.
  QString expiresText(long long expiresAt);

  // The projects registry loaded for name validation / the default-name seed —
  // shared by the modal prompt, the inline rename and createNew.
  std::shared_ptr<core::ProjectsStore> loadedNameStore(const std::vector<Project>& projects);

  // Live name validation both name editors share (browser parity, utils.js
  // wireNameEditor): ✓ enables only for a valid name, and only a rejected one gets a
  // tooltip. `current` adds the other half — an unchanged name is nothing to save, so ✓
  // goes dead with "No change" (rename passes it; the create/copy prompts do not).
  // The cursor follows the state, Qt having no `:disabled { cursor }` in QSS.
  std::function<void()> makeNameValidator(std::shared_ptr<core::ProjectsStore> store,
                                          QLineEdit* edit, QAbstractButton* okBtn,
                                          const QString& exceptId,
                                          const QString& current = QString());

  // Name prompt on the shell (modalChrome promptModal) with the inline rename's live
  // rules: Save enabled only when the trimmed name is non-empty, ≤80 chars, and unique
  // (excluding `exceptId`); the reason shows under the field. nullopt on cancel.
  std::optional<QString> promptValidatedName(QWidget* parent, const QString& title,
                                             const QString& initial,
                                             const QString& exceptId,
                                             const std::vector<Project>& projects);

  // The browser's pickServer (projectsModal.js): the connected servers in the picker
  // shell, auto-picked when there is only one. Empty on cancel.
  QString pickServer(QWidget* parent, const QStringList& urls, const QString& message,
                     const QString& title = QStringLiteral("Choose server"),
                     const QString& confirmLabel = QStringLiteral("OK"),
                     const QString& confirmIcon = QStringLiteral("server"));

  // The "⋯" kebab strip — shared by the delegate (paint) and eventFilter (hit-test).
  QRect kebabZone(const QRect& rowRect);

  // The accent chip the kebab paints inside its strip (browser: the per-row "…" button).
  QRect kebabChip(const QRect& rowRect);

  // A square, center-cropped (cover) thumbnail for the uniform row icon — mirrors the
  // browser's `object-fit: cover` thumbnails so rows are equal height regardless of aspect.
  QPixmap squareThumb(const QPixmap& src, int size);

}  // namespace stencil::gui
