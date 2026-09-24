#include "guiHelpers.hpp"

#include "theme.hpp"
#include "iconSet.hpp"
#include "skinPrefs.hpp"
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

  namespace {
    // The toolbar's own 32x16 chip (updateColorSwatch), as the browser's <input type="color">.
    const QSize SWATCH_CHIP(32, 16);
    // A well's frame is per-widget QSS, baked in the theme live when written; each well
    // re-swatches itself off the application palette change.
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
    // The frame is taken from the APPLICATION palette: a widget built before applyTheme
    // still carries Qt's default white one, which ringed the wells in near-white.
    const QPalette appPal = QGuiApplication::palette();
    const Palette pal = themePalette(appPal.color(QPalette::Base).lightness() < 128);
    btn->setText(withHex ? color.name().toUpper() : QString());
    btn->setCursor(Qt::PointingHandCursor);
    // Written only when it would change: a live picker preview re-swatches on every drag
    // tick, and each setStyleSheet is a QSS re-parse plus re-polish. Hex sits flush left (browser .vs-color).
    // A skin squares every corner (support/skinPrefs.hpp), the chip below included.
    const bool skin = support::isWebcore();
    const QString sheet = QString("QAbstractButton{background:%1;border:1px solid %2;"
                                  "border-radius:%4;padding:%5;%6}"
                                  "QAbstractButton:hover{border-color:%3;}")
                              .arg(pal.inputBg.name(), pal.borderMain.name(),
                                   appPal.color(QPalette::Highlight).name(),   // the LIVE accent
                                   skin ? QStringLiteral("0px")
                                        : withHex ? QStringLiteral("6px") : QStringLiteral("7px"),
                                   withHex ? QStringLiteral("0 10px") : QStringLiteral("0"),
                                   withHex ? QStringLiteral("text-align:left;color:%1;")
                                                 .arg(pal.inputText.name())
                                           : QString());
    if (btn->styleSheet() != sheet) btn->setStyleSheet(sheet);
    // A luminance-tuned outline, so a colour close to the input's ground stays visible. Alpha honoured.
    QPixmap pm(SWATCH_CHIP);
    pm.fill(Qt::transparent);
    {
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing, !skin);
      const bool lightFill = color.lightnessF() > 0.7;
      p.setPen(QPen(skin ? QColor(Qt::black)
                         : lightFill ? QColor(0, 0, 0, 102) : QColor(255, 255, 255, 102), 1));
      p.setBrush(color);
      const QRectF chip(0.5, 0.5, SWATCH_CHIP.width() - 1.0, SWATCH_CHIP.height() - 1.0);
      if (skin) p.drawRect(chip); else p.drawRoundedRect(chip, 4, 4);
    }
    btn->setIcon(QIcon(pm));
    btn->setIconSize(pm.size());
    // AFTER the stylesheet: setStyleSheet re-polishes and recomputes the minimum from the QSS box.
    btn->setFixedSize(size);
  }
}

