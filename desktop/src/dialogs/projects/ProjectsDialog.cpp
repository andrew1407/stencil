#include "ProjectsDialog.hpp"

#include "projectsRowChrome.hpp"
#include "ReorderableListWidget.hpp"
#include "guiHelpers.hpp"
#include "fetchGuard.hpp"
#include "iconSet.hpp"
#include "../../support/motion/scrollReveal.hpp"  // revealOpacityForItem (scroll edge fade)
#include "../../app/mainWindowHelpers.hpp"   // NAME_CHIP_BOX / NAME_CHIP_GLYPH — the shared chip
#include "../../support/control/reveal/controlReveal.hpp"       // the rename ✓/✗ form/come apart as dust
#include "../../support/displayName.hpp"          // shortName for the remove confirm
#include "../../support/theme/filterFade.hpp"          // filtered-out rows fade + collapse
#include "../../support/menu/menuReveal.hpp"
#include "../../support/theme/theme.hpp"          // themePalette().danger for the Remove row
#include "../../support/menu/menuDangerRow.hpp"    // the red "Remove" row (label + glyph)
#include "../../support/modal/modalChrome.hpp"         // the browser modal shell
#include "../../support/modal/modalReveal.hpp"         // animated colour picker
#include <QColor>
#include <QPixmap>
#include <QPushButton>
#include <QMargins>
#include <QVBoxLayout>
#include <functional>
#include <limits>
#include <memory>
#include <algorithm>
#include <optional>

namespace stencil::gui {

  namespace fetchGuard = stencil::net::fetchGuard;

  namespace {
    // The list rides the window's right edge with its bar in the gutter, as Settings' scroll body
    // does; every other row keeps the body's padding, so the rows still line up under the search.
    void rideRightEdge(QVBoxLayout* body, QLayout* listSlot, QWidget* batchBar, QListWidget* list) {
      const int pad = body->contentsMargins().right();
      const auto widen = [pad](QLayout* row) {
        if (!row) return;
        QMargins m = row->contentsMargins();
        m.setRight(m.right() + pad);
        row->setContentsMargins(m);
      };
      QMargins edge = body->contentsMargins();
      edge.setRight(0);
      body->setContentsMargins(edge);
      for (int i = 0; i < body->count(); ++i)
        if (QLayout* row = body->itemAt(i)->layout(); row != listSlot) widen(row);
      if (batchBar) widen(batchBar->layout());
      if (auto* reList = dynamic_cast<ReorderableListWidget*>(list)) reList->rideEdge(pad);
    }
  }  // namespace

  ProjectsDialog::ProjectsDialog(const std::vector<Project>& projects, long long now,
                                 stencil::net::ConnectionManager* connections,
                                 const QHash<QString, QPixmap>& thumbs,
                                 QWidget* parent,
                                 const QString& activeProjectId,
                                 const QColor& accentColor)
      : QDialog(parent), projects(projects), now(now),
        connections(connections), activeProjectId(activeProjectId),
        thumbs(thumbs) {
    // `accentColor` is unused: the delegate reads the installed palette's Highlight/Link (theme.cpp
    // publishes accent + accent-2 there). Kept in the signature so callers stay untouched.
    Q_UNUSED(accentColor);
    setWindowTitle("Projects");
    // The browser modal's footprint (projects.css: 576px wide, so the three filters keep one
    // line; height min(82vh, 760px) of the window whatever the row count, so a filter never resizes it).
    constexpr int PROJECTS_WIDTH = 576;
    constexpr int PROJECTS_MIN_H = 420, PROJECTS_MAX_H = 760;
    const int ceiling = parent ? int(parent->window()->height() * 0.82) : PROJECTS_MAX_H;
    setMinimumSize(PROJECTS_WIDTH, PROJECTS_MIN_H);
    resize(PROJECTS_WIDTH, std::clamp(ceiling, PROJECTS_MIN_H, PROJECTS_MAX_H));

    // Most-recently-updated first, matching the browser store ordering.
    std::sort(this->projects.begin(), this->projects.end(),
              [](const Project& a, const Project& b) {
                return a.meta.updatedAt > b.meta.updatedAt;
              });

    // Browser projectsModal.js parity: the shared modal shell — glyph + "Projects"
    // title with the outlined Close pill — instead of a bare bold caption.
    ModalChrome chrome = installModalChrome(this, "layers", tr("Projects"));
    QVBoxLayout* layout = chrome.body;

    buildSearchRow(layout);
    buildBatchBar(layout);
    buildProjectList();
    wireRowGestures();
    buildFooter(chrome);
    rideRightEdge(layout, barSlot, batch.batchBar, list);
    startRemotePolling();
  }



  // The project this window holds — a local id, or a server project with its url — for the
  // "(Current)" mark and the Close drop. The caller repaints.
  void ProjectsDialog::setOpenHere(const QString& id, const QString& serverUrl) {
    activeProjectId = serverUrl.isEmpty() ? id : QString();
    press.openServerUrl = serverUrl;
    press.openServerId = serverUrl.isEmpty() ? QString() : id;
  }

  // Replace the listed projects and repaint (see the header): lets the owner act on a
  // request without the dialog having to close and be reopened.
  void ProjectsDialog::setProjects(const std::vector<Project>& projects) {
    setProjects(projects, temporary, incognito);
  }

  // …and the same repaint carrying the owner window's session state, so a removal that
  // also blanks the editor lands as ONE frame (see the header).
  void ProjectsDialog::setProjects(const std::vector<Project>& projects, bool temporary,
                                   bool incognito) {
    this->projects = projects;
    this->temporary = temporary;
    this->incognito = incognito;
    if (clearAllBtn) clearAllBtn->setEnabled(!this->projects.empty());   // nothing left to clear
    refresh();
  }

}  // namespace stencil::gui
