#pragma once
// The open dialog's tab indices, preview box and row shims, private to the OpenImageDialog*.cpp TUs.
#include "modalChrome.hpp"
#include "../support/scrubBar.hpp"
#include "UnderlineTabBar.hpp"

#include "../support/clickToToggle.hpp"

#include <algorithm>
#include <QFileInfo>
#include <QCheckBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QSize>
#include <QStringList>
#include <QTabWidget>
#include <QWidget>

namespace stencil::gui {

  // Tab order (QTabWidget indices).
  enum { TabFile = 0, TabUrl = 1, TabBlank = 2 };

  // The window's height ease AND the read-out's own slide, on one clock so they land
  // together (browser twin: motion/easeBoxHeight.js BOX_RESIZE_MS).
  inline constexpr int OI_RESIZE_MS = 380;
  inline constexpr QSize SWATCH_SIZE{140, 30};   // chip + its hex, as SettingsDialog's wells
  // Browser openImage.css: #open-image-modal-overlay .app-modal min-height min(430px, 82vh).
  inline constexpr int PREVIEW_COL_GAP = 6;   // owned by the row above it, not the layout
  inline constexpr int OI_CHECK_GAP = 10;     // browser .oi-crop-opt / .oi-incognito gap
  inline constexpr int OI_MIN_H = 430;
  inline constexpr int PREVIEW_MAX_W = 440;  // preview scaled to fit this box,
  inline constexpr int PREVIEW_MAX_H = 300;  // keeping aspect ratio (browser parity).
  inline constexpr int PREVIEW_MIN_H = 120;  // the picture never gives up room below this

  // A checkbox whose CAPTION is its own wrapping label. A QCheckBox cannot wrap, so a box carrying a
  // whole sentence makes that sentence the row's minimum width, which the compact popover then clips.
  // Browser twin: the .footer-hint span. Zero spacing — the bare box already carries the QSS gap.
  inline QHBoxLayout* checkCaptionRow(QWidget* parent, QCheckBox*& box, const QString& text) {
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(OI_CHECK_GAP);   // .oi-crop-opt / .oi-incognito gap
    box = new QCheckBox(parent);
    box->setObjectName(QStringLiteral("captionCheck"));   // app.qss: no spacing, no gap chip
    auto* hint = new QLabel(text, parent);
    hint->setWordWrap(true);
    // Centred against the row's own height, exactly what the browser's align-items:
    // center gives a one-liner — and, for a caption that wraps in a narrow popover,
    // what it gives there too: the box centred on the whole block, not pinned to line 1.
    support::captionToggles(hint, box);   // the words are the box's label
    row->addWidget(box, 0, Qt::AlignVCenter);
    row->addWidget(hint, 1, Qt::AlignVCenter);
    return row;
  }

  // One browser .vs-row (components.css): a hairline-underlined form row with a
  // fixed label column (stencil-open-image-modal label min-width: 88px, row
  // padding 7px 4px). The QSS half ([vsRow]/[vsLabel]) lives in theme.cpp.
  inline QWidget* vsRow(QWidget* parent, const QString& label, QLayout* content) {
    return modalRow(parent, label, content, /*labelMinW=*/88);
  }
  inline QWidget* vsRow(QWidget* parent, const QString& label, QWidget* field, int stretch = 1) {
    auto* h = new QHBoxLayout;
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(field, stretch);
    if (!stretch) h->addStretch(1);
    return vsRow(parent, label, h);
  }

  // QTabWidget::setTabBar is protected — this shim installs the browser-parity
  // underline tab strip (support/UnderlineTabBar.hpp) before any tab is added.
  struct OiTabWidget : QTabWidget {
    explicit OiTabWidget(QWidget* parent) : QTabWidget(parent) {
      setTabBar(new UnderlineTabBar(this));
    }
  };

  // Video extensions the loader (MediaLoader) can seek + grab a frame from. A
  // source with one of these — local or in a URL — reveals the frame control.
  inline bool looksLikeVideo(const QString& src) {
    static const QStringList EXT = {"mp4", "mov", "webm", "mkv", "avi", "m4v", "mpg", "mpeg"};
    const QString s = src.trimmed();
    if (s.isEmpty()) return false;
    // Strip a URL query/fragment before reading the extension.
    QString tail = s.section('/', -1).section('?', 0, 0).section('#', 0, 0);
    return EXT.contains(QFileInfo(tail).suffix().toLower());
  }

}  // namespace stencil::gui
