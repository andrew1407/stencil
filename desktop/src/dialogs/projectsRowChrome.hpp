#pragma once

// The projects dialog's row furniture: declarations only.

#include "fileStore.hpp"
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

  // Browser PREVIEW_ZOOM (1.67) × its 160px thumbnails, so the popped preview matches.
  inline constexpr int kHoverPreviewPx = 178;
  inline constexpr int kHoverPreviewAltPx = 324;
  // On the SHARED floating-tip clock (disintegrateOverlay.hpp kTipDust*/kDustHold/kDustHandOverMs).
  inline constexpr int kHoverFadeMs = 90;

  // Per-session, deliberately NOT persisted (the browser modal's sessionStorage).
  inline QString g_projectsSortMode = QStringLiteral("name");
  inline QStringList g_projectsManualOrder;

  // Browser projectsModal.js menuBtn.title.
  inline const QString kKebabTip = QStringLiteral("More actions");

  // Parsed once — paintRow runs per row per frame.
  inline const QColor kGoldEdge("#d4a017");
  inline const QColor kBronzeEdge("#c1783c");
  inline const QColor kGreyOrigin("#9aa4b2");

  // UserRole+7: the removal scatter is playing; the delegate paints NOTHING (the overlay animates a snapshot).
  inline constexpr int kDoomedRole = Qt::UserRole + 7;
  // UserRole+8: the project open in THIS editor (browser "(Current)"); the accent comes from the palette's Highlight/Link.
  inline constexpr int kActiveRole = Qt::UserRole + 8;
  // UserRole+10: the muted meta line under the bold name.
  inline constexpr int kMetaRole = Qt::UserRole + 10;
  // UserRole+11: the synthetic "Temporary (unsaved)" row (browser `project-temp`); its UserRole stays null.
  inline constexpr int kTempRole = Qt::UserRole + 11;

  // Qt maps macOS ⌘ to ControlModifier; Meta is accepted too for a remapped keyboard.
  bool isNewWindowMod(Qt::KeyboardModifiers m);

  // Qt ships no stock magnifier cursor (browser cursor:zoom-in); built once, after QGuiApplication exists.
  const QCursor& zoomInCursor();

  // Browser expiryLabel(): "EXPIRED", "expires in 1 day", "expires in N days".
  QString expiryText(const core::ProjectsStore& store,
                     const core::ProjectMeta& meta, long long now);

  QString createdText(long long createdAt);

  // Epoch ms; empty when 0 (keep forever). Local rows use expiryText() instead.
  QString expiresText(long long expiresAt);

  std::shared_ptr<core::ProjectsStore> loadedNameStore(const std::vector<Project>& projects);

  // Browser utils.js wireNameEditor: ✓ only for a valid name, tooltip only when rejected; `current`
  // deadens ✓ with "No change". The cursor follows the state — QSS has no `:disabled { cursor }`.
  std::function<void()> makeNameValidator(std::shared_ptr<core::ProjectsStore> store,
                                          QLineEdit* edit, QAbstractButton* okBtn,
                                          const QString& exceptId,
                                          const QString& current = QString());

  // Save enabled only when trimmed, non-empty, ≤80 chars and unique (excluding `exceptId`); nullopt on cancel.
  std::optional<QString> promptValidatedName(QWidget* parent, const QString& title,
                                             const QString& initial,
                                             const QString& exceptId,
                                             const std::vector<Project>& projects);

  // Browser pickServer: auto-picked when there is only one. Empty on cancel.
  QString pickServer(QWidget* parent, const QStringList& urls, const QString& message,
                     const QString& title = QStringLiteral("Choose server"),
                     const QString& confirmLabel = QStringLiteral("OK"),
                     const QString& confirmIcon = QStringLiteral("server"));

  QRect kebabZone(const QRect& rowRect);

  QRect kebabChip(const QRect& rowRect);

  // Browser `object-fit: cover` parity, so rows are equal height regardless of aspect.
  QPixmap squareThumb(const QPixmap& src, int size);

}  // namespace stencil::gui
