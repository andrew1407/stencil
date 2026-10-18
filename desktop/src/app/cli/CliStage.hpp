#pragma once
// CLI mode's stage (browser js/ui/cli/stage.js): the Zig console over the whole window — the
// pinned banner, the status line with the image's name, the scrollback and a prompt — painted as
// a cell grid on the app's theme. A view over the live editor: every command is the console's,
// so each edit is real and stays when the stage leaves. The window hands it Hooks; it holds no MainWindow.
#include "CliBanner.hpp"
#include "CliSkins.hpp"
#include "consoleReply.hpp"
#include "../../support/console/PromptLine.hpp"
#include <QColor>
#include <QFont>
#include <QImage>
#include <QPointer>
#include <QRect>
#include <QWidget>
#include <functional>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QTimer;

namespace stencil::support { class TypewriterReveal; }

namespace stencil::gui {

  class CliPeek;
  class CliSkinFx;

  struct CliStatus {
    QString text;
    QString name;    // the image's, painted in `color`; empty with no image
    QColor color;
  };

  class CliStage : public QWidget {
    Q_OBJECT
   public:
    struct Hooks {
      std::function<QColor()> accent;
      std::function<QString()> accentKey;
      std::function<bool()> dark;
      std::function<void()> cycleAccent;                  // a click on the banner
      std::function<void(const QString& hex)> customAccent;   // a double-click: a random vivid colour
      std::function<void(const QString& line, support::ConsoleDone done)> run;
      std::function<QStringList(const QString& prefix)> completions;
      std::function<CliStatus()> status;
      std::function<QImage()> editingPicture;
      std::function<QImage()> originalPicture;
      std::function<double()> speed;
      std::function<void()> leave;
      std::function<void(const QString& hex)> previewAccent;   // the fairy lights' bulbs: a look, never stored
      std::function<void()> endAccentPreview;                   // …and the accent back when they go
      int holdMs = 3000;   // a press this long on the banner leaves
    };
    CliStage(QWidget* host, Hooks hooks);

    void enter();
    void leave();
    bool isOpen() const { return open; }
    // What Enter does with a line.
    void submit(const QString& line);
    void refreshStatus();
    // A new accent wipes over the banner; a new theme repaints.
    void onThemeChanged();
    const CliSkin* skin() const { return worn; }
    void wearSkin(const CliSkin* next);
    const support::TypewriterReveal& scrollback() const { return *reveal; }
    const support::PromptLine& promptLine() const { return line; }
    CliPeek* peek() const { return peekView; }
    bool wipeRunning() const;
    QRect nameRect() const { return statusName; }
    QRect bannerRect() const { return bannerBox; }
    int bodyTop() const { return headerBottom; }
    // The mouse selection over the body, a terminal's: dragged over cells, Ctrl/Cmd+C copies it.
    QString selectedText() const;
    bool hasSelection() const { return selAnchor.line >= 0 && (selAnchor.line != selEnd.line || selAnchor.col != selEnd.col); }
    QColor accentPainted() const { return paintedAccent; }
    bool pressFrameShown() const { return compactFrame; }

   protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    bool event(QEvent* e) override;   // every chord is the prompt's: no window action fires under the stage
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    bool editChord(QKeyEvent* e);
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

   private:
    enum class Clicked { NONE, NAME, BANNER };
    void print(const QVector<support::ConsoleLine>& lines);
    void applyOutput(const support::ConsoleOutput& out);
    // `/eastereggs` lists the words in gold; a skin word puts its skin on, or off when it is on.
    bool secretWord(const QString& word);
    void relayout();
    void altPeek(const QPoint& at, bool shift);
    void startWipe(const QColor& to);
    void wipeStep();
    void settleWipe();
    int wipeReachNow() const;
    // The paint: a monospace cell grid through QPainter only.
    int cellW() const;
    int cellH() const;
    QColor inkColor() const;
    QColor paperColor() const;
    QColor accentAt(int row, int col, const QVector<cliwipe::RowSpan>& swept) const;
    QColor accentInk() const;   // the accent as the worn skin paints it (cliAccentUnder)
    // A run in the accent at cell column `col0`, re-dressed in the worn skin (restyle.zig): pictures
    // two cells wide when `letters`, a hue a column, the cell painter's fg, else the wipe's seam. Returns the cells taken.
    int paintRun(QPainter& p, int x, int y, const QString& text, int col0, int rowKey, bool letters);
    int paintBanner(QPainter& p, int x, int y);
    void paintWordmark(QPainter& p, int x, int y, int col0);
    int frameNow() const;   // the worn skin's frame, 0 when none ticks
    int paintStatus(QPainter& p, int x, int y);
    void paintRule(QPainter& p, int x, int y, int width);
    void paintBody(QPainter& p, const QRect& box);
    void paintPrompt(QPainter& p, int x, int y);
    bool isSecret(const QString& word) const;
    void scrollBy(int rows);   // older lines up, newer down; 0 = the live bottom
    struct Cell { int line = -1, col = 0; };
    Cell cellAt(const QPoint& at) const;
    void paintSelected(QPainter& p, int i, int x, int y, const QString& text);

    QWidget* hostWindow;
    Hooks hooks;
    QFont mono;
    bool open = false;
    const CliSkin* worn = nullptr;
    support::TypewriterReveal* reveal;
    support::PromptLine line;
    CliPeek* peekView;
    CliSkinFx* fx;
    QTimer* hold;    // "cliHold": the press on the banner that leaves
    QTimer* click;   // the deferred single click, on the name or the banner
    QTimer* press;   // the compact logo frame a banner click shows
    QTimer* wipe;
    Clicked clicked = Clicked::NONE;
    QPoint pressAt;
    bool holdFired = false;
    bool compactFrame = false;
    bool bannerHeld = false;   // the button is down on the banner: the press frame stays
    QColor paintedAccent, wipeFrom, wipeTo;
    QVector<cliwipe::Event> wipeEvents;
    int wipeIndex = 0, wipeX = 0, wipeReach = 0, litLetters = -1;
    QRect statusName, bannerBox;
    int headerBottom = 0;   // y under the header, where the body begins
    int scrollOff = 0;      // lines scrolled up from the live bottom
    int wheelPool = 0;      // wheel pixels not yet a whole row
    int bodyFirst = 0;      // the first scrollback line on screen
    Cell selAnchor, selEnd;
    bool selecting = false;
    int bodyRowsNow = 0;    // the scrollback rows the last paint had room for
    QPointer<QWidget> priorFocus;
  };

}  // namespace stencil::gui
