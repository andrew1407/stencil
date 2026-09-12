#pragma once
// Filter transitions (browser js/ui/motion.js filterDust): an excluded row never plays
// OUT — its slot closes at once; the rows LEFT re-form in place. Q_OBJECT-free, no MOC.
#include "disintegrateOverlay.hpp"
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

  // A fifth of DisintegrateOverlay::kMs: a filter has to keep up with typing.
  inline constexpr int kFilterFadeMs = 170;
  inline constexpr int kFilterFadeTickMs = 16;
  // One mesh budget for the whole change; past the row ceiling the rest just fade
  // (browser scatterGridFor).
  inline constexpr int kFilterDustMaxRows = 8;
  inline constexpr int kFilterDustCells = 2400;
  inline constexpr int kFilterDustMs = 253;   // browser FILTER_DUST_MS
  // A row the list GAINS forms on the browser's arrival clock (motion.js materialize 560) / 1.5.
  inline constexpr int kRowArriveMs = 373;
  // Named apart from DisintegrateOverlay::kObjectName: tests counting removals must not
  // mistake an arrival for one.
  inline constexpr const char* kFilterDustObjectName = "stencilFilterDust";
  inline constexpr double kFilterDustVeilStop = 0.62;   // browser `@keyframes markForm` stop

  // Row state lives ON the item so a rebuild never leaks a half-faded row; the dialogs'
  // own data sits at Qt::UserRole..+7.
  inline constexpr int kFilterPresenceRole = Qt::UserRole + 40;    // 0 gone … 1 present
  inline constexpr int kFilterTargetRole = Qt::UserRole + 41;
  inline constexpr int kFilterFullHeightRole = Qt::UserRole + 42;
  // The veil a delegate-painted row waits behind while its dust gathers (0 hidden … 1 shown).
  inline constexpr int kFilterDustRole = Qt::UserRole + 43;

  // Set while the fade owns a row widget's opacity, so the scroll-edge reveal keeps off.
  inline constexpr const char* kFilterFadeProperty = "stencilFiltering";
  inline constexpr const char* kFilterFadeAnimName = "stencilFilterFade";

  // The fade LEADS the slot: a row stays invisible while its slot is still opening.
  inline constexpr double kFilterFadeLead = 0.4;

  double filterOpacity(double presence);

  double filterHeightFraction(double presence);

  int filterHeight(int fullHeight, double presence);

  double filterPresenceOf(const QModelIndex& idx);

  double filterInk(const QModelIndex& idx);

  bool filteredIn(const QListWidgetItem* it);

  // One shared tick for the whole list.
  class ListFilterFade : public QObject {
   public:
    explicit ListFilterFade(QListWidget* list) : QObject(list), list_(list) {}

    std::function<void(QListWidgetItem*, double presence)> writeRow;
    // `beforeFrame` suppresses whatever the container hangs off itemChanged (setData fires it).
    std::function<void()> beforeFrame, afterFrame;
    std::function<void(QListWidgetItem*)> onArrive;   // optional

    void apply(const std::function<bool(QListWidgetItem*)>& wanted);

    bool running() const { return timer_ && timer_->isActive(); }

    void dustRowIn(QListWidgetItem* it, QWidget* host, int ms = kFilterDustMs);

    void finishNow();

   private:
    void write(QListWidgetItem* it, double p, double target);

    void tick();

    void startTicking();
    void stopTicking() { if (timer_) timer_->stop(); }

    QListWidget* list_ = nullptr;
    QTimer* timer_ = nullptr;
    int dustBudget_ = 0;
  };

  void fadeFiltered(QWidget* w, bool show);

}  // namespace stencil::gui
