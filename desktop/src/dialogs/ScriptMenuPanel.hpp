#pragma once

#include <QString>
#include <QWidget>
#include <functional>

class QPushButton;

namespace stencil::gui {

  struct Palette;
  class ScriptEditorWidget;

  /* The script editor hosted INSIDE the canvas context menu (browser js/ui/ctxScript.js +
   * ctxScriptEditor.js): the window's own ScriptEditorWidget at menu scale, never a second copy
   * of it. Typing, running and failing leave the menu open, and the text survives its closing. */
  class ScriptMenuPanel : public QWidget {
    Q_OBJECT

   public:
    /* A file dialog cannot open under the menu's popup grab, so `upload` and `download` are
     * the window's: they dismiss the chain first and hand the text back through setScript(). */
    struct Hooks {
      std::function<void(QString)> run;
      std::function<void()> upload;
      std::function<void()> download;
      std::function<void(QString)> notice;   // a toast, the browser's notify()
    };

    ScriptMenuPanel(QWidget* parent, Hooks hooks);

    QWidget* editor() const;    // the menu's keyTarget (StayOpenMenu::setInteractiveArea)
    QString script() const;
    void setScript(const QString& text);
    // What the last run made of the script: the first error, or nothing when it was clean.
    void showRunDiagnostics();
    void restyle(const Palette& pal);   // a theme flip re-inks the glyphs and the formats

   private:
    void gateActions(bool empty);
    void run();

    Hooks hooks_;
    ScriptEditorWidget* edit_ = nullptr;
    QPushButton* copyBtn_ = nullptr;
    QPushButton* downloadBtn_ = nullptr;
    QPushButton* uploadBtn_ = nullptr;
    QPushButton* runBtn_ = nullptr;
  };

  // MainWindow stores the panel as a plain QWidget* member — this types it back.
  inline ScriptMenuPanel* asScriptMenu(QWidget* w) { return static_cast<ScriptMenuPanel*>(w); }

}  // namespace stencil::gui
