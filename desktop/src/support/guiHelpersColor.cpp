#include "guiHelpers.hpp"

#include "theme.hpp"
#include "iconSet.hpp"
#include "modalChrome.hpp"   // confirmModal — the browser-styled yes/no question
#include "modalReveal.hpp"   // support::motionReduced()
#include <QAbstractButton>
#include <QBuffer>
#include <QGuiApplication>
#include <QColor>
#include <QComboBox>
#include <QMenu>
#include <QDialog>
#include <QEasingCurve>
#include <QEvent>
#include <QFileDialog>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QVariantAnimation>
#include <QtMath>
#include <cmath>

namespace stencil::gui {

  // The colour chip inside the well — the toolbar's own 32x16 (mainWindow.cpp
  // updateColorSwatch), and what the browser's padded <input type="color"> shows.
  static const QSize kSwatchChip(32, 16);

  namespace {
    // A colour well's frame is per-widget QSS, so it is baked in the theme that was live
    // when it was written — switching the app to the other theme left the wells in the old
    // one (dark wells in a light Settings dialog). Each well re-swatches
    // itself off the application palette change instead.
    class SwatchRestyler : public QObject {
     public:
      using QObject::QObject;
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::PaletteChange || e->type() == QEvent::ApplicationPaletteChange) {
          auto* b = qobject_cast<QAbstractButton*>(o);
          if (b && !b->property("swatchColor").isNull())
            setColorSwatch(b, b->property("swatchColor").value<QColor>(),
                           b->property("swatchSize").toSize(), b->property("swatchHex").toBool());
        }
        return QObject::eventFilter(o, e);
      }
    };
  }  // namespace

  void setColorSwatch(QAbstractButton* btn, const QColor& color, const QSize& size,
                      bool withHex) {
    if (!btn) return;
    // …remembered, so the filter above can rewrite the frame in the theme that arrives.
    btn->setProperty("swatchColor", color);
    btn->setProperty("swatchSize", size);
    btn->setProperty("swatchHex", withHex);
    if (!btn->property("swatchRestyled").toBool()) {
      btn->setProperty("swatchRestyled", true);
      btn->installEventFilter(new SwatchRestyler(btn));
    }
    // ONE colour well across the app: a small colour chip inside the shared input frame,
    // the treatment the toolbar's pickers use (mainWindow.cpp updateColorSwatch) and the
    // one the browser mirrors. Painting the button's whole surface read as a colour slab.
    // The frame is the theme's own inputBg/borderMain, taken from the APPLICATION palette
    // — a widget built before applyTheme still carries Qt's default white one, which
    // resolved the light theme inside a dark app and ringed the wells in near-white.
    const QPalette appPal = QGuiApplication::palette();
    const Palette pal = themePalette(appPal.color(QPalette::Base).lightness() < 128);
    btn->setText(withHex ? color.name().toUpper() : QString());
    btn->setCursor(Qt::PointingHandCursor);
    // The frame's sheet does not depend on `color` — only the chip below does — and a
    // live picker preview re-swatches on every drag tick, each setStyleSheet costing a
    // QSS re-parse and a re-polish. Written only when it would actually change.
    // With the hex, chip + text sit flush left in the frame (browser .vs-color).
    const QString sheet = QString("QAbstractButton{background:%1;border:1px solid %2;"
                                  "border-radius:%4;padding:%5;%6}"
                                  "QAbstractButton:hover{border-color:%3;}")
                              .arg(pal.inputBg.name(), pal.borderMain.name(),
                                   appPal.color(QPalette::Highlight).name(),   // the LIVE accent
                                   withHex ? QStringLiteral("6px") : QStringLiteral("7px"),
                                   withHex ? QStringLiteral("0 10px") : QStringLiteral("0"),
                                   withHex ? QStringLiteral("text-align:left;color:%1;")
                                                 .arg(pal.inputText.name())
                                           : QString());
    if (btn->styleSheet() != sheet) btn->setStyleSheet(sheet);
    // The chip itself: a rounded rect with a soft luminance-tuned outline, so a colour
    // close to the input's own ground stays visible in either theme. Alpha is honoured —
    // a translucent fill shows as one (cssColor.hpp).
    QPixmap pm(kSwatchChip);
    pm.fill(Qt::transparent);
    {
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing);
      const bool lightFill = color.lightnessF() > 0.7;
      p.setPen(QPen(lightFill ? QColor(0, 0, 0, 102) : QColor(255, 255, 255, 102), 1));
      p.setBrush(color);
      p.drawRoundedRect(QRectF(0.5, 0.5, kSwatchChip.width() - 1.0, kSwatchChip.height() - 1.0), 4, 4);
    }
    btn->setIcon(QIcon(pm));
    btn->setIconSize(pm.size());
    // AFTER the stylesheet: setStyleSheet re-polishes the widget, which recomputes its
    // minimum from the QSS box and undid a fixed size set before it.
    btn->setFixedSize(size);
  }
}

