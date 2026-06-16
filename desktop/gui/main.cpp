#include "launchOptions.hpp"
#include "mainWindow.hpp"
#include <QApplication>
#include <QFileOpenEvent>
#include <QIcon>
#include <QStringList>
#include <QStyleFactory>

namespace {

  // QApplication subclass that catches the macOS QFileOpenEvent — emitted when a
  // file is double-clicked in Finder, dropped on the Dock icon, or passed via
  // "Open With" for a type declared in CFBundleDocumentTypes. The event can arrive
  // BEFORE the window exists (at launch), so paths are buffered until the window
  // is registered, then flushed. On other platforms this event never fires (those
  // shells pass the file as an argv positional instead — handled in main()).
  class StencilApplication : public QApplication {
   public:
    using QApplication::QApplication;

    void setMainWindow(stencil::gui::MainWindow* w) {
      window_ = w;
      for (const QString& f : pending_) window_->openPathFromOS(f);
      pending_.clear();
    }

   protected:
    bool event(QEvent* e) override {
      if (e->type() == QEvent::FileOpen) {
        const QString file = static_cast<QFileOpenEvent*>(e)->file();
        if (!file.isEmpty()) {
          if (window_) window_->openPathFromOS(file);
          else pending_ << file;  // buffer until the window is ready
        }
        return true;
      }
      return QApplication::event(e);
    }

   private:
    stencil::gui::MainWindow* window_ = nullptr;
    QStringList pending_;
  };

}  // namespace

// Entry point for the desktop app — the counterpart of browser/js/index.js.
int main(int argc, char** argv) {
  StencilApplication app(argc, argv);
  app.setApplicationName("Stencil");
  app.setOrganizationName("Stencil");
  // Window/taskbar icon: the browser app's favicon, embedded as a Qt resource
  // and drawn by the SVG icon engine, instead of the OS default. The desktop
  // file name lets Wayland/X11 taskbars map the window to this icon.
  app.setWindowIcon(QIcon(QStringLiteral(":/icons/appicon.svg")));
  app.setDesktopFileName(QStringLiteral("stencil"));
  // Fusion honors widget-level QSS + palettes uniformly across the whole app,
  // unlike the native Adwaita/gtk style on Fedora which leaves the menubar /
  // toolbar unthemed. Set it before constructing the window (S14).
  if (auto* fusion = QStyleFactory::create("Fusion")) {
    QApplication::setStyle(fusion);
  }
  // Parse CLI launch options before the window so --help/bad args exit cleanly,
  // then apply them after show() (the image/URL/video + layout resolution is
  // async and needs the running event loop). A plain launch is a no-op.
  const stencil::gui::LaunchOptions opts = stencil::gui::parseLaunchOptions(app);
  // An incognito launch (not opening a saved project) starts empty — skip
  // restoring the last session so `--incognito` gives a brand-new blank editor
  // (and `--incognito --src` isn't briefly overlaid by the prior session).
  const bool restoreLast = !(opts.incognito && opts.project.isEmpty());
  stencil::gui::MainWindow window(nullptr, restoreLast);
  // Register the window so any QFileOpenEvent buffered during launch is delivered
  // (and future ones routed) before the event loop starts.
  app.setMainWindow(&window);
  window.show();
  window.applyLaunchOptions(opts);
  return app.exec();
}
