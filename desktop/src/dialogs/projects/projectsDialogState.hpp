#pragma once
// The projects dialog's own furniture, grouped as plain value-type bags held by value — the batch
// bar, the hover preview with its kebab shimmer, and the press state a click/drag is decided from.
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QString>
#include <QVector>
#include <Qt>

class QLabel;
class QListWidgetItem;
class QPushButton;
class QTimer;
class QVariantAnimation;
class QWidget;

namespace stencil::gui {

  struct ProjectsBatchParts {
    QWidget* batchSelectedGroup = nullptr;
    QWidget* batchBar = nullptr;
    QLabel* batchCount = nullptr;
    QPushButton* batchToServer = nullptr;
    QPushButton* batchCopyServer = nullptr;
    QPushButton* batchToLocal = nullptr;
    QPushButton* batchCopyLocal = nullptr;
    QPushButton* batchRemove = nullptr;
    QPushButton* batchClear = nullptr;
    QSet<QString> checked;
    QVector<QPair<QString, QString>> batchItems;
  };

  enum class RowCursor { None, Pointer, Zoom };

  struct ProjectsHoverParts {
    QLabel* hoverPreview = nullptr;
    QListWidgetItem* hoverItem = nullptr;
    QVariantAnimation* hoverFade = nullptr;
    bool hoverClosing = false;
    // The preview's one dust cloud in flight: a new one ends it, so two photos never stand at once.
    QPointer<QWidget> hoverDust;
    RowCursor rowCursor = RowCursor::None;
    int kebabHoverRow = -1;
    class ShimmerOverlay* kebabSweep = nullptr;
    QString tipRowText;
    QRect menuKebabRect;
  };

  struct ProjectsPressParts {
    QTimer* clickTimer = nullptr;
    int pendingRow = -1;
    bool pendingNewWindow = false;
    Qt::KeyboardModifiers pressMods;
    QPoint pressPos;
    bool pressOnCheck = false;
    bool rowDragging = false;
  };

}  // namespace stencil::gui
