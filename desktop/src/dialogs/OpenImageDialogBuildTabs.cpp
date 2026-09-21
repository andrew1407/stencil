// The three source tabs — a local file, a web URL and a new blank canvas — and their strip.
#include "OpenImageDialog.hpp"
#include "openImageDialogParts.hpp"
#include "../support/UnderlineTabBar.hpp"
#include "guiHelpers.hpp"
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace stencil::gui {

  void OpenImageDialog::buildTabs(QVBoxLayout* layout, int blankW, int blankH) {
    // Source tabs: Local file / URL link / Blank — the browser .oi-tab strip
    // (UnderlineTabBar.hpp): hover, sliding underline, and an accent-tinted glyph.
    tabs = new OiTabWidget(this);
    // No pane box: the .vs-rows carry their own hairlines, so a rounded pane doubled
    // up — only the strip's own full-width hairline remains (theme.cpp).
    tabs->setObjectName("oiTabs");
    // Hug the tab page. QTabWidget expands by default, so the pane stretched into a tall
    // empty box under a two-field form (the browser's tab panel is content-height).
    tabs->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    // Tab: Local file (browser: one .vs-row "Choose" + the file input) — a read-only
    // path field + Choose (images AND videos). It auto-previews, so it needs no button.
    auto* fileTab = new QWidget(this);
    auto* fileV = new QVBoxLayout(fileTab);
    fileV->setContentsMargins(0, 14, 0, 0);   // browser .oi-tabs margin-bottom: 14px
    fileV->setSpacing(0);
    // ONE control, not a button beside a field: the accent CTA butted against the path readout inside
    // a single outlined box - the browser's .oi-file. The box carries the outline, the halves none.
    auto* fileBox = new QFrame(this);
    fileBox->setObjectName(QStringLiteral("oiFileBox"));
    auto* fileRow = new QHBoxLayout(fileBox);
    fileRow->setContentsMargins(0, 0, 0, 0);
    fileRow->setSpacing(0);
    path = new QLineEdit(this);
    path->setReadOnly(true);
    path->setObjectName(QStringLiteral("oiPathField"));
    path->setPlaceholderText("No file chosen");
    auto* browse = new QPushButton("Choose File", this);
    browse->setObjectName(QStringLiteral("oiChooseBtn"));
    makeModalCta(browse, "folder");
    connect(browse, &QPushButton::clicked, this, &OpenImageDialog::browse);
    // The whole box opens the chooser, not just the button — the browser's is one control
    // end to end, and a read-only field that ignores a click reads as broken.
    support::clickActivates(path, browse);
    path->setToolTip(tr("Click to choose an image or video"));
    fileRow->addWidget(browse);
    fileRow->addWidget(path, 1);
    fileV->addWidget(vsRow(fileTab, tr("Choose"), fileBox));
    tabs->addTab(fileTab, "Local file");

    // Tab: URL link (browser: one .vs-row "URL" with the field AND the Preview button inline).
    // Resolved via MediaLoader; preview is explicit so a half-typed URL never spins a fetch.
    auto* urlTab = new QWidget(this);
    auto* urlV = new QVBoxLayout(urlTab);
    urlV->setContentsMargins(0, 14, 0, 0);
    urlV->setSpacing(0);
    auto* urlRow = new QHBoxLayout;
    urlRow->setContentsMargins(0, 0, 0, 0);
    url = new QLineEdit(this);
    url->setPlaceholderText("https://… (image or video)");
    previewBtn = new QPushButton("Preview", this);
    makeModalCta(previewBtn, "image");   // the browser's inline Preview button
    previewBtn->setToolTip("Show the image / first video frame before opening");
    connect(previewBtn, &QPushButton::clicked, this, &OpenImageDialog::doPreview);
    urlRow->addWidget(url, 1);
    urlRow->addWidget(previewBtn);
    urlV->addWidget(vsRow(urlTab, tr("URL"), urlRow));
    tabs->addTab(urlTab, "URL link");

    // Tab: Blank (browser FILL COLOR / SIZE (PX)): White and Black are swatch BUTTONS
    // with the custom swatch beside them, then plain px fields — no radios, no suffix.
    auto* blankTab = new QWidget(this);
    auto* blankV = new QVBoxLayout(blankTab);
    blankV->setContentsMargins(0, 14, 0, 0);
    blankV->setSpacing(0);
    // Browser .vs-section spacing: 14px above (none on the first), 6px below.
    auto* fillSection = modalSectionLabel(tr("Fill color"), blankTab);
    fillSection->setContentsMargins(0, 0, 0, 6);
    blankV->addWidget(fillSection);
    auto* presetRow = new QHBoxLayout;
    presetRow->setContentsMargins(0, 0, 0, 0);
    presetRow->setSpacing(8);
    auto* whiteBtn = new QPushButton(tr("White"), this);
    whiteBtn->setObjectName("biPresetWhite");
    whiteBtn->setToolTip("Fill with white");   // browser bi-preset titles
    auto* blackBtn = new QPushButton(tr("Black"), this);
    blackBtn->setObjectName("biPresetBlack");
    blackBtn->setToolTip("Fill with black");
    blank.swatch = new QToolButton(this);
    // A QToolButton is icon-ONLY by default, which would drop the hex setColorSwatch writes.
    blank.swatch->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setColorSwatch(blank.swatch, blank.color, SWATCH_SIZE, /*withHex=*/true);
    connect(blank.swatch, &QToolButton::clicked, this, &OpenImageDialog::pickCustomColor);
    connect(whiteBtn, &QPushButton::clicked, this, [this] {
      blank.color = QColor(Qt::white);
      setColorSwatch(blank.swatch, blank.color, SWATCH_SIZE, /*withHex=*/true);
    });
    connect(blackBtn, &QPushButton::clicked, this, [this] {
      blank.color = QColor(Qt::black);
      setColorSwatch(blank.swatch, blank.color, SWATCH_SIZE, /*withHex=*/true);
    });
    presetRow->addWidget(whiteBtn);
    presetRow->addWidget(blackBtn);
    presetRow->addStretch(1);
    blankV->addWidget(vsRow(blankTab, tr("Presets"), presetRow));
    blankV->addWidget(vsRow(blankTab, tr("Custom color"), blank.swatch, /*stretch=*/0));
    auto* sizeSection = modalSectionLabel(tr("Size (px)"), blankTab);
    sizeSection->setContentsMargins(0, 14, 0, 6);
    blankV->addWidget(sizeSection);
    blank.width = new QSpinBox(this);
    blank.width->setRange(1, 8192);
    blank.width->setValue(blankW);
    blank.height = new QSpinBox(this);
    blank.height->setRange(1, 8192);
    blank.height->setValue(blankH);
    blankV->addWidget(vsRow(blankTab, tr("Width"), blank.width));
    blankV->addWidget(vsRow(blankTab, tr("Height"), blank.height));
    tabs->addTab(blankTab, "Blank");
    // Browser tab glyphs, named so the strip re-tints them per state (muted / hover /
    // accent-selected) instead of a fixed-colour QIcon.
    auto* tabStrip = static_cast<UnderlineTabBar*>(tabs->tabBar());
    tabStrip->setTabGlyph(TabFile, "file-text");
    tabStrip->setTabGlyph(TabUrl, "link");
    tabStrip->setTabGlyph(TabBlank, "plus-circle");
    layout->addWidget(tabs);
  }

}
