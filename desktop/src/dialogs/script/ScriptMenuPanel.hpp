#pragma once

#include <QString>
#include <QWidget>
#include <functional>

class QHBoxLayout;
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
    /* Qt takes every popup down when a file dialog opens, so `upload` and `download` are the
     * window's: they dismiss the chain, pick, hand the text back through setScript(), and put
     * the menu back on the script row. */
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
    void gateActions();
    void run();
    int rowWidth() const;   // what the action row needs, gutters included

    Hooks hooks;
    ScriptEditorWidget* edit = nullptr;
    QHBoxLayout* actions = nullptr;   // the row the panel's width is re-derived from
    QPushButton* copyBtn = nullptr;
    QPushButton* downloadBtn = nullptr;
    QPushButton* uploadBtn = nullptr;
    QPushButton* clearBtn = nullptr;
    QPushButton* runBtn = nullptr;
  };

  // MainWindow stores the panel as a plain QWidget* member — this types it back.
  inline ScriptMenuPanel* asScriptMenu(QWidget* w) { return static_cast<ScriptMenuPanel*>(w); }

}  // namespace stencil::gui
