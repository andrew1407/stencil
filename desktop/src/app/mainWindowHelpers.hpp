#pragma once

// Inline helpers shared by the MainWindow translation units.

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

  inline constexpr int TOOL_ICON = 18;   // toolbar glyph box
  // Qt's text-beside-icon gap is a fixed 4px and QSS `spacing` does nothing for a QToolButton, so the air is
  // made in the icon RECT (browser twin: .btn-icon-text `gap: 6px`).
  inline constexpr int FACE_ICON_GAP = 3;
  // The ✎/🎨/✓/✗ chips beside the project name. Browser twin: .name-edit-btn.
  inline constexpr int NAME_CHIP_BOX = 28;
  // Half the box, as .name-edit-btn draws it (28px chip, 14px glyph).
  inline constexpr int NAME_CHIP_GLYPH = 15;
  // One size for all four, so edit mode never resizes the row.
  inline constexpr int TOOL_ROW_H = 33;   // icon-button height; every section row matches it
  // The "Controls" pill's chevron is sized against its label, not the toolbar icons.
  inline constexpr int PILL_CHEVRON = 7;    // Controls show/hide arrow (1.5x smaller, user decision)
  inline constexpr int HEADER_LOGO = 33;   // header-row logo mark (user decision); sets the row height floor
  // The browser's inline width (#formula-x / #formula-y); shrinks toward the floor rather than overflowing into "»".
  inline constexpr int FORMULA_FIELD_W = 180;
  inline constexpr int FORMULA_FIELD_MIN_W = 72;   // still shows a typical "x/2 + 10"
  // centralLayout_'s RIGHT inset while the panel is docked (browser: the .main-content gap); collapsed,
  // updatePanelReopenButton swaps in CANVAS_RIGHT_MARGIN_COLLAPSED.
  inline constexpr int CENTRAL_SIDE_MARGIN = 14;

  inline long long nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

  // A modal dialog must NEVER open under a menu still holding the popup grab (it comes up behind, unfocused).
  inline void closeOpenPopupMenus() {
    for (int i = 0; i < 8; ++i) {
      auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!popup) break;
      popup->close();
    }
  }

  // On macOS the primary delete key emits Backspace, so "Alt+Delete" binds to Backspace (browser platformizeCombo).
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

  // The server is codec-free: it gets PNG bytes + the dimensions separately.
  inline QByteArray pngBytes(const QImage& img) {
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
  }

  // QMainWindow overrides a child's min/max during its own layout passes, so `apply` pins every frame and `done` releases.
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

  inline bool readFileBytes(const QString& path, QByteArray& out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    out = f.readAll();
    return true;
  }
  inline bool writeFileBytes(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(data);
    return true;
  }

  // Content-gated: an auto-focused empty search field must not pin its window open.
  inline bool typedContentInside(QWidget* w) {
    QWidget* f = QApplication::focusWidget();
    if (!f || !w || !w->isAncestorOf(f)) return false;
    if (auto* le = qobject_cast<QLineEdit*>(f)) return !le->text().trimmed().isEmpty();
    if (auto* pe = qobject_cast<QPlainTextEdit*>(f)) return !pe->toPlainText().trimmed().isEmpty();
    if (auto* te = qobject_cast<QTextEdit*>(f)) return !te->toPlainText().trimmed().isEmpty();
    return false;
  }

  // NOT orientation-swapped — only the proportions matter. Browser: cropModal.pageDims.
  inline core::PageSize naturalPageCm(const QString& pageSize, double customW,
                                      double customH) {
    if (pageSize == "custom") return {customW, customH};
    const core::PageSize ps = core::namedPageSize(pageSize.toStdString());
    return ps.width > 0 ? ps : core::namedPageSize("A4");
  }

}  // namespace stencil::gui
