// Shared bits of the desktop use-case capture (usecases/capture-runner/desktop): where shots go,
// grab-to-file, and the composites that paint a dialog, a popup or another window over the
// main one the way the screen shows it.
#pragma once

#include "MainWindow.hpp"
#include "support/uiPin.hpp"

#include <QAction>
#include <QApplication>
#include <QSet>
#include <QStringList>
#include <QDialog>
#include <QDir>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QString>
#include <QTimer>
#include <cstdio>
#include <functional>

inline QString outDir() { return qEnvironmentVariable("STENCIL_DOCS_OUT"); }
inline QString framesDir() { return qEnvironmentVariable("STENCIL_DOCS_FRAMES"); }

inline void save(const QString& name, const QImage& img) {
  const QString path = outDir() + "/" + name + ".png";
  std::printf("  %s%s\n", qPrintable(name), img.save(path, "PNG") ? ".png" : " FAILED");
}

inline void save(const QString& name, QWidget* w) {
  clearToasts(w);
  pumpFor(60);
  save(name, w->grab().toImage());
}

// A dialog centred over its dimmed window, then cropped to the dialog and a dimmed margin: the rest is
// toolbar the shot is not about, and at full size a small dialog reads as a speck.
inline constexpr int MODAL_MARGIN_PX = 64;

inline void saveOver(const QString& name, QWidget* win, QWidget* dlg) {
  const QPixmap shot = win->grab();
  const QPixmap top = dlg->grab();
  // The canvas takes the dialog AND its margin even when they outgrow the window: composed into
  // the window grab alone, a dialog sized off the screen lost its footer row to the crop below.
  const QSize margin(MODAL_MARGIN_PX * 2, MODAL_MARGIN_PX * 2);
  const QSize canvas = win->size().expandedTo(dlg->size() + margin);
  const qreal dpr = shot.devicePixelRatio();
  QImage base(QSize(qRound(canvas.width() * dpr), qRound(canvas.height() * dpr)),
              QImage::Format_ARGB32_Premultiplied);
  base.setDevicePixelRatio(dpr);
  base.fill(win->palette().color(QPalette::Window));
  QPainter p(&base);
  p.drawPixmap(QPoint((canvas.width() - win->width()) / 2, (canvas.height() - win->height()) / 2), shot);
  p.fillRect(QRect(QPoint(), canvas), QColor(0, 0, 0, 80));
  const QPoint at((canvas.width() - dlg->width()) / 2, (canvas.height() - dlg->height()) / 2);
  p.drawPixmap(at, top);
  p.end();
  QRect box(at - QPoint(MODAL_MARGIN_PX, MODAL_MARGIN_PX), dlg->size() + margin);
  box &= QRect(QPoint(), canvas);
  // grab() hands back a device-pixel image with its ratio set; copy() counts device pixels.
  QImage out = base.copy(QRect(box.topLeft() * dpr, box.size() * dpr));
  out.setDevicePixelRatio(dpr);
  save(name, out);
}

// The window dimmed under popups drawn where they are, cropped to them and a margin; with `centreFirst`
// the first (a dialog) is centred on the window, as saveOver() centres one, the rest kept beside it.
inline void saveLayers(const QString& name, QWidget* win, const QList<QWidget*>& tops, bool centreFirst) {
  const QPixmap shot = win->grab();
  const qreal dpr = shot.devicePixelRatio();
  QPoint shift = -win->geometry().topLeft();
  if (centreFirst && !tops.isEmpty()) {
    const QWidget* first = tops.first();
    shift = QPoint((win->width() - first->width()) / 2, (win->height() - first->height()) / 2)
            - first->geometry().topLeft();
  }
  QRect used;
  for (QWidget* top : tops) used |= top->geometry().translated(shift);
  const QRect canvas = QRect(QPoint(), win->size()) | used.adjusted(-MODAL_MARGIN_PX, -MODAL_MARGIN_PX,
                                                                    MODAL_MARGIN_PX, MODAL_MARGIN_PX);
  QImage base(canvas.size() * dpr, QImage::Format_ARGB32_Premultiplied);
  base.setDevicePixelRatio(dpr);
  base.fill(win->palette().color(QPalette::Window));
  QPainter p(&base);
  p.translate(-canvas.topLeft());
  p.drawPixmap(QPoint(), shot);
  p.fillRect(canvas, QColor(0, 0, 0, 80));
  for (QWidget* top : tops) p.drawPixmap(top->geometry().translated(shift).topLeft(), top->grab());
  p.end();
  const QRect box = used.adjusted(-MODAL_MARGIN_PX, -MODAL_MARGIN_PX, MODAL_MARGIN_PX, MODAL_MARGIN_PX)
                        .translated(-canvas.topLeft());
  QImage out = base.copy(QRect(box.topLeft() * dpr, box.size() * dpr));
  out.setDevicePixelRatio(dpr);
  save(name, out);
}

// The window, an overlay it paints and the dialog, undimmed (a dim would grey the drag out); the overlay
// is drawn in by hand, as a child of the scroll VIEWPORT stays out of the window's grab().
inline void saveDragOver(const QString& name, QWidget* win, QWidget* overlay, QWidget* dlg) {
  QImage base = win->grab().toImage();
  QPainter p(&base);
  if (overlay) p.drawPixmap(overlay->mapTo(win, QPoint()), overlay->grab());
  const QPixmap top = dlg->grab();
  p.drawPixmap((win->width() - dlg->width()) / 2, (win->height() - dlg->height()) / 2, top);
  p.end();
  save(name, base);
}

// Windows that block nothing (a floating dock, the console) drawn where they sit over the main
// one, which stays undimmed: a modal dim would say the opposite of what the shot shows.
inline void saveTops(const QString& name, QWidget* win, const QList<QWidget*>& tops) {
  clearToasts(win);
  pumpFor(60);
  QImage base = win->grab().toImage();
  QPainter p(&base);
  for (QWidget* top : tops) p.drawPixmap(top->geometry().topLeft() - win->geometry().topLeft(), top->grab());
  p.end();
  save(name, base);
}

// Trigger an action that exec()s a dialog, grab the dialog from inside its own loop, close it.
inline void grabModal(stencil::gui::MainWindow& win, QAction* act, const QString& name) {
  if (!act) {
    std::printf("  %s SKIPPED (no action)\n", qPrintable(name));
    return;
  }
  bool done = false;
  QTimer::singleShot(0, [&] {
    QWidget* dlg = nullptr;
    waitUntil([&] { dlg = QApplication::activeModalWidget(); return dlg && dlg->isVisible(); }, 4000);
    if (!dlg) return;
    pumpFor(500);
    saveOver(name, &win, dlg);
    done = true;
    if (auto* d = qobject_cast<QDialog*>(dlg)) d->reject(); else dlg->close();
  });
  QTimer::singleShot(8000, [] { if (QWidget* stuck = QApplication::activeModalWidget()) stuck->close(); });
  act->trigger();
  waitUntil([&] { return done; }, 9000);
  pumpFor(200);
}

// The URLs, prompts and canned plan come from config/shared.json through desktop.mjs; the
// literal is the same value, for a run started by hand.
inline QString envOr(const char* key, const char* fallback) {
  const QString value = qEnvironmentVariable(key);
  return value.isEmpty() ? QString::fromUtf8(fallback) : value;
}

inline QString iconUrl() {
  return envOr("STENCIL_DOCS_ICON_URL", "https://raw.githubusercontent.com/andrew1407/stencil/main/bot/assets/icon.png");
}
inline QString suffixed(const QString& name, const QString& theme) { return name + "-" + theme; }

inline stencil::core::Line line(std::vector<stencil::core::Point> pts, const char* color, double thick, const char* style) {
  stencil::core::Line l;
  l.points = std::move(pts);
  l.color = color;
  l.thickness = thick;
  l.style = style;
  return l;
}

// Menu text carries mnemonics: "&&" is a literal ampersand, a lone "&" marks the key.
inline QAction* actionNamed(QWidget& root, const QString& text) {
  for (QAction* a : root.findChildren<QAction*>()) {
    QString t = a->text();
    t.replace(QLatin1String("&&"), QLatin1String("\x01")).remove('&').replace(QLatin1Char('\x01'), QLatin1Char('&'));
    if (t == text) return a;
  }
  return nullptr;
}

// The shots this pass should take (STENCIL_DOCS_SHOTS, comma-separated); empty takes every one.
class ShotSet {
 public:
  explicit ShotSet(const QString& csv) {
    for (const QString& name : csv.split(QLatin1Char(','), Qt::SkipEmptyParts)) names.insert(name.trimmed());
  }
  bool has(const QString& name) const { return names.isEmpty() || names.contains(name); }
  bool hasAny(const QStringList& names) const {
    for (const QString& name : names) if (has(name)) return true;
    return false;
  }

 private:
  QSet<QString> names;
};

namespace stencil::gui { class OpenImageDialog; }

// The capture driver; the name is the friend MainWindow.hpp / ChatDock.hpp / OpenImageDialog.hpp
// declare — so only ITS members may reach their privates, never a free helper.
class MainWindowGuiTest {
 public:
  static void windowStates(const QString& theme, const ShotSet& shots);
  static void themeClip();
  // windowStates' later shots by feature, each in its turn over the one window.
  static void assistantShots(stencil::gui::MainWindow& win, const ShotSet& shots);
  static void dialogShots(stencil::gui::MainWindow& win, const ShotSet& shots);
  static void videoShots(stencil::gui::MainWindow& win, const ShotSet& shots);
  // The file-drop overlay mid-drag: the save/incognito split, and the one layout zone.
  static void dropShots(stencil::gui::MainWindow& win, const ShotSet& shots);
  // The Make a copy flyouts and dialog, and the swatch picker.
  static void copyScriptShots(stencil::gui::MainWindow& win, const ShotSet& shots);
  // A collaboration server: the Servers dialog connected, and its projects in the Projects list.
  static void serverShots(stencil::gui::MainWindow& win, const ShotSet& shots);
  // The toolbar's lists and views: filters, page sizes, the export options, the compare split.
  static void toolbarShots(stencil::gui::MainWindow& win, const ShotSet& shots);
  // The Open Image dialog on its URL tab with Stencil's icon previewed, then the icon opened whole.
  static void openImageUrlShot(stencil::gui::MainWindow& win, const ShotSet& shots);
  // The chrome drags as frame strips: icon to window, mark to clean view, theme lens, colour swatch.
  static void gestureShots(stencil::gui::MainWindow& win, const ShotSet& shots);
  // The standard edits as frame strips: lines clicked in, then a filter; a rectangle and an area.
  static void editingShots(stencil::gui::MainWindow& win, const ShotSet& shots);
  // Fit the image to the window, then clear the toasts the load left.
  static void fit(stencil::gui::MainWindow& win);
  // The open project becomes `name` (made on first use) holding `picture`: an opened picture, no blank.
  static void useProject(stencil::gui::MainWindow& win, const char* id, const char* name, const QImage& picture);
  // `load` fills a tab as that tab's own control does; `crop` also ticks the quick-crop box.
  static void grabVideoDialog(stencil::gui::MainWindow& win, const QString& name,
                              const std::function<void(stencil::gui::OpenImageDialog*)>& load,
                              bool crop);
};
