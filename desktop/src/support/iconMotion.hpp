#pragma once
// Per-icon hover motion — every glyph mimes its OWN action. Port of
// browser/js/config/iconMotion.json (the canonical table, qrc-embedded here) and its CSS
// realisation in browser/css/animations.css. One generic tilt for everything is worse than
// none — a minus that swells reads as "increase".
//
// QSvgRenderer has no CSS engine and cannot address a class, so a frame is produced by
// REWRITING the glyph's markup: a `transform` (and, for draw-on marks, a stroke-dasharray/
// dashoffset) is injected into the start tag of each element carrying the table's `ic-*`
// hook, and the result goes down iconSet's ordinary rasterize path. Those hooks are in the
// shared canon (browser/js/config/icons.json) and inert at rest.
//
// One application-wide event filter (installIconMotion()) is the trigger: a button carries
// no glyph name, but iconSet::iconRequestForKey() traces its QIcon back to the glyph,
// colour and size it was built from, so no call site changes. Only the icon's own pixels
// move, so nothing reflows. Reduced motion cancels it all: the rest pose IS the end state.
//
// Header-only and Q_OBJECT-free (no signals or slots of its own), so it needs no MOC.
#include "faceSwap.hpp"      // faceSwapping() — a face mid-swap owns the glyph
#include "iconSet.hpp"
#include "modalReveal.hpp"   // support::motionReduced()

#include <QAbstractAnimation>
#include <QAbstractButton>
#include <QCoreApplication>
#include <QEasingCurve>
#include <QEvent>
#include <QFile>
#include <QHash>
#include <QHoverEvent>
#include <QIcon>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointF>
#include <QSignalBlocker>
#include <QString>
#include <QVariantAnimation>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace stencil::gui {

  // Set on a control that must keep its glyph out of this: the fold chevrons, whose
  // rotation is STATE (open/closed), not hover feedback — the browser's `[id^="toggle-"]`
  // opt-out (iconMotion.json trigger.excluded).
  inline constexpr const char* kNoIconMotionProperty = "stencilNoIconMotion";
  // Optional state a glyph's motion branches on — "active" picks iconMotion.json's
  // `variants.active` (the fullscreen control that LEAVES fullscreen). A checkable
  // button's checked state means the same thing and needs no property.
  inline constexpr const char* kIconStateProperty = "stencilIconState";
  inline constexpr const char* kIconMotionAnimName = "stencilIconMotion";
  // Set on a QMenu once its hovered() signal has been wired to the row motion.
  inline constexpr const char* kMenuHoverWiredProperty = "stencilIcmHovered";

  // A pose in the glyph's own 24-unit space. Absent fields are identity.
  struct IconPose {
    double tx = 0, ty = 0;
    double rotate = 0;    // degrees, clockwise (SVG y-down)
    double sx = 1, sy = 1;
    double skewX = 0;     // degrees
    double dashOffset = 0;
    bool hasDashOffset = false;
  };

  struct IconMotionKey {
    double at = 0;        // percent along the play, ascending
    IconPose pose;
  };

  struct IconMotionPart {
    QString hook;              // "" = the whole glyph
    QPointF origin{12, 12};    // transform pivot in view-box units
    bool originSelf = false;   // …or the part's OWN centre (CSS transform-box: fill-box)
    int durationMs = 220;
    int delayMs = 0;
    int staggerMs = 0;         // added per element sharing the hook, in markup order
    QEasingCurve::Type easing = QEasingCurve::OutQuint;
    double dashArray = 0;
    IconPose to;                   // hold: the pose held while hovered
    QVector<IconMotionKey> keys;   // settle: played once
  };

  struct IconMotionSpec {
    bool hold = true;
    QVector<IconMotionPart> parts;
    QVector<IconMotionPart> activeParts;   // variants.active, empty when there is none
    int totalMs = 0;                       // longest delay+stagger+duration over the parts
  };

  namespace icm {

    double num(const QJsonObject& o, const char* k, double dflt);

    QEasingCurve::Type easingFor(const QString& css, QEasingCurve::Type dflt);

    QEasingCurve::Type namedEasing(const QString& name, QEasingCurve::Type dflt);

    IconPose readPose(const QJsonObject& o);

    QVector<IconMotionPart> readParts(const QJsonArray& arr, bool hold,
                                      int dfltMs, QEasingCurve::Type dfltEase);

    // One start tag in the canon's inner markup: where an attribute can be injected,
    // its class list, and the tag text itself (for the originSelf centre).
    struct Tag {
      int insertAt = 0;
      QString cls;
      QString text;
    };

    QVector<Tag> scanTags(const QString& s);

    const QVector<Tag>& tagsOf(const QString& glyph);

    bool hasHook(const Tag& t, const QString& hook);

    int hookedCount(const QString& glyph, const QString& hook);

    double attr(const QString& tag, const char* name, double dflt);

    QPointF selfCentre(const Tag& t);

    const QHash<QString, IconMotionSpec>& table();

    inline double mix(double a, double b, double u) { return a + (b - a) * u; }

    IconPose lerpPose(const IconPose& a, const IconPose& b, double u);

    double ease(QEasingCurve::Type type, double u);

    bool isIdentity(const IconPose& p);

    IconPose poseAt(const IconMotionPart& part, bool hold, int index, double elapsed);

    QString transformAttr(const IconPose& p, const QPointF& origin);

  }  // namespace icm

  QString iconMotionMarkup(const QString& glyph, const IconMotionSpec& spec,
                           const QVector<IconMotionPart>& parts, double elapsed);

  QString iconMotionMarkup(const QString& glyph, const IconMotionSpec& spec,
                           double elapsed);

  const IconMotionSpec* iconMotionFor(const QString& glyph);

  // One per hovered button, parented to it. Holds the elapsed clock the parts read and
  // repaints the button's icon from the posed markup each frame.
  class IconMotionRunner : public QObject {
   public:
    IconMotionRunner(QAbstractButton* btn, const IconRequest& req, const IconMotionSpec* spec,
                     const QVector<IconMotionPart>* parts);

    void enter();

    void leave();

    bool running() const { return anim_->state() == QAbstractAnimation::Running; }
    double elapsedMs() const { return elapsed_; }
    const IconRequest& request() const { return req_; }

    void rest();

   private:
    void run(double target);

    bool tookOver() const;

    void paint(double elapsed);

    QAbstractButton* btn_ = nullptr;
    IconRequest req_;
    const IconMotionSpec* spec_ = nullptr;
    const QVector<IconMotionPart>* parts_ = nullptr;
    QVariantAnimation* anim_ = nullptr;
    double elapsed_ = 0;
  };

  // The same runner for a MENU ROW: a QMenu's items are QActions, not buttons, so the
  // hover watcher drives this one from the menu's own mouse moves instead of Enter/
  // Leave (browser parity: a .chat-more-item / .ctx-item icon animates on row hover).
  // No face-swap check — action glyphs never face-swap.
  class ActionIconMotionRunner : public QObject {
   public:
    ActionIconMotionRunner(QAction* act, const IconRequest& req, const IconMotionSpec* spec,
                           const QVector<IconMotionPart>* parts);

    void enter();
    void leave();
    const IconRequest& request() const { return req_; }

    void rest();

   private:
    void run(double target);

    bool tookOver() const;

    void paint(double elapsed);

    QAction* act_ = nullptr;
    IconRequest req_;
    const IconMotionSpec* spec_ = nullptr;
    const QVector<IconMotionPart>* parts_ = nullptr;
    QVariantAnimation* anim_ = nullptr;
    double elapsed_ = 0;
  };

  namespace icm {

    const QVector<IconMotionPart>* partsFor(const QAbstractButton* btn,
                                            const IconMotionSpec& spec);

    bool eligible(QAbstractButton* btn);

    IconMotionRunner* runnerOf(QAbstractButton* btn);

    ActionIconMotionRunner* runnerOfAction(QAction* act);

    const QVector<IconMotionPart>* partsForAction(const QAction* act,
                                                  const IconMotionSpec& spec);

  }  // namespace icm

  // The one application-wide hover watcher. Enter/Leave are rare events, so this costs
  // nothing at rest and needs no per-button installation — which is what lets every
  // dynamically built row, menu panel and dialog get the motion for free.
  inline constexpr const char* kIconMotionFilterName = "stencilIconMotionFilter";

  class IconMotionFilter : public QObject {
   public:
    explicit IconMotionFilter(QObject* parent);

   protected:
    bool eventFilter(QObject* o, QEvent* e) override;

   private:
    void hoverMenuAction(QMenu* menu, QAction* a);

    QHash<QObject*, QPointer<QAction>> menuHover_;
  };

  void installIconMotion();

}  // namespace stencil::gui
