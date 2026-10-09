#pragma once
// The co-edit GUI suites' common ground: the server project they seed and the lines they draw,
// over tests/support/mockRest.hpp. The window's private steps (sync, connections) stay in each
// suite, the window's friend.
#include "../../support/mockRest.hpp"
#include "MainWindow.hpp"
#include "fileStore.hpp"
#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QPointer>

namespace stencil::guitest {

  // A flat 120x80 PNG.
  inline QByteArray pngOf(const QColor& fill) {
    QImage img(120, 80, QImage::Format_RGB32);
    img.fill(fill);
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return png;
  }

  // A horizontal two-point line at height `y`, across most of the picture.
  inline core::Line lineAt(double y) {
    core::Line l;
    l.points = {{10, y}, {100, y}};
    return l;
  }

  // The layout a 120x80 picture with `lines` carries, uncropped.
  inline QJsonObject layoutOf(const core::Lines& lines) {
    return gui::fileStore::buildLayoutJson(120, 80, lines, "none", gui::DEFAULT_ACCENT_HEX,
                                           core::CropRect{0, 0, 120, 80});
  }

  // Project `id` on the stand-in server: a flat picture and no lines yet.
  inline test::MockProject& seedProject(test::MockRest& mock, const QString& id, const QString& name,
                                        const QColor& fill = Qt::white) {
    test::MockProject& p = mock.projects[id];
    p.name = name;
    p.original = pngOf(fill);
    p.layout = layoutOf({});
    return p;
  }

  // A self-owned editor window, shown at the co-edit suites' size; the caller waits for it.
  inline QPointer<gui::MainWindow> newShownWindow() {
    QPointer<gui::MainWindow> win = new gui::MainWindow(nullptr, false);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->resize(1000, 760);
    win->show();
    return win;
  }

}  // namespace stencil::guitest
