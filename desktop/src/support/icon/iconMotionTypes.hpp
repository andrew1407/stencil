#pragma once
// The icon-motion data model and its canon: the pose/key/part/spec shapes read out of the
// qrc-embedded browser/js/config/iconMotion.json, and the glyph tag index they are applied to.
// Included from iconMotion.hpp, so every call site keeps reaching these by their own names.
#include "iconSet.hpp"

#include <QEasingCurve>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointF>
#include <QString>
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
}  // namespace stencil::gui
