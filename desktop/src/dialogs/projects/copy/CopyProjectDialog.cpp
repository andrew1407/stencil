#include "CopyProjectDialog.hpp"
#include "../../../support/control/dblReset.hpp"
#include "../../../support/modal/modalChrome.hpp"
#include "../../../support/displayName.hpp"
#include "../../../support/motion/ShimmerOverlay.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    constexpr int ROW_PAD_Y = 9;   // px above and below a box (browser .cp-row padding)
  }

  CopyProjectDialog::CopyProjectDialog(QWidget* parent, const QString& name, const QString& scopeLabel,
                                       const QString& copyName, const QString& serverUrl)
      : QDialog(parent), serverUrl(serverUrl) {
    setWindowTitle(tr("Make a copy"));
    setMinimumWidth(MODAL_WIDTH);
    ModalChrome chrome = installModalChrome(this, QStringLiteral("duplicate"), tr("Make a copy"));
    // Plain body text, as the confirm modal's: a section label would upper-case the names.
    auto* question = new QLabel(QStringLiteral("Copy “%1” (%2) as “%3”?")
                                    .arg(support::shortName(name), scopeLabel.toLower(), copyName), this);
    question->setObjectName(QStringLiteral("copyProjectQuestion"));
    question->setWordWrap(true);
    question->setTextInteractionFlags(Qt::NoTextInteraction);
    QSizePolicy wrap(QSizePolicy::Preferred, QSizePolicy::Minimum);
    wrap.setHeightForWidth(true);
    question->setSizePolicy(wrap);
    chrome.body->addWidget(question);

    // Box first, its label beside it (browser .vs-inline-check), the pair's own tooltip: the row
    // around it stays plain, and the two rows stack flush as the browser's do.
    auto* boxes = new QVBoxLayout;
    boxes->setSpacing(0);
    chrome.body->addLayout(boxes);
    const auto boxRow = [this, boxes](QCheckBox* box, const QString& tip) {
      support::setResetDefault(box, false);
      box->setToolTip(tip);
      box->setCursor(Qt::PointingHandCursor);
      box->setFocusPolicy(Qt::TabFocus);   // a click leaves no :focus wash; Tab still rings it
      box->setProperty(NO_SHIMMER_PROPERTY, true);   // the browser's check wears no hover sweep
      auto* fit = new QHBoxLayout;
      fit->setContentsMargins(0, 0, 0, 0);
      fit->addWidget(box);
      fit->addStretch(1);
      QWidget* row = modalRow(this, QString(), fit);
      row->layout()->setContentsMargins(4, ROW_PAD_Y, 4, ROW_PAD_Y);
      boxes->addWidget(row);
      return row;
    };
    local = new QCheckBox(tr("Make a local copy"), this);
    local->setObjectName(QStringLiteral("copyProjectLocal"));
    boxRow(local, tr("Make it on this computer instead of on %1.").arg(serverUrl))->setVisible(!serverUrl.isEmpty());
    incognito = new QCheckBox(tr("Open in incognito"), this);
    incognito->setObjectName(QStringLiteral("copyProjectIncognito"));
    boxRow(incognito, tr("Open the copy without ever saving it."))->setObjectName(QStringLiteral("oiNoDivider"));   // the last row

    QHBoxLayout* buttons = addModalFooter(chrome);
    const auto cta = [this, buttons](const QString& text, const char* icon, const char* id) {
      auto* b = new QPushButton(text, this);
      b->setObjectName(QString::fromLatin1(id));
      makeModalCta(b, QString::fromLatin1(icon));
      buttons->addWidget(b);
      return b;
    };
    connect(cta(tr("Cancel"), "x", "copyProjectCancel"), &QPushButton::clicked, this, &QDialog::reject);
    justCopy = cta(tr("Just copy"), "duplicate", "copyProjectJust");
    connect(justCopy, &QPushButton::clicked, this, [this] { finish(JUST_COPY); });
    connect(cta(tr("Open in new window"), "external", "copyProjectNewWindow"), &QPushButton::clicked, this,
            [this] { finish(OPEN_NEW_WINDOW); });
    connect(cta(tr("Open"), "folder", "copyProjectOpen"), &QPushButton::clicked, this, [this] { finish(OPEN); });

    connect(local, &QCheckBox::toggled, this, [this] { sync(); });
    connect(incognito, &QCheckBox::toggled, this, [this] { sync(); });
    sync();
  }

  // Only a local copy may stay unsaved, and an unsaved copy exists only once it is opened.
  void CopyProjectDialog::sync() {
    const bool onServer = !serverUrl.isEmpty() && !local->isChecked();
    incognito->setEnabled(!onServer);
    incognito->setCursor(onServer ? Qt::ArrowCursor : Qt::PointingHandCursor);
    if (onServer && incognito->isChecked()) incognito->setChecked(false);
    justCopy->setEnabled(!incognito->isChecked());
  }

  void CopyProjectDialog::finish(Outcome how) {
    outcome = how;
    accept();
  }

  bool CopyProjectDialog::getIncognito() const { return incognito->isChecked(); }
  bool CopyProjectDialog::getLocal() const { return local->isChecked(); }

}  // namespace stencil::gui
