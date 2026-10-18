#pragma once
// The console window (browser js/ui/console/modal.js): a non-modal, resizable window on the
// shared modal shell with a typewriter scrollback and a prompt, open beside whatever else is up
// and never dimming it. The command flows arrive as Hooks; it holds no MainWindow.
#include "consoleReply.hpp"
#include "modalChrome.hpp"
#include "../../support/console/PromptLine.hpp"
#include <QColor>
#include <QDialog>
#include <QString>
#include <QStringList>
#include <functional>

class QLineEdit;
class QPlainTextEdit;

namespace stencil::support { class TypewriterReveal; }

namespace stencil::gui {

  class ConsoleWindow : public QDialog {
    Q_OBJECT
   public:
    struct Hooks {
      std::function<void(const QString& line, support::ConsoleDone done)> run;
      std::function<QStringList(const QString& prefix)> completions;
      std::function<double()> speed;
      std::function<QColor()> accent;
      std::function<QColor()> danger;
      std::function<QColor()> gold;
      std::function<QColor()> ink;     // the theme's text colour on the field
      std::function<QColor()> muted;   // the theme's muted text
    };
    ConsoleWindow(QWidget* parent, Hooks hooks);

    void print(const QVector<support::ConsoleLine>& lines);
    void clearOutput();
    // A theme or accent change: every line rewritten in the colours it now has.
    void repaintAll();
    void focusPrompt();
    // What Enter does: the echo, the run, the effect.
    void submit(const QString& line);
    QString outputText() const;
    QLineEdit* promptField() const { return prompt; }
    // The skin the shell was built for: a switch rebuilds the window, as a re-opened dialog would be.
    bool builtForWebcore() const { return webcore; }
    QVector<support::ConsoleLine> scrollbackLines() const;
    support::TypewriterReveal& revealModel() const { return *reveal; }

   signals:
    void closing();   // before the window hides, so a dismissal flight can still grab it

   public slots:
    void reject() override;

   protected:
    bool eventFilter(QObject* o, QEvent* e) override;

   private:
    void applyEffect(support::ConsoleEffect effect);
    // The unsettled tail rewritten on every reveal tick.
    void repaintTail();
    void resetDocument();
    QColor inkFor(support::ConsoleKind kind) const;

    Hooks hooks;
    ModalChrome chrome;
    bool webcore = false;
    QPlainTextEdit* output = nullptr;
    QLineEdit* prompt = nullptr;
    support::TypewriterReveal* reveal = nullptr;
    int committed = 0;   // lines written whole into the document
    int tailStart = 0;   // the document position the unsettled tail begins at
    support::PromptLine line;   // the history and the Tab cycle; the field itself edits the text
  };

}  // namespace stencil::gui
