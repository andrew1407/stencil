#pragma once
// The ChatDock's own furniture, grouped as plain value-type bags the dock holds by value — the
// same shape MainWindow uses for pop/fs/units. Keeps the moc'd ChatDock.hpp to its interface.
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

class QLabel;
class QProgressBar;
class QToolButton;
class QVariantAnimation;
class QVBoxLayout;
class QWidget;

namespace stencil::gui {

  class PillSplitter;

  struct ChatTranscriptParts {
    PillSplitter* splitter = nullptr;
    QToolButton* jumpTop = nullptr;
    QToolButton* jumpBottom = nullptr;
    QWidget* transcript = nullptr;
    QVBoxLayout* transcriptLayout = nullptr;
    QWidget* inputArea = nullptr;
  };

  struct ChatTitleBarParts {
    QWidget* titleBar = nullptr;
    QLabel* headerIcon = nullptr;
    QLabel* headerTitle = nullptr;
    QToolButton* clearBtn = nullptr;
    QToolButton* floatBtn = nullptr;
    QToolButton* closeBtn = nullptr;
    QList<QToolButton*> dockBtns;
  };

  struct ChatComposerParts {
    QWidget* dropCue = nullptr;
    QLabel* dropCueIcon = nullptr;
    QLabel* dropCueText = nullptr;
    QVariantAnimation* dropCueAnim = nullptr;
    QWidget* suggest = nullptr;
    QToolButton* send = nullptr;
    QToolButton* attach = nullptr;
    QToolButton* gear = nullptr;
    QToolButton* more = nullptr;
    QLabel* statusDot = nullptr;
    QWidget* attachTray = nullptr;
    QProgressBar* busy = nullptr;
    bool busyFlag = false;
    QList<QImage> images;
    QStringList imageNames;
    QString videoPath;
  };

}  // namespace stencil::gui
