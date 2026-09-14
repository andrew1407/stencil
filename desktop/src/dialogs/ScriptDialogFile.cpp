#include "ScriptDialog.hpp"

#include "ScriptEditorWidget.hpp"
#include "scriptFile.hpp"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QMimeData>
#include <QUrl>

// The script window's file half: open, save and the .stc drops it accepts while open.
namespace stencil::gui {

  namespace {

    // The one local .stc among a drag's urls, or empty.
    QString droppedScript(const QMimeData* mime) {
      if (!mime) return QString();
      for (const QUrl& u : mime->urls()) {
        const QString path = u.toLocalFile();
        if (!path.isEmpty() && path.endsWith(QStringLiteral(".stc"), Qt::CaseInsensitive)) return path;
      }
      return QString();
    }

  }  // namespace

  bool ScriptDialog::readInto(const QString& path) {
    QString text;
    if (!readScriptFile(path, &text)) return false;
    editor_->setScript(text);
    return true;
  }

  void ScriptDialog::loadFile() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open script"), QString(),
                                                      scriptFileFilter());
    if (!path.isEmpty()) readInto(path);
  }

  void ScriptDialog::dragEnterEvent(QDragEnterEvent* event) {
    if (!droppedScript(event->mimeData()).isEmpty()) event->acceptProposedAction();
  }

  void ScriptDialog::dropEvent(QDropEvent* event) {
    const QString path = droppedScript(event->mimeData());
    if (path.isEmpty()) return;
    event->acceptProposedAction();
    readInto(path);
  }

  void ScriptDialog::saveFile() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Save script"),
                                                      QStringLiteral("stencil.stc"),
                                                      scriptFileFilter());
    if (path.isEmpty()) return;
    writeScriptFile(path, editor_->script());
  }

}  // namespace stencil::gui
