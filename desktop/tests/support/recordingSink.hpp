#pragma once
// Every notice a window raises, whatever its lifetime on screen: a GUI case installs it as the
// system sink and reads `shown` instead of waiting on a toast.
#include "../../src/support/notify/Notifications.hpp"
#include <QStringList>

namespace stencil::test {

  struct RecordingSink : gui::NotificationSink {
    QStringList shown;
    bool show(const gui::Notice& n) override { shown << n.text; return true; }
    bool isAvailable() const override { return true; }
    void setActive(bool) override {}
    // How many notices so far contain `fragment`.
    int count(const QString& fragment) const { return int(shown.filter(fragment).size()); }
  };

}  // namespace stencil::test
