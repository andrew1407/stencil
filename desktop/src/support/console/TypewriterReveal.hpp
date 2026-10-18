#pragma once
// The console's scrollback as it types in: row after row, top to bottom, as the Zig console
// prints a block line by line and reveals each (the first after a pause in the long form, the
// rows pouring after it in the burst form, past the burst window at once), at the pace
// consolePace.hpp sets. One timer; a clock a test can set. Twin of browser/js/ui/console/view.js.
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

#include "consolePace.hpp"
#include "consoleReply.hpp"

class QTimer;

namespace stencil::support {

  class TypewriterReveal : public QObject {
    Q_OBJECT
   public:
    struct Line {
      QString text;
      ConsoleKind kind = ConsoleKind::PLAIN;
      int head = 0;   // leading characters in the accent
      int cut = 0;      // characters shown
      int step = 0;     // characters per jump once planned; 0 = not yet its turn
      int stepMs = 0;   // the pace it was planned at
      int print = 0;    // its own sweep: every row is one, as the Zig console prints them
    };
    // The Zig console's scrollback cap (screen.zig): older lines fall off the top.
    static constexpr int MAX_LINES = 5000;

    explicit TypewriterReveal(QObject* parent = nullptr);

    void setSpeed(std::function<double()> speed) { this->speed = std::move(speed); }
    void setClock(std::function<qint64()> clock) { this->clock = std::move(clock); }

    void enqueue(const QVector<ConsoleLine>& lines);
    void enqueue(const QStringList& texts, ConsoleKind kind);
    void clear();
    void settleNow();
    // One jump of every unfinished line: the timer's, or a test's.
    void step();

    int count() const { return lines.size(); }
    const Line& at(int i) const { return lines.at(i); }
    QString visible(int i) const { return lines.at(i).text.left(lines.at(i).cut); }
    bool settled() const { return firstUnsettled() < 0; }
    int firstUnsettled() const;

   signals:
    void changed();
    // Lines fell off the top; the view's committed text is stale.
    void trimmed();

   private:
    void arm(int ms);
    qint64 now() const;

    QVector<Line> lines;
    int prints = 0;
    QTimer* timer;
    RevealTracker tracker;
    std::function<double()> speed;
    std::function<qint64()> clock;
  };

}  // namespace stencil::support
