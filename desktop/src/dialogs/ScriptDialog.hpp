#pragma once

#include "ScriptDoc.hpp"

#include <QDialog>
#include <QString>

class QPushButton;

namespace stencil::gui {

  class ScriptEditorWidget;

  /* The script window: write a .stc, see it coloured as you type, run it on the open project.
   * Browser twin js/ui/scriptModal.js — nothing is reported until the script has been RUN, and
   * Copy / Download / Run are dead while the editor is empty. Accepted means "run it". */
  class ScriptDialog : public QDialog {
    Q_OBJECT

   public:
    explicit ScriptDialog(const QString& initialText, QWidget* parent = nullptr);

    QString script() const;
    // The parse the colouring came from, so a Run lexes the text once.
    const model::ScriptDoc& program() const;

    // Shows what a run made of the script: the first error, or nothing when it was clean.
    void showRunDiagnostics();

   protected:
    // A .stc dropped on the OPEN window fills the editor; the browser twin does the same.
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

   private:
    void gateActions();
    void loadFile();
    bool readInto(const QString& path);
    void saveFile();

    ScriptEditorWidget* editor_ = nullptr;
    QPushButton* copyBtn_ = nullptr;
    QPushButton* downloadBtn_ = nullptr;
    QPushButton* runBtn_ = nullptr;
  };

}  // namespace stencil::gui
