#pragma once

// Small inline helpers shared by the MainWindow translation units
// (mainWindow.cpp and the TUs split out of it). Header-only on purpose.

#include "pageMetrics.hpp"

#include <QApplication>
#include <QBuffer>
#include <QDateTime>
#include <QEasingCurve>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QRandomGenerator>
#include <QString>
#include <QTextEdit>
#include <QVariantAnimation>
#include <functional>
#include <string>

namespace stencil::gui {

  // ── shared toolbar metrics ──
  inline constexpr int kToolIcon = 18;   // toolbar glyph box
  inline constexpr int kToolRowH = 33;   // icon-button height; every section row matches it
  // The "Controls" pill's chevron sits BESIDE its label, so it is sized against
  // the text (a toolbar-sized 18 px glyph towered over it).
  inline constexpr int kPillChevron = 7;    // Controls show/hide arrow (1.5x smaller, user decision)
  inline constexpr int kHeaderLogo = 33;   // header-row logo mark (user decision); sets the row height floor
  // The f(x,y) inputs take the browser's inline width (#formula-x / #formula-y,
  // toolbar.js) where the row has room, shrinking toward the floor rather than
  // tipping their row into QToolBar's "»".
  inline constexpr int kFormulaFieldW = 180;
  inline constexpr int kFormulaFieldMinW = 72;   // still shows a typical "x/2 + 10"
  // centralLayout_'s RIGHT inset while the points panel is docked open (browser parity: the
  // slim .main-content gap, css/layout.css). LEFT stays 0 — the canvas gets its own left inset
  // instead (see MainWindow's ctor). Collapsed, updatePanelReopenButton swaps this for the
  // wider kCanvasRightMarginCollapsed (mainWindow.cpp), sized for the floating re-open chevron.
  inline constexpr int kCentralSideMargin = 14;

  inline long long nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

  // Dismiss the whole open popup-menu chain. A modal dialog must NEVER open
  // under a menu that still holds the popup grab: it would come up behind the
  // popup and unfocused, and a native file dialog fights the grab outright.
  inline void closeOpenPopupMenus() {
    for (int i = 0; i < 8; ++i) {
      auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!popup) break;
      popup->close();
    }
  }

  // On macOS the primary delete key emits Backspace (⌫), so the shared
  // "Alt+Delete" defaults must bind to Backspace to fire on the key Mac users
  // actually press (mirrors the browser's platformizeCombo Delete→Backspace).
  inline QString platformizeSeq(QString seq) {
#ifdef Q_OS_MACOS
    seq.replace(QStringLiteral("Delete"), QStringLiteral("Backspace"),
                Qt::CaseInsensitive);
#endif
    return seq;
  }

  inline std::string makeSalt() {
    return QString::number(QRandomGenerator::global()->bounded(1 << 24), 36)
        .toStdString();
  }

  // Encode a QImage as PNG bytes for upload (the server is codec-free, so the
  // desktop hands it already-encoded image bytes + the dimensions separately).
  inline QByteArray pngBytes(const QImage& img) {
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
  }

  // min==max-pinned extent slide shared by the points panel, the chat dock and
  // the toolbar rows: QMainWindow overrides a child's min/max during its own
  // layout passes, so `apply` must pin the extent every frame and `done` must
  // release the constraint.
  inline QVariantAnimation* startExtentSlide(QObject* owner, int from, int to, int ms,
                                             std::function<void(int)> apply,
                                             std::function<void()> done,
                                             QEasingCurve::Type easing = QEasingCurve::OutCubic) {
    auto* anim = new QVariantAnimation(owner);
    anim->setDuration(ms);
    anim->setEasingCurve(easing);
    anim->setStartValue(from);
    anim->setEndValue(to);
    QObject::connect(anim, &QVariantAnimation::valueChanged, owner,
                     [apply = std::move(apply)](const QVariant& v) { apply(v.toInt()); });
    QObject::connect(anim, &QVariantAnimation::finished, owner, std::move(done));
    anim->start(QAbstractAnimation::DeleteWhenStopped);
    return anim;
  }

  // Read all of `path` into `out`; false (leaving `out` untouched) on open failure.
  inline bool readFileBytes(const QString& path, QByteArray& out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    out = f.readAll();
    return true;
  }
  // Overwrite `path` with `data` (truncating); false on open failure.
  inline bool writeFileBytes(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(data);
    return true;
  }

  // Whether the focused control inside `w` holds TYPED CONTENT — the linger
  // hold. Content-gated on purpose: an auto-focused empty search field must not
  // pin its window open.
  inline bool typedContentInside(QWidget* w) {
    QWidget* f = QApplication::focusWidget();
    if (!f || !w || !w->isAncestorOf(f)) return false;
    if (auto* le = qobject_cast<QLineEdit*>(f)) return !le->text().trimmed().isEmpty();
    if (auto* pe = qobject_cast<QPlainTextEdit*>(f)) return !pe->toPlainText().trimmed().isEmpty();
    if (auto* te = qobject_cast<QTextEdit*>(f)) return !te->toPlainText().trimmed().isEmpty();
    return false;
  }

  // Natural page dimensions (cm) as selected — NOT orientation-swapped (only
  // the proportions matter for the crop aspect). Mirrors the browser
  // cropModal.pageDims helper.
  inline core::PageSize naturalPageCm(const QString& pageSize, double customW,
                                      double customH) {
    if (pageSize == "custom") return {customW, customH};
    const core::PageSize ps = core::namedPageSize(pageSize.toStdString());
    return ps.width > 0 ? ps : core::namedPageSize("A4");
  }

}  // namespace stencil::gui
