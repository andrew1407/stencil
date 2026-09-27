#pragma once
// Drives one io/MediaLoader load and pumps the event loop until it answers, for the suites that
// walk the loader: its candidate list and its decode routing.
#include "MediaLoader.hpp"

#include <QEventLoop>
#include <QImage>
#include <QString>
#include <QTimer>
#include <functional>

namespace stencil::test {

  struct Run {
    int loads = 0;
    int fails = 0;
    QString error;
    QString localPath;
  };

  // `start` begins the load from inside the loop; the answer (or the 4 s bound) ends it.
  inline Run drive(gui::MediaLoader& loader, const std::function<void()>& start) {
    Run run;
    QEventLoop loop;
    QObject::connect(&loader, &gui::MediaLoader::loaded, &loop,
                     [&](const QImage&, const QString& path) {
                       ++run.loads;
                       run.localPath = path;
                       loop.quit();
                     });
    QObject::connect(&loader, &gui::MediaLoader::failed, &loop, [&](const QString& message) {
      ++run.fails;
      run.error = message;
      loop.quit();
    });
    QTimer::singleShot(0, &loop, start);
    QTimer::singleShot(4000, &loop, &QEventLoop::quit);
    loop.exec();
    loader.disconnect(&loop);
    return run;
  }

}  // namespace stencil::test
