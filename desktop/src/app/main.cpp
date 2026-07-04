#include "launchOptions.hpp"
#include "mainWindow.hpp"
#include <QApplication>
#include <QFileOpenEvent>
#include <QIcon>
#include <QProxyStyle>
#include <QStringList>
#include <QStyleFactory>
#include <QUrl>

namespace {

  // QApplication subclass that catches the macOS QFileOpenEvent — emitted when a
  // file is double-clicked in Finder, dropped on the Dock icon, or passed via
  // "Open With" for a type declared in CFBundleDocumentTypes — and, since the
  // bundle also registers the stencil:// scheme (CFBundleURLTypes), when a
  // stencil:// deep link is opened (the event then carries a url, not a file).
  // Either can arrive BEFORE the window exists (at launch), so entries are
  // buffered until the window is registered, then flushed. On other platforms
  // this event never fires (those shells pass the file/URL as an argv positional
  // instead — handled in main() / parseLaunchOptions).
  class StencilApplication : public QApplication {
   public:
    using QApplication::QApplication;

    void setMainWindow(stencil::gui::MainWindow* w) {
      window_ = w;
      for (const QString& f : pending_) route(f);
      pending_.clear();
    }

   protected:
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
          if (window_) route(entry);
          else pending_ << entry;  // buffer until the window is ready
        }
        return true;
      }
      return QApplication::event(e);
    }

   private:
    void route(const QString& entry) {
      if (entry.startsWith(QLatin1String("stencil:"), Qt::CaseInsensitive))
        window_->openStencilUrl(QUrl(entry));
      else
        window_->openPathFromOS(entry);
    }

    stencil::gui::MainWindow* window_ = nullptr;
    QStringList pending_;
  };

  // Tooltips at the default ~700ms wake-up delay read as "not showing"; this proxy makes them
  // appear almost immediately on hover (matching the browser's instant control tooltips) while
  // deferring everything else to the wrapped base style (Fusion).
  class SnappyTooltipStyle : public QProxyStyle {
   public:
    using QProxyStyle::QProxyStyle;
    int styleHint(StyleHint hint, const QStyleOption* opt = nullptr,
                  const QWidget* w = nullptr,
                  QStyleHintReturn* ret = nullptr) const override {
      if (hint == SH_ToolTip_WakeUpDelay) return 120;     // ms (was ~700)
      if (hint == SH_ToolTip_FallAsleepDelay) return 0;
      return QProxyStyle::styleHint(hint, opt, w, ret);
    }
  };

}  // namespace

// Entry point for the desktop app — the counterpart of browser/js/index.js.
int main(int argc, char** argv) {
  StencilApplication app(argc, argv);
  app.setApplicationName("Stencil");
  app.setOrganizationName("Stencil");
  // Window/taskbar icon: the browser app's favicon as a Qt resource. Skipped on
  // macOS, where setWindowIcon() would shadow the bundle's themed AppIcon in the
  // Dock (macOS windows have no title-bar icon); desktop-file is X11/Wayland-only.
#ifndef Q_OS_MACOS
  app.setWindowIcon(QIcon(QStringLiteral(":/icons/appicon.svg")));
  app.setDesktopFileName(QStringLiteral("stencil"));
#endif
  // Fusion honors widget-level QSS + palettes uniformly across the whole app,
  // unlike the native Adwaita/gtk style on Fedora which leaves the menubar /
  // toolbar unthemed. Set it before constructing the window (S14).
  // Wrap Fusion in the snappy-tooltip proxy (QProxyStyle takes ownership of the base style).
  if (auto* fusion = QStyleFactory::create("Fusion")) {
    QApplication::setStyle(new SnappyTooltipStyle(fusion));
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
