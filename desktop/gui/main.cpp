#include "mainWindow.hpp"
#include <QApplication>
#include <QIcon>
#include <QStyleFactory>

// Entry point for the desktop app — the counterpart of browser/js/index.js.
int main(int argc, char** argv) {
  QApplication app(argc, argv);
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
  stencil::gui::MainWindow window;
  window.show();
  return app.exec();
}
