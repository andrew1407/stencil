#include "ScriptMenuPanel.hpp"

#include "ScriptDoc.hpp"
#include "ScriptHighlighter.hpp"
#include "../support/iconSet.hpp"
#include "../support/modalChrome.hpp"   // makeModalCta — Run is the primary action
#include "scriptMenuPanelParts.hpp"

#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QVBoxLayout>

namespace stencil::gui {

  ScriptMenuPanel::ScriptMenuPanel(QWidget* parent, Hooks hooks)
      : QWidget(parent), hooks_(std::move(hooks)) {
    setObjectName(QStringLiteral("scriptMenuPanel"));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(12, 2, 12, 6);
    col->setSpacing(0);

    // The window's two frames at menu scale: the halo carries the hover glow, the wrap the
    // box, and the editor inside them is bare (ScriptDialog.cpp says why they are separate).
    glow_ = new QFrame(this);
    glow_->setObjectName(QStringLiteral("scriptMenuGlow"));
    auto* glowLayout = new QVBoxLayout(glow_);
    glowLayout->setContentsMargins(0, 0, 0, 0);

    wrap_ = new QFrame(glow_);
    wrap_->setObjectName(QStringLiteral("scriptMenuWrap"));
    auto* wrapLayout = new QVBoxLayout(wrap_);
    wrapLayout->setContentsMargins(MENU_SCRIPT_PAD_X, MENU_SCRIPT_PAD_Y,
                                  MENU_SCRIPT_PAD_X, MENU_SCRIPT_PAD_Y);
    glowLayout->addWidget(wrap_);

    edit_ = new QPlainTextEdit(wrap_);
    edit_->setObjectName(QStringLiteral("scriptMenuText"));
    edit_->setPlaceholderText(QStringLiteral("@crop 10%\n@filter bw\n@save"));
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit_->setFrameShape(QFrame::NoFrame);
    edit_->setAttribute(Qt::WA_MacShowFocusRect, false);
    // The desktop twin of the flyout's data-ctx-keep-tab: a code editor owns Tab, so the
    // menu's Tab-walks-the-controls navigation steps aside for it (stayOpenMenuStops.hpp).
    edit_->setProperty("keepTab", true);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(MENU_SCRIPT_FONT_PX);
    edit_->setFont(mono);
    edit_->installEventFilter(this);
    applyLineHeight();
    wrapLayout->addWidget(edit_);
    wrap_->setMinimumHeight(MENU_SCRIPT_EDITOR_MIN);
    col->addWidget(glow_, 1);   // the editor takes whatever the strip and the row leave

    diag_ = new QLabel(this);
    diag_->setObjectName(QStringLiteral("scriptMenuDiag"));
    diag_->setWordWrap(true);
    col->addWidget(diag_);

    highlighter_ = new dialogs::ScriptHighlighter(edit_->document());

    // Copy · Download · Upload · Run, right-aligned, Run primary and last (browser order).
    auto* row = new QHBoxLayout;
    row->setSpacing(6);
    row->addStretch(1);
    const auto mk = [this, row](const char* name, const QString& label, const QString& tip) {
      auto* b = new QPushButton(label, this);
      b->setObjectName(QString::fromLatin1(name));
      b->setToolTip(tip);
      b->setFocusPolicy(Qt::TabFocus);
      row->addWidget(b);
      return b;
    };
    copyBtn_ = mk("scriptMenuCopy", tr("Copy"), tr("Copy this script to the clipboard"));
    downloadBtn_ = mk("scriptMenuDownload", tr("Download"), tr("Save this script as a .stc file"));
    uploadBtn_ = mk("scriptMenuUpload", tr("Upload"), tr("Load a .stc file into the editor"));
    runBtn_ = mk("scriptMenuRun", tr("Run"),
                 tr("Run this script on the open project (Ctrl+Enter)"));
    makeModalCta(runBtn_);
    col->addSpacing(8);
    col->addLayout(row);

    connect(copyBtn_, &QPushButton::clicked, this, &ScriptMenuPanel::copyToClipboard);
    connect(downloadBtn_, &QPushButton::clicked, this,
            [this] { if (hooks_.download) hooks_.download(); });
    connect(uploadBtn_, &QPushButton::clicked, this,
            [this] { if (hooks_.upload) hooks_.upload(); });
    connect(runBtn_, &QPushButton::clicked, this, &ScriptMenuPanel::run);
    // Synchronous, like the window's: the colours ARE the text as far as the reader is
    // concerned, and a deferred paint reads as the characters appearing late.
    connect(edit_, &QPlainTextEdit::textChanged, this, [this] {
      if (painting_) return;
      checked_ = false;   // editing clears the last verdict: it was about older text
      repaint(false);
    });

    setFixedWidth(MENU_SCRIPT_WIDTH);
    setFixedHeight(menuScriptHeight());
    repaint(false);
  }

  QWidget* ScriptMenuPanel::editor() const { return edit_; }

  QString ScriptMenuPanel::script() const { return edit_->toPlainText(); }

  void ScriptMenuPanel::setScript(const QString& text) {
    edit_->setPlainText(text);
    applyLineHeight();   // setPlainText resets the document's block formats
    edit_->moveCursor(QTextCursor::End);
  }

}  // namespace stencil::gui
