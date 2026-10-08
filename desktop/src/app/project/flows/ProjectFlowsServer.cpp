#include "MainWindow.hpp"
#include "../../../support/control/reveal/controlReveal.hpp"
#include "ProjectFlows.hpp"
#include "ChatSessionController.hpp"
#include "mainWindowShared.hpp"
#include "mainWindowHelpers.hpp"
#include "displayName.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"
#include "ServerClient.hpp"
#include "lineUnion.hpp"

#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>

#include <optional>

// Opening a server-stored project.

namespace stencil::gui {

  // Mirrors the browser projectsModal openRemote(). Async chain: getProject →
  // downloadFile("original") → fetchUrlBytes(source) on empty → decode (pool) → adopt.
  void ProjectFlows::openServerProject(const QString& serverUrl, const QString& id, bool silent,
                                     bool link) {
    if (!w.remote.connections) return;
    stencil::net::ServerClient* c = w.remote.session->requireClient(serverUrl);
    if (!c || (silent && w.remote.reloading)) return;
    // Loading emits changed(); the flag stops it being pushed straight back. Async-in-flight: the
    // shared clearer's destructor resets it once the last continuation is gone.
    w.remote.reloading = true;
    const int seq = ++w.remote.reloadSeq;
    auto reloadGuard = std::shared_ptr<void>(nullptr, [self = QPointer<MainWindow>(&w), seq](void*) {
      if (self && self->remote.reloadSeq == seq) self->remote.reloading = false;
    });
    // Lines drawn while a reload of this same project is in flight are merged into what lands.
    const bool sameProject = silent && link && w.remote.session->address() == serverUrl &&
                             w.remote.session->id() == id;
    auto editsSince = [this, sameProject, before = w.canvas->allLines()] {
      const core::Lines now = w.canvas->allLines();
      return sameProject && !model::sameLines(now, before) ? std::make_optional(now) : std::nullopt;
    };
    // A peer's edit that left the original alone lands in place as one undo step, a new crop or
    // turn rebuilt from the picture already held. False only for a layout with no crop.
    auto applyLayoutOnly = [this](const QJsonObject& layout) {
      int lw = 0, lh = 0, rot = 0;
      bool mirrored = false;
      core::CropRect crop;
      const core::Lines lines = fileStore::parseLayoutJson(layout, lw, lh, &crop, &rot, &mirrored);
      if (crop.width <= 0) return false;
      const core::CropRect cur = w.canvas->getCropRect();
      const bool sameView = ((rot % 4) + 4) % 4 == w.canvas->getRotationQuarters() &&
                            mirrored == w.canvas->getMirrored() && crop.x == cur.x &&
                            crop.y == cur.y && crop.width == cur.width && crop.height == cur.height;
      adoptServerLayoutMeta(layout);
      const core::PageSize page =
          naturalPageCm(w.pageSizeValue(), w.settings.customPageWidth, w.settings.customPageHeight);
      w.canvas->setPageCm(page.width, page.height);
      QString filter, tint;
      parseLayoutFilter(layout, w.settings.filterColor, filter, tint);
      w.applyTintColor(QColor(tint), /*asUndoStep=*/false);   // the peer's step carries it
      w.applyImageFilter(filter, false);
      // A filter-only edit is its own step, or undoing a later stroke would hand it back; a
      // result upload over the same layout pushes none.
      if (!sameView) w.canvas->commitLayout(lines, crop, rot, mirrored);
      else if (!model::sameLines(lines, w.canvas->allLines())) w.canvas->commitLines(lines);
      else w.canvas->commitFilter(filter, w.tools.filterColorValue);
      return true;
    };
    QPointer<MainWindow> self(&w);
    c->getProjectAsync(id, [this, self, c, serverUrl, id, silent, link, reloadGuard, applyLayoutOnly,
                            editsSince, seq](
                               bool ok, stencil::net::ServerProject meta, QJsonObject layout) {
      if (!self) return;
      if (!ok) {
        w.notify->error(QString("Could not open server project — %1").arg(c->lastError()));
        return;
      }
      // A null image is the layout-only path: the picture on the canvas IS the server's.
      auto adopt = [this, self, serverUrl, id, silent, link, meta, layout, reloadGuard, seq](
                       QImage img, std::optional<core::Lines> local) {
        if (!self) return;
        if (!img.isNull()) w.loadImageWithLayout(img, layout);
        if (local) {
          const model::LineUnion merged = model::unionLines(w.canvas->allLines(), *local);
          if (merged.lines.size() != w.canvas->allLines().size()) w.canvas->commitLines(merged.lines);
        }
        w.docSource.blankColor = meta.blankColor;  // restore blank-fill so the recolour control tracks it
        w.canvas->setBlankPage(!w.docSource.blankColor.isEmpty());
        // Unlinked (incognito deep-link) opens adopt the content only, mirroring the browser's
        // copyServerProjectToIncognito.
        w.activeProjectId.clear();
        if (link) {
          w.remote.session->getLink().bind(serverUrl, id, meta.name, meta.color, meta.version);
          if (!img.isNull())
            w.remote.session->noteAdoptedOriginal(meta.originalHash, w.canvas->getOriginalImage().cacheKey());
        } else {
          w.remote.session->getLink().unbind();
          w.remoteSync->stopRemotePoll();
        }
        w.docSource.currentSource = meta.source;
        w.docSource.currentResource = meta.resource;
        w.filterDirty = false;   // we just adopted the server/project filter
        w.refreshActions();
        // Fit on open (browser switchToProject); skipped for a silent live-poll reload, dust
        // arrival included — a peer's stroke is an edit, not an image appearing.
        if (!silent) {
          w.fitToWindow();
          w.playImageArrival();
        }
        if (link) w.remoteSync->startRemotePoll();   // live co-edit: watch for peer changes
        // Chat persistence (§12): a linked, user-initiated open pulls the server-stored chat;
        // silent reloads must not stomp it.
        if (link && !silent && w.settings.saveChatsWithProject) {
          if (auto* cc = w.remote.connections ? w.remote.connections->find(serverUrl) : nullptr) {
            cc->downloadFileAsync(id, QStringLiteral("chat"),
                                  [this, self](bool cok, QByteArray data) {
                                    if (!self) return;
                                    w.chatSession->restoreChatFromDoc(
                                        cok ? QJsonDocument::fromJson(data).object() : QJsonObject());
                                  });
          } else {
            w.chatSession->restoreChatFromDoc(QJsonObject());
          }
        }
        if (!silent)
          w.notify->success(QString("Opened \"%1\" from %2")
                               .arg(support::shortName(meta.name.isEmpty() ? QStringLiteral("Untitled") : meta.name),
                                    serverUrl));
        // The merged local edit is the reload's last change; it goes out like any other edit.
        if (local && w.remote.reloadSeq == seq) {
          w.remote.reloading = false;
          w.remoteSync->scheduleRemotePush();
        }
      };
      const bool sameOriginal = silent && link && w.remote.session->address() == serverUrl &&
          w.remote.session->id() == id && w.canvas->hasImage() &&
          w.remote.session->isAdoptedOriginal(meta.originalHash, w.canvas->getOriginalImage().cacheKey());
      if (std::optional<core::Lines> local = editsSince(); sameOriginal && applyLayoutOnly(layout)) {
        adopt(QImage(), std::move(local));
        return;
      }
      auto decode = [this, adopt, editsSince](QByteArray bytes) {
        w.decodeForCanvas(
            [bytes] { return QImage::fromData(bytes); },
            [this, adopt, editsSince](const QImage& img) {
              if (img.isNull()) {
                w.notify->error("Server image could not be decoded");
                return;
              }
              adopt(img, editsSince());
            });
      };
      c->downloadFileAsync(id, "original", [this, self, c, meta, decode,
                                            reloadGuard](bool dok, QByteArray bytes) {
        if (!self) return;
        if (dok && !bytes.isEmpty()) {
          decode(bytes);
          return;
        }
        // No stored bytes (an extension-added project records only the web URL): fetch that
        // source. Qt Network has no CORS limit.
        fetchUrlBytesAsync(&w, meta.source, [this, self, c, decode, reloadGuard](QByteArray b) {
          if (!self) return;
          if (b.isEmpty()) {
            w.notify->error(QString("Could not download image — %1").arg(c->lastError()));
            return;
          }
          decode(b);
        });
      });
    });
  }

  // Only the keys it carries, so older projects keep the current page/formulas. Signals blocked.
  void ProjectFlows::adoptServerLayoutMeta(const QJsonObject& layout) {
    if (layout.contains("pageSize")) {
      const fileStore::LayoutMeta m = fileStore::parseLayoutMeta(layout);
      if (m.customPageWidth > 0) w.settings.customPageWidth = m.customPageWidth;
      if (m.customPageHeight > 0) w.settings.customPageHeight = m.customPageHeight;
      {
        QSignalBlocker bs(w.units.pageSize);
        const int idx = w.units.pageSize->findData(m.pageSize);
        if (idx >= 0) w.units.pageSize->setCurrentIndex(idx);
      }
      w.settings.pageSize = w.pageSizeValue();
      revealControls(w.units.customGroup, w.settings.pageSize == "custom");
      if (w.units.customW && w.units.customH) {
        QSignalBlocker bw(w.units.customW), bh(w.units.customH);
        const double f = w.unitFormat().factor;
        w.units.customW->setValue(w.settings.customPageWidth * f);
        w.units.customH->setValue(w.settings.customPageHeight * f);
      }
    }
    if (layout.contains("allowFormulas") || layout.contains("formulaX") ||
        layout.contains("formulaY")) {
      const bool allow = layout.value("allowFormulas").toBool(false);
      // Keep the expressions regardless of the toggle.
      const QString fx = layout.value("formulaX").toString();
      const QString fy = layout.value("formulaY").toString();
      w.settings.allowFormulas = allow;
      w.settings.formulaX = fx;
      w.settings.formulaY = fy;
      {
        QSignalBlocker ba(w.tools.allowFormulas);
        w.tools.allowFormulas->setChecked(allow);
      }
      if (w.acts.allowFormulas) {
        QSignalBlocker b(w.acts.allowFormulas);
        w.acts.allowFormulas->setChecked(allow);
      }
      revealControls(w.tools.formulaGroup, allow);
      {
        QSignalBlocker bx(w.tools.formulaX), by(w.tools.formulaY);
        w.tools.formulaX->setText(fx);
        w.tools.formulaY->setText(fy);
      }
      if (w.tools.formulaError) w.tools.formulaError->setVisible(false);
    }
    w.persistSettings();
  }

}  // namespace stencil::gui
