#include "TypewriterReveal.hpp"

#include <QDateTime>
#include <QTimer>
#include <algorithm>

namespace stencil::support {

  TypewriterReveal::TypewriterReveal(QObject* parent) : QObject(parent), timer(new QTimer(this)) {
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, &TypewriterReveal::step);
    speed = [] { return consoleSpeed(); };
    clock = [] { return QDateTime::currentMSecsSinceEpoch(); };
  }

  qint64 TypewriterReveal::now() const { return clock(); }

  void TypewriterReveal::enqueue(const QVector<ConsoleLine>& in) {
    if (in.isEmpty()) return;
    // A command's echo lands at once and starts no burst (the Zig console's skip_reveal_once), so
    // the reply that follows it still gets the long sweep.
    const bool echo = std::all_of(in.begin(), in.end(), [](const ConsoleLine& l) { return l.kind == ConsoleKind::ECHO; });
    for (const ConsoleLine& l : in) {
      Line line;
      line.text = l.text;
      line.kind = l.kind;
      line.head = l.head;
      line.print = prints++;
      line.cut = echo || l.text.isEmpty() ? int(l.text.size()) : 0;   // waits its turn; an echo lands whole
      lines.push_back(line);
    }
    if (lines.size() > MAX_LINES) {
      lines.remove(0, lines.size() - MAX_LINES);
      emit trimmed();
    }
    emit changed();
    if (!settled() && !timer->isActive()) arm(1);
  }

  void TypewriterReveal::enqueue(const QStringList& texts, ConsoleKind kind) {
    QVector<ConsoleLine> out;
    for (const QString& t : texts) out.push_back({t, kind});
    enqueue(out);
  }

  void TypewriterReveal::clear() {
    timer->stop();
    lines.clear();
    tracker.reset();
    emit trimmed();
    emit changed();
  }

  void TypewriterReveal::settleNow() {
    timer->stop();
    for (Line& l : lines) l.cut = l.text.size();
    emit changed();
  }

  int TypewriterReveal::firstUnsettled() const {
    for (int i = 0; i < lines.size(); ++i)
      if (lines[i].cut < lines[i].text.size()) return i;
    return -1;
  }

  // One jump of the row whose turn it is (reveal.zig revealNew); its first jump plans it against
  // the burst tracker, and its last stamps the sweep's end.
  void TypewriterReveal::step() {
    const int i = firstUnsettled();
    if (i < 0) return;
    QVector<int> open;
    int reach = 0;
    for (int j = i; j < lines.size() && lines[j].print == lines[i].print; ++j)
      if (lines[j].cut < lines[j].text.size()) { open.push_back(j); reach = std::max(reach, int(lines[j].text.size())); }
    if (lines[i].step == 0) {
      const RevealPlan plan = tracker.plan(now(), speed());
      const int per = plan.jumps <= 0 ? reach : std::max(1, (reach + plan.jumps - 1) / plan.jumps);   // 0 jumps: whole at once
      for (int j : open) { lines[j].step = per; lines[j].stepMs = plan.stepMs; }
    }
    bool more = false;
    for (int j : open) {
      Line& l = lines[j];
      l.cut = std::min(int(l.text.size()), l.cut + l.step);
      more = more || l.cut < l.text.size();
    }
    if (!more) tracker.done(now());
    emit changed();
    if (!settled()) arm(more ? lines[i].stepMs : 1);
  }

  void TypewriterReveal::arm(int ms) { timer->start(std::max(1, ms)); }

}  // namespace stencil::support
