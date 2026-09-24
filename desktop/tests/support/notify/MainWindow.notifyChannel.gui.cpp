// MainWindow GUI e2e — The Notifications row: a two-way select, stored as `notifyChannel`, that
// routes every notice (the chat toast included) to the OS sink or back to the toasts.
// Shared ground (helpers, the loaded window) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../../../src/support/control/dblReset.hpp"
#include <memory>

namespace {
  struct FakeSink : stencil::gui::NotificationSink {
    QStringList shown;
    std::function<void()> lastClick;
    bool show(const stencil::gui::Notice& notice) override {
      shown << notice.text;
      lastClick = notice.onClick;
      return true;
    }
    bool isAvailable() const override { return true; }
    void setActive(bool) override {}
  };

  QStringList standingToasts(QWidget& host) {
    QStringList out;
    for (QLabel* l : host.findChildren<QLabel*>("toast"))
      if (!l->property("stencilToastLeaving").toBool()) out << l->property("stencilToastText").toString();
    return out;
  }
}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void rowIsATwoWaySelectDefaultingToTheApp() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QCOMPARE(win.settings.notifyChannel, QString("toast"));
    QCOMPARE(win.notify->channel(), stencil::gui::NotifyChannel::TOAST);

    stencil::gui::SettingsDialog dlg(win.settings, &win);
    auto* combo = dlg.findChild<QComboBox*>(QStringLiteral("notifyChannelCombo"));
    QVERIFY(combo);
    QCOMPARE(combo->count(), 2);
    QCOMPARE(combo->currentData().toString(), QString("toast"));
    QCOMPARE(combo->itemData(1).toString(), QString("system"));
    QVERIFY2(!dlg.findChild<QCheckBox*>(QStringLiteral("notifyChannelCheck")), "a select, never a checkbox");

    combo->setCurrentIndex(1);
    QCOMPARE(dlg.result().notifyChannel, QString("system"));
    // The rows this dialog does not edit ride through untouched.
    QCOMPARE(dlg.result().motionMode, win.settings.motionMode);
  }

  void settingRoundTripsThroughTheStoreAndReset() {
    Settings s;
    s.notifyChannel = QStringLiteral("system");
    const Settings back = stencil::gui::fileStore::settingsFromJson(stencil::gui::fileStore::settingsToJson(s));
    QCOMPARE(back.notifyChannel, QString("system"));
    QCOMPARE(stencil::gui::fileStore::settingsFromJson(QJsonObject()).notifyChannel, QString("toast"));

    MainWindow win(nullptr, /*restoreLast=*/false);
    stencil::gui::SettingsDialog dlg(s, &win);
    auto* combo = dlg.findChild<QComboBox*>(QStringLiteral("notifyChannelCombo"));
    QVERIFY(combo);
    QCOMPARE(combo->currentData().toString(), QString("system"));
    QCOMPARE(combo->property(stencil::support::RESET_DEFAULT_PROPERTY).toString(), QString("toast"));
  }

  void applySettingsRoutesEveryNoticeByTheChannel() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto owned = std::make_unique<FakeSink>();
    FakeSink* fake = owned.get();
    win.notify->setSystemSink(std::move(owned));

    Settings s = win.settings;
    s.notifyChannel = QStringLiteral("system");
    win.applySettings(s, /*persist=*/false);
    QCOMPARE(win.notify->channel(), stencil::gui::NotifyChannel::SYSTEM);
    win.notify->success(QStringLiteral("Saved"));
    QCOMPARE(fake->shown, QStringList{QStringLiteral("Saved")});
    QVERIFY2(!standingToasts(win).contains(QStringLiteral("Saved")), "no toast on the system channel");

    // The chat's own toast follows the channel too, its click still opening the chat.
    win.showChatToast(QStringLiteral("Assistant finished"), true);
    QCOMPARE(fake->shown.last(), QString("Assistant finished"));
    QVERIFY(fake->lastClick);
    QVERIFY(!win.chatToast || !win.chatToast->isVisible());

    s.notifyChannel = QStringLiteral("toast");
    win.applySettings(s, /*persist=*/false);
    QCOMPARE(win.notify->channel(), stencil::gui::NotifyChannel::TOAST);
    win.notify->success(QStringLiteral("Connected"));
    QTRY_VERIFY(standingToasts(win).contains(QStringLiteral("Connected")));
    QCOMPARE(fake->shown.size(), 2);
    win.showChatToast(QStringLiteral("Assistant finished"), true);
    QVERIFY(win.chatToast && win.chatToast->isVisible());
    QCOMPARE(fake->shown.size(), 2);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.notifyChannel.gui.moc"
