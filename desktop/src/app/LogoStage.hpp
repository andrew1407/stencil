#pragma once
// The logo stage (browser js/ui/logoStage.js): a full-window child of the main window paints the
// big mark, its light and its cloud, and while it is up it swallows every key and click the
// editor would have taken. The window passes what it needs as Hooks, so it holds no MainWindow.
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
      std::function<QPixmap(int)> makeMark;      // the mark at a size, in the current accent
      std::function<QColor()> accent;
      std::function<QString()> accentKey;        // a preset key, or "#rrggbb" for a custom one
      std::function<bool()> bareWindow;          // no fullscreen, no modal, no popover
      std::function<void(const QString&)> toast;
      std::function<void()> pinkVibe;
      std::function<void()> stopClick;           // drop the pending accent cycle
      std::function<void(bool)> coverChrome;     // hide the header mark's own overlay
      std::function<void(bool)> hideNotices;    // momentary, for the backdrop photograph only
    };
    LogoStage(QWidget* window, QToolButton* logo, Hooks hooks);

    // The show a hold would open right now, or empty.
    QString heldShow() const;
    bool activateByName(const QString& name);
    void dismiss();
    bool isOpen() const { return open_; }
    // How far the press has carried the light and the cloud: 1 at rest, holdBoost held down.
    double boostNow() const;
    QString showName() const { return show_; }
    // The mark's place and size, as the browser's currentLogoStage() reports them.
    QPointF markPos() const { return pos_; }
    double markSize() const { return size_; }

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
    // The two sizes this show travels between: the one it rests at, and the far end of its bounce.
    std::pair<double, double> ends(int w, int h) const;
    void remakeMark();
    // The big end of this show: a bounce fills the window, anything else rests at logoShare.
    int bigEnd(int w, int h) const;
    void tick();
    void paintCloud(QPainter& p, const support::StagePose& pose);
    bool onMark(const QPoint& at) const;
    void rampBoost(double dt);
    void pressed(const QPoint& at);
    bool typedKey(const QString& text);
    bool lockEvent(QObject* o, QEvent* e);

    QWidget* window_;
    QToolButton* logo_;
    Hooks hooks_;
    QTimer* hold_ = nullptr;     // "logoHold": the press that opens a show
    QTimer* clock_ = nullptr;
    QElapsedTimer since_;
    QPoint pressAt_;
    QString show_;
    QString typed_;
    support::StageEffect effect_ = support::StageEffect::NEON;
    support::StagePose from_;
    support::BounceState bounce_;
    support::FlyState fly_;
    support::ChaseState chase_;
    QPointF heading_;   // the way it travels; the cloud lays its tail the other way
    support::LogoStageCloud cloud_;
    QPointF pos_;
    QPointF cursor_;
    QPixmap mark_;
    QPixmap backdrop_;   // the window behind, blurred as a modal blurs it
    QImage halo_;        // the glow, drawn small and scaled up (LogoStagePaint.cpp)
    bool photographing_ = false;
    bool handCursor_ = false;
    QPointer<QWidget> priorFocus_;   // whatever held the keyboard before the show took it
    double size_ = 0;
    double last_ = 0;
    double refitAt_ = 0;   // ms on `since_` when the deferred refit falls due, 0 = none
    double leftAt_ = -1;   // ms since `since_` when the hide began, else -1
    bool open_ = false;
    double boost_ = 1.0;     // eased toward the press, never stepped
    bool held_ = false;      // the pointer is down on the mark
    bool hasCloud_ = false;
    bool reduced_ = false;
    bool fired_ = false;   // this press already opened a show
  };

}  // namespace stencil::gui
