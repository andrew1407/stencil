#pragma once
// The logo stage (browser js/ui/logoStage.js): a full-hostWindow child of the main hostWindow paints the
// big mark, its light and its cloud, and while it is up it swallows every key and click the
// editor would have taken. The hostWindow passes what it needs as Hooks, so it holds no MainWindow.
#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QPointer>
#include <QPointF>
#include <QWidget>
#include <functional>
#include <utility>

#include "logoStageCloud.hpp"
#include "logoStageMotion.hpp"
#include "logoStageRules.hpp"

class QTimer;
class QToolButton;

namespace stencil::gui {

  class LogoStage : public QWidget {
    Q_OBJECT
   public:
    struct Hooks {
      std::function<QPixmap(int)> makeMark;      // the mark at a markPx, in the current accent
      std::function<QColor()> accent;
      std::function<QString()> accentKey;        // a preset key, or "#rrggbb" for a custom one
      std::function<bool()> bareWindow;          // no fullscreen, no modal, no popover
      std::function<void(const QString&)> toast;
      std::function<void()> pinkVibe;
      std::function<void()> stopClick;           // drop the pending accent cycle
      std::function<void(bool)> coverChrome;     // hide the header mark's own overlay
      std::function<void(bool)> hideNotices;    // momentary, for the backdrop photograph only
    };
    LogoStage(QWidget* host, QToolButton* logo, Hooks hooks);

    // The showWord a hold would open right now, or empty.
    QString heldShow() const;
    bool activateByName(const QString& name);
    void dismiss();
    bool isOpen() const { return open; }
    // How far the press has carried the light and the cloud: 1 at rest, holdBoost held down.
    double boostNow() const;
    QString showName() const { return showWord; }
    // The mark's place and size, as the browser's currentLogoStage() reports them.
    QPointF markPos() const { return markCentre; }
    double markSize() const { return markPx; }

   protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

   private:
    static constexpr int REFIT_MS = 120;   // ms of quiet that ends a resize drag
    void start(const QString& name);
    void relayout();
    void syncCursor();
    void takeBackdrop();
    // The costly half of a resize, once the drag has settled (LogoStagePaint.cpp).
    void refit();
    // The two sizes this showWord travels between: the one it rests at, and the far end of its bounce.
    std::pair<double, double> ends(int w, int h) const;
    void remakeMark();
    // The big end of this showWord: a bounce fills the hostWindow, anything else rests at logoShare.
    int bigEnd(int w, int h) const;
    void tick();
    void paintCloud(QPainter& p, const support::StagePose& pose);
    bool onMark(const QPoint& at) const;
    void rampBoost(double dt);
    void pressed(const QPoint& at);
    bool typedKey(const QString& text);
    bool lockEvent(QObject* o, QEvent* e);

    QWidget* hostWindow;
    QToolButton* logo;
    Hooks hooks;
    QTimer* hold = nullptr;     // "logoHold": the press that opens a showWord
    QTimer* clock = nullptr;
    QElapsedTimer since;
    QPoint pressAt;
    QString showWord;
    QString typed;
    support::StageEffect effect = support::StageEffect::NEON;
    support::StagePose from;
    support::BounceState bounce;
    support::FlyState fly;
    support::ChaseState chase;
    QPointF heading;   // the way it travels; the cloud lays its tail the other way
    support::LogoStageCloud cloud;
    QPointF markCentre;
    QPointF cursorPos;
    QPixmap mark;
    QPixmap backdrop;   // the hostWindow behind, blurred as a modal blurs it
    QImage halo;        // the glow, drawn small and scaled up (LogoStagePaint.cpp)
    bool photographing = false;
    bool handCursor = false;
    QPointer<QWidget> priorFocus;   // whatever held the keyboard before the showWord took it
    double markPx = 0;
    double last = 0;
    double refitAt = 0;   // ms on `since` when the deferred refit falls due, 0 = none
    double leftAt = -1;   // ms since `since` when the hide began, else -1
    bool open = false;
    double boost = 1.0;     // eased toward the press, never stepped
    bool held = false;      // the pointer is down on the mark
    bool hasCloud = false;
    bool reduced = false;
    bool fired = false;   // this press already opened a showWord
  };

}  // namespace stencil::gui
