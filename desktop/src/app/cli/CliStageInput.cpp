#include "CliStage.hpp"
#include "CliPeek.hpp"
#include "CliSkinFx.hpp"
#include "consoleGrammar.hpp"
#include "../../support/console/TypewriterReveal.hpp"
#include "../../support/uiTimings.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QRandomGenerator>
#include <QTimer>
#include <QWheelEvent>

// The stage's keys and presses: the prompt's editing, the banner's hold, click and double-click,
// and the gestures on the image's name (browser js/ui/cli/status.js).

namespace stencil::gui {

  void CliStage::submit(const QString& typed) {
    line.setText(typed);
    const QString text = line.take(support::consoleHistoryText);
    if (text.isEmpty()) return;
    scrollOff = 0;   // sending a line brings a scrolled-up view back to the bottom
    // The echo as the Zig console prints it: the prompt and a known command's word in the accent,
    // the arguments plain, a typed key masked; a secret word's line whole in gold.
    const support::ParsedConsoleLine parsed = support::parseConsoleLine(text);
    const QString shown = support::consoleEchoText(text);
    const int wordEnd = shown.indexOf(QLatin1Char(' ')) < 0 ? shown.size() : shown.indexOf(QLatin1Char(' '));
    if (parsed.slashed && isSecret(parsed.word)) print({{QStringLiteral("> ") + text, support::ConsoleKind::SECRET}});
    else print({{QStringLiteral("> ") + shown, support::ConsoleKind::ECHO, support::resolveConsoleCommand(parsed.word, true) ? 2 + wordEnd : 2}});
    if (parsed.slashed && secretWord(parsed.word)) return;
    if (!hooks.run) return;
    hooks.run(text, [guard = QPointer<CliStage>(this)](const support::ConsoleOutput& out) {
      if (guard) guard->applyOutput(out);
    });
  }

  void CliStage::keyPressEvent(QKeyEvent* e) {
    if (e->matches(QKeySequence::Copy) && hasSelection()) {
      QGuiApplication::clipboard()->setText(selectedText());
      e->accept();
      return;
    }
    fx->cutShort();   // a queued key settles whatever plays: the rain, a wipe, the reveal (timing.zig)
    settleWipe();
    reveal->settleNow();
    const bool plain = !(e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier));
    if (editChord(e)) {
      e->accept();
      update();
      return;
    }
    switch (e->key()) {
      case Qt::Key_Escape: break;   // a lone Escape does nothing, as in the Zig line editor
      case Qt::Key_PageUp: scrollBy(qMax(1, bodyRowsNow)); break;
      case Qt::Key_PageDown: scrollBy(-qMax(1, bodyRowsNow)); break;
      case Qt::Key_Return:
      case Qt::Key_Enter: submit(line.text()); break;
      case Qt::Key_Tab:
        line.completeWith(hooks.completions, [this](const QStringList& names) {
          QStringList slashed;
          for (const QString& n : names) slashed << QLatin1Char('/') + n;
          print({{slashed.join(QStringLiteral("  ")), support::ConsoleKind::PLAIN}});
        });
        break;
      case Qt::Key_Up: line.historyUp(); break;
      case Qt::Key_Down: line.historyDown(); break;
      case Qt::Key_Left: line.left(); break;
      case Qt::Key_Right: line.right(); break;
      case Qt::Key_Home: line.home(); break;
      case Qt::Key_End: line.end(); break;
      case Qt::Key_Backspace: line.backspace(); break;
      case Qt::Key_Delete: line.del(); break;
      case Qt::Key_Alt: altPeek(mapFromGlobal(QCursor::pos()), e->modifiers() & Qt::ShiftModifier); break;
      case Qt::Key_Shift:   // Shift joining a held Alt magnifies the peek
        if (e->modifiers() & Qt::AltModifier) altPeek(mapFromGlobal(QCursor::pos()), true);
        break;
      default:
        if (plain && !e->text().isEmpty() && e->text().at(0).isPrint()) line.insert(e->text());
        break;
    }
    e->accept();
    update();
  }

  // The line editor's chords as this platform spells them (Cmd/Option on macOS, Ctrl elsewhere),
  // then the Zig editor's own Ctrl-A/E/K/W (line_edit/read.zig). True when the key was one.
  bool CliStage::editChord(QKeyEvent* e) {
    if (e->matches(QKeySequence::MoveToStartOfLine) || e->matches(QKeySequence::MoveToStartOfBlock)) line.home();
    else if (e->matches(QKeySequence::MoveToEndOfLine) || e->matches(QKeySequence::MoveToEndOfBlock)) line.end();
    else if (e->matches(QKeySequence::MoveToPreviousWord)) line.wordLeft();
    else if (e->matches(QKeySequence::MoveToNextWord)) line.wordRight();
    else if (e->matches(QKeySequence::DeleteStartOfWord)) line.deleteWordBack();
    else if (e->matches(QKeySequence::DeleteEndOfWord)) line.deleteWordForward();
    else if (e->matches(QKeySequence::DeleteEndOfLine)) line.killToEnd();
    else {
      const bool ctrl = e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
      if (!ctrl) return false;
      switch (e->key()) {
        case Qt::Key_A: line.home(); break;
        case Qt::Key_E: line.end(); break;
        case Qt::Key_K: line.killToEnd(); break;
        case Qt::Key_W: line.deleteWordBack(); break;
        case Qt::Key_Backspace: line.killToStart(); break;   // Cmd+Backspace: the line before the caret
        default: return false;
      }
    }
    return true;
  }

  void CliStage::keyReleaseEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Alt) peekView->release();
    if (e->key() == Qt::Key_Shift && (e->modifiers() & Qt::AltModifier)) altPeek(mapFromGlobal(QCursor::pos()), false);
    e->accept();
  }

  // A wheel notch is three rows (screen.zig wheel_step); a trackpad's fine deltas pool until a row.
  void CliStage::wheelEvent(QWheelEvent* e) {
    const int ch = cellH();
    wheelPool += e->pixelDelta().isNull() ? e->angleDelta().y() * 3 * ch / 120 : e->pixelDelta().y();
    const int rows = wheelPool / ch;
    if (rows) {
      scrollBy(rows);
      wheelPool -= rows * ch;
    }
    e->accept();
  }

  // Alt over the name peeks the editing picture while the key is down; Shift too shows it magnified.
  void CliStage::altPeek(const QPoint& at, bool shift) {
    if (statusName.contains(at)) peekView->hold(CliPeek::Kind::EDITING, shift);
    else peekView->release();
  }

  void CliStage::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    if (e->button() != Qt::LeftButton || e->modifiers() != Qt::NoModifier) return;
    const QPoint at = e->position().toPoint();
    selAnchor = selEnd = Cell{};
    update();
    if (at.y() >= headerBottom && !statusName.contains(at)) {
      selAnchor = selEnd = cellAt(at);
      selecting = true;
    } else if (bannerBox.contains(at)) {
      holdFired = false;
      pressAt = e->globalPosition().toPoint();
      hold->start(hooks.holdMs);
      bannerHeld = true;
      compactFrame = true;   // the press frame, on the press (press.zig pressLogo)
      update();
      press->start(cliFx().pressMs);
    } else if (statusName.contains(at) && !click->isActive()) {
      clicked = Clicked::NAME;
      click->start(support::uiTimings().doubleClickMs);
    }
  }

  void CliStage::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    selecting = false;
    bannerHeld = false;
    if (compactFrame && !press->isActive()) {
      compactFrame = false;
      update();
    }
    const bool held = hold->isActive();
    hold->stop();
    if (holdFired) {
      holdFired = false;
      return;
    }
    // A press that neither held nor drifted is a click on the banner; it waits out the
    // double-click window before acting (line_edit/read.zig).
    if (held && bannerBox.contains(e->position().toPoint()) && !click->isActive()) {
      clicked = Clicked::BANNER;
      click->start(support::uiTimings().doubleClickMs);
    }
  }

  void CliStage::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const QPoint at = e->position().toPoint();
    hold->stop();
    click->stop();
    clicked = Clicked::NONE;
    if (statusName.contains(at)) peekView->toggle(CliPeek::Kind::ORIGINAL);
    else if (bannerBox.contains(at)) {   // a double-click: a worn skin off, else a random vivid colour (hooks.zig logoCustom)
      if (worn) wearSkin(nullptr);
      else if (hooks.customAccent) hooks.customAccent(QColor::fromHsvF(QRandomGenerator::global()->bounded(360) / 360.0, 0.7, 0.95).name());
    }
  }

  void CliStage::mouseMoveEvent(QMouseEvent* e) {
    if (selecting && (e->buttons() & Qt::LeftButton)) {
      selEnd = cellAt(e->position().toPoint());
      update();
    }
    if (hold->isActive() && (e->globalPosition().toPoint() - pressAt).manhattanLength() > support::uiTimings().pressSlopPx) hold->stop();
    if (QGuiApplication::keyboardModifiers() & Qt::AltModifier)
      altPeek(e->position().toPoint(), QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
    setCursor(statusName.contains(e->position().toPoint()) || bannerBox.contains(e->position().toPoint()) ? Qt::PointingHandCursor : Qt::ArrowCursor);
  }

  QString CliStage::selectedText() const {
    if (!hasSelection()) return QString();
    const bool forward = selAnchor.line < selEnd.line || (selAnchor.line == selEnd.line && selAnchor.col <= selEnd.col);
    const Cell lo = forward ? selAnchor : selEnd, hi = forward ? selEnd : selAnchor;
    QStringList out;
    for (int i = lo.line; i <= hi.line && i < reveal->count(); ++i) {
      const QString& t = reveal->at(i).text;
      const int c0 = i == lo.line ? lo.col : 0, c1 = i == hi.line ? hi.col : t.size();
      out << t.mid(c0, qMax(0, c1 - c0));
    }
    return out.join(QLatin1Char('\n'));
  }

}  // namespace stencil::gui
