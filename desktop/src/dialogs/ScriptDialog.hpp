#pragma once

#include <QDialog>
#include <QString>

class QFrame;
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace stencil::dialogs { class ScriptHighlighter; }

namespace stencil::gui {

  /* The script window: write a .stc, see it coloured as you type, run it on the open
   * project. Browser twin: js/ui/scriptModal.js, down to the behaviour — nothing is
   * reported until the script has been RUN, and Copy / Download / Run are disabled while
   * the editor is empty. Accepted means "run it"; the caller does the running. */
  class ScriptDialog : public QDialog {
    Q_OBJECT

   public:
    explicit ScriptDialog(const QString& initialText, QWidget* parent = nullptr);

    QString script() const;

    // Shows what a run made of the script: the first error, or nothing when it was clean.
    void showRunDiagnostics();

   protected:
    // A .stc dropped on the OPEN window fills the editor; the browser twin does the same.
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    // The frame's hover glow and the editor's focus border, which QSS alone cannot express.
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void repaint(bool withDiagnostics);
    void gateActions();
    void loadFile();
    bool readInto(const QString& path);
    void saveFile();
    void copyToClipboard();
    void setWrapState(const char* key, bool on);

    QFrame* wrap_ = nullptr;
    QPlainTextEdit* edit_ = nullptr;
    QLabel* diag_ = nullptr;
    QPushButton* copyBtn_ = nullptr;
    QPushButton* downloadBtn_ = nullptr;
    QPushButton* runBtn_ = nullptr;
    dialogs::ScriptHighlighter* highlighter_ = nullptr;
    bool checked_ = false;   // nothing is reported until the script has been run once
    /* Re-colouring the document is itself a document change, so the editor's textChanged
     * comes back at us mid-paint; without this the two call each other until the stack ends. */
    bool painting_ = false;
  };

}  // namespace stencil::gui
