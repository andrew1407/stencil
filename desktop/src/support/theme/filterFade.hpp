#pragma once
// Filter transitions (browser js/ui/motion.js filterDust): an excluded row never plays
// OUT — its slot closes at once; the rows LEFT re-form in place. Q_OBJECT-free, no MOC.
#include "DisintegrateOverlay.hpp"
#include "modalReveal.hpp"

#include <QGraphicsOpacityEffect>
#include <QListWidget>
#include <QListWidgetItem>
#include <QModelIndex>
#include <QObject>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVariantAnimation>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>

namespace stencil::gui {

  // A fifth of DisintegrateOverlay::DUST_MS: a filter has to keep up with typing.
  inline constexpr int FILTER_FADE_MS = 170;
  inline constexpr int FILTER_FADE_TICK_MS = 16;
  // One mesh budget for the whole change; past the row ceiling the rest just fade
  // (browser scatterGridFor).
  inline constexpr int FILTER_DUST_MAX_ROWS = 8;
  inline constexpr int FILTER_DUST_CELLS = 2400;
  inline constexpr int FILTER_DUST_MS = 253;   // browser FILTER_DUST_MS
  // A row the list GAINS forms on the arrival clock both surfaces share (browser
  // motion/enterLeave.js ROW_ARRIVE_MS, the same number)…
  inline constexpr int ROW_ARRIVE_MS = 373;
  // One beat after the rebuild that added it, so the leaving ash has the screen to itself first.
  // NOT the browser's ROW_ARRIVE_DELAY_MS: its scatter runs half again as long as this one's.
  inline constexpr int ROW_ARRIVE_DELAY_MS = 220;
  // Named apart from DisintegrateOverlay::OBJECT_NAME: tests counting removals must not
  // mistake an arrival for one.
  inline constexpr const char* FILTER_DUST_OBJECT_NAME = "stencilFilterDust";
  inline constexpr double FILTER_DUST_VEIL_STOP = 0.62;   // browser `@keyframes markForm` stop

  // Row state lives ON the item so a rebuild never leaks a half-faded row; the dialogs'
  // own data sits at Qt::UserRole..+7.
  inline constexpr int FILTER_PRESENCE_ROLE = Qt::UserRole + 40;    // 0 gone … 1 present
  inline constexpr int FILTER_TARGET_ROLE = Qt::UserRole + 41;
  inline constexpr int FILTER_FULL_HEIGHT_ROLE = Qt::UserRole + 42;
  // The veil a delegate-painted row waits behind while its dust gathers (0 hidden … 1 shown).
  inline constexpr int FILTER_DUST_ROLE = Qt::UserRole + 43;

  // Set while the fade owns a row widget's opacity, so the scroll-edge reveal keeps off.
  inline constexpr const char* FILTER_FADE_PROPERTY = "stencilFiltering";
  inline constexpr const char* FILTER_FADE_ANIM_NAME = "stencilFilterFade";

  // The fade LEADS the slot: a row stays invisible while its slot is still opening.
  inline constexpr double FILTER_FADE_LEAD = 0.4;

  double filterOpacity(double presence);

  double filterHeightFraction(double presence);

  int filterHeight(int fullHeight, double presence);

  double filterPresenceOf(const QModelIndex& idx);

  double filterInk(const QModelIndex& idx);

  bool filteredIn(const QListWidgetItem* it);

  // One shared tick for the whole list.
  class ListFilterFade : public QObject {
   public:
    explicit ListFilterFade(QListWidget* list) : QObject(list), list(list) {}

    std::function<void(QListWidgetItem*, double presence)> writeRow;
    // `beforeFrame` suppresses whatever the container hangs off itemChanged (setData fires it).
    std::function<void()> beforeFrame, afterFrame;
    std::function<void(QListWidgetItem*)> onArrive;   // optional

    void apply(const std::function<bool(QListWidgetItem*)>& wanted);

    bool running() const { return timer && timer->isActive(); }

    void dustRowIn(QListWidgetItem* it, QWidget* host, int ms = FILTER_DUST_MS);

    void finishNow();

   private:
    void write(QListWidgetItem* it, double p, double target);

    void tick();

    void startTicking();
    void stopTicking() { if (timer) timer->stop(); }

    QListWidget* list = nullptr;
    QTimer* timer = nullptr;
    int dustBudget = 0;
  };

  void fadeFiltered(QWidget* w, bool show);

}  // namespace stencil::gui
