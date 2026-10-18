#include "CliStage.hpp"
#include "CliPeek.hpp"
#include "CliSkinFx.hpp"
#include "consoleGrammar.hpp"
#include "../../support/console/TypewriterReveal.hpp"
#include "../../support/uiTimings.hpp"

#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QTimer>
#include <QWindow>

namespace stencil::gui {

  using support::ConsoleKind;
  using support::ConsoleLine;

  namespace {
    constexpr int PAD = 12;
    // The Zig console's own intro (uiText.txt @intro-fullscreen), less its clipboard clause.
    const char* INTRO = "Console mode — '/command <args>' (the '/' is optional). Tab completes, Up/Down recall history. '/help' lists commands, '/exit' leaves.";
    bool inputEvent(QEvent::Type t) {
      return t == QEvent::KeyPress || t == QEvent::KeyRelease || t == QEvent::ShortcutOverride ||
             t == QEvent::MouseButtonPress || t == QEvent::MouseButtonRelease || t == QEvent::MouseButtonDblClick ||
             t == QEvent::MouseMove || t == QEvent::Wheel;
    }
  }  // namespace

  CliStage::CliStage(QWidget* host, Hooks hooks)
      : QWidget(host), hostWindow(host), hooks(std::move(hooks)), reveal(new support::TypewriterReveal(this)),
        peekView(new CliPeek(this, this->hooks.editingPicture, this->hooks.originalPicture)), fx(new CliSkinFx(this)),
        hold(new QTimer(this)), click(new QTimer(this)), press(new QTimer(this)), wipe(new QTimer(this)) {
    setObjectName(QStringLiteral("cliStage"));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    // The terminal's face wherever it runs: the platforms' fixed fonts first, the system's as the floor.
    mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setFamilies({QStringLiteral("Menlo"), QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono"), mono.family()});
    mono.setStyleHint(QFont::Monospace);
    mono.setFixedPitch(true);
    mono.setPixelSize(13);   // the browser stage's size (cli.css)
    reveal->setSpeed([this] { return this->hooks.speed ? this->hooks.speed() : support::consoleSpeedRange().def; });
    connect(reveal, &support::TypewriterReveal::changed, this, qOverload<>(&QWidget::update));
    for (QTimer* t : {hold, click, press, wipe}) t->setSingleShot(true);
    hold->setObjectName(QStringLiteral("cliHold"));
    connect(hold, &QTimer::timeout, this, [this] {
      holdFired = true;
      if (this->hooks.leave) this->hooks.leave();
    });
    connect(click, &QTimer::timeout, this, [this] {
      if (clicked == Clicked::NAME) peekView->toggle(CliPeek::Kind::EDITING);
      // A banner click takes a worn skin off and changes nothing else; bare, it advances the accent (hooks.zig).
      if (clicked == Clicked::BANNER) {
        if (worn) wearSkin(nullptr);
        else if (this->hooks.cycleAccent) this->hooks.cycleAccent();
      }
      clicked = Clicked::NONE;
    });
    connect(press, &QTimer::timeout, this, [this] {   // the frame's time (press.zig), and then as long as the button stays down
      if (bannerHeld) return;
      compactFrame = false;
      update();
    });
    connect(wipe, &QTimer::timeout, this, &CliStage::wipeStep);
    connect(fx, &CliSkinFx::frame, this, qOverload<>(&QWidget::update));
    connect(fx, &CliSkinFx::rainChanged, this, qOverload<>(&QWidget::update));
    connect(fx, &CliSkinFx::bulb, this, [this](const QColor& c) {
      if (this->hooks.previewAccent) this->hooks.previewAccent(c.name(QColor::HexRgb));
    });
    hostWindow->installEventFilter(this);
    hide();
  }

  void CliStage::enter() {
    if (open) return;
    open = true;
    priorFocus = QApplication::focusWidget();
    paintedAccent = hooks.accent ? hooks.accent() : QColor(Qt::magenta);
    relayout();
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
    qApp->installEventFilter(this);
    reveal->clear();
    print({{QString::fromUtf8(INTRO), ConsoleKind::PLAIN}});
  }

  void CliStage::leave() {
    if (!open) return;
    open = false;
    qApp->removeEventFilter(this);
    settleWipe();
    hold->stop();
    click->stop();
    press->stop();
    compactFrame = false;
    bannerHeld = false;
    fx->stop();
    if (worn && hooks.endAccentPreview) hooks.endAccentPreview();
    worn = nullptr;
    peekView->hideNow();
    hide();
    if (priorFocus) priorFocus->setFocus(Qt::OtherFocusReason);
    else hostWindow->setFocus(Qt::OtherFocusReason);
    priorFocus.clear();
  }

  void CliStage::print(const QVector<ConsoleLine>& lines) { reveal->enqueue(lines); }

  void CliStage::applyOutput(const support::ConsoleOutput& out) {
    if (!out.lines.isEmpty()) print(out.lines);
    if (out.effect == support::ConsoleEffect::CLEAR) reveal->clear();
    if (out.effect == support::ConsoleEffect::EXIT && hooks.leave) hooks.leave();
  }

  bool CliStage::secretWord(const QString& word) {
    if (word == cliListWord()) {
      QVector<ConsoleLine> lines;
      for (const CliSkin& s : cliSkins()) lines.push_back({QLatin1Char('/') + s.word, ConsoleKind::SECRET});
      print(lines);
      return true;
    }
    const CliSkin* s = cliSkinByWord(word);
    if (!s) return false;
    wearSkin(worn == s ? nullptr : s);
    return true;
  }

  void CliStage::wearSkin(const CliSkin* next) {
    settleWipe();
    fx->stop();
    if (worn) {
      if (!worn->phraseOff.isEmpty()) print({{worn->phraseOff, ConsoleKind::SECRET}});
      if (hooks.endAccentPreview) hooks.endAccentPreview();
    }
    worn = next;
    if (next) {
      if (!next->phrase.isEmpty()) print({{next->phrase, ConsoleKind::SECRET}});
      // The blue screen prints its screen: the title bar, then the body.
      if (!next->title.isEmpty()) {
        QVector<ConsoleLine> screen{{next->title, ConsoleKind::TITLE}, {QString(), ConsoleKind::PLAIN}};
        for (const QString& row : next->body.split(QLatin1Char('\n'))) screen.push_back({row, ConsoleKind::PLAIN});
        print(screen);
      }
      const int bodyRows = qMax(0, (height() - PAD - cellH() - headerBottom) / qMax(1, cellH()));
      fx->start(*next, qMax(1, (width() - 2 * PAD) / qMax(1, cellW())), bodyRows);
    }
    update();
  }

  bool CliStage::isSecret(const QString& word) const { return word.toLower() == cliListWord() || cliSkinByWord(word) != nullptr; }

  void CliStage::scrollBy(int rows) {
    scrollOff = qBound(0, scrollOff + rows, qMax(0, reveal->count() - bodyRowsNow));
    update();
  }

  void CliStage::relayout() {
    raise();   // a layout pass may have re-raised a dock edge over the stage
    resize(hostWindow->size());
    move(0, 0);
    peekView->fitTo(width() - 2 * PAD, height());
    update();
  }

  void CliStage::resizeEvent(QResizeEvent*) { peekView->fitTo(width() - 2 * PAD, height()); }

  // While up, the host window's input is the stage's: shortcuts are swallowed, keys typed here,
  // presses elsewhere dropped. Another window (the colour picker a double-click opens) keeps its own.
  bool CliStage::event(QEvent* e) {
    if (open && e->type() == QEvent::ShortcutOverride) {
      e->accept();
      return true;
    }
    return QWidget::event(e);
  }

  bool CliStage::eventFilter(QObject* o, QEvent* e) {
    if (o == hostWindow && e->type() == QEvent::Resize) relayout();
    if (!open || !inputEvent(e->type())) return false;
    auto* w = qobject_cast<QWidget*>(o);
    if (!w || w->window() != hostWindow) return false;
    const bool own = w == this || isAncestorOf(w);
    if (own && e->type() != QEvent::KeyPress && e->type() != QEvent::KeyRelease && e->type() != QEvent::ShortcutOverride) return false;
    if (e->type() == QEvent::ShortcutOverride) {
      e->accept();
      return true;
    }
    // A key is the stage's alone, consumed here so no later filter — the Alt peek of a toolbar
    // icon still latched under the stage — acts on it.
    if (e->type() == QEvent::KeyPress) keyPressEvent(static_cast<QKeyEvent*>(e));
    if (e->type() == QEvent::KeyRelease) keyReleaseEvent(static_cast<QKeyEvent*>(e));
    return true;
  }

}  // namespace stencil::gui
