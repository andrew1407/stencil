// The Motion section — the three toggles, the interface-animation mode with its glyphs, and its
// speed — and the Notifications section: where a notice shows.
#include "SettingsDialog.hpp"
#include "../../support/menu/SearchCombo.hpp"
#include "../../support/icon/motionIcons.hpp"
#include "../../support/motionPrefs.hpp"
#include "../../support/console/consolePace.hpp"
#include "../../support/control/dblReset.hpp"
#include "../../support/control/scrubBar.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLocale>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QValidator>
#include <algorithm>
#include <QListView>
#include <QPalette>

namespace stencil::gui {

  namespace {
    // A number typed past an end is let through and lands on that end (the browser field clamps on change).
    class ClampingSpin : public QDoubleSpinBox {
     public:
      using QDoubleSpinBox::QDoubleSpinBox;
      QValidator::State validate(QString& text, int& pos) const override {
        const QValidator::State state = QDoubleSpinBox::validate(text, pos);
        bool ok = false;
        locale().toDouble(text, &ok);
        return state == QValidator::Invalid && ok ? QValidator::Intermediate : state;
      }
      void fixup(QString& text) const override {
        bool ok = false;
        const double v = locale().toDouble(text, &ok);
        if (ok) text = textFromValue(std::clamp(v, minimum(), maximum()));
      }
    };
  }  // namespace

  void SettingsDialog::buildMotionRows(Rows& r, const Settings& current) {
    // Motion (browser modal.js "Motion", the same rows in the same order). Live-applied,
    // so the dialog's own closing flight is already the mode you just picked.
    addSection(r, tr("Motion"));
    // Seeded from the switches in FORCE, not the stored ones: under a skin the interface is
    // still, and a row that said otherwise would be describing something the user cannot see.
    heldMotionMode = current.motionMode;
    heldDrawAnim = current.drawingAnimations;
    // A switch the user moves ends a skin's session override, even one picked back to the stored
    // value (browser setMotionPrefs clears it on every call).
    const auto touched = [this] { motionTouched = true; support::clearMotionOverride(); };

    addCheck(r, drawAnim, support::drawingAnimations(),
          "On: a new point flies to where you put it, popping and rippling as it lands.\n"
          "Off: every point goes straight down.");
    addRow(r, tr("Drawing animation"), drawAnim, /*column=*/false);

    connect(drawAnim, &QCheckBox::clicked, this, touched);

    addCheck(r, multiWindow, current.multiWindow,   // the browser row's data-title, word for word
          "Keep several windows open side by side: nothing is dimmed or blurred, a click outside "
          "closes none, and the app stays usable behind them");
    addRow(r, tr("Multiple windows"), multiWindow, /*column=*/false);

    motionMode = addCombo(r, QString());   // no tooltip — the browser's dropdown has none (the glyphs say it)
    motionMode->setObjectName(QStringLiteral("motionModeCombo"));
    // The browser's MOTION_MODE_LABELS, in its order (ui/motion/motionPrefs.js).
    motionMode->addItem("Dust", "particles");
    motionMode->addItem("Water", "water");
    motionMode->addItem("Fire", "fire");
    motionMode->addItem("Sliding", "slide");
    motionMode->addItem("None", "none");
    // Each mode's glyph (support/icon/motionIcons.hpp — the browser's motion/icons.js): on the
    // trigger at rest, and on the popup rows animated as they are hovered.
    {
      // In the text colour, like the labels (never the accent — user decision).
      const QColor ink = palette().color(QPalette::Text);
      for (int i = 0; i < motionMode->count(); ++i)
        motionMode->setItemIcon(i, support::motionModeIcon(motionMode->itemData(i).toString(), ink));
      auto* mm = static_cast<SearchComboBox*>(motionMode);   // addCombo(r, ) builds SearchComboBoxes
      mm->setListDelegate(new support::MotionIconDelegate(mm->popupList(), mm));
      new support::MotionIconFace(mm);   // the face's glyph plays on change and on hover
    }
    {
      const int idx = motionMode->findData(support::motionModeKey(support::motionMode()));
      motionMode->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    addRow(r, tr("Interface animation"), motionMode);
    connect(motionMode, &QComboBox::activated, this, [this, touched] { touched(); applyLive(); });

    // A drag or a held arrow moves a speed itself at once (one number every animation reads); the
    // whole-window apply and save wait until it rests, or the dialog closes (SettingsDialogs settle-up).
    speedSettle = new QTimer(this);
    speedSettle->setSingleShot(true);
    speedSettle->setInterval(SPEED_SETTLE_MS);
    connect(speedSettle, &QTimer::timeout, this, &SettingsDialog::applyLive);
    const support::MotionSpeedRange& speed = support::motionSpeedRange();
    QWidget* box = buildSpeedPair(r, motionSpeedSpin, motionSpeed, "motionSpeed", speed.min, speed.max, speed.step,
                                  support::clampMotionSpeed(current.motionSpeed));
    addRow(r, tr("Animation speed"), box);
    for (QWidget* target : {box, static_cast<QWidget*>(motionSpeed), static_cast<QWidget*>(motionSpeedSpin)})
      support::setResetHook(target, [this] { setMotionSpeedRow(Settings().motionSpeed); applyLive(); });
    const auto moved = [this](double v) {
      setMotionSpeedRow(v);
      support::setMotionSpeed(motionSpeedSpin->value());
      speedSettle->start();
    };
    connect(motionSpeed, &QSlider::valueChanged, this, [moved](int v) { moved(v / 100.0); });
    connect(motionSpeedSpin, &QDoubleSpinBox::valueChanged, this, moved);

    // The console text speed (browser vs-console-speed): how fast the console windows type.
    const support::ConsoleSpeedRange& pace = support::consoleSpeedRange();
    QWidget* consoleBox = buildSpeedPair(r, consoleSpeedSpin, consoleSpeed, "consoleSpeed", pace.min, pace.max, pace.step,
                                         support::clampConsoleSpeed(current.consoleSpeed));
    addRow(r, tr("Console text speed"), consoleBox);
    for (QWidget* target : {consoleBox, static_cast<QWidget*>(consoleSpeed), static_cast<QWidget*>(consoleSpeedSpin)})
      support::setResetHook(target, [this] { setSpeedPair(consoleSpeedSpin, consoleSpeed, Settings().consoleSpeed); applyLive(); });
    const auto typed = [this](double v) {
      setSpeedPair(consoleSpeedSpin, consoleSpeed, v);
      support::setConsoleSpeed(consoleSpeedSpin->value());
      speedSettle->start();
    };
    connect(consoleSpeed, &QSlider::valueChanged, this, [typed](int v) { typed(v / 100.0); });
    connect(consoleSpeedSpin, &QDoubleSpinBox::valueChanged, this, typed);
  }

  QWidget* SettingsDialog::buildSpeedPair(Rows& r, QDoubleSpinBox*& spin, QSlider*& slider, const char* name,
                                          double min, double max, double step, double current) {
    auto* box = new QWidget(r.host);
    auto* lay = new QHBoxLayout(box);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);
    spin = new ClampingSpin(box);
    spin->setObjectName(QString::fromLatin1(name) + QStringLiteral("Spin"));
    spin->setLocale(QLocale::c());   // "0.37", as the browser writes it
    spin->setRange(min, max);
    spin->setSingleStep(step);
    spin->setDecimals(2);
    spin->setFixedWidth(64);
    slider = new QSlider(Qt::Horizontal, box);
    makeScrubBar(slider, QString::fromLatin1(name) + QStringLiteral("Slider"));
    slider->setRange(qRound(min * 100), qRound(max * 100));
    slider->setSingleStep(qMax(1, qRound(step * 100)));
    lay->addWidget(spin);
    lay->addWidget(slider, 1);
    setSpeedPair(spin, slider, current);
    return box;
  }

  void SettingsDialog::setSpeedPair(QDoubleSpinBox* spin, QSlider* slider, double speed) {
    const QSignalBlocker quietSpin(spin);
    const QSignalBlocker quietSlider(slider);
    spin->setValue(speed);
    slider->setValue(qRound(speed * 100));
  }

  // Reset All's motion half, as the browser's reset does.
  void SettingsDialog::resetMotionRows() {
    const Settings d;
    drawAnim->setChecked(d.drawingAnimations);
    multiWindow->setChecked(d.multiWindow);
    motionMode->setCurrentIndex(qMax(0, motionMode->findData(d.motionMode)));
    setMotionSpeedRow(d.motionSpeed);
    setSpeedPair(consoleSpeedSpin, consoleSpeed, d.consoleSpeed);
  }

  void SettingsDialog::buildNotifyRows(Rows& r, const Settings& current) {
    addSection(r, tr("Notifications"));
    notifyChannel = addCombo(r, "In the app: the notice stack in the window's corner.\n"
                                "System: the OS notification centre, from a tray icon.");
    notifyChannel->setObjectName(QStringLiteral("notifyChannelCombo"));
    notifyChannel->addItem(tr("In the app"), "toast");
    notifyChannel->addItem(tr("System notifications"), "system");
    notifyChannel->setCurrentIndex(qMax(0, notifyChannel->findData(current.notifyChannel)));
    addRow(r, tr("Show notifications"), notifyChannel);
    connect(notifyChannel, &QComboBox::activated, this, &SettingsDialog::applyLive);
  }

}
