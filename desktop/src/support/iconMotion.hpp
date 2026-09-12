#pragma once
// Per-icon hover motion — port of browser/js/config/iconMotion.json (qrc-embedded) and
// its CSS in browser/css/animations.css. QSvgRenderer has no CSS engine, so a frame
// REWRITES the markup: a transform is injected on each `ic-*` hooked element. Q_OBJECT-free.
#include "faceSwap.hpp"
#include "iconSet.hpp"
#include "modalReveal.hpp"

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

  // The browser's `[id^="toggle-"]` opt-out (iconMotion.json trigger.excluded).
  inline constexpr const char* NO_ICON_MOTION_PROPERTY = "stencilNoIconMotion";
  // "active" picks iconMotion.json `variants.active`; a checkable button's checked state
  // means the same and needs no property.
  inline constexpr const char* ICON_STATE_PROPERTY = "stencilIconState";
  inline constexpr const char* ICON_MOTION_ANIM_NAME = "stencilIconMotion";
  inline constexpr const char* MENU_HOVER_WIRED_PROPERTY = "stencilIcmHovered";

  // In the glyph's own 24-unit space; absent fields are identity.
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

  // One per hovered button, parented to it.
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

    bool hasTakenOver() const;

    void paint(double elapsed);

    QAbstractButton* btn_ = nullptr;
    IconRequest req_;
    const IconMotionSpec* spec_ = nullptr;
    const QVector<IconMotionPart>* parts_ = nullptr;
    QVariantAnimation* anim_ = nullptr;
    double elapsed_ = 0;
  };

  // For a menu row (browser: a .chat-more-item / .ctx-item icon animates on row hover),
  // driven from the menu's mouse moves. No face-swap check — action glyphs never swap.
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

    bool hasTakenOver() const;

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

  // Application-wide; Enter/Leave are rare, so it costs nothing at rest.
  inline constexpr const char* ICON_MOTION_FILTER_NAME = "stencilIconMotionFilter";

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
