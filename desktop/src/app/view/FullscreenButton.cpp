// The fullscreen toggle's click / double-click / right-click / hold, its menu of both kinds, and the
// action texts and tooltip that follow the live kind (browser ui/fullscreen/button.js).
#include "MainWindow.hpp"
#include "EditorView.hpp"
#include "iconSet.hpp"
#include "menuReveal.hpp"
#include "menuRowPolish.hpp"
#include "uiTimings.hpp"
#include "toolbarMetrics.hpp"

#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QTimer>
#include <QToolButton>

namespace stencil::gui {

  namespace {
    QString comboOf(const QAction* a) {
      return a && !a->shortcut().isEmpty() ? a->shortcut().toString(QKeySequence::NativeText) : QString();
    }

    // Clicks are counted on release, so a press can still become a hold; the press itself is
    // swallowed, or the button would trigger its action at once.
    class FullscreenButtonFilter : public QObject {
     public:
      FullscreenButtonFilter(QToolButton* btn, EditorView& view) : QObject(btn), btn(btn), view(view) {
        click.setSingleShot(true);
        click.setInterval(support::uiTimings().doubleClickMs);
        hold.setSingleShot(true);
        hold.setInterval(support::uiTimings().longPressMs);
        QObject::connect(&click, &QTimer::timeout, this, [this] { this->view.fullscreenButtonClick(false); });
        QObject::connect(&hold, &QTimer::timeout, this, [this] {
          swallow = true;
          click.stop();
          this->view.popupFullscreenMenu(this->btn, /*held=*/true);
        });
      }

     protected:
      bool eventFilter(QObject* obj, QEvent* e) override {
        if (obj != btn) return false;
        const auto mouse = [e] { return static_cast<QMouseEvent*>(e); };
        switch (e->type()) {
          case QEvent::MouseButtonPress:
          case QEvent::MouseButtonDblClick:
            if (mouse()->button() != Qt::LeftButton) return false;
            if (!btn->isEnabled()) return true;
            press = mouse()->position().toPoint();
            swallow = false;
            hold.start();
            return true;
          case QEvent::MouseMove:
            if (hold.isActive() && (mouse()->position().toPoint() - press).manhattanLength() > support::uiTimings().pressSlopPx)
              hold.stop();
            return false;
          case QEvent::MouseButtonRelease:
            if (mouse()->button() != Qt::LeftButton) return false;
            hold.stop();
            if (swallow || !btn->rect().contains(mouse()->position().toPoint())) { swallow = false; return true; }
            if (click.isActive()) { click.stop(); view.fullscreenButtonClick(true); } else click.start();
            return true;
          case QEvent::ContextMenu:
            click.stop();
            hold.stop();
            view.popupFullscreenMenu(btn, /*held=*/false);
            return true;
          case QEvent::Enter:
            view.syncFullscreenTip();   // a rebound combo shows at once
            return false;
          default:
            return false;
        }
      }

     private:
      QToolButton* btn;
      EditorView& view;
      QTimer click, hold;
      QPoint press;
      bool swallow = false;
    };
    // A list the hold opened answers that press's release: the row under it is chosen, anywhere else
    // closes (Qt would leave it open for a release that never pressed inside it).
    constexpr char HELD_PROPERTY[] = "fsHeldOpen";
    class HeldReleaseFilter : public QObject {
     public:
      explicit HeldReleaseFilter(QMenu* menu) : QObject(menu), menu(menu) {}

     protected:
      bool eventFilter(QObject* obj, QEvent* e) override {
        if (obj != menu || e->type() != QEvent::MouseButtonRelease || !menu->property(HELD_PROPERTY).toBool()) return false;
        menu->setProperty(HELD_PROPERTY, false);
        QAction* a = menu->actionAt(static_cast<QMouseEvent*>(e)->position().toPoint());
        menu->hide();
        if (a && a->isEnabled() && !a->isSeparator()) a->trigger();
        return true;
      }

     private:
      QMenu* menu;
    };
  }  // namespace

  void EditorView::wireFullscreenButton() {
    syncFullscreenTip();
    for (QToolButton* b : w.findChildren<QToolButton*>())
      if (b->defaultAction() == w.acts.fullscreen) b->installEventFilter(new FullscreenButtonFilter(b, *this));
  }

  // While on, either gesture only leaves.
  void EditorView::fullscreenButtonClick(bool inWindow) {
    if (w.fs.active) toggleFullscreen();
    else toggleFullscreenKind(inWindow);
  }

  void EditorView::popupFullscreenMenu(QToolButton* btn, bool held) {
    if (!fsMenu) {
      fsMenu = new QMenu(&w);
      support::wireMenuRowPolish(fsMenu, &w, /*compact=*/true);
      support::revealMenuFrom(*fsMenu, btn);
      fsMenu->installEventFilter(new HeldReleaseFilter(fsMenu));
    }
    fsMenu->setProperty(HELD_PROPERTY, held);
    fsMenu->clear();
    addFullscreenKindRows(*fsMenu, TOOL_ICON);
    fsMenu->popup(btn->mapToGlobal(QPoint(0, btn->height())));
  }

  // Each kind's row, resting and live: the glyph and the word (browser fullscreen/button.js ROWS).
  struct FullscreenRow { const char* glyph; const char* label; };
  static constexpr FullscreenRow FULLSCREEN_ROWS[2][2] = {
      {{"monitor", "Full Screen"}, {"minimize", "Exit Full Screen"}},
      {{"maximize", "Fullscreen in Window"}, {"minimize", "Exit Fullscreen in Window"}}};

  void EditorView::addFullscreenKindRows(QMenu& menu, int iconPx) {
    for (const bool inWindow : {false, true}) {
      const bool live = w.fs.active && w.fs.inWindow == inWindow;
      const FullscreenRow& row = FULLSCREEN_ROWS[inWindow ? 1 : 0][live ? 1 : 0];
      const QString label = QString::fromLatin1(row.label);
      const QString combo = comboOf(inWindow ? w.acts.fullscreenWindow : w.acts.fullscreen);
      QAction* a = menu.addAction(themedIcon(QString::fromLatin1(row.glyph), w.painted.iconColor, iconPx),
                                  combo.isEmpty() ? label : label + QLatin1Char('\t') + combo);
      // A turn later: the menu that holds the row is closing, and the switch re-lays the window.
      QObject::connect(a, &QAction::triggered, &w, [this, inWindow] {
        QTimer::singleShot(0, &w, [this, inWindow] { toggleFullscreenKind(inWindow); });
      });
    }
  }

  // Every Enter of the button asks; nothing is rewritten while the kind and the chords stand.
  void EditorView::syncFullscreenTip() {
    if (!w.acts.fullscreen || !w.acts.fullscreenWindow) return;
    const bool screen = w.fs.active && !w.fs.inWindow, window = w.fs.active && w.fs.inWindow;
    const QString synced = QStringLiteral("%1|%2|%3|%4").arg(screen).arg(window)
                               .arg(comboOf(w.acts.fullscreen), comboOf(w.acts.fullscreenWindow));
    if (w.acts.fullscreenWindow->property("tipSynced").toString() == synced) return;
    w.acts.fullscreenWindow->setProperty("tipSynced", synced);
    w.acts.fullscreen->setText(screen ? QStringLiteral("Exit Full Screen") : QStringLiteral("Full Screen"));
    w.acts.fullscreenWindow->setText(window ? QStringLiteral("Exit Fullscreen in Window") : QStringLiteral("Fullscreen in Window"));
    const QString glyph = window ? QStringLiteral("minimize") : QStringLiteral("monitor");
    w.painted.iconNames.insert(w.acts.fullscreenWindow, glyph);
    w.acts.fullscreenWindow->setIcon(themedIcon(glyph, w.painted.iconColor, TOOL_ICON));
    const QString win = comboOf(w.acts.fullscreenWindow);
    const QString dbl = w.fs.active ? QString()
        : QStringLiteral("\nDouble-click — in the window%1").arg(win.isEmpty() ? QString() : QStringLiteral(", ") + win);
    w.setActionTip(w.acts.fullscreen, (w.fs.active ? QStringLiteral("Exit fullscreen") : QStringLiteral("Full screen"))
                                          + dbl + QStringLiteral("\nRight-click or hold — both kinds"));
    w.setActionTip(w.acts.fullscreenWindow, window ? QStringLiteral("Exit fullscreen in window")
                                                   : QStringLiteral("Fullscreen in the window"));
  }

}  // namespace stencil::gui
