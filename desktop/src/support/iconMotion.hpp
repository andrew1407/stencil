#pragma once
// Per-icon hover motion — port of browser/js/config/iconMotion.json (qrc-embedded) and
// its CSS in browser/css/animations.css. QSvgRenderer has no CSS engine, so a frame
// REWRITES the markup: a transform is injected on each `ic-*` hooked element. Q_OBJECT-free.
#include "iconMotionTypes.hpp"
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

    void restPaint();   // the rest glyph back on, WITHOUT re-arming a spent settle

    QAbstractButton* btn_ = nullptr;
    IconRequest req_;
    const IconMotionSpec* spec_ = nullptr;
    const QVector<IconMotionPart>* parts_ = nullptr;
    QVariantAnimation* anim_ = nullptr;
    double elapsed_ = 0;
    /* A settle plays ONCE per hover, like the CSS animation-name the browser switches on
     * with :hover (animations/iconHover.css): re-entering while the pointer never left
     * must not replay it, and leaving cancels it back to rest. */
    bool spent_ = false;
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

    void restPaint();

    QAction* act_ = nullptr;
    IconRequest req_;
    const IconMotionSpec* spec_ = nullptr;
    const QVector<IconMotionPart>* parts_ = nullptr;
    QVariantAnimation* anim_ = nullptr;
    double elapsed_ = 0;
    bool spent_ = false;
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
