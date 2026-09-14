#pragma once

#include "ScriptDoc.hpp"

#include <QString>
#include <QWidget>

class QFrame;
class QLabel;
class QPlainTextEdit;

namespace stencil::gui {

  class ScriptHighlighter;

  /* The .stc editor both script surfaces host: the halo, the box, the editor, the diagnostics
   * strip and one ScriptHighlighter over one model::ScriptDoc parse. ScriptDialog hosts it at
   * window scale and ScriptMenuPanel at menu scale, differing only in the Style they pass. */
  class ScriptEditorWidget : public QWidget {
    Q_OBJECT

   public:
    struct Style {
      QString glowName, wrapName, editName, diagName;
      int fontPx = 13;
      int padX = 10;
      int padY = 8;
      int lineHeightPct = 155;
      int editorMinH = 160;
      int stripGap = 0;        // the gap the host used to leave between the box and the strip
      int indent = 2;          // spaces a Tab inserts, where the editor owns Tab
      bool codeKeys = false;   // Tab indents and Ctrl/⌘+Enter runs (the flyout)
      bool hoverOnFrame = false;   // hover read off the halo, not the editor
      bool tabStops = false;       // Tab advances `indent` spaces' worth of pixels
    };

    ScriptEditorWidget(QWidget* parent, const Style& style);

    QPlainTextEdit* editor() const { return edit_; }
    QString script() const;
    void setScript(const QString& text);
    bool isEmpty() const;   // nothing but whitespace
    void copyToClipboard() const;

    // The parse the colours came from, so a Run lexes the text once.
    const model::ScriptDoc& program() const { return program_; }

    // Re-lexes and re-colours; the strip stays empty until a run reports.
    void recolour();
    // What the last run made of the script: the first error, or nothing when it was clean.
    void showRunDiagnostics();
    // A theme flip re-inks the formats; the verdict on screen stands.
    void restyleFormats();

   signals:
    // Per keystroke, after the recolour: the hosts gate their own buttons on it.
    void edited(bool empty);
    void runRequested();

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void parseAndPaint(const QString& text, bool withDiagnostics);
    void showFirstDiagnostic(bool withDiagnostics);
    void setFrameState(const char* key, bool on);
    void applyLineHeight();

    Style style_;
    QFrame* glow_ = nullptr;
    QFrame* wrap_ = nullptr;
    QPlainTextEdit* edit_ = nullptr;
    QLabel* diag_ = nullptr;
    ScriptHighlighter* highlighter_ = nullptr;
    model::ScriptDoc program_;
    /* Re-colouring the document is itself a document change, so the editor's textChanged
     * comes back at us mid-paint; without this the two call each other until the stack ends. */
    bool painting_ = false;
  };

}  // namespace stencil::gui
