#include "modalChrome.hpp"
#include "../skinPrefs.hpp"

#include "clickToToggle.hpp"
#include <QCheckBox>
#include <QRadioButton>
#include "modalChromeShared.hpp"
#include "iconSet.hpp"
#include "modalReveal.hpp"

#include <QColor>
#include <QGuiApplication>
#include <QDialog>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    // The bar's width is RESERVED, never taken and given back, or a search that shortens a list
    // past the scroll point widens every row on one keystroke and narrows them on the next.
    // The bar shows INSIDE the reserved gutter (the margin gives way to it), so rows never move.
    class GutterScrollArea : public QScrollArea {
     public:
      using QScrollArea::QScrollArea;
      void reserveBar() {
        verticalScrollBar()->installEventFilter(this);
        fitGutter();
      }

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (o == verticalScrollBar() && (e->type() == QEvent::Show || e->type() == QEvent::Hide))
          fitGutter();
        return QScrollArea::eventFilter(o, e);
      }

     private:
      void fitGutter() {
        const bool shown = verticalScrollBar()->isVisibleTo(this);
        setViewportMargins(0, 0, shown ? 0 : verticalScrollBar()->sizeHint().width(), 0);
      }
    };
  }  // namespace

  int modalFooterLineWidth(const ModalChrome& chrome, const QHBoxLayout* footer) {
    if (!footer) return 0;
    int items = 0;
    int need = PAD_X * 2 + 2 + footerButtonsWidth(footer, chrome.footerHint, &items);
    QLabel* hint = chrome.footerHint;
    if (hint && !hint->text().isEmpty()) {
      hint->ensurePolished();   // the QSS font, before it is measured
      need += hint->fontMetrics().horizontalAdvance(hint->text());
      if (items > 0) need += footer->spacing();
    }
    return need;
  }

  QFrame* modalDivider(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setObjectName(QStringLiteral("modalDivider"));
    line->setFrameShape(QFrame::NoFrame);   // the QSS paints it; a Sunken bevel doubles up
    line->setFixedHeight(1);
    return line;
  }

  QLabel* modalSectionLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(support::isWebcore() ? text : text.toUpper(), parent);
    l->setObjectName(QStringLiteral("modalSection"));
    return l;
  }

  QLabel* modalSectionLabel(const QString& text, QWidget* parent, bool first) {
    QLabel* l = modalSectionLabel(text, parent);
    l->setContentsMargins(0, first ? 0 : 14, 0, 6);
    return l;
  }

  QWidget* modalRow(QWidget* parent, const QString& label, QLayout* content, int labelMinW) {
    auto* row = new QWidget(parent);
    row->setProperty("vsRow", true);
    row->setAttribute(Qt::WA_StyledBackground, true);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(4, 7, 4, 7);
    h->setSpacing(12);
    if (!label.isEmpty()) {
      auto* l = new QLabel(label, row);
      l->setProperty("vsLabel", true);
      if (labelMinW > 0) l->setMinimumWidth(labelMinW);
      h->addWidget(l);
    }
    h->addLayout(content, 1);
    return row;
  }

  QWidget* modalRow(QWidget* parent, const QString& label, QWidget* field, bool grow,
                    int labelMinW) {
    auto* h = new QHBoxLayout;
    h->setContentsMargins(0, 0, 0, 0);
    if (!grow) h->addStretch(1);   // justify-content: space-between
    h->addWidget(field, grow ? 1 : 0);
    QWidget* row = modalRow(parent, label, h, labelMinW);
    // A row whose field is a CHECK is labelled by the row's own words: clicking them toggles it
    // (browser `<label for>`). Only checks and radios - a checkable push button is a control itself.
    if (qobject_cast<QCheckBox*>(field) || qobject_cast<QRadioButton*>(field))
      support::captionToggles(row->findChild<QLabel*>(), static_cast<QAbstractButton*>(field));
    return row;
  }

  QLabel* modalEmptyLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("modalEmpty"));
    return l;
  }

  int modalHeightCap(const QDialog* dlg) {
    constexpr int MODAL_MAX_H = 760;
    constexpr int BROWSER_CHROME_PX = 85;   // a maximised browser's own bars, for an unhosted dialog
    const QWidget* host = support::dialogHost(dlg);
    int avail = host && host->isVisible() ? host->height() : 0;
    if (avail <= 0) {
      const QScreen* screen = dlg && dlg->screen() ? dlg->screen() : QGuiApplication::primaryScreen();
      avail = (screen ? screen->availableGeometry().height() : 900) - BROWSER_CHROME_PX;
    }
    return qBound(360, int(avail * 0.82), MODAL_MAX_H);
  }

  void sizeModalTall(QDialog* dlg, int width) {
    if (!dlg) return;
    dlg->setMinimumSize(width, 360);
    dlg->resize(width, modalHeightCap(dlg));
  }

  QLineEdit* addModalSearchBar(ModalChrome& chrome, const QString& placeholder) {
    QWidget* shell = chrome.root ? chrome.root->parentWidget() : nullptr;
    auto* search = new QLineEdit(shell);
    search->setObjectName(QStringLiteral("modalSearch"));
    search->setPlaceholderText(placeholder);
    search->setClearButtonEnabled(true);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(PAD_X, 12, PAD_X, 6);
    bar->addWidget(search, 1);
    chrome.root->insertLayout(2, bar);
    return search;
  }

  ModalScrollBody makeModalScrollBody(ModalChrome& chrome, int topPad) {
    ModalScrollBody b;
    QWidget* shell = chrome.root ? chrome.root->parentWidget() : nullptr;
    // The padding moves inside the column so the scrollbar rides the shell's own edge.
    chrome.body->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new GutterScrollArea(shell);
    b.scroll = scroll;
    b.scroll->setObjectName(QStringLiteral("modalScroll"));
    b.scroll->setWidgetResizable(true);
    b.scroll->setFrameShape(QFrame::NoFrame);
    b.scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->reserveBar();
    b.scroll->viewport()->setAutoFillBackground(false);
    b.content = new QWidget(b.scroll);
    b.content->setAutoFillBackground(false);
    b.layout = new QVBoxLayout(b.content);
    b.layout->setContentsMargins(PAD_X, topPad, PAD_X, BODY_PAD_Y);
    b.layout->setSpacing(0);
    b.scroll->setWidget(b.content);
    chrome.body->addWidget(b.scroll, 1);
    return b;
  }

  void alignModalForm(QFormLayout* form, bool growFields) {
    if (!form) return;
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    if (growFields) form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  }

  void makeModalCta(QPushButton* btn, const QString& iconName) {
    if (!btn) return;
    // A dynamic property: theme.cpp matches QPushButton[accentCta="true"]; objectName stays free for tests.
    btn->setProperty("accentCta", true);
    // labelIcon carries the browser's 6px gap to its label.
    if (!iconName.isEmpty()) btn->setIcon(labelIcon(iconName, QColor("#ffffff"), 15));
  }

  void makeModalDanger(QPushButton* btn, const QString& iconName) {
    if (!btn) return;
    btn->setProperty("dangerCta", true);   // theme.cpp's #dangerButton face, by property
    if (!iconName.isEmpty()) btn->setIcon(labelIcon(iconName, QColor("#ffffff"), 15));
  }

  void makeModalGo(QPushButton* btn, const QString& iconName) {
    if (!btn) return;
    btn->setProperty("successCta", true);   // theme.cpp's green face, by property
    if (!iconName.isEmpty()) btn->setIcon(labelIcon(iconName, QColor("#ffffff"), 15));
  }
}  // namespace stencil::gui

