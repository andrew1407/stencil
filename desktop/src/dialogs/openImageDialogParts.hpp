#pragma once
// The open dialog's tab indices, preview box and row shims, private to the openImageDialog*.cpp TUs.
#include "modalChrome.hpp"
#include "underlineTabBar.hpp"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QStringList>
#include <QTabWidget>
#include <QWidget>

namespace stencil::gui {

  // Tab order (QTabWidget indices).
  enum { TabFile = 0, TabUrl = 1, TabBlank = 2 };

  inline constexpr int kPreviewMaxW = 440;  // preview scaled to fit this box,
  inline constexpr int kPreviewMaxH = 300;  // keeping aspect ratio (browser parity).

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
  // underline tab strip (support/underlineTabBar.hpp) before any tab is added.
  struct OiTabWidget : QTabWidget {
    explicit OiTabWidget(QWidget* parent) : QTabWidget(parent) {
      setTabBar(new UnderlineTabBar(this));
    }
  };

  // Video extensions the loader (MediaLoader) can seek + grab a frame from. A
  // source with one of these — local or in a URL — reveals the frame control.
  inline bool looksLikeVideo(const QString& src) {
    static const QStringList kExt = {"mp4", "mov", "webm", "mkv", "avi", "m4v", "mpg", "mpeg"};
    const QString s = src.trimmed();
    if (s.isEmpty()) return false;
    // Strip a URL query/fragment before reading the extension.
    QString tail = s.section('/', -1).section('?', 0, 0).section('#', 0, 0);
    return kExt.contains(QFileInfo(tail).suffix().toLower());
  }

}  // namespace stencil::gui
