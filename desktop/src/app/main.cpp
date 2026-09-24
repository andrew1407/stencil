#include "launchOptions.hpp"
#include "deferredWrite.hpp"
#include "MainWindow.hpp"
#include "tipContent.hpp"
#include <QApplication>
#include <QFileOpenEvent>
#include <QIcon>
#include "../support/tip/SnappyTooltipStyle.hpp"
#include "../support/guiHelpers.hpp"
#include <QStringList>
#include <QUrl>

namespace {

  // Catches the macOS QFileOpenEvent (Finder / Dock / "Open With", and stencil:// via CFBundleURLTypes, which carries a url).
  // Either can arrive BEFORE the window exists, so entries are buffered until a window registers. Other platforms pass argv.
  class StencilApplication : public QApplication {
   public:
    using QApplication::QApplication;

    void setMainWindow(stencil::gui::MainWindow* w) {
      window = w;
      for (const QString& f : pending) route(f);
      pending.clear();
    }

   protected:
    // Cmd+Q must work while a modal exec() runs (macOS disables the app menu then).
    bool notify(QObject* receiver, QEvent* e) override {
      // Re-renders every plain tooltip string as the rich tooltip the browser shows; the re-set fires this again,
      // but enrichedToolTip leaves rich text alone, so it settles after one pass.
      if (e->type() == QEvent::ToolTipChange) {
        if (auto* w = qobject_cast<QWidget*>(receiver)) {
          const QString plain = w->toolTip();
          const QString rich = stencil::gui::enrichedToolTip(plain);
          if (!rich.isEmpty()) {
            // The rendering bakes in palette colours, so a theme change rebuilds from the plain source.
            w->setProperty(stencil::gui::PLAIN_TIP_PROPERTY, plain);
            w->setToolTip(rich);
          }
        }
      }
      if (e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->matches(QKeySequence::Quit)) {
          closeAllWindows();
          quit();
          return true;
        }
      }
      return QApplication::notify(receiver, e);
    }

    bool event(QEvent* e) override {
      if (e->type() == QEvent::FileOpen) {
        const auto* fo = static_cast<QFileOpenEvent*>(e);
        // A registered URL scheme arrives with url() set and file() empty.
        const QUrl url = fo->url();
        const QString entry =
            (url.scheme().compare(QLatin1String("stencil"), Qt::CaseInsensitive) == 0)
                ? url.toString(QUrl::FullyEncoded)
                : fo->file();
        if (!entry.isEmpty()) {
          if (window) route(entry);
          else pending << entry;  // buffer until the window is ready
        }
        return true;
      }
      return QApplication::event(e);
    }

   private:
    void route(const QString& entry) {
      if (entry.startsWith(QLatin1String("stencil:"), Qt::CaseInsensitive))
        window->openStencilUrl(QUrl(entry));
      else
        window->openPathFromOS(entry);
    }

    stencil::gui::MainWindow* window = nullptr;
    QStringList pending;
  };

}  // namespace

// Entry point for the desktop app — the counterpart of browser/js/index.js.
int main(int argc, char** argv) {
  StencilApplication app(argc, argv);
  app.setApplicationName("Stencil");
  app.setOrganizationName("Stencil");
  // Qt hides shortcuts in context menus by default; the browser shows its hotkey hints.
  app.setAttribute(Qt::AA_DontShowShortcutsInContextMenus, false);
  // Skipped on macOS, where setWindowIcon() would shadow the bundle's themed AppIcon in the Dock.
#ifndef Q_OS_MACOS
  app.setWindowIcon(QIcon(QStringLiteral(":/icons/appicon.svg")));
  app.setDesktopFileName(QStringLiteral("stencil"));
#endif
  // Fusion honours widget-level QSS uniformly (the native gtk style leaves the menubar unthemed).
  stencil::support::installAppStyle("Fusion");
  stencil::gui::installDisabledCursor(&app);
  // Parse before the window so --help/bad args exit cleanly; apply after show() (resolution is async).
  const stencil::gui::LaunchOptions opts = stencil::gui::parseLaunchOptions(app);
  // An incognito launch starts empty — no session restore.
  const bool restoreLast = !(opts.incognito && opts.project.isEmpty());
  stencil::gui::MainWindow window(nullptr, restoreLast);
  // Register before the event loop so buffered QFileOpenEvents are delivered.
  app.setMainWindow(&window);
  window.show();
  window.applyLaunchOptions(opts);
  const int code = app.exec();
  stencil::gui::deferredWrite::flush();   // nothing debounced leaves the app unwritten
  return code;
}
