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
    tabs_ = new OiTabWidget(this);
    // No pane box: the .vs-rows carry their own hairlines, so a rounded pane doubled
    // up — only the strip's own full-width hairline remains (theme.cpp).
    tabs_->setObjectName("oiTabs");
    // Hug the tab page. QTabWidget expands by default, so the pane stretched into a tall
    // empty box under a two-field form (the browser's tab panel is content-height).
    tabs_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    // Tab: Local file (browser: one .vs-row "Choose" + the file input) — a read-only
    // path field + Choose (images AND videos). It auto-previews, so it needs no button.
    auto* fileTab = new QWidget(this);
    auto* fileV = new QVBoxLayout(fileTab);
    fileV->setContentsMargins(0, 14, 0, 0);   // browser .oi-tabs margin-bottom: 14px
    fileV->setSpacing(0);
    // ONE control, not a button beside a field: the accent CTA on the left butted straight
    // against the path readout, both inside a single outlined box — the browser's .oi-file
    // (css/components/openImage.css). The box carries the outline, so the halves carry none.
    auto* fileBox = new QFrame(this);
    fileBox->setObjectName(QStringLiteral("oiFileBox"));
    auto* fileRow = new QHBoxLayout(fileBox);
    fileRow->setContentsMargins(0, 0, 0, 0);
    fileRow->setSpacing(0);
    path_ = new QLineEdit(this);
    path_->setReadOnly(true);
    path_->setObjectName(QStringLiteral("oiPathField"));
    path_->setPlaceholderText("No file chosen");
    auto* browse = new QPushButton("Choose File", this);
    browse->setObjectName(QStringLiteral("oiChooseBtn"));
    makeModalCta(browse, "folder");
    connect(browse, &QPushButton::clicked, this, &OpenImageDialog::browse);
    // The whole box opens the chooser, not just the button — the browser's is one control
    // end to end, and a read-only field that ignores a click reads as broken.
    support::clickActivates(path_, browse);
    path_->setToolTip(tr("Click to choose an image or video"));
    fileRow->addWidget(browse);
    fileRow->addWidget(path_, 1);
    fileV->addWidget(vsRow(fileTab, tr("Choose"), fileBox));
    tabs_->addTab(fileTab, "Local file");

    // Tab: URL link (browser: one .vs-row "URL" with the field AND the Preview button
    // inline — never on a row of its own). Resolved via MediaLoader (CORS-free fetch +
    // video-frame grab); preview is explicit so a half-typed URL never spins a fetch.
    auto* urlTab = new QWidget(this);
    auto* urlV = new QVBoxLayout(urlTab);
    urlV->setContentsMargins(0, 14, 0, 0);
    urlV->setSpacing(0);
    auto* urlRow = new QHBoxLayout;
    urlRow->setContentsMargins(0, 0, 0, 0);
    url_ = new QLineEdit(this);
    url_->setPlaceholderText("https://… (image or video)");
    previewBtn_ = new QPushButton("Preview", this);
    makeModalCta(previewBtn_, "image");   // the browser's inline Preview button
    previewBtn_->setToolTip("Show the image / first video frame before opening");
    connect(previewBtn_, &QPushButton::clicked, this, &OpenImageDialog::doPreview);
    urlRow->addWidget(url_, 1);
    urlRow->addWidget(previewBtn_);
    urlV->addWidget(vsRow(urlTab, tr("URL"), urlRow));
    tabs_->addTab(urlTab, "URL link");

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
    blank_.swatch = new QToolButton(this);
    // A QToolButton is icon-ONLY by default, which would drop the hex setColorSwatch writes.
    blank_.swatch->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setColorSwatch(blank_.swatch, blank_.color, SWATCH_SIZE, /*withHex=*/true);
    connect(blank_.swatch, &QToolButton::clicked, this, &OpenImageDialog::pickCustomColor);
    connect(whiteBtn, &QPushButton::clicked, this, [this] {
      blank_.color = QColor(Qt::white);
      setColorSwatch(blank_.swatch, blank_.color, SWATCH_SIZE, /*withHex=*/true);
    });
    connect(blackBtn, &QPushButton::clicked, this, [this] {
      blank_.color = QColor(Qt::black);
      setColorSwatch(blank_.swatch, blank_.color, SWATCH_SIZE, /*withHex=*/true);
    });
    presetRow->addWidget(whiteBtn);
    presetRow->addWidget(blackBtn);
    presetRow->addStretch(1);
    blankV->addWidget(vsRow(blankTab, tr("Presets"), presetRow));
    blankV->addWidget(vsRow(blankTab, tr("Custom color"), blank_.swatch, /*stretch=*/0));
    auto* sizeSection = modalSectionLabel(tr("Size (px)"), blankTab);
    sizeSection->setContentsMargins(0, 14, 0, 6);
    blankV->addWidget(sizeSection);
    blank_.width = new QSpinBox(this);
    blank_.width->setRange(1, 8192);
    blank_.width->setValue(blankW);
    blank_.height = new QSpinBox(this);
    blank_.height->setRange(1, 8192);
    blank_.height->setValue(blankH);
    blankV->addWidget(vsRow(blankTab, tr("Width"), blank_.width));
    blankV->addWidget(vsRow(blankTab, tr("Height"), blank_.height));
    tabs_->addTab(blankTab, "Blank");
    // Browser tab glyphs, named so the strip re-tints them per state (muted / hover /
    // accent-selected) instead of a fixed-colour QIcon.
    auto* tabStrip = static_cast<UnderlineTabBar*>(tabs_->tabBar());
    tabStrip->setTabGlyph(TabFile, "file-text");
    tabStrip->setTabGlyph(TabUrl, "link");
    tabStrip->setTabGlyph(TabBlank, "plus-circle");
    layout->addWidget(tabs_);
  }

}
