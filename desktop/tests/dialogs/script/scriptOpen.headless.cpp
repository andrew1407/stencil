// The two OS-driven .stc entry points over the REAL MainWindow: a path handed over by the
// shell (MainWindowSource's openPathFromOS) and a file dropped on the window
// (MainWindowDnd's dropEvent). Both must RUN the script, not open it as a document.
#include "CanvasWidget.hpp"
#include "MainWindow.hpp"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QImage>
#include <QMimeData>
#include <QPointF>
#include <QTemporaryDir>
#include <QUrl>
#include <QWindow>
#include <cstdio>

#include "../../support/check.hpp"

using stencil::gui::CanvasWidget;
using stencil::gui::MainWindow;

namespace {

  QString writeScript(const QDir& dir, const QString& name, const QString& text) {
    const QString path = dir.filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(text.toUtf8());
    return path;
  }

  // Hand-built drag: QApplication drops a QDropEvent sent straight at a widget, so the
  // gesture goes to the window handle the way the platform delivers a real one.
  bool dropPath(QWidget* target, const QString& path) {
    QWindow* window = target->windowHandle();
    if (!window) return false;
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    const QPoint at(20, 20);
    QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    qApp->sendEvent(window, &enter);
    QDragMoveEvent move(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    qApp->sendEvent(window, &move);
    QDropEvent drop(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    qApp->sendEvent(window, &drop);
    return drop.isAccepted();
  }

  CanvasWidget* showWindow(MainWindow& win) {
    win.resize(800, 600);
    win.show();
    return win.findChild<CanvasWidget*>();
  }

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);   // offscreen via QT_QPA_PLATFORM

  QTemporaryDir tmp;
  const QDir dir(tmp.path());
  QImage img(40, 30, QImage::Format_RGB32);
  img.fill(QColor(0x33, 0x66, 0xcc));
  const QString png = dir.filePath(QStringLiteral("shot.png"));
  check(img.save(png, "PNG"), "the source image was written");

  // Each script brings its own @source, so it runs on a window that has nothing open.
  const QString drawsRect = writeScript(dir, QStringLiteral("opened.stc"),
      QStringLiteral("@source %1:\n  @rect (1,1) (9,9)\n").arg(png));
  const QString drawsLine = writeScript(dir, QStringLiteral("dropped.stc"),
      QStringLiteral("@source %1:\n  @line (1,1) (5,5) (9,1)\n").arg(png));
  const QString fails = writeScript(dir, QStringLiteral("broken.stc"),
      QStringLiteral("@source %1:\n  @nope 1\n").arg(png));

  std::printf("a .stc opened from the OS runs:\n");
  {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = showWindow(win);
    check(canvas && !canvas->hasImage(), "the window starts empty");
    win.openPathFromOS(drawsRect);
    check(canvas->hasImage(), "the script's @source filled the canvas");
    check(canvas->getLines().size() == 1, "the @rect landed as a layout line");
    if (canvas->getLines().size() == 1)
      check(canvas->getLines()[0].locked, "a @rect is closed, so it fills");
  }

  std::printf("a dropped .stc runs where a dropped image would open:\n");
  {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = showWindow(win);
    check(dropPath(&win, drawsLine), "the window took the drop");
    check(canvas->hasImage(), "the dropped script opened its own source");
    check(canvas->getLines().size() == 1, "the @line landed as a layout line");
    if (canvas->getLines().size() == 1)
      check(canvas->getLines()[0].points.size() == 3, "@line kept its three points");
  }

  std::printf("a dropped .stc is never opened as a picture:\n");
  {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = showWindow(win);
    check(dropPath(&win, fails), "the window took the drop");
    check(!canvas->hasImage(), "a script that fails leaves the canvas as it was");
    win.openPathFromOS(fails);
    check(!canvas->hasImage(), "and so does the same file opened from the OS");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
