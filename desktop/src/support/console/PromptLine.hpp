#pragma once
// The prompt both consoles type into, as the Zig line editor reads it (cli/src/line_edit/): a
// caret in the line, a 50-deep history that keeps a repeat once and gives the typed line back past
// its newest entry, and Tab completing the first word. Browser twin: js/ui/console/input.js.
#include <QString>
#include <QStringList>
#include <functional>

namespace stencil::support {

  class PromptLine {
   public:
    static constexpr int MAX_HISTORY = 50;   // line_edit/history.zig max_history
    using Completions = std::function<QStringList(const QString& prefix)>;
    using Lister = std::function<void(const QStringList& names)>;

    const QString& text() const { return line; }
    int caret() const { return at; }
    void setText(const QString& text);
    void insert(const QString& text);
    void backspace();
    void del();
    void left() { at = qMax(0, at - 1); }
    void right() { at = qMin(line.size(), at + 1); }
    void home() { at = 0; }
    void end() { at = line.size(); }
    // The word moves and deletes of the Zig line editor (line_edit/words.zig, read.zig): a word
    // is a run of non-blanks, a jump crosses the blanks before it; the kills take the line's ends.
    void wordLeft() { at = wordStart(at); }
    void wordRight() { at = wordEnd(at); }
    void deleteWordBack();
    void deleteWordForward();
    void killToEnd() { line.truncate(at); }
    void killToStart();

    // The trimmed line taken for running, remembered as `remembered` maps it (a typed key, bare);
    // empty when there was nothing.
    QString take(const std::function<QString(const QString&)>& remembered = {});
    void historyUp();
    void historyDown();
    const QStringList& history() const { return past; }

    // Tab (line_edit/editing.zig): only the first word; one match fills in with a space after,
    // several extend to their common prefix, an ambiguous one is listed through `list`.
    void completeWith(const Completions& completions, const Lister& list = {});

   private:
    int wordStart(int from) const;
    int wordEnd(int from) const;

    QString line;
    int at = 0;
    QStringList past;
    int cursor = 0;
    QString stash;   // the line being typed when Up first recalled
  };

}  // namespace stencil::support
