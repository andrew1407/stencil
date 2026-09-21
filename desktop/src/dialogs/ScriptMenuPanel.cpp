#include "ScriptMenuPanel.hpp"

#include "ScriptEditorWidget.hpp"
#include "../support/modalChrome.hpp"   // makeModalCta/Go/Danger — the row's three faces
#include "scriptMenuPanelParts.hpp"

#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {

    ScriptEditorWidget::Style menuStyle() {
      ScriptEditorWidget::Style s;
      s.glowName = QStringLiteral("scriptMenuGlow");
      s.wrapName = QStringLiteral("scriptMenuWrap");
      s.editName = QStringLiteral("scriptMenuText");
      s.diagName = QStringLiteral("scriptMenuDiag");
      s.fontPx = MENU_SCRIPT_FONT_PX;
      s.padX = MENU_SCRIPT_PAD_X;
      s.padY = MENU_SCRIPT_PAD_Y;
      s.lineHeightPct = MENU_SCRIPT_LINE_HEIGHT_PCT;
      s.editorMinH = MENU_SCRIPT_EDITOR_MIN;
      s.indent = MENU_SCRIPT_INDENT;
      s.codeKeys = true;
      return s;
    }

  }  // namespace

  ScriptMenuPanel::ScriptMenuPanel(QWidget* parent, Hooks hooks)
      : QWidget(parent), hooks(std::move(hooks)) {
    setObjectName(QStringLiteral("scriptMenuPanel"));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(MENU_SCRIPT_EDGE, 2, MENU_SCRIPT_EDGE, 6);
    col->setSpacing(0);

    edit = new ScriptEditorWidget(this, menuStyle());

    // Run / Copy / Download / Upload / Clear, ABOVE the editor like the window's bar. Clear wears the
    // shared danger red at the far END, because it throws work away and must sit clear of Run.
    auto* row = actions = new QHBoxLayout;
    row->setSpacing(6);
    row->addStretch(1);
    const auto mk = [this, row](const char* name, const QString& label, const QString& tip) {
      auto* b = new QPushButton(label, this);
      b->setObjectName(QString::fromLatin1(name));
      b->setToolTip(tip);
      b->setFocusPolicy(Qt::TabFocus);
      makeModalCta(b);
      row->addWidget(b);
      return b;
    };
    runBtn = mk("scriptMenuRun", tr("Run"),
                 tr("Run this script on the open project (Ctrl+Enter)"));
    makeModalGo(runBtn);   // the one GO action: green, not the accent the others wear
    copyBtn = mk("scriptMenuCopy", tr("Copy"), tr("Copy this script to the clipboard"));
    downloadBtn = mk("scriptMenuDownload", tr("Download"), tr("Save this script as a .stc file"));
    uploadBtn = mk("scriptMenuUpload", tr("Upload"), tr("Load a .stc file into the editor"));
    clearBtn = new QPushButton(tr("Clear"), this);
    clearBtn->setObjectName(QStringLiteral("scriptMenuClear"));
    clearBtn->setToolTip(tr("Empty the script editor"));
    clearBtn->setFocusPolicy(Qt::TabFocus);
    makeModalDanger(clearBtn);
    row->addWidget(clearBtn);
    col->addLayout(row);
    col->addSpacing(8);
    col->addWidget(edit, 1);   // the editor takes whatever the strip and the row leave

    connect(copyBtn, &QPushButton::clicked, this, [this] {
      edit->copyToClipboard();
      if (this->hooks.notice) this->hooks.notice(tr("Script copied"));
    });
    connect(downloadBtn, &QPushButton::clicked, this,
            [this] { if (this->hooks.download) this->hooks.download(); });
    connect(uploadBtn, &QPushButton::clicked, this,
            [this] { if (this->hooks.upload) this->hooks.upload(); });
    connect(clearBtn, &QPushButton::clicked, this, [this] { edit->setScript(QString()); });
    connect(runBtn, &QPushButton::clicked, this, &ScriptMenuPanel::run);
    connect(edit, &ScriptEditorWidget::runRequested, this, &ScriptMenuPanel::run);
    connect(edit, &ScriptEditorWidget::edited, this, &ScriptMenuPanel::gateActions);

    setFixedWidth(rowWidth());   // provisional; restyle() re-derives it once the glyphs are on
    setFixedHeight(menuScriptHeight(this));
    gateActions();
  }

  // The flyout IS its action row: the five buttons plus the panel's gutters. A layout caches its
  // size hint, so it is invalidated first - restyle() asks again once the glyphs and QSS font land.
  int ScriptMenuPanel::rowWidth() const {
    actions->invalidate();
    return actions->sizeHint().width() + 2 * MENU_SCRIPT_EDGE;
  }

  QWidget* ScriptMenuPanel::editor() const { return edit->editor(); }

  QString ScriptMenuPanel::script() const { return edit->script(); }

  void ScriptMenuPanel::setScript(const QString& text) { edit->setScript(text); }

}  // namespace stencil::gui
