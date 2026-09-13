#include "ScriptDialog.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QUrl>

// The script window's file half: open, save, copy, and the .stc drops it accepts while open.
namespace stencil::gui {

  namespace {

    const QString& scriptFilter() {
      static const QString filter = QStringLiteral("Stencil script (*.stc)");
      return filter;
    }

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
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    edit_->setPlainText(QString::fromUtf8(file.readAll()));
    edit_->moveCursor(QTextCursor::End);
    return true;
  }

  void ScriptDialog::loadFile() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open script"), QString(),
                                                      scriptFilter());
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
                                                      scriptFilter());
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    file.write(edit_->toPlainText().toUtf8());
  }

}  // namespace stencil::gui
