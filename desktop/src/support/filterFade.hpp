#pragma once
// Filter transitions — the LIGHT twin of disintegrateOverlay's scatter.
//
// A filter is a QUESTION being re-answered, not a removal: a row it excludes was never
// destroyed, so it does not play OUT at all — its slot closes the instant the answer
// changes. The effect belongs to the rows that are LEFT: one already listed re-forms in
// place, one the filter reveals opens its slot first. Playing the excluded rows out put
// the eye on what you had just ruled out and made every keystroke wait on an exit.
//
// Two consumers, one curve: ListFilterFade (QListWidget rows — connectDialog's widget
// rows, projectsDialog's painted ones) owns the clock and the per-row bookkeeping;
// fadeFiltered is the fade alone for a laid-out row with no slot (shortcutsDialog).
//
// Header-only and Q_OBJECT-free (no signals/slots), so it needs no MOC.
#include "disintegrateOverlay.hpp"   // the arrival dust (dustRowIn)
#include "modalReveal.hpp"           // motionReduced()

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

  // A fifth of DisintegrateOverlay::kMs: filtering is a view change, not a removal, and
  // it has to keep up with typing in a search box.
  inline constexpr int kFilterFadeMs = 170;
  inline constexpr int kFilterFadeTickMs = 16;
  // The sand the rows that are LEFT arrive out of (browser js/ui/motion.js filterDust).
  // One shared mesh budget for the whole change, so a search that leaves a dozen rows
  // costs about what one removal does — and past the row ceiling the rest simply fade,
  // which is what the browser's scatterGridFor does too.
  inline constexpr int kFilterDustMaxRows = 8;
  inline constexpr int kFilterDustCells = 2400;
  // …on the FILTER's clock, not a removal's: a view change has to keep up with typing in
  // a search box (browser FILTER_DUST_MS).
  inline constexpr int kFilterDustMs = 253;
  // A row the list genuinely GAINS is not a filter keeping up with a keystroke: the
  // pinned session row a removal reveals, a project a listing brings in. It forms on the
  // browser's arrival clock (motion.js materialize, FILTER_DUST_MS 560) taken at the 1.5
  // ratio the rest of the desktop's motion runs at (kControlRevealInMs = 520 / 1.5).
  inline constexpr int kRowArriveMs = 373;
  // Named apart from the removal's scatter (DisintegrateOverlay::kObjectName) on purpose:
  // a filter's sand is an arrival, and anything counting live removals — the tests
  // included — must never mistake one for the other.
  inline constexpr const char* kFilterDustObjectName = "stencilFilterDust";
  // Where an arriving row's veil lifts, as a share of the gather — the browser's
  // `@keyframes markForm` stop. The motes ARE the row, so it waits behind them.
  inline constexpr double kFilterDustVeilStop = 0.62;

  // Row state lives ON the item, so a list rebuild can never leak a half-faded row.
  // High roles: the dialogs' own data sits at Qt::UserRole..Qt::UserRole+7.
  inline constexpr int kFilterPresenceRole = Qt::UserRole + 40;    // 0 gone … 1 present
  inline constexpr int kFilterTargetRole = Qt::UserRole + 41;      // where it is heading
  inline constexpr int kFilterFullHeightRole = Qt::UserRole + 42;  // uncollapsed height
  // …and the veil a row waits behind while its DUST gathers (0 hidden … 1 shown). The
  // motes ARE the row arriving, so the row itself must not be there yet — the browser's
  // `.mark-forming` / `@keyframes markForm`, written as a role because these rows are
  // painted by a delegate and have no widget to put an effect on.
  inline constexpr int kFilterDustRole = Qt::UserRole + 43;

  // Set on a row WIDGET while its filter fade owns the opacity, so the scroll-edge
  // reveal keeps its hands off it (ScrollReveal::kEnteringProperty idiom).
  inline constexpr const char* kFilterFadeProperty = "stencilFiltering";
  inline constexpr const char* kFilterFadeAnimName = "stencilFilterFade";

  // Where the fade finishes, as a share of the presence range. The fade LEADS the slot:
  // a row is still invisible while its slot is opening, so you never see a squashed
  // half-row with its buttons clipped.
  inline constexpr double kFilterFadeLead = 0.4;

  double filterOpacity(double presence);

  double filterHeightFraction(double presence);

  int filterHeight(int fullHeight, double presence);

  double filterPresenceOf(const QModelIndex& idx);

  double filterInk(const QModelIndex& idx);

  bool filteredIn(const QListWidgetItem* it);

  // Drives a QListWidget's rows between "in the filtered set" and "out of it". One
  // shared tick for the whole list, so N rows cost one pass per frame.
  class ListFilterFade : public QObject {
   public:
    explicit ListFilterFade(QListWidget* list) : QObject(list), list_(list) {}

    // Applies one row's visual state — the height/opacity only the container knows how
    // to reach. Called for every row on every frame.
    std::function<void(QListWidgetItem*, double presence)> writeRow;
    // Run around each pass over the rows: `beforeFrame` to suppress whatever the
    // container hangs off itemChanged (setData fires it), `afterFrame` to repaint.
    std::function<void()> beforeFrame, afterFrame;

    // Called for each row that is LEFT when the answer really changed — the arrival a
    // caller may want to dust. Optional; the fade alone is a complete effect without it.
    std::function<void(QListWidgetItem*)> onArrive;

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
    int dustBudget_ = 0;   // rows already dusted by the filter change in flight
  };

  void fadeFiltered(QWidget* w, bool show);

}  // namespace stencil::gui
