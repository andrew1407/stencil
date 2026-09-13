#pragma once

#include <QDialog>
#include <QString>

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

   private:
    void repaint(bool withDiagnostics);
    void gateActions();
    void loadFile();
    void saveFile();
    void copyToClipboard();

    QPlainTextEdit* edit_ = nullptr;
    QLabel* diag_ = nullptr;
    QPushButton* copyBtn_ = nullptr;
    QPushButton* downloadBtn_ = nullptr;
    QPushButton* runBtn_ = nullptr;
    dialogs::ScriptHighlighter* highlighter_ = nullptr;
    bool checked_ = false;   // nothing is reported until the script has been run once
  };

}  // namespace stencil::gui
