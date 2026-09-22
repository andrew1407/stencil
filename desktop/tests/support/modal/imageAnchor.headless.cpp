// The open-image flow's two flight rules (support/modal/imageAnchor.hpp), the port of
// browser/tests/ui/modal/imageAnchor.test.js: a confirm about opening an image grows out of the
// CANVAS CENTRE however it was raised, and an answer that OPENS an image pours into the place the
// toolbar's ⧉ icon takes once it is in, while a cancel goes back to the canvas.
#include "imageAnchor.hpp"

#include <QApplication>
#include <QWidget>
#include <cstdio>

#include "../../support/check.hpp"

using namespace stencil::gui;

namespace {
  QRect globalOf(const QWidget& w) { return QRect(w.mapToGlobal(QPoint(0, 0)), w.size()); }

  QRect boxOn(const QWidget& w) { return imageAnchorBox(globalOf(w).center()); }

  QWidget* child(QWidget& host, const char* name, const QRect& at) {
    auto* w = new QWidget(&host);
    w->setObjectName(QLatin1String(name));
    w->setGeometry(at);
    w->show();
    return w;
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  QWidget host;
  host.resize(1000, 800);
  QWidget* viewport = child(host, CANVAS_VIEWPORT_NAME, QRect(100, 100, 600, 400));
  QWidget* openAnother = child(host, OPEN_ANOTHER_BTN_NAME, QRect(820, 12, 28, 24));
  QWidget* openImage = child(host, OPEN_IMAGE_BTN_NAME, QRect(20, 12, 140, 34));
  host.show();

  check(canvasAnchorRect(&host) == boxOn(*viewport), "the canvas anchor is a small box on the viewport centre");
  check(canvasAnchorRect(&host).size() == QSize(IMAGE_ANCHOR_PX, IMAGE_ANCHOR_PX), "…IMAGE_ANCHOR_PX a side");
  // Nothing is loaded here at all, which is the state the ask about a dropped image is raised in.
  check(canvasAnchorRect(&host).isValid(), "it is valid with no image open");
  check(canvasAnchorRect(viewport) == canvasAnchorRect(&host), "any widget of the window answers the same");
  check(canvasAnchorRect(nullptr).isNull(), "no window, no anchor");

  // The icon is where an answer that opens an image lands, hidden or not: the swap that shows it
  // runs after the dialog is gone, so the anchor is read where the icon WILL be.
  check(openImageAnchorRect(&host) == globalOf(*openAnother), "an image is open: the ⧉ icon");
  openAnother->hide();
  check(openImageAnchorRect(&host) == globalOf(*openAnother), "…and it answers while its swap is still in the air");
  openAnother->setObjectName(QStringLiteral("notTheOpenPair"));
  check(openImageAnchorRect(&host) == globalOf(*openImage), "no icon at all: the \"Open Image\" button answers");
  openImage->hide();
  check(openImageAnchorRect(&host) == canvasAnchorRect(&host), "neither half there: the canvas stands in");
  openAnother->setObjectName(QLatin1String(OPEN_ANOTHER_BTN_NAME));
  openAnother->show();
  openImage->show();

  viewport->hide();
  check(canvasAnchorRect(&host) == imageAnchorBox(globalOf(host).center()),
        "nothing laid out: the window's own centre stands in");
  check(openImageAnchorRect(&host) == globalOf(*openAnother), "the toolbar is unaffected");
  viewport->show();

  const FlightAnchors flight = openImageConfirmFlight(&host);
  check(flight.openRect == canvasAnchorRect(&host), "the confirm grows out of the canvas");
  check(bool(flight.closeRectFor), "and resolves where it lands from the outcome");
  check(flight.closeRectFor(true) == globalOf(*openAnother), "an answer that opens an image lands on the Open control");
  check(flight.closeRectFor(false) == canvasAnchorRect(&host), "a cancel pours back into the canvas");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
