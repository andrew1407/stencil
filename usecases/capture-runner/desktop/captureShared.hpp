// Shared bits of the desktop use-case capture (usecases/capture-runner/desktop): where shots go,
// grab-to-file, the frame loop behind the theme-swap clip, and a modal composite that
// paints an exec()'d dialog over its window the way the screen shows it.
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
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QTimer>
#include <cstdio>

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

// A dialog over its window: the window dimmed, the dialog centred on it.
inline void saveOver(const QString& name, QWidget* win, QWidget* dlg) {
  QImage base = win->grab().toImage();
  const QPixmap top = dlg->grab();
  QPainter p(&base);
  p.fillRect(QRect(QPoint(), win->size()), QColor(0, 0, 0, 80));
  p.drawPixmap((win->width() - dlg->width()) / 2, (win->height() - dlg->height()) / 2, top);
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

// PNG frames every `everyMs` for `ms`, for the GIF the Node driver assembles.
inline void film(const QString& name, QWidget* w, int ms, int everyMs) {
  const QString dir = framesDir() + "/" + name;
  QDir().mkpath(dir);
  QElapsedTimer t;
  t.start();
  for (int i = 1; t.elapsed() < ms; ++i) {
    w->grab().save(QStringLiteral("%1/frame-%2.png").arg(dir).arg(i, 4, 10, QLatin1Char('0')), "PNG");
    // A grab can outlast a frame: the animation's timers still need the loop every time.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    const qint64 due = qint64(i) * everyMs;
    while (t.elapsed() < due) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }
  std::printf("  %s/ (frames)\n", qPrintable(name));
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
    for (const QString& name : csv.split(QLatin1Char(','), Qt::SkipEmptyParts)) names_.insert(name.trimmed());
  }
  bool has(const QString& name) const { return names_.isEmpty() || names_.contains(name); }
  bool hasAny(const QStringList& names) const {
    for (const QString& name : names) if (has(name)) return true;
    return false;
  }

 private:
  QSet<QString> names_;
};

// The capture driver; the name is the friend MainWindow.hpp / ChatDock.hpp declare.
class MainWindowGuiTest {
 public:
  static void windowStates(const QString& theme, const ShotSet& shots);
  static void themeClip();
};
