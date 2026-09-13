#pragma once

#include <QString>
#include <QWidget>
#include <functional>

class QFrame;
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace stencil::dialogs { class ScriptHighlighter; }

namespace stencil::gui {

  struct Palette;

  /* The script editor hosted INSIDE the canvas context menu (browser js/ui/ctxScript.js +
   * ctxScriptEditor.js): a compact twin of ScriptDialog, not a second editor — the same
   * ScriptHighlighter over the same model::ScriptDoc parse, with the window's own rule that
   * nothing is reported until a run. Typing, running and failing leave the menu open, and
   * the text survives its closing (the panel is the window's, not the menu's). */
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

   protected:
    // Tab indents (the row carries "keepTab", the menu's walk steps aside) and Ctrl/⌘+Enter runs.
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void repaint(bool withDiagnostics);
    void gateActions();
    void run();
    void copyToClipboard();
    void applyLineHeight();

    Hooks hooks_;
    QFrame* glow_ = nullptr;
    QFrame* wrap_ = nullptr;
    QPlainTextEdit* edit_ = nullptr;
    QLabel* diag_ = nullptr;
    QPushButton* copyBtn_ = nullptr;
    QPushButton* downloadBtn_ = nullptr;
    QPushButton* uploadBtn_ = nullptr;
    QPushButton* runBtn_ = nullptr;
    dialogs::ScriptHighlighter* highlighter_ = nullptr;
    bool checked_ = false;   // nothing is reported until the script has been run once
    /* Re-colouring the document is itself a document change, so the editor's textChanged
     * comes back at us mid-paint; without this the two call each other until the stack ends. */
    bool painting_ = false;
  };

  // MainWindow stores the panel as a plain QWidget* member — this types it back.
  inline ScriptMenuPanel* asScriptMenu(QWidget* w) { return static_cast<ScriptMenuPanel*>(w); }

}  // namespace stencil::gui
