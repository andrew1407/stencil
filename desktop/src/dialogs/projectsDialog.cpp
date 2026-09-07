#include "../support/searchCombo.hpp"
#include "projectsDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "expirationDialog.hpp"
#include "projectDragZones.hpp"
#include "projectsStore.hpp"
#include "reorderableListWidget.hpp"
#include "../app/scrollReveal.hpp"  // revealOpacityForItem (scroll edge fade)
#include "../support/controlReveal.hpp"       // the rename ✓/✗ form/come apart as dust
#include "../support/flowLayout.hpp"           // the filter row + batch bar wrap, never clip
#include "../support/disintegrateOverlay.hpp"  // deleted rows come apart
#include "../support/displayName.hpp"          // shortName for the remove confirm
#include "../support/dissolveEffect.hpp"      // scroll-edge grain dissolve
#include "../support/filterFade.hpp"          // filtered-out rows fade + collapse
#include "../support/guiHelpers.hpp"
#include "../support/menuReveal.hpp"
#include "../support/theme.hpp"          // themePalette().danger for the Remove row
#include "../support/menuDangerRow.hpp"    // the red "Remove" row (label + glyph)
#include "../support/menuShimmer.hpp"         // ctx rows' glass hover sweep
#include "../support/shimmerOverlay.hpp"      // hovered row's glass sweep (browser ui-shimmer)
#include "../support/modalChrome.hpp"         // the browser modal shell
#include "../support/modalReveal.hpp"         // animated colour picker
#include "serverClient.hpp"
#include <QAbstractItemView>
#include <QAction>
#include <QBrush>
#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QComboBox>
#include <QMenu>
#include <QColor>
#include <QCursor>
#include <QFont>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include "appTooltip.hpp"
#include "shimmerOverlay.hpp"
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPointer>
#include <QPolygonF>
#include <QPushButton>
#include <QScreen>
#include <QSize>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>
#include <limits>
#include <memory>
#include <QVariant>
#include <QVariantAnimation>
#include <algorithm>
#include <optional>

namespace stencil::gui {

  namespace {
    // Ctrl on Windows/Linux; Qt maps macOS ⌘ to ControlModifier, and Meta is
    // accepted too so a remapped keyboard still works. Used by the row-open
    // gestures to pick "new window" over "current window".
    bool isNewWindowMod(Qt::KeyboardModifiers m) {
      return m.testFlag(Qt::ControlModifier) || m.testFlag(Qt::MetaModifier);
    }

    // Longest edge of the floating hover-magnify preview. Matches the browser modal's
    // PREVIEW_ZOOM (1.67) applied to its 160px stored thumbnails, so the popped preview
    // is the same size on both surfaces.
    constexpr int kHoverPreviewPx = 178;    // 1.5× smaller than the old 267 (user decision)
    constexpr int kHoverPreviewAltPx = 324; // Alt glance: the old 2× (534) shrunk 1.65×
    // The preview is sand too, on the SHARED floating-tip clock (disintegrateOverlay.hpp
    // kTipDust*/kDustHold/kDustHandOverMs — browser surfaceIn/surfaceOut).
    constexpr int kHoverFadeMs = 90;         // plain ramp when the dust can't play

    // The browser thumb advertises the glance with cursor:zoom-in; Qt ships no stock
    // magnifier cursor, so paint the classic lens-with-plus (dark glyph under a white
    // halo, like the system cursors, so it reads on any row colour). Hotspot on the
    // lens centre. Built once, after QGuiApplication exists.
    const QCursor& zoomInCursor() {
      static const QCursor cursor = [] {
        const qreal dpr = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
        constexpr int kEdge = 22;
        QPixmap pm(qRound(kEdge * dpr), qRound(kEdge * dpr));
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF lens(9, 9);
        const double r = 5.5;
        const auto pass = [&](const QColor& c, double extra) {
          p.setBrush(Qt::NoBrush);
          p.setPen(QPen(c, 2.0 + extra, Qt::SolidLine, Qt::RoundCap));
          p.drawEllipse(lens, r, r);
          p.drawLine(lens + QPointF(r, r) * 0.72, QPointF(19.0, 19.0));
          p.setPen(QPen(c, 1.4 + extra, Qt::SolidLine, Qt::RoundCap));
          p.drawLine(lens - QPointF(2.6, 0), lens + QPointF(2.6, 0));
          p.drawLine(lens - QPointF(0, 2.6), lens + QPointF(0, 2.6));
        };
        pass(Qt::white, 2.2);
        pass(QColor(40, 40, 40), 0.0);
        return QCursor(pm, qRound(lens.x()), qRound(lens.y()));
      }();
      return cursor;
    }
  }  // namespace

  namespace {
    // Per-session project sort mode + manual drag order — shared across dialog
    // re-opens, reset on app restart (the desktop analogue of the browser modal's
    // sessionStorage; deliberately NOT persisted). Modes mirror the browser.
    QString g_projectsSortMode = QStringLiteral("name");
    QStringList g_projectsManualOrder;   // (serverUrl|id) keys in manual order

    // Human expiry label for one project, mirroring the browser modal's
    // expiryLabel(): "EXPIRED", "expires in 1 day", or "expires in N days".
    QString expiryText(const core::ProjectsStore& store,
                       const core::ProjectMeta& meta, long long now) {
      if (store.isExpired(meta, now)) return "EXPIRED";
      const auto at = store.expiresAt(meta);
      if (!at.has_value()) return QString();
      const long long day = 24LL * 60 * 60 * 1000;
      long long days = (*at - now + day - 1) / day;  // ceil
      if (days < 0) days = 0;
      return days <= 1 ? QString("expires in 1 day")
                       : QString("expires in %1 days").arg(days);
    }

    // "Created <localized short date>" for a project's createdAt (epoch ms), or
    // empty when unset. Shown on local + server rows so the create date is visible.
    QString createdText(long long createdAt) {
      if (createdAt <= 0) return QString();
      const QDate d = QDateTime::fromMSecsSinceEpoch(createdAt).date();
      return QString("Created %1").arg(QLocale().toString(d, QLocale::ShortFormat));
    }

    // "Expires <localized short date>" for a server project's expiresAt (epoch ms),
    // or empty when 0/unset (keep forever). Shown next to the created date on server
    // rows; local rows use expiryText() (a relative "expires in N days") instead.
    QString expiresText(long long expiresAt) {
      if (expiresAt <= 0) return QString();
      const QDate d = QDateTime::fromMSecsSinceEpoch(expiresAt).date();
      return QString("Expires %1").arg(QLocale().toString(d, QLocale::ShortFormat));
    }

    // The projects registry loaded for name validation / the default-name seed —
    // shared by the modal prompt, the inline rename and createNew.
    std::shared_ptr<core::ProjectsStore> loadedNameStore(const std::vector<Project>& projects) {
      auto store = std::make_shared<core::ProjectsStore>();
      std::vector<core::ProjectMeta> metas;
      for (const auto& p : projects) metas.push_back(p.meta);
      store->load(metas);
      return store;
    }

    // Live name validation both name editors share (browser parity, utils.js
    // wireNameEditor): ✓ enables only for a valid name, and only a rejected one gets a
    // tooltip. `current` adds the other half — an unchanged name is nothing to save, so ✓
    // goes dead with "No change" (rename passes it; the create/copy prompts do not).
    // The cursor follows the state, Qt having no `:disabled { cursor }` in QSS.
    std::function<void()> makeNameValidator(std::shared_ptr<core::ProjectsStore> store,
                                            QLineEdit* edit, QAbstractButton* okBtn,
                                            const QString& exceptId,
                                            const QString& current = QString()) {
      return [store = std::move(store), edit, okBtn, exceptId, current] {
        const QString name = edit->text().trimmed();
        const bool unchanged = !current.isNull() && name == current;
        const auto res = store->validateName(name.toStdString(), exceptId.toStdString());
        const bool ok = res.ok && !unchanged;
        okBtn->setEnabled(ok);
        okBtn->setCursor(ok ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
        okBtn->setToolTip(ok ? QString()
                             : (unchanged ? QObject::tr("No change")
                                          : QString::fromStdString(res.reason)));
      };
    }

    // Name prompt on the shell (modalChrome promptModal) with the inline rename's live
    // rules: Save enabled only when the trimmed name is non-empty, ≤80 chars, and unique
    // (excluding `exceptId`); the reason shows under the field. nullopt on cancel.
    std::optional<QString> promptValidatedName(QWidget* parent, const QString& title,
                                               const QString& initial,
                                               const QString& exceptId,
                                               const std::vector<Project>& projects) {
      PromptSpec spec;
      spec.title = title;
      spec.titleIcon = QStringLiteral("plus-circle");   // a name to collect, not a warning
      spec.message = QObject::tr("Project name:");
      spec.defaultValue = initial;
      spec.validate = [store = loadedNameStore(projects), exceptId](const QString& name) {
        const auto res = store->validateName(name.toStdString(), exceptId.toStdString());
        return res.ok ? QString() : QString::fromStdString(res.reason);
      };
      return promptModal(parent, spec);
    }

    // The browser's pickServer (projectsModal.js): the connected servers in the picker
    // shell, auto-picked when there is only one. Empty on cancel.
    QString pickServer(QWidget* parent, const QStringList& urls, const QString& message,
                       const QString& title = QStringLiteral("Choose server"),
                       const QString& confirmLabel = QStringLiteral("OK"),
                       const QString& confirmIcon = QStringLiteral("server")) {
      if (urls.isEmpty()) return QString();
      if (urls.size() == 1) return urls.first();
      ChooseSpec spec;
      spec.title = title;
      spec.message = message;
      spec.confirmLabel = confirmLabel;
      spec.confirmIcon = confirmIcon;
      for (const QString& u : urls) spec.options.push_back({u, u});
      return chooseModal(parent, spec).value_or(QString());
    }

    // What the "⋯" says on hover — its own tip, not the row's (browser projectsModal.js
    // menuBtn.title = 'More actions').
    const QString kKebabTip = QStringLiteral("More actions");

    // The "⋯" kebab strip — shared by the delegate (paint) and eventFilter (hit-test).
    QRect kebabZone(const QRect& rowRect) {
      const int w = 50;
      return QRect(rowRect.right() - w, rowRect.top(), w, rowRect.height());
    }
    // The accent chip the kebab paints inside its strip (browser: the per-row "…" button).
    QRect kebabChip(const QRect& rowRect) {
      const QRect zone = kebabZone(rowRect);
      const QSize chip(38, 30);
      return QRect(zone.center().x() - chip.width() / 2, zone.center().y() - chip.height() / 2,
                   chip.width(), chip.height());
    }

    // Delegate paint colours, parsed once — paintRow runs per row per frame, and a
    // QColor("#…") parse there is measurable churn.
    const QColor kGoldEdge("#d4a017");    // server rows (browser .project-remote)
    const QColor kBronzeEdge("#c1783c");  // .stencil-file rows
    const QColor kGreyOrigin("#9aa4b2");  // the "computer" origin line

    // UserRole+7: "doomed" — this row's removal scatter is playing. The delegate
    // paints NOTHING for it (the overlay animates a snapshot; the row must not keep
    // painting underneath); the slot stays until retireRow drops the item.
    constexpr int kDoomedRole = Qt::UserRole + 7;
    // UserRole+8: this row is the project open in THIS editor right now (browser parity:
    // projectsModal.js's "(Current)" — the word itself, in the accent, right after the
    // origin badge). The accent itself comes from the installed palette (Highlight /
    // Link — theme.cpp buildQPalette publishes accent + accent-2 there).
    constexpr int kActiveRole = Qt::UserRole + 8;
    // UserRole+10: the row's muted meta line ("Created … · expires …"), drawn under the
    // bold name (browser projectsModal row parity: name / dates / origin, stacked).
    constexpr int kMetaRole = Qt::UserRole + 10;
    // Paints a rounded golden outline around server (shared) rows — the desktop analogue
    // of the browser's `.project-remote` border — plus a vertical "⋯" kebab on every
    // real row (the browser modal's per-row "more actions" button).
    class ProjectRowDelegate : public QStyledItemDelegate {
     public:
      using QStyledItemDelegate::QStyledItemDelegate;
      // Where each row's name was last painted (viewport coords) — the dialog's
      // name-hover stroke and inline-rename hit tests read it.
      QRect nameRectFor(int row) const { return nameRects_.value(row); }
      // Cap the row width to the viewport so long "<name> — <url>" labels ELIDE instead of
      // forcing a horizontal scrollbar (which clipped the row at narrow window widths).
      QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        if (const auto* av = qobject_cast<const QAbstractItemView*>(opt.widget))
          s.setWidth(av->viewport()->width());
        // Three stacked text lines (name / meta / origin) need a floor the base
        // (icon + padding) does not guarantee on every platform.
        if (!idx.data(Qt::UserRole).isNull()) s.setHeight(std::max(s.height(), 76));
        // A row leaving the filtered set collapses its slot (support/filterFade), so the
        // rows below it close the gap instead of jumping once it disappears.
        s.setHeight(filterHeight(s.height(), filterPresenceOf(idx)));
        return s;
      }
      // Hover slide (browser .project-row:hover: translateX(3px) over 0.14s ease,
      // temp rows excluded): the whole painted row eases right while hovered and
      // eases back as the pointer moves on — driven by a small per-row progress
      // ticked below, since a painted row has no widget to transition.
      void paint(QPainter* p, const QStyleOptionViewItem& opt,
                 const QModelIndex& idx) const override {
        // One initStyleOption per paint — recordIconRect and paintRow both read it.
        QStyleOptionViewItem o(opt);
        initStyleOption(&o, idx);
        recordIconRect(o, idx);   // the magnify hit test + dust origin read it
        const double dx = hoverSlideDx(o, idx);
        if (dx <= 0.0) { paintFaded(p, o, idx); return; }
        p->save();
        p->translate(dx, 0.0);
        paintFaded(p, o, idx);
        p->restore();
      }

     public:
      // Where the THUMBNAIL was painted (viewport coords) — the exact decoration
      // rect the style laid out, checkbox excluded. The magnify hover hit test
      // and the preview dust's origin both anchor here, so they can never
      // disagree with the paint (they did, sourcing the dust from the checkbox
      // and flickering the preview; user report).
      QRect iconRectFor(int row) const { return iconRects_.value(row); }
      // Which row's "⋯" the cursor is actually on — only its own hover styles it, not the
      // row's. The sweep over it is the app's shared ShimmerOverlay, not painted here.
      void setKebabHover(int row) { kebabRow_ = row; }
      QRect kebabChipFor(const QRect& rowRect) const { return kebabChip(rowRect); }

     private:
      mutable QHash<int, QRect> iconRects_;
      // The row whose "⋯" the cursor is on (-1 = none).
      int kebabRow_ = -1;
      // `o` is already initStyleOption'd by paint().
      void recordIconRect(const QStyleOptionViewItem& o, const QModelIndex& idx) const {
        QStyle* st = o.widget ? o.widget->style() : QApplication::style();
        iconRects_[idx.row()] =
            st->subElementRect(QStyle::SE_ItemViewItemDecoration, &o, o.widget);
      }

      static constexpr double kSlidePx = 3.0;   // browser translateX(3px)
      static constexpr int kSlideMs = 140;      // browser transform 0.14s
      // Keyed by ROW — transient hover state, so a rebuild's brief re-keying is
      // harmless, and no QPersistentModelIndex is registered per row per paint.
      mutable QHash<int, double> slide_;    // per-row progress 0..1
      mutable int slideHover_ = -1;         // the row under the pointer
      mutable QPointer<QTimer> slideTick_;
      mutable QPointer<QAbstractItemView> slideView_;

      // Track the hovered row from the style state Qt hands every paint, keep the
      // 60fps tick alive only while something is mid-slide, and hand back this
      // row's current offset.
      double hoverSlideDx(const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
        const bool realRow = !idx.data(Qt::UserRole).isNull();
        const int key = idx.row();
        const bool over = realRow && (opt.state & QStyle::State_MouseOver);
        if (over) slideHover_ = key;
        else if (slideHover_ == key) slideHover_ = -1;
        if (support::motionReduced()) {  // the end state, at once
          if (over) return kSlidePx;
          slide_.remove(key);
          return 0.0;
        }
        double v = slide_.value(key, 0.0);
        if (over && !slide_.contains(key)) slide_.insert(key, v);
        if ((over && v < 1.0) || (!over && v > 0.0)) {
          if (const auto* av = qobject_cast<const QAbstractItemView*>(opt.widget))
            slideView_ = const_cast<QAbstractItemView*>(av);
          startSlideTick();
        }
        return kSlidePx * v;
      }

      void startSlideTick() const {
        if (!slideTick_) {
          auto* self = const_cast<ProjectRowDelegate*>(this);
          slideTick_ = new QTimer(self);
          slideTick_->setInterval(16);
          QObject::connect(slideTick_, &QTimer::timeout, self, [this] {
            const double step = 16.0 / kSlideMs;
            bool active = false;
            for (auto it = slide_.begin(); it != slide_.end();) {
              const bool toward = it.key() == slideHover_;
              double v = std::clamp(it.value() + (toward ? step : -step), 0.0, 1.0);
              it.value() = v;
              if ((toward && v < 1.0) || (!toward && v > 0.0)) active = true;
              // Repaint only THIS row — a full-viewport update() repainted every
              // row per frame while only one or two animate.
              if (slideView_ && slideView_->model())
                slideView_->update(slideView_->model()->index(it.key(), 0));
              if (!toward && v <= 0.0) it = slide_.erase(it);
              else ++it;
            }
            if (!active) slideTick_->stop();
          });
        }
        if (!slideTick_->isActive()) slideTick_->start();
      }

     public:
      // The whole row — background, text, badges — rides one painter opacity, so
      // nothing can fade out of step.
      void paintFaded(QPainter* p, const QStyleOptionViewItem& opt,
                      const QModelIndex& idx) const {
        if (idx.data(kDoomedRole).toBool()) return;   // scatter plays over the held-open slot
        // A filter fade rides one painter opacity over the whole row. Distinct from the
        // grain above on purpose: excluded is not deleted.
        const double fo = filterInk(idx);
        if (fo <= 0.004) return;   // faded out — its slot is still closing
        if (fo < 1.0) {
          p->save();
          p->setOpacity(p->opacity() * fo);
          paintRevealed(p, opt, idx);
          p->restore();
          return;
        }
        paintRevealed(p, opt, idx);
      }

     private:
      // The scroll-edge reveal (unchanged): rows dissolve toward the list's top/bottom
      // edges as it scrolls (browser parity: .reveal-item in css/animations.css).
      void paintRevealed(QPainter* p, const QStyleOptionViewItem& opt,
                         const QModelIndex& idx) const {
        const auto* av = qobject_cast<const QAbstractItemView*>(opt.widget);
        const double dissolve = revealDissolveForItem(av ? av->viewport() : nullptr, opt.rect);
        if (dissolve <= 0.001) { paintRow(p, opt, idx); return; }
        if (dissolve >= 0.999) return;   // fully out — nothing to draw
        // A painted row has no widget to hang a QGraphicsEffect on, so it is rendered
        // into a scratch pixmap and masked by hand — the same grain + bottom-to-top
        // wipe DissolveEffect applies to the transcript's cards.
        const qreal dpr = p->device()->devicePixelRatioF();
        QPixmap buf(opt.rect.size() * dpr);
        buf.setDevicePixelRatio(dpr);
        buf.fill(Qt::transparent);
        {
          QPainter bp(&buf);
          QStyleOptionViewItem shifted(opt);
          shifted.rect.moveTo(0, 0);   // the scratch buffer's own origin
          paintRow(&bp, shifted, idx);
          bp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
          const int h = opt.rect.height();
          const int viewH = av && av->viewport() ? av->viewport()->height() : 0;
          const double visStart = h > 0 ? std::clamp(double(-opt.rect.top()) / h, 0.0, 1.0) : 0.0;
          const double visEnd = h > 0 ? std::clamp(double(viewH - opt.rect.top()) / h, 0.0, 1.0) : 1.0;
          bp.drawImage(0, 0, DissolveEffect::maskFor(opt.rect.size(), dissolve, dpr, visStart, visEnd));
        }
        p->drawPixmap(opt.rect.topLeft(), buf);
      }

      // Breathing room between a row's thumbnail and its name: at this icon size the style's
      // own gap leaves the text sitting against the picture.
      static constexpr int kThumbTextGap = 12;
      mutable QHash<int, QRect> nameRects_;

      // `opt` arrives already initStyleOption'd by paint() — no second init here.
      void paintRow(QPainter* p, const QStyleOptionViewItem& opt,
                    const QModelIndex& idx) const {
        const QStyleOptionViewItem& o = opt;
        const QString full = o.text;
        const bool realRow = !idx.data(Qt::UserRole).isNull();
        QStyle* st = o.widget ? o.widget->style() : QApplication::style();
        if (!realRow) {
          QStyledItemDelegate::paint(p, o, idx);  // placeholder rows: default rendering
          return;
        }
        // Draw bg/selection/checkbox/icon WITHOUT the text via the style directly.
        // State_Selected is cleared too: the style repaints a selected item's ICON in
        // QIcon::Selected mode — blended with the highlight colour — which washed a
        // white thumbnail lilac (user report). The selection FILL is already
        // transparent (theme.cpp #projectsList::item:selected), so nothing is lost.
        QStyleOptionViewItem bg(o);
        bg.text.clear();
        bg.state &= ~QStyle::State_Selected;
        st->drawControl(QStyle::CE_ItemViewItem, &bg, p, o.widget);

        const bool remote = !idx.data(Qt::UserRole + 1).toString().isEmpty();
        const bool fileOrigin = idx.data(Qt::UserRole + 6).toBool();
        const bool current = idx.data(kActiveRole).toBool();
        // The live theme accent, from the installed palette: Highlight = accent,
        // Link = the accent-2 shade (--text-key) — theme.cpp buildQPalette.
        const QColor accent = o.palette.color(QPalette::Highlight);

        // Row card outline — 1px hairlines like the browser's .project-row borders:
        // gold = server (+ a soft 1px outer ring, its box-shadow), bronze = .stencil
        // file (same ring), the accent-2 SHADE (--text-key, not the raw accent — less
        // shouty) for the project open in THIS editor, neutral hairline otherwise.
        QColor edge;
        QColor ring;   // browser box-shadow 0 0 0 1px @55% — server/file rows only
        if (remote) { edge = kGoldEdge; ring = edge; ring.setAlpha(140); }
        else if (fileOrigin) { edge = kBronzeEdge; ring = edge; ring.setAlpha(140); }
        else if (current) { edge = o.palette.color(QPalette::Link); }
        else { edge = o.palette.color(QPalette::Mid); edge.setAlpha(70); }
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(edge, 1));
        p->drawRoundedRect(QRectF(opt.rect).adjusted(1.5, 1.5, -1.5, -1.5), 8, 8);
        if (ring.isValid()) {
          p->setPen(QPen(ring, 1));
          p->drawRoundedRect(QRectF(opt.rect).adjusted(0.5, 0.5, -0.5, -0.5), 9, 9);
        }
        p->restore();

        // Stacked text column (browser row layout): bold name over the muted meta
        // line over the origin badge — not one long "name · created · expires" line.
        const QRect base = st->subElementRect(QStyle::SE_ItemViewItemText, &o, o.widget);
        const int colLeft = base.left() + kThumbTextGap;
        const int colWidth = kebabZone(opt.rect).left() - 8 - colLeft;
        if (colWidth > 20) {
          const QString name = idx.data(Qt::UserRole + 3).toString();
          const QString nameStr = name.isEmpty() ? full : name;
          QString meta = idx.data(kMetaRole).toString();
          const QColor def = o.palette.color(QPalette::Text);
          QColor nameCol = idx.data(Qt::UserRole + 4).value<QColor>();
          if (!nameCol.isValid()) nameCol = def;
          QFont nameF(o.font);
          nameF.setBold(true);
          QFont metaF(o.font);
          metaF.setPointSizeF(std::max(8.0, o.font.pointSizeF() - 1.0));
          const QFontMetrics nfm(nameF), mfm(metaF);
          constexpr int kLineGap = 3;
          int totalH = nfm.height() + kLineGap + mfm.height();
          if (!meta.isEmpty()) totalH += kLineGap + mfm.height();
          int y = opt.rect.top() + (opt.rect.height() - totalH) / 2;
          p->save();
          p->setFont(nameF);
          p->setPen(nameCol);
          p->drawText(QRect(colLeft, y, colWidth, nfm.height()),
                      Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                      nfm.elidedText(nameStr, Qt::ElideRight, colWidth));
          const int nameW = std::min(nfm.horizontalAdvance(nameStr), colWidth);
          nameRects_[idx.row()] = QRect(colLeft, y, nameW, nfm.height());
          y += nfm.height() + kLineGap;
          if (!meta.isEmpty()) {
            p->setFont(metaF);
            p->setPen(o.palette.color(QPalette::PlaceholderText));
            p->drawText(QRect(colLeft, y, colWidth, mfm.height()),
                        Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                        mfm.elidedText(meta, Qt::ElideRight, colWidth));
            y += mfm.height() + kLineGap;
          }
          // Origin line: glyph + word — `server` (gold) with the address, `file-text`
          // (bronze) with ".stencil", `monitor` (grey) with "computer" — plus the
          // accent "(Current)" for the project open in this editor (browser parity).
          const QString gname = remote ? QStringLiteral("server")
                                       : (fileOrigin ? QStringLiteral("file-text")
                                                     : QStringLiteral("monitor"));
          const QColor gcol = remote ? kGoldEdge : (fileOrigin ? kBronzeEdge : kGreyOrigin);
          const QString gtext = remote ? idx.data(Qt::UserRole + 1).toString()
                                       : (fileOrigin ? QStringLiteral(".stencil")
                                                     : QStringLiteral("computer"));
          if (hasIcon(gname)) {
            const int gs = 13;
            p->setRenderHint(QPainter::Antialiasing, true);
            themedIcon(gname, gcol, gs).paint(
                p, QRect(colLeft, y + (mfm.height() - gs) / 2, gs, gs));
            p->setFont(metaF);
            p->setPen(gcol);
            int tx = colLeft + gs + 4;
            const int gw = std::min(mfm.horizontalAdvance(gtext), colWidth - (gs + 4));
            // Only a server ADDRESS is ellipsised — it can be any length. The other two are
            // short fixed words that always fit, and cutting one to "local…" reads as a bug
            // rather than as a truncation (user report).
            p->drawText(QRect(tx, y, gw, mfm.height()),
                        Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                        remote ? mfm.elidedText(gtext, Qt::ElideRight, gw) : gtext);
            tx += gw;
            if (current) {
              p->setPen(accent);
              p->drawText(QRect(tx, y, colWidth - (tx - colLeft), mfm.height()),
                          Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                          QStringLiteral(" (Current)"));
            }
          }
          p->restore();
        }

        // Kebab: an accent-filled rounded chip with white dots (the browser row's
        // "…" more-actions button). It reacts to ITS OWN hover only — row hover leaves it
        // alone — and then a band of glass sweeps across it.
        const QRect chip = kebabChip(opt.rect);
        const bool onKebab = idx.row() == kebabRow_;
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setPen(Qt::NoPen);
        p->setBrush(onKebab ? accent.lighter(115) : accent);
        p->drawRoundedRect(chip, 8, 8);
        p->setBrush(Qt::white);
        const int cx = chip.center().x();
        const int cy = chip.center().y();
        for (int dx = -5; dx <= 5; dx += 5) p->drawEllipse(QPointF(cx + dx, cy + 2), 1.6, 1.6);
        p->restore();
      }
    };

    // A square, center-cropped (cover) thumbnail for the uniform row icon — mirrors the
    // browser's `object-fit: cover` thumbnails so rows are equal height regardless of aspect.
    QPixmap squareThumb(const QPixmap& src, int size) {
      if (src.isNull()) return src;
      const QPixmap scaled =
          src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
      const int x = (scaled.width() - size) / 2;
      const int y = (scaled.height() - size) / 2;
      return scaled.copy(x, y, size, size);
    }
  }  // namespace

  ProjectsDialog::ProjectsDialog(const std::vector<Project>& projects, long long now,
                                 stencil::net::ConnectionManager* connections,
                                 const QHash<QString, QPixmap>& thumbs,
                                 QWidget* parent,
                                 const QString& activeProjectId,
                                 const QColor& accentColor)
      : QDialog(parent), projects_(projects), now_(now),
        connections_(connections), thumbs_(thumbs),
        activeProjectId_(activeProjectId) {
    // `accentColor` is unused now: the delegate reads the installed palette's
    // Highlight/Link (theme.cpp publishes accent + accent-2 there). Kept in the
    // signature so callers stay untouched.
    Q_UNUSED(accentColor);
    setWindowTitle("Projects");
    // The browser modal's footprint (app-modal width, min-height min(560px, 82vh))
    // — the row text elides / stacks instead of demanding width.
    setMinimumSize(kModalWidth, 420);
    resize(kModalWidth, 560);

    // Most-recently-updated first, matching the browser store ordering.
    std::sort(projects_.begin(), projects_.end(),
              [](const Project& a, const Project& b) {
                return a.meta.updatedAt > b.meta.updatedAt;
              });

    // Browser projectsModal.js parity: the shared modal shell — glyph + "Projects"
    // title with the outlined Close pill — instead of a bare bold caption.
    ModalChrome chrome = installModalChrome(this, "layers", tr("Projects"));
    QVBoxLayout* layout = chrome.body;

    // Search sits on its own full-width row, ABOVE the filter selects — mirroring the
    // browser modal's layout (a shared single-row bar squeezed the search box; the
    // browser resolved that by giving search the whole row first).
    {
      auto* srow = new QHBoxLayout;
      search_ = new QLineEdit(this);
      search_->setPlaceholderText(tr("Search projects…"));
      search_->setClearButtonEnabled(true);
      srow->addWidget(search_, 1);
      layout->addLayout(srow);
      connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { applyFilter(); });
    }

    // Filter row: a compact "Show:" dropdown (All / Local / all-servers / a specific connected
    // server) + sort/search-mode selects + Select all (mirrors the browser modal's row below).
    // A FlowLayout: squeezed, the selects wrap onto a second line at their natural widths
    // instead of being crushed or cut off at the edge (browser .modal-search-bar flex-wrap).
    {
      auto* frow = new FlowLayout(nullptr, 0, 8, 6);
      filter_ = new SearchComboBox(this, /*searchable=*/false);
      filter_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
      filter_->setMaximumWidth(260);
      filter_->setToolTip("Filter the list: all, local only, or a specific server");
      rebuildFilterOptions();
      frow->addWidget(filter_);                       // natural width — no stretch
      // Sort mode (mirrors the browser modal): the data role carries the mode key.
      sortCombo_ = new SearchComboBox(this, /*searchable=*/false);
      sortCombo_->setToolTip("Sort projects (drag a row to set a manual order)");
      sortCombo_->addItem(tr("Name"), QStringLiteral("name"));
      sortCombo_->addItem(tr("Local first"), QStringLiteral("local"));
      sortCombo_->addItem(tr("Server first"), QStringLiteral("server"));
      sortCombo_->addItem(tr("Newest"), QStringLiteral("date-desc"));
      sortCombo_->addItem(tr("Oldest"), QStringLiteral("date-asc"));
      sortCombo_->addItem(tr("Manual order"), QStringLiteral("manual"));
      {
        const int mi = sortCombo_->findData(g_projectsSortMode);
        sortCombo_->setCurrentIndex(mi >= 0 ? mi : 0);
      }
      frow->addWidget(sortCombo_);
      // Search-mode (what the search box matches): name+keywords (default), names, keywords.
      searchModeCombo_ = new SearchComboBox(this, /*searchable=*/false);
      searchModeCombo_->setToolTip("What the search box matches");
      searchModeCombo_->addItem(tr("Name + keywords"), QStringLiteral("common"));
      searchModeCombo_->addItem(tr("Names only"), QStringLiteral("names"));
      searchModeCombo_->addItem(tr("Keywords only"), QStringLiteral("keywords"));
      frow->addWidget(searchModeCombo_);
      layout->addLayout(frow);
      connect(filter_, &QComboBox::currentIndexChanged, this, [this](int) { applyFilter(); });
      connect(sortCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        g_projectsSortMode = sortCombo_->currentData().toString();
        refresh();
      });
      connect(searchModeCombo_, &QComboBox::currentIndexChanged, this, [this](int) { applyFilter(); });
    }

    // Batch-select toolbar — shown whenever the filtered view has selectable rows
    // (it hosts Select all), with the selection-only controls coming and going with
    // the checked set. Labels + glyphs mirror the browser modal's batch bar; direction
    // buttons show by selection homogeneity (all-local → to-server; all-server → to-local).
    // The bar WRAPS (browser .projects-batch-bar flex-wrap, gap 10): a narrow dialog
    // never cuts "Remove selected" off at its edge (user report, with a picture).
    {
      batchBar_ = new QWidget(this);
      auto* bh = new FlowLayout(batchBar_, 0, 10, 6);
      batchCount_ = new QLabel("0 selected", this);
      bh->addWidget(batchCount_);
      // Select/deselect every row in the CURRENT filtered view (browser: `selectables`).
      // Accent-filled batch actions (browser parity): the accent rides the shared
      // accentCta property (theme.cpp), leaving objectNames free for the tests.
      const auto accentBtn = [this](const QString& label, const QString& icon,
                                    const QString& tip) {
        auto* b = new QPushButton(label, this);
        b->setToolTip(tip);
        b->setProperty("accentCta", true);
        b->setIcon(labelIcon(icon, QColor("#ffffff"), 13));
        return b;
      };
      selectAllBtn_ = accentBtn(tr("Select all"), "check",
                                "Select every project in the current filtered view");
      selectAllBtn_->setObjectName("projectsSelectAll");   // the batch-bar test finds it
      bh->addWidget(selectAllBtn_);
      connect(selectAllBtn_, &QPushButton::clicked, this, &ProjectsDialog::toggleSelectAll);
      batchToServer_ = accentBtn(tr("Move to server"), "server",
                                 "Move the checked local projects to a server");
      batchCopyServer_ = accentBtn(tr("Copy to server"), "copy",
                                   "Copy the checked local projects to a server");
      batchToLocal_ = accentBtn(tr("Move to local"), "download",
                                "Move the checked server projects to local storage");
      batchCopyLocal_ = accentBtn(tr("Copy to local"), "copy",
                                  "Copy the checked server projects to local storage");
      batchRemove_ = new QPushButton("Remove selected", this);
      batchRemove_->setToolTip("Remove the checked projects");
      batchRemove_->setObjectName("dangerButton");
      batchRemove_->setIcon(labelIcon("trash", QColor("#ffffff"), 13));
      batchClear_ = accentBtn("Clear", "x", "Clear the current checkbox selection");
      // The selection-only actions live in ONE group so the bar's swap is a single flight,
      // not one per button: a control's dust is photographed where it sits at that instant,
      // and siblings revealed in the same turn are still animating their own width.
      // Browser twin: .projects-batch-selected in projectsModal.js. At rest the group
      // wraps too (gap 6); only while its slot slides open or shut does it hold one line.
      batchSelectedGroup_ = new QWidget(batchBar_);
      auto* gh = new FlowLayout(batchSelectedGroup_, 0, 6, 6);
      gh->setLineSizeHint(true);
      gh->setHoldsLineWhileCapped(true);
      gh->addWidget(batchToServer_);
      gh->addWidget(batchCopyServer_);
      gh->addWidget(batchToLocal_);
      gh->addWidget(batchCopyLocal_);
      gh->addWidget(batchClear_);
      gh->addWidget(batchRemove_);   // destructive last, as in the browser's bar
      batchSelectedGroup_->setVisible(false);
      bh->addWidget(batchSelectedGroup_);
      batchBar_->setVisible(false);
      layout->addWidget(batchBar_);
      connect(batchToServer_, &QPushButton::clicked, this, [this] { runBatch(Action::BatchMoveToServer); });
      connect(batchCopyServer_, &QPushButton::clicked, this, [this] { runBatch(Action::BatchCopyToServer); });
      connect(batchToLocal_, &QPushButton::clicked, this, [this] { runBatch(Action::BatchMoveToLocal); });
      connect(batchCopyLocal_, &QPushButton::clicked, this, [this] { runBatch(Action::BatchCopyToLocal); });
      connect(batchRemove_, &QPushButton::clicked, this, [this] { runBatch(Action::BatchRemove); });
      connect(batchClear_, &QPushButton::clicked, this, [this] { checked_.clear(); refresh(); });
    }

    auto* reList = new ReorderableListWidget(this);
    list_ = reList;
    // Drag a row onto another to set a per-session Manual order (switches the Sort combo to
    // Manual). Rows are delegate-painted (no grip), so drags are view-initiated.
    reList->setDragEnabled(true);
    reList->onReorder = [this](int from, int to) {
      const int n = list_->count();
      if (from < 0 || from >= n) return;
      QVector<QString> keys(n);
      for (int i = 0; i < n; ++i) keys[i] = rowKeyAt(i);  // "" for placeholder rows
      if (to < 0) to = 0;
      if (to >= n) to = n - 1;
      const QString moved = keys[from];
      keys.remove(from);
      keys.insert(to, moved);
      QStringList order;
      for (const auto& k : keys) if (!k.isEmpty()) order << k;
      g_projectsManualOrder = order;
      g_projectsSortMode = QStringLiteral("manual");
      if (sortCombo_) { const int mi = sortCombo_->findData(g_projectsSortMode); if (mi >= 0) { QSignalBlocker b(sortCombo_); sortCombo_->setCurrentIndex(mi); } }
      refresh();
    };
    // Show/hide the main-window drag-out zones (Open here / Open in a new window / Remove) for the
    // duration of a row drag. The dialog covers the centre; zones are reachable in its margins.
    reList->onDragStart = [this] {
      rowDragging_ = true;
      if (clickTimer_) clickTimer_->stop();  // a drag is not a click
      if (dragZones_) dragZones_->begin(frameGeometry());
    };
    reList->onDragEnd = [this] {
      rowDragging_ = false;
      if (dragZones_) dragZones_->end();
    };
    // Drag a row OUT of the dialog and release in a zone → run that action. Open uses
    // openSelected() (local Open / remote OpenRemote); new-window + Remove are LOCAL-only (mirrors
    // the ⋯ menu; Remove routes through deleteSelected → its in-dialog Yes/No confirm).
    reList->onDragOut = [this](int rowIdx) {
      const auto zone = dragZones_ ? dragZones_->zoneAt(QCursor::pos()) : ProjectDragZones::Zone::None;
      if (zone == ProjectDragZones::Zone::None) return;  // released over the dialog / nowhere → keep
      QListWidgetItem* it = list_->item(rowIdx);
      if (!it || it->data(Qt::UserRole).isNull()) return;
      list_->setCurrentItem(it);
      const bool remote = !it->data(Qt::UserRole + 1).toString().isEmpty();
      using Zone = ProjectDragZones::Zone;
      // Open sets action_ + accept(); its confirm is shown by MainWindow AFTER the dialog
      // closes — never inside the drag release, which dismissed it. Remove confirms
      // in-dialog instead, deferred a turn (deleteSelected) for the same reason.
      if (zone == Zone::Here) {
        openSelected();  // local Open / remote OpenRemote
      } else if (zone == Zone::NewWindow) {
        if (remote) openSelected();  // no remote-in-new-window → open here
        else openSelectedInNewWindow();
      } else if (zone == Zone::Remove) {
        if (!remote) deleteSelected();  // server delete has no dialog action
      }
    };
    list_->setObjectName("projectsList");  // scopes the clearer row-checkbox style (theme.cpp)
    // Row icons hold each project's edited-result preview (local) or its stored
    // result/original image (server); size the list's icon column to fit them.
    list_->setIconSize(QSize(56, 56));
    list_->setSpacing(4);  // 8px gaps between row cards (browser .project-row margin-bottom)
    // Rows fit the viewport (the delegate clamps their width + elides) — never scroll sideways.
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Golden outline (not fill) around shared rows + the per-row "⋯" kebab,
    // mirroring the browser modal.
    list_->setItemDelegate(new ProjectRowDelegate(list_));
    // Hover-magnify + kebab clicks: track moves over the viewport to pop a larger
    // preview, and catch left-clicks on the "⋯" zone (handled in eventFilter).
    list_->viewport()->setMouseTracking(true);
    list_->viewport()->installEventFilter(this);
    // Hover glass shimmer over the hovered row (browser .project-row's ui-shimmer
    // sweep — the same overlay the other rows/buttons in the app already play).
    installRowShimmer(list_);
    // Right-click anywhere on a row opens the same actions as the "⋯" kebab.
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
              QListWidgetItem* it = list_->itemAt(pos);
              if (!it || it->data(Qt::UserRole).isNull()) return;
              list_->setCurrentItem(it);
              showRowMenu(it, list_->viewport()->mapToGlobal(pos));
            });
    layout->addWidget(list_, 1);
    refresh();

    // Row-open gestures (see the header's scheduleRowOpen mapping).
    connect(list_, &QListWidget::itemClicked, this, &ProjectsDialog::scheduleRowOpen);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
      // THE crux: kill the pending single-click open before it can raise the
      // confirmation, so the dialog never flashes on the way to a double click.
      if (clickTimer_) clickTimer_->stop();
      if (pressOnCheck_) return;   // double-tapping the checkbox never opens
      // Dblclick on the NAME edits it inline (browser parity: the name's dblclick
      // never opens the row) — local rows only; anywhere else still opens.
      if (it && !it->data(Qt::UserRole).isNull() &&
          it->data(Qt::UserRole + 1).toString().isEmpty()) {
        auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
        if (del && del->nameRectFor(list_->row(it)).adjusted(-4, -3, 4, 3).contains(pressPos_)) {
          beginInlineRename(it);
          return;
        }
      }
      openRow(it, isNewWindowMod(pressMods_), /*confirm=*/false);
    });
    // Return/Enter on the focused row opens it like a single click (confirms).
    // Consumed so it can't also trigger the dialog's default button.
    list_->installEventFilter(this);
    installEventFilter(this);   // own deactivation → hide the hover preview
    connect(list_, &QListWidget::itemChanged, this, &ProjectsDialog::onItemChanged);

    // Footer (browser settings-footer): the auto-save hint left, then the create actions
    // + danger Clear All, every enabled button accent-filled; Close lives in the header
    // pill. Per-row actions live on the "⋯" kebab + right-click menu, multi-row ones on
    // the batch toolbar above.
    QHBoxLayout* row = addModalFooter(
        chrome, tr("Projects auto-save · unopened projects expire after 7 days"));
    auto* blankBtn = new QPushButton("Blank image", this);
    makeModalCta(blankBtn, "image");
    blankBtn->setToolTip("Create a blank image (white, black, or any color) to draw on");
    auto* newBtn = new QPushButton("New editor", this);
    makeModalCta(newBtn, "plus-circle");
    newBtn->setToolTip("Create a new empty project from the current canvas");
    // "Clear All" only ever wipes local projects. When a server is connected, label it
    // "Clear All Local" so the button matches the actual (local-only) removal; the label is
    // kept in step reactively via ConnectionManager::changed() (see below).
    auto* clearAllBtn = new QPushButton("Clear All", this);
    clearAllBtn_ = clearAllBtn;
    clearAllBtn->setObjectName("dangerButton");  // red danger styling (mirrors the browser modal)
    clearAllBtn->setIcon(labelIcon("trash", QColor("#ffffff"), 15));
    clearAllBtn->setToolTip("Remove all local projects (server projects are not affected)");
    row->addWidget(blankBtn);
    row->addWidget(newBtn);
    row->addWidget(clearAllBtn);

    connect(newBtn, &QPushButton::clicked, this, &ProjectsDialog::createNew);
    connect(blankBtn, &QPushButton::clicked, this, &ProjectsDialog::createBlank);
    connect(clearAllBtn, &QPushButton::clicked, this, [this] {
      // Confirm HERE, parented to this dialog: the question then sits ON TOP of the still
      // open Projects window. Closing first and asking afterwards left the user answering
      // about a list they could no longer see.
      const int n = static_cast<int>(projects_.size());
      if (n == 0) return;
      ConfirmSpec spec;
      spec.title = tr("Clear all projects");
      spec.message = tr("Are you sure? This removes all %1 local project(s) and cannot be "
                        "undone. Server projects are not affected.")
                         .arg(n);
      spec.confirmIcon = QStringLiteral("trash");
      spec.danger = true;
      if (!confirmModal(this, spec)) return;
      scatterRows();              // the whole list comes apart before it empties
      emit clearAllRequested();   // the owner removes them, then calls setProjects()
    });

    // Keep the local-only "Clear All" label honest: "Clear All Local" while any server is
    // connected, plain "Clear All" otherwise. Driven off ConnectionManager::changed() so it
    // flips the moment a server connects/disconnects (no poll), matching the browser modal.
    if (connections_) {
      auto syncClearAllLabel = [this] {
        clearAllBtn_->setText(connections_->urls().isEmpty() ? "Clear All" : "Clear All Local");
      };
      syncClearAllLabel();
      connect(connections_, &stencil::net::ConnectionManager::changed, this, syncClearAllLabel);
    }
    // Nothing local to clear ⇒ nothing to offer (browser parity: projectsModal disables it
    // when only the synthetic "temporary (unsaved)" row is on screen).
    clearAllBtn->setEnabled(!projects_.empty());

    // Every control in the window gets the app's glass hover sweep, the way the toolbar
    // and the selection panel do; the list already had its row version. The browser's rule
    // covers the same set: `button, .btn-icon, .btn-icon-text` plus its text inputs.
    installHoverShimmerIn(this);

    // Server (shared) projects: list them now and keep them live with a periodic
    // re-list while the dialog is open. The desktop talks REST only, so this
    // polling stands in for the browser modal's WebSocket project-event feed.
    if (connections_ && !connections_->urls().isEmpty()) {
      // Defer the (synchronous) first listing to the next event-loop turn so the
      // dialog paints immediately with local rows + a "Loading shared projects…"
      // placeholder, instead of freezing on the network before it even shows.
      QTimer::singleShot(0, this, &ProjectsDialog::refreshRemote);
      remoteTimer_ = new QTimer(this);
      remoteTimer_->setInterval(5000);
      connect(remoteTimer_, &QTimer::timeout, this, &ProjectsDialog::refreshRemote);
      remoteTimer_->start();
    }
  }

  void ProjectsDialog::commitRowEdit(
      const QString& id, const QString& server,
      const std::function<void(Project&)>& mutate,
      const std::function<void(stencil::net::ServerClient*, qint64,
                               std::function<void(bool, qint64)>)>& push,
      const std::function<void(stencil::net::ServerProject&)>& cache) {
    if (server.isEmpty()) {
      for (auto& p : projects_)
        if (QString::fromStdString(p.meta.id) == id) { mutate(p); break; }
      fileStore::saveProjects(projects_);
      refresh();
      return;
    }
    stencil::net::ServerClient* c = connections_ ? connections_->find(server) : nullptr;
    if (!c) return;
    qint64 version = 0;
    for (const auto& sp : remote_)
      if (sp.id == id && sp.serverUrl == server) { version = sp.version; break; }
    QPointer<ProjectsDialog> self(this);
    push(c, version, [this, self, id, server, cache](bool ok, qint64 newVersion) {
      if (!self || !ok) return;
      for (auto& sp : remote_)
        if (sp.id == id && sp.serverUrl == server) { cache(sp); sp.version = newVersion; break; }
      refresh();
    });
  }

  void ProjectsDialog::refreshRemote() {
    if (!connections_ || remoteBusy_) return;
    remoteBusy_ = true;
    // Async cross-connection list (no nested event loop): the dialog stays responsive while the
    // server(s) respond; the rows populate when the merged listing resolves.
    QPointer<ProjectsDialog> self(this);
    connections_->sharedProjectsAsync([this, self](QVector<stencil::net::ServerProject> ps) {
      if (!self) return;
      remote_ = ps;
      remoteBusy_ = false;
      remoteLoaded_ = true;  // first listing resolved → drop the loading placeholder
      refresh();
    });
  }

  QPixmap ProjectsDialog::remoteThumb(const stencil::net::ServerProject& sp) {
    if (!connections_) return {};
    const QString key = QString("%1|%2|%3").arg(sp.serverUrl, sp.id).arg(sp.version);
    const auto cached = remoteThumbs_.constFind(key);
    if (cached != remoteThumbs_.constEnd()) return *cached;
    // Not cached — fetch asynchronously (result → original → source URL) so the dialog never
    // blocks on the network; the placeholder shows now and the icon swaps in on arrival.
    fetchServerThumbAsync(key, sp);
    return {};
  }

  // Download a server project's preview without blocking: rendered `result` →
  // `original` → the `source` web URL. Downloads bind to the connection's network
  // manager (which outlives this dialog), so a QPointer guards every callback.
  void ProjectsDialog::fetchServerThumbAsync(const QString& key,
                                             const stencil::net::ServerProject& sp) {
    if (thumbInFlight_.contains(key)) return;  // already downloading this version
    stencil::net::ServerClient* c = connections_ ? connections_->find(sp.serverUrl) : nullptr;
    if (!c) {
      fetchSourceThumbAsync(key, sp);  // no live client — try the source URL directly
      return;
    }
    thumbInFlight_.insert(key);
    const QString id = sp.id;
    const QString serverUrl = sp.serverUrl;
    const stencil::net::ServerProject spCopy = sp;
    QPointer<ProjectsDialog> self(this);
    c->downloadFileAsync(id, "result", [this, self, key, id, serverUrl, spCopy, c](bool ok,
                                                                                   QByteArray bytes) {
      if (!self) return;
      QImage img;
      if (ok && !bytes.isEmpty() && img.loadFromData(bytes)) {
        thumbInFlight_.remove(key);
        applyRemoteThumb(key, id, serverUrl, img);
        return;
      }
      // No rendered result — fall back to the uploaded original.
      c->downloadFileAsync(id, "original", [this, self, key, id, serverUrl, spCopy](bool ok2,
                                                                                    QByteArray b2) {
        if (!self) return;
        thumbInFlight_.remove(key);
        QImage img2;
        if (ok2 && !b2.isEmpty() && img2.loadFromData(b2)) {
          applyRemoteThumb(key, id, serverUrl, img2);
          return;
        }
        // No stored bytes at all — fetch the project's `source` web URL (extension-added).
        fetchSourceThumbAsync(key, spCopy);
      });
    });
  }

  void ProjectsDialog::fetchSourceThumbAsync(const QString& key,
                                             const stencil::net::ServerProject& sp) {
    const QUrl u(sp.source);
    if (!u.isValid() || (u.scheme() != "http" && u.scheme() != "https")) {
      remoteThumbs_.insert(key, QPixmap());  // nothing to fetch — cache the miss
      return;
    }
    if (thumbInFlight_.contains(key)) return;  // already downloading this version
    thumbInFlight_.insert(key);
    if (!thumbNet_) thumbNet_ = new QNetworkAccessManager(this);
    QNetworkRequest req(u);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = thumbNet_->get(req);
    const QString id = sp.id;
    const QString serverUrl = sp.serverUrl;
    // `this` as context: Qt drops the connection (and never fires into a dead dialog)
    // if the dialog is destroyed before the download completes.
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, id, serverUrl] {
      thumbInFlight_.remove(key);
      QImage img;
      if (reply->error() == QNetworkReply::NoError) img.loadFromData(reply->readAll());
      reply->deleteLater();
      applyRemoteThumb(key, id, serverUrl, img);  // caches even a miss so we don't refetch
    });
  }

  void ProjectsDialog::applyRemoteThumb(const QString& key, const QString& id,
                                        const QString& serverUrl, const QImage& img) {
    QPixmap pm;
    if (!img.isNull())
      pm = QPixmap::fromImage(img.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    remoteThumbs_.insert(key, pm);  // cache even a miss so we don't refetch every tick
    if (pm.isNull()) return;
    // Swap the placeholder for the picture on the live row (found by id+server, so a list rebuild
    // between request and response can't target a stale item).
    for (int i = 0; i < list_->count(); ++i) {
      QListWidgetItem* it = list_->item(i);
      if (it->data(Qt::UserRole).toString() == id &&
          it->data(Qt::UserRole + 1).toString() == serverUrl) {
        it->setIcon(QIcon(squareThumb(pm, 112)));   // uniform square row icon (cover)
        it->setData(Qt::UserRole + 2, pm);          // full-aspect source for hover-magnify
        break;
      }
    }
  }

  QPixmap ProjectsDialog::placeholderIcon(bool remote) const {
    QPixmap pm(56, 56);
    pm.fill(Qt::transparent);
    // Hand-drawn 56×56 glyphs (QStyle's SP_DriveNetIcon globe jars on macOS): gold
    // "rack" for server rows (matching the Servers dialog), muted picture for local.
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (remote) {
      const QColor gold("#d4a017");
      p.setPen(QPen(gold, 3));
      p.setBrush(Qt::NoBrush);
      const QRectF top(14, 15, 28, 11);
      const QRectF bot(14, 30, 28, 11);
      p.drawRoundedRect(top, 3, 3);
      p.drawRoundedRect(bot, 3, 3);
      p.setPen(Qt::NoPen);
      p.setBrush(gold);
      p.drawEllipse(QPointF(20, top.center().y()), 2, 2);
      p.drawEllipse(QPointF(20, bot.center().y()), 2, 2);
    } else {
      const QColor muted = palette().color(QPalette::Disabled, QPalette::Text);
      const QRectF frame(13, 15, 30, 26);
      p.setPen(QPen(muted, 2.5));
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(frame, 4, 4);
      p.setClipRect(frame);  // keep the little scene inside the frame
      p.setPen(Qt::NoPen);
      p.setBrush(muted);
      p.drawEllipse(QPointF(22, 23), 3, 3);  // sun
      QPolygonF mountain;
      mountain << QPointF(16, 41) << QPointF(27, 29) << QPointF(34, 35)
               << QPointF(41, 27) << QPointF(44, 41);
      p.drawPolygon(mountain);
    }
    return pm;
  }

  bool ProjectsDialog::dustHoverPreview(QListWidgetItem* it, bool gather) {
    if (!hoverPreview_ || !list_ || !it) return false;
    // The SAME thumbnail rect the hover hit test uses — the flight starts and
    // ends on the picture, never on the checkbox beside it (user report).
    const auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
    QRect iconCell = del ? del->iconRectFor(list_->row(it)) : QRect();
    if (!iconCell.isValid()) iconCell = list_->visualItemRect(it);
    const QPoint origin = list_->viewport()->mapToGlobal(iconCell.center());
    // alwaysEscape: the preview is its own ToolTip window ABOVE the dialog — a child
    // layer's motes played underneath it (user report); paintNow on a close, so the
    // preview never blinks out before any mote shows.
    return gui::flyTipDust(hoverPreview_, window(), origin, gather,
                           gather ? gui::kTipDustInMs : gui::kTipDustOutMs,
                           /*escapeHost=*/true, /*paintNow=*/!gather,
                           /*alwaysEscape=*/true) != nullptr;
  }

  QVariantAnimation* ProjectsDialog::hoverFade() {
    if (!hoverFade_) {
      hoverFade_ = new QVariantAnimation(this);
      connect(hoverFade_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        if (hoverPreview_) hoverPreview_->setWindowOpacity(v.toDouble());
      });
      connect(hoverFade_, &QVariantAnimation::finished, this, [this] {
        if (!hoverClosing_) return;
        hoverClosing_ = false;
        if (hoverPreview_) hoverPreview_->hide();
      });
    }
    return hoverFade_;
  }

  void ProjectsDialog::placeHoverPreview(const QPoint& globalCursor) {
    if (!hoverPreview_) return;
    // Down-right of the cursor, flipped/clamped to stay on-screen (browser positionZoom).
    QScreen* s = QGuiApplication::screenAt(globalCursor);
    const QRect scr = (s ? s : QGuiApplication::primaryScreen())->availableGeometry();
    const QSize sz = hoverPreview_->size();
    QPoint gp = globalCursor + QPoint(18, 18);
    if (gp.x() + sz.width() > scr.right()) gp.setX(globalCursor.x() - 18 - sz.width());
    if (gp.y() + sz.height() > scr.bottom()) gp.setY(scr.bottom() - sz.height());
    if (gp.x() < scr.left()) gp.setX(scr.left());
    if (gp.y() < scr.top()) gp.setY(scr.top());
    hoverPreview_->move(gp);
  }

  void ProjectsDialog::revealHoverPreview(QListWidgetItem* it) {
    if (!hoverPreview_) return;
    hoverClosing_ = false;   // BEFORE stop(): stop() emits finished, which would hide()
    auto* fade = hoverFade();
    fade->stop();
    fade->setKeyValues({});
    if (support::motionReduced()) {  // the end state, at once
      hoverPreview_->setWindowOpacity(1.0);
      return;
    }
    if (dustHoverPreview(it, /*gather=*/true)) {
      // The preview waits behind its own motes and fades up as the last of them land
      // (the shared surfaceForm ramp).
      hoverPreview_->setWindowOpacity(0.0);
      gui::holdFadeKeys(fade, gui::kTipDustInMs);
    } else {
      fade->setDuration(kHoverFadeMs);
      fade->setStartValue(hoverPreview_->windowOpacity());
      fade->setEndValue(1.0);
    }
    fade->start();
  }

  void ProjectsDialog::hideHoverPreview() {
    if (!hoverPreview_ || !hoverPreview_->isVisible() || hoverClosing_) return;
    // Photographed and dusted while it is still the box on screen — the cloud is what
    // it leaves behind, so the hand-over is one beat, not a cut: the label itself
    // fades out BEHIND the leaving motes instead of blinking off under them.
    const bool dusted = !support::motionReduced() && dustHoverPreview(hoverItem_, /*gather=*/false);
    hoverItem_ = nullptr;
    if (!dusted) {
      if (hoverFade_) { hoverClosing_ = false; hoverFade_->stop(); }
      hoverPreview_->hide();
      return;
    }
    auto* fade = hoverFade();
    fade->stop();
    fade->setKeyValues({});
    hoverClosing_ = true;
    fade->setDuration(gui::kDustHandOverMs);
    fade->setStartValue(hoverPreview_->windowOpacity());
    fade->setEndValue(0.0);
    fade->start();
  }

  bool ProjectsDialog::pointerOverPreviewedIcon() const {
    if (!list_ || !hoverItem_) return false;
    const QPoint vpos = list_->viewport()->mapFromGlobal(QCursor::pos());
    if (!list_->viewport()->rect().contains(vpos)) return false;
    if (list_->itemAt(vpos) != hoverItem_) return false;
    const auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
    const QRect dec = del ? del->iconRectFor(list_->row(hoverItem_)) : QRect();
    return dec.isValid() && dec.adjusted(-2, -2, 2, 2).contains(vpos);
  }

  bool ProjectsDialog::eventFilter(QObject* obj, QEvent* ev) {
    // The hover preview is a ToolTip window: switching window or app delivers the
    // list no Leave, and the popup would float on over whatever came to the front.
    // Verified against the CURSOR, not taken on faith: our own preview window
    // materializing under a stationary pointer (the clamped Alt glance reaches it)
    // makes the platform emit spurious Leave/deactivate to the widget below, and an
    // unconditional hide then loops show-dust/hide-dust on every move while the
    // pointer never actually left the thumbnail (user report).
    if (hoverPreview_ && obj == this &&
        (ev->type() == QEvent::WindowDeactivate || ev->type() == QEvent::ApplicationDeactivate) &&
        !(QGuiApplication::applicationState() == Qt::ApplicationActive &&
          pointerOverPreviewedIcon())) {
      hideHoverPreview();
    }
    // Alt pressed/released while the preview is up: re-scale it in place from the
    // source pixmap it carries (held = 2x, released = back to the glance size), and
    // replay its gather — the size change is a real appearance, same as landing on a
    // new row.
    if (hoverPreview_ && hoverPreview_->isVisible() && hoverItem_ &&
        (ev->type() == QEvent::KeyPress || ev->type() == QEvent::KeyRelease) &&
        static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Alt &&
        !static_cast<QKeyEvent*>(ev)->isAutoRepeat()) {
      const QPixmap src = hoverPreview_->property("srcPixmap").value<QPixmap>();
      if (!src.isNull()) {
        const int edge = (ev->type() == QEvent::KeyPress) ? kHoverPreviewAltPx : kHoverPreviewPx;
        hoverPreview_->setPixmap(
            src.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        hoverPreview_->adjustSize();
        // Re-clamped for the new size (the doubled glance can run off the screen) and
        // re-formed: the size change IS a re-appearance, dust and hold-fade included.
        placeHoverPreview(QCursor::pos());
        revealHoverPreview(hoverItem_);
      }
    }
    // Return/Enter on the focused row = a plain open (with confirmation), and
    // consumed so the dialog's default button can't also fire.
    if (list_ && obj == list_ && ev->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(ev);
      if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
        if (clickTimer_) clickTimer_->stop();
        openRow(list_->currentItem(), isNewWindowMod(ke->modifiers()), /*confirm=*/true);
        return true;
      }
    }
    if (list_ && obj == list_->viewport()) {
      // On a width change, recompute item sizeHints so rows re-clamp + re-elide to the new
      // viewport width (the delegate caps width to the viewport).
      if (ev->type() == QEvent::Resize) list_->doItemsLayout();
      // Left-click on the "⋯" kebab strip pops the row's menu (consume it so it
      // doesn't also start a drag/selection); it's the right-click menu's twin.
      // The magnified preview IS the tooltip — never stack the text one on it.
      if (ev->type() == QEvent::ToolTip && hoverPreview_ && hoverPreview_->isVisible())
        return true;
      if (ev->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(ev);
        // itemClicked/itemDoubleClicked carry no modifiers — remember the ones
        // that were down for the press that produces them (and where it landed,
        // for the name-dblclick rename hit test).
        pressMods_ = me->modifiers();
        const QPoint vpos = me->position().toPoint();
        pressPos_ = vpos;
        QListWidgetItem* it = list_->itemAt(vpos);
        // A press on the checkbox strip is a SELECTION gesture: it must toggle
        // the box and nothing else (no row-open, no confirm, dialog stays).
        const QRect vr = it ? list_->visualItemRect(it) : QRect();
        pressOnCheck_ = me->button() == Qt::LeftButton && it &&
                        QRect(vr.left(), vr.top(), 34, vr.height()).contains(vpos);
        if (me->button() == Qt::LeftButton && it &&
            !it->data(Qt::UserRole).isNull() &&
            kebabZone(list_->visualItemRect(it)).contains(vpos)) {
          list_->setCurrentItem(it);
          showRowMenu(it, me->globalPosition().toPoint());
          return true;
        }
      }
      if (ev->type() == QEvent::ToolTip) {
        // The rows' tooltips go through the app's tooltip, not Qt's plain label:
        // AppTooltip's filter skips item views by design (they resolve a per-index tip in
        // viewportEvent), so these rows were the one place still showing Qt's box.
        auto* he = static_cast<QHelpEvent*>(ev);
        QListWidgetItem* it = list_->itemAt(he->pos());
        // The "⋯" carries its OWN tip, as the browser's per-row button does
        // (projectsModal.js menuBtn.title), not the row's picture info.
        const bool onKebab = it && !it->data(Qt::UserRole).isNull() &&
                             kebabZone(list_->visualItemRect(it)).contains(he->pos());
        const QString tip = !it ? QString()
                                : (onKebab ? kKebabTip : it->toolTip());
        if (tip.isEmpty()) { gui::appTooltip()->hideTip(); return true; }
        // …forming out of the CURSOR, which is where the browser's own tooltip flies from
        // and back into (ui/tooltip.js dust). Left to the default it grew out of the
        // owner's centre — here the whole viewport, i.e. the middle of the list.
        tipRowText_ = tip;
        gui::appTooltip()->showFor(list_->viewport(), tip, he->globalPos(),
                                   QRect(he->globalPos(), QSize(1, 1)));
        return true;
      }
      if (ev->type() == QEvent::MouseMove) {
        const QPoint vpos = static_cast<QMouseEvent*>(ev)->position().toPoint();
        QListWidgetItem* it = list_->itemAt(vpos);
        // Which row's "⋯" the pointer is actually on — the chip styles itself only for
        // that, never for a hover anywhere else on the row (user report).
        const bool onKebab = it && !it->data(Qt::UserRole).isNull() &&
                             kebabZone(list_->visualItemRect(it)).contains(vpos);
        setKebabHover(onKebab ? list_->row(it) : -1);
        // …and the tooltip TRAVELS with the pointer while it is up: moveTo SLIDES it, with
        // no re-measure and no entrance. Going back through showFor on every move re-ran
        // its appearance bookkeeping and made the tip stutter and jump (user report).
        if (auto* tip = gui::appTooltip(); tip->isVisible() && tip->owner() == list_->viewport()) {
          // Which tip belongs HERE — the kebab's own, or the row's picture info. Reading
          // only the row's would slide it on over the "⋯", where a different tip is due.
          const QString text = !it ? QString() : (onKebab ? kKebabTip : it->toolTip());
          if (text.isEmpty() || text != tipRowText_) tip->hideTip();
          else tip->moveTo(static_cast<QMouseEvent*>(ev)->globalPosition().toPoint());
        }
        const QPixmap src = it ? it->data(Qt::UserRole + 2).value<QPixmap>() : QPixmap();
        // Magnify only while over the THUMBNAIL itself — the decoration rect the
        // delegate recorded at paint time (checkbox excluded), so the hit test
        // matches the pixels exactly and cannot flicker against a re-derived guess.
        bool overIcon = false;
        if (it && !src.isNull()) {
          const auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
          const QRect dec = del ? del->iconRectFor(list_->row(it)) : QRect();
          overIcon = dec.isValid() && dec.adjusted(-2, -2, 2, 2).contains(vpos);
        }
        // The magnifiable thumb advertises itself (browser .project-thumb img
        // cursor:zoom-in); back to the default arrow the moment the pointer leaves it.
        if (overIcon != hoverZoomCursor_) {
          if (overIcon) list_->viewport()->setCursor(zoomInCursor());
          else list_->viewport()->unsetCursor();
          hoverZoomCursor_ = overIcon;
        }
        if (overIcon) {
          // An APPEARANCE (first show, or a swap onto a different row) forms out of
          // the row and anchors there; moves that stay on the same thumb keep the
          // preview's dust alone (it replayed per move before, scattering motes with
          // every pixel of travel; user report) but still carry the box along with
          // the pointer, browser positionZoom parity.
          const QPoint cur = static_cast<QMouseEvent*>(ev)->globalPosition().toPoint();
          const bool appearing = !hoverPreview_ || !hoverPreview_->isVisible() ||
                                 hoverClosing_ || hoverItem_ != it;
          if (appearing) {
            // A different row's preview still up? Dust it back into ITS row first —
            // the browser's old-thumb mouseleave plays surfaceOut before the new
            // thumb's mouseenter gathers.
            if (hoverPreview_ && hoverPreview_->isVisible() && hoverItem_ && hoverItem_ != it)
              hideHoverPreview();
            if (!hoverPreview_) {
              // Input-transparent, like the browser zoom's pointer-events:none — the
              // clamped glance (the doubled Alt one especially) can land UNDER the
              // pointer, and a window that eats the hover makes the viewport churn
              // Leave/Enter, replaying the dust on every move (user report).
              hoverPreview_ = new QLabel(this, Qt::ToolTip | Qt::FramelessWindowHint |
                                                   Qt::WindowTransparentForInput |
                                                   Qt::WindowDoesNotAcceptFocus);
              hoverPreview_->setAttribute(Qt::WA_ShowWithoutActivating, true);
              hoverPreview_->setStyleSheet(
                  "QLabel{background:#1e1e1e;border:2px solid #d4a017;"
                  "border-radius:8px;padding:4px;}");
            }
            // Alt HELD magnifies the glance (chat HoverPreview / browser parity). The
            // SOURCE rides along so the Alt toggle below can re-scale without a move.
            const int edge =
                (QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier)
                    ? kHoverPreviewAltPx
                    : kHoverPreviewPx;
            hoverPreview_->setProperty("srcPixmap", src);
            hoverPreview_->setPixmap(
                src.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            hoverPreview_->adjustSize();
            placeHoverPreview(cur);
            // Shown, but held invisible behind its gathering motes (revealHoverPreview
            // ramps it up); reduced motion shows the end state at once.
            hoverPreview_->setWindowOpacity(support::motionReduced() ? 1.0 : 0.0);
            hoverPreview_->show();
            hoverItem_ = it;
            revealHoverPreview(it);
          } else {
            placeHoverPreview(cur);
          }
        } else if (hoverPreview_) {
          hideHoverPreview();
        }
      } else if (ev->type() == QEvent::Leave) {
        setKebabHover(-1);   // …or the chip stays lit after the pointer has gone
        if (auto* tip = gui::appTooltip(); tip->owner() == list_->viewport()) tip->hideTip();
        // Same verified-against-the-cursor rule as the deactivate backstop above: a
        // Leave fired by our own preview window sliding under the pointer must not
        // hide what the pointer is still hovering.
        if (!pointerOverPreviewedIcon()) {
          if (hoverPreview_) hideHoverPreview();
          if (hoverZoomCursor_) {
            list_->viewport()->unsetCursor();
            hoverZoomCursor_ = false;
          }
        }
      }
    }
    // Inline rename editor: Esc cancels (and must NOT fall through to the dialog's
    // own reject), click-away (focus loss) discards — browser blur parity. The ✓/✗
    // buttons are NoFocus, so their clicks land before any focus-out can fire.
    if (renameBox_ && obj->parent() == renameBox_) {
      if (ev->type() == QEvent::KeyPress &&
          static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Escape) {
        closeInlineRename();
        return true;
      }
      if (ev->type() == QEvent::FocusOut) closeInlineRename();
    }
    return QDialog::eventFilter(obj, ev);
  }

  // Replace the listed projects and repaint (see the header): lets the owner act on a
  // request without the dialog having to close and be reopened.
  void ProjectsDialog::setProjects(const std::vector<Project>& projects) {
    projects_ = projects;
    if (clearAllBtn_) clearAllBtn_->setEnabled(!projects_.empty());   // nothing left to clear
    refresh();
  }

  void ProjectsDialog::refresh() {
    // Preserve the selected row across a live remote re-list so the polling timer
    // doesn't yank the user's selection out from under them.
    const int prevRow = list_->currentRow();
    // Keep the "Show:" per-server entries in step if servers were connected/disconnected.
    if (filter_ && connections_ && connections_->urls() != knownServerUrls_)
      rebuildFilterOptions();
    building_ = true;   // ignore the itemChanged storm from setCheckState below
    hideHoverPreview();   // clear() is about to delete whatever hoverItem_ points to
    closeInlineRename();  // …and the row the inline editor floats over
    list_->clear();
    const core::ProjectsStore store;  // pure helpers only; reads meta, no state

    // Multi-line row tooltip: image size with its orientation under it, the description
    // when set, then the origin note. The separator is the browser's " · ", which is what
    // tipContent splits a heading on. No drawn-line length — nobody hovers a row for it.
    auto rowTooltip = [&](int w, int h, const QString& description,
                          const QString& origin) {
      QStringList lines;
      if (w > 0 && h > 0)
        lines << QString("%1x%2 px · %3").arg(w).arg(h).arg(
            h >= w ? QStringLiteral("portrait") : QStringLiteral("landscape"));
      if (!description.isEmpty()) lines << QString("Description: %1").arg(description);
      if (!origin.isEmpty()) lines << origin;
      return lines.join('\n');
    };

    // Build one LOCAL project row (edited-result thumb, expiry-aware name colour, checkbox).
    auto buildLocalRow = [&](const Project& pr) {
      // Name · created · expiry only — no line/point counts (mirrors the browser projects list).
      const QString expiry = expiryText(store, pr.meta, now_);
      QString label = QString::fromStdString(pr.meta.name);
      const QString created = createdText(pr.meta.createdAt);
      if (!created.isEmpty()) label += QString("   ·   %1").arg(created);
      if (!expiry.isEmpty()) label += QString("   ·   %1").arg(expiry);
      auto* it = new QListWidgetItem(label, list_);
      it->setData(Qt::UserRole, QString::fromStdString(pr.meta.id));
      it->setData(Qt::UserRole + 3, QString::fromStdString(pr.meta.name));  // search key (name)
      // The stacked row's muted middle line + the accent its kebab chip paints with.
      {
        QStringList metaBits;
        if (!created.isEmpty()) metaBits << created;
        if (!expiry.isEmpty()) metaBits << expiry;
        it->setData(kMetaRole, metaBits.join(QStringLiteral(" · ")));
      }
      // Multi-select checkbox (key "|<id>" — empty server marks a local row).
      it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
      it->setCheckState(checked_.contains("|" + QString::fromStdString(pr.meta.id))
                            ? Qt::Checked : Qt::Unchecked);
      // Edited-result preview, pre-rendered by the caller through the canvas/export
      // path. Absent for pathless (in-memory) sources — those fall back to a
      // uniform placeholder tile so every row keeps the same height.
      const auto thumb = thumbs_.constFind(QString::fromStdString(pr.meta.id));
      if (thumb != thumbs_.constEnd() && !thumb->isNull()) {
        it->setIcon(QIcon(squareThumb(*thumb, 112)));
        it->setData(Qt::UserRole + 2, *thumb);
      } else {
        it->setIcon(QIcon(placeholderIcon(false)));
      }
      // NAME colour (UserRole+4) — the delegate paints ONLY the name in it. Red once
      // expired, amber within a day of expiry (warnings win over the swatch), else
      // the per-project colour, else the shared neutral grey (browser/CLI default).
      const QString pcol = QString::fromStdString(pr.meta.color);
      const QColor custom(pcol);
      QColor nameCol;
      if (store.isExpired(pr.meta, now_)) nameCol = QColor("#dc3545");
      else if (store.isExpiringSoon(pr.meta, now_)) nameCol = QColor("#e0a800");
      else if (!pcol.isEmpty() && custom.isValid()) nameCol = custom;
      else nameCol = QColor("#80868f");
      it->setData(Qt::UserRole + 4, nameCol);
      // UserRole+5: space-joined keywords, the search key for the keyword/common modes.
      QStringList kw;
      for (const auto& k : pr.meta.keywords) kw << QString::fromStdString(k);
      it->setData(Qt::UserRole + 5, kw.join(' '));
      // UserRole+6: file-origin flag (opened from a .stencil) → the delegate's bronze outline
      // + file glyph. The tooltip names where the project lives (local disk vs a .stencil file).
      it->setData(Qt::UserRole + 6, pr.meta.fromFile);
      // UserRole+8: this row is the project open in THIS editor right now → the
      // delegate's "(Current)" mark, painted in the palette's live accent.
      if (!activeProjectId_.isEmpty() && QString::fromStdString(pr.meta.id) == activeProjectId_)
        it->setData(kActiveRole, true);
      // A LOCAL project says nothing about its origin here: the row already carries the
      // "computer" badge, and the browser's own tip carries no origin line at all. A .stencil
      // project keeps its note, which tells you more than that badge's one word does.
      it->setToolTip(rowTooltip(pr.meta.imageW, pr.meta.imageH,
                                QString::fromStdString(pr.meta.description),
                                pr.meta.fromFile ? QStringLiteral("Opened from a .stencil project file")
                                                 : QString()));
    };

    // Build one SERVER (shared) project row: golden outline (delegate) + server badge.
    // UserRole+1 carries the origin server URL; a non-empty value marks the row as remote so
    // Open routes to OpenRemote (and tells the delegate to draw the outline).
    auto buildRemoteRow = [&](const stencil::net::ServerProject& sp) {
      QString label = QString("%1   —   %2")
                          .arg(sp.name.isEmpty() ? QStringLiteral("Untitled") : sp.name)
                          .arg(sp.serverUrl);
      const QString spCreated = createdText(sp.createdAt);
      if (!spCreated.isEmpty()) label += QString("   ·   %1").arg(spCreated);
      const QString spExpires = expiresText(sp.expiresAt);
      if (!spExpires.isEmpty()) label += QString("   ·   %1").arg(spExpires);
      auto* it = new QListWidgetItem(label, list_);
      it->setData(Qt::UserRole, sp.id);
      it->setData(Qt::UserRole + 1, sp.serverUrl);
      {
        QStringList metaBits;
        if (!spCreated.isEmpty()) metaBits << spCreated;
        if (!spExpires.isEmpty()) metaBits << spExpires;
        it->setData(kMetaRole, metaBits.join(QStringLiteral(" · ")));
      }
      it->setData(Qt::UserRole + 3, sp.name.isEmpty() ? QStringLiteral("Untitled") : sp.name);  // search key
      it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
      it->setCheckState(checked_.contains(sp.serverUrl + "|" + sp.id) ? Qt::Checked : Qt::Unchecked);
      // The NAME colour (UserRole+4) — the delegate paints ONLY the name in it, so the "— <url>"
      // suffix stays the default colour. Per-project colour when set, else the shared neutral grey
      // (same as local rows + the browser default — not gold). The gold outline marks server rows.
      const QColor custom(sp.color);
      it->setData(Qt::UserRole + 4,
                  (!sp.color.isEmpty() && custom.isValid()) ? custom : QColor("#80868f"));
      it->setData(Qt::UserRole + 5, sp.keywords.join(' '));  // keyword search key
      it->setToolTip(rowTooltip(sp.imageW, sp.imageH, sp.description,
                                QString("Server project on %1").arg(sp.serverUrl)));
      // Edited preview: the rendered `result`, falling back to `original` (browser
      // makeRemoteRow parity). Cached by id+version so the periodic re-list
      // doesn't re-download an unchanged project.
      const QPixmap pm = remoteThumb(sp);
      if (pm.isNull()) {
        it->setIcon(QIcon(placeholderIcon(true)));
      } else {
        it->setIcon(QIcon(squareThumb(pm, 112)));
        it->setData(Qt::UserRole + 2, pm);
      }
    };

    // Assemble a combined, sortable entry list (local + server) and order it per the active
    // sort mode, so name/date modes interleave local and server rows (mirrors the browser modal;
    // see js/ui/projectSort.js). Then build the rows in that order.
    struct Entry { bool remote; int idx; QString key; QString name; long long date; };
    std::vector<Entry> entries;
    for (int i = 0; i < static_cast<int>(projects_.size()); ++i) {
      const auto& m = projects_[i].meta;
      entries.push_back({ false, i, "|" + QString::fromStdString(m.id),
                          QString::fromStdString(m.name).toLower(), static_cast<long long>(m.updatedAt) });
    }
    for (int i = 0; i < remote_.size(); ++i) {
      const auto& sp = remote_[i];
      entries.push_back({ true, i, sp.serverUrl + "|" + sp.id, sp.name.toLower(), static_cast<long long>(sp.createdAt) });
    }
    const QString mode = g_projectsSortMode;
    QHash<QString, int> manualPos;
    if (mode == "manual")
      for (int i = 0; i < g_projectsManualOrder.size(); ++i) manualPos.insert(g_projectsManualOrder[i], i);
    auto cmpName = [](const Entry& a, const Entry& b) -> int {
      int c = QString::localeAwareCompare(a.name, b.name);
      if (c) return c;
      if (a.date != b.date) return a.date > b.date ? -1 : 1;  // newest first on a name tie
      return QString::compare(a.key, b.key);
    };
    std::stable_sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
      if (mode == "local") { if (a.remote != b.remote) return !a.remote; return cmpName(a, b) < 0; }
      if (mode == "server") { if (a.remote != b.remote) return a.remote; return cmpName(a, b) < 0; }
      if (mode == "date-desc") { if (a.date != b.date) return a.date > b.date; return cmpName(a, b) < 0; }
      if (mode == "date-asc") { if (a.date != b.date) return a.date < b.date; return cmpName(a, b) < 0; }
      if (mode == "manual") {
        const int pa = manualPos.value(a.key, std::numeric_limits<int>::max());
        const int pb = manualPos.value(b.key, std::numeric_limits<int>::max());
        if (pa != pb) return pa < pb;
        return cmpName(a, b) < 0;
      }
      return cmpName(a, b) < 0;  // name (default): server + local interleaved
    });
    for (const auto& e : entries) {
      if (e.remote) buildRemoteRow(remote_[e.idx]);
      else buildLocalRow(projects_[e.idx]);
    }

    // While the first server listing is still in flight, show a loading hint rather
    // than a misleading "No projects yet" — the dialog itself already opened (the
    // remote fetch is deferred); this row is replaced when the listing resolves.
    if (connections_ && !connections_->urls().isEmpty() && !remoteLoaded_) {
      auto* it = new QListWidgetItem(QStringLiteral("Loading shared projects…"), list_);
      it->setFlags(Qt::NoItemFlags);
      it->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
    }

    // Drop selections whose project is GONE — removed here, or from another window: the
    // bar reads checked_.size(), so a dead key kept "1 selected" on screen over an empty
    // list (user report). A LOCAL key is "|<id>" (UserRole+1, its server url, is empty);
    // a remote key is left alone — a listing that has not answered is not proof it is gone.
    if (!checked_.isEmpty()) {
      QSet<QString> liveIds;
      liveIds.reserve(projects_.size());
      for (const Project& p : projects_) liveIds.insert(QString::fromStdString(p.meta.id));
      for (auto it = checked_.begin(); it != checked_.end();) {
        if (it->startsWith(QLatin1Char('|')) && !liveIds.contains(it->mid(1)))
          it = checked_.erase(it);
        else
          ++it;
      }
    }

    if (list_->count() == 0) {
      auto* it = new QListWidgetItem("No projects yet", list_);
      it->setFlags(Qt::NoItemFlags);
      building_ = false;
      updateBatchBar();
      return;
    }
    list_->setCurrentRow(prevRow >= 0 && prevRow < list_->count() ? prevRow : 0);
    applyFilter();   // re-hide rows the current filter excludes (survives the live re-list)
    building_ = false;
    updateBatchBar();
  }

  // A row's checkbox toggled → update the checked set + the batch toolbar.
  void ProjectsDialog::onItemChanged(QListWidgetItem* it) {
    if (building_ || !it || it->data(Qt::UserRole).isNull()) return;
    const QString key = QString("%1|%2").arg(it->data(Qt::UserRole + 1).toString(),
                                             it->data(Qt::UserRole).toString());
    if (it->checkState() == Qt::Checked) checked_.insert(key);
    else checked_.remove(key);
    updateBatchBar();
  }

  // The pure homogeneity rule behind the batch bar (headless-testable without a server).
  BatchDirections batchDirectionsFor(int locals, int remotes, bool haveServers) {
    BatchDirections d;
    d.toServer = locals > 0 && remotes == 0 && haveServers;
    d.toLocal = remotes > 0 && locals == 0;
    return d;
  }

  // Show/hide the batch toolbar. Inapplicable directions are HIDDEN, not greyed: a
  // local-only selection never moves "to local", so a disabled button is just noise
  // (browser parity: updateBatchBar in projectsModal.js).
  void ProjectsDialog::updateBatchBar() {
    if (!batchBar_) return;
    int locals = 0, remotes = 0;
    for (const QString& k : checked_) {
      if (k.startsWith('|')) ++locals; else ++remotes;
    }
    const int n = checked_.size();
    // The bar hosts Select all too, so it shows whenever the filtered view HAS
    // selectable rows — the selection-only controls inside it come and go with the
    // checked set (browser parity: projectsModal.js updateBatchBar).
    const auto anySelectableNow = [this] {
      for (int i = 0; i < list_->count(); ++i) {
        const QListWidgetItem* it = list_->item(i);
        if (filteredIn(it) && !it->data(Qt::UserRole).isNull() &&
            (it->flags() & Qt::ItemIsUserCheckable))
          return true;
      }
      return false;
    };
    // Opens at once, closes only once its contents have flown (support/controlReveal) —
    // taking the strip away outright took Select all's own out-flight off the screen
    // before a frame of it showed (the connections dialog's twin, and its user report).
    revealBar(batchBar_, [this, anySelectableNow] {
      return !checked_.isEmpty() || anySelectableNow();
    });
    // The count rides the same swap as the buttons: a hard show/hide on the FIRST thing in
    // the row shoved everything after it sideways in one frame, which is most of what read
    // as "jumping" (user report). Text first, so it is right before the slot opens.
    if (batchCount_) {
      batchCount_->setText(tr("%1 selected").arg(n));
      revealControls(batchCount_, n > 0);
    }
    const bool haveServers = connections_ && !connections_->urls().isEmpty();
    const auto dir = batchDirectionsFor(locals, remotes, haveServers);
    // Which directions apply is a plain visibility flip INSIDE the group — it is the group
    // that flies, so these never carry a cloud of their own.
    if (batchToServer_) batchToServer_->setVisible(dir.toServer);
    if (batchCopyServer_) batchCopyServer_->setVisible(dir.toServer);
    if (batchToLocal_) batchToLocal_->setVisible(dir.toLocal);
    if (batchCopyLocal_) batchCopyLocal_->setVisible(dir.toLocal);
    // …and the GROUP comes and goes as the app's control swap (support/controlReveal,
    // browser motion.js revealControls). A no-op when the state is already right, so an
    // unrelated refresh() plays nothing. Laid out first: the swap photographs the group
    // as it stands, and the flips above have only QUEUED its re-flow — the picture would
    // still hold the hidden buttons' gaps and the old wrap.
    if (batchSelectedGroup_) {
      if (QLayout* gl = batchSelectedGroup_->layout()) gl->activate();
      revealControls(batchSelectedGroup_, n > 0);
    }
    updateSelectAll();
  }

  // True when the filtered view has selectable rows and every one of them is checked.
  bool ProjectsDialog::allFilteredChecked() const {
    bool any = false;
    for (int i = 0; i < list_->count(); ++i) {
      const QListWidgetItem* it = list_->item(i);
      // filteredIn, not isHidden: a row still fading OUT has already left the pool.
      if (!filteredIn(it) || it->data(Qt::UserRole).isNull() ||
          !(it->flags() & Qt::ItemIsUserCheckable))
        continue;   // placeholders, filtered-out and doomed rows are not selectable
      if (!checked_.contains(rowKeyAt(i))) return false;
      any = true;
    }
    return any;
  }

  // Shown whenever the filtered view has selectable rows; the label flips to
  // "Deselect all" once everything visible is checked (browser: updateSelectAll).
  void ProjectsDialog::updateSelectAll() {
    if (!selectAllBtn_) return;
    bool any = false;
    for (int i = 0; i < list_->count() && !any; ++i) {
      const QListWidgetItem* it = list_->item(i);
      any = filteredIn(it) && !it->data(Qt::UserRole).isNull() &&
            (it->flags() & Qt::ItemIsUserCheckable);
    }
    revealControls(selectAllBtn_, any);
    // Label AND glyph say which way it goes: a check gathers, a cross lets go
    // (browser icons.js setSelectAllFace).
    const bool all = allFilteredChecked();
    selectAllBtn_->setText(all ? tr("Deselect all") : tr("Select all"));
    selectAllBtn_->setIcon(labelIcon(all ? "x" : "check", QColor("#ffffff"), 13));
  }

  // Select-all toggles over the CURRENT filtered view, so a filtered "select all" never
  // sweeps up projects the user cannot see; deselect clears the WHOLE selection.
  void ProjectsDialog::toggleSelectAll() {
    if (allFilteredChecked()) {
      checked_.clear();
      refresh();   // re-sync every row's checkbox (also off-filter ones)
      return;      // refresh() already ran updateBatchBar
    }
    for (int i = 0; i < list_->count(); ++i) {
      QListWidgetItem* it = list_->item(i);
      if (filteredIn(it) && !it->data(Qt::UserRole).isNull() &&
          (it->flags() & Qt::ItemIsUserCheckable))
        it->setCheckState(Qt::Checked);   // onItemChanged maintains checked_
    }
    updateBatchBar();
  }

  // Resolve the checked rows into (id, serverUrl) pairs, pick a target server for the
  // to-server actions, then accept() so the main window applies the batch.
  void ProjectsDialog::runBatch(Action act) {
    batchItems_.clear();
    for (const QString& k : checked_) {
      const int bar = k.indexOf('|');
      batchItems_.append({ k.mid(bar + 1), k.left(bar) });  // (id, serverUrl)
    }
    if (batchItems_.isEmpty()) return;
    if (act == Action::BatchMoveToServer || act == Action::BatchCopyToServer) {
      if (!connections_ || connections_->urls().isEmpty()) return;
      const QString target = pickServer(
          this, connections_->urls(),
          act == Action::BatchMoveToServer ? tr("Move the selected projects to which server?")
                                           : tr("Copy the selected projects to which server?"));
      if (target.isEmpty()) return;
      selectedServerUrl_ = target;
    }
    if (act == Action::BatchRemove) {
      // Confirm HERE (like Clear All): the question sits over the still-open list, the
      // owner removes on removeRequested and repaints via setProjects() — no close.
      ConfirmSpec spec;
      spec.title = tr("Remove projects");
      spec.message = tr("Remove %1 selected project(s)? Server projects are deleted from "
                        "the server.")
                         .arg(batchItems_.size());
      spec.confirmIcon = QStringLiteral("trash");
      spec.danger = true;
      if (!confirmModal(this, spec)) return;
      scatterRows(checked_);   // they come apart on the way out — bar and rows together
      // A checked row the current filter HIDES has no dust to leave with, so scatterRows
      // never saw it; batchItems_ is already captured, and the whole selection is going.
      checked_.clear();
      updateBatchBar();
      emit removeRequested(batchItems_);
      return;
    }
    action_ = act;
    accept();
  }

  // Hide rows the storage filter excludes. Placeholders (no project id) always show.
  void ProjectsDialog::rebuildFilterOptions() {
    if (!filter_) return;
    const QString prev = filter_->currentData().toString();  // preserve the selection
    filter_->blockSignals(true);
    filter_->clear();
    filter_->addItem(tr("All"), "all");
    filter_->addItem(tr("Local"), "local");
    const QStringList urls = connections_ ? connections_->urls() : QStringList();
    if (!urls.isEmpty()) {
      filter_->addItem(tr("All servers"), "server");
      for (const QString& u : urls) filter_->addItem(u, u);  // one entry per specific server URL
    }
    knownServerUrls_ = urls;
    const int idx = filter_->findData(prev.isEmpty() ? QStringLiteral("all") : prev);
    filter_->setCurrentIndex(idx < 0 ? 0 : idx);
    filter_->blockSignals(false);
  }

  // The filter/sort/search transition: an excluded row fades and collapses its slot,
  // an included one plays that backwards (support/filterFade). Deliberately lighter and
  // quicker than the removal scatter — filtered out is not deleted.
  ListFilterFade* ProjectsDialog::filterFade() {
    if (filterFade_ || !list_) return filterFade_;
    filterFade_ = new ListFilterFade(list_);
    // setData fires itemChanged; the check-state bookkeeping must ignore our frames.
    filterFade_->beforeFrame = [this] { building_ = true; };
    filterFade_->afterFrame = [this] {
      building_ = false;
      list_->viewport()->update();   // the delegate paints the fade; setData relayouts
    };
    // …and each row that is LEFT arrives out of sand as well as a fade — the shared
    // ListFilterFade::dustRowIn (browser js/ui/motion.js filterDust).
    filterFade_->onArrive = [this](QListWidgetItem* it) { filterFade_->dustRowIn(it, window()); };
    return filterFade_;
  }

  void ProjectsDialog::applyFilter() {
    if (!filter_) return;
    const QString mode = filter_->currentData().toString();
    const QString needle = search_ ? search_->text().trimmed() : QString();
    const QString smode = searchModeCombo_ ? searchModeCombo_->currentData().toString()
                                           : QStringLiteral("common");
    auto wanted = [&](QListWidgetItem* it) {
      if (it->data(Qt::UserRole).isNull()) return true;  // "Loading…"/"No projects" placeholders
      const QString srv = it->data(Qt::UserRole + 1).toString();
      const bool remote = !srv.isEmpty();
      bool show = true;
      if (mode == "local") show = !remote;
      else if (mode == "server") show = remote;          // any server
      else if (mode != "all") show = (srv == mode);      // a specific server URL
      if (show && !needle.isEmpty()) {                   // name / keyword search (case-insensitive)
        const QString name = it->data(Qt::UserRole + 3).toString();
        const QString kw = it->data(Qt::UserRole + 5).toString();
        const bool nameHit = name.contains(needle, Qt::CaseInsensitive);
        const bool kwHit = kw.contains(needle, Qt::CaseInsensitive);
        show = smode == "names" ? nameHit : smode == "keywords" ? kwHit : (nameHit || kwHit);
      }
      return show;
    };
    if (auto* fade = filterFade()) fade->apply(wanted);
    updateBatchBar();   // the filtered view IS the select-all pool (and the bar's reason to show)
  }

  // Point the delegate at the row whose "⋯" is under the cursor, and sweep the app's own
  // glass shimmer across the chip as the pointer arrives — the same 325ms InOutSine band
  // every other control plays (support/shimmerOverlay.hpp), once per entry.
  void ProjectsDialog::setKebabHover(int row) {
    if (row == kebabHoverRow_) return;
    kebabHoverRow_ = row;
    auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
    if (del) del->setKebabHover(row);
    list_->viewport()->update();
    if (row < 0) { if (kebabSweep_) kebabSweep_->cancel(); return; }
    if (!kebabSweep_)
      kebabSweep_ = new gui::ShimmerOverlay(nullptr, list_, /*externalBands=*/true);
    if (QListWidgetItem* it = list_->item(row))
      kebabSweep_->sweepBand(del ? del->kebabChipFor(list_->visualItemRect(it)) : QRect());
  }

  void ProjectsDialog::showRowMenu(QListWidgetItem* it, const QPoint& globalPos) {
    if (!it || it->data(Qt::UserRole).isNull()) return;
    const bool remote = !it->data(Qt::UserRole + 1).toString().isEmpty();
    const QColor ico = palette().color(QPalette::WindowText);
    const bool haveServers = connections_ && !connections_->urls().isEmpty();
    QMenu menu(this);

    // Where a window raised from this menu flies back to: it grows out of the picked row,
    // but the menu is gone by close time, so the motes pour into the row's "⋯" chip.
    // Browser twin: projectsModal.js passes `menuBtn` the same way.
    const QRect kebabGlobal = [this, it]() -> QRect {
      auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
      if (!del) return {};
      const QRect chip = del->kebabChipFor(list_->visualItemRect(it));
      return chip.isValid() ? QRect(list_->viewport()->mapToGlobal(chip.topLeft()), chip.size())
                            : QRect();
    }();

    // "Add description" — edit the row's free-text description inline (no accept()/close),
    // mirroring the colour edit. An empty value clears.
    auto editDescription = [this, it, kebabGlobal] {
      const QString id = it->data(Qt::UserRole).toString();
      const QString server = it->data(Qt::UserRole + 1).toString();
      QString current;
      if (server.isEmpty()) {
        for (const auto& p : projects_)
          if (QString::fromStdString(p.meta.id) == id) {
            current = QString::fromStdString(p.meta.description);
            break;
          }
      } else {
        for (const auto& sp : remote_)
          if (sp.id == id && sp.serverUrl == server) { current = sp.description; break; }
      }
      PromptSpec spec;
      spec.title = tr("Project description");
      spec.titleIcon = QStringLiteral("info");
      spec.message = tr("Description:");
      spec.defaultValue = current;
      spec.multiline = true;              // a description is a sentence, not a word
      spec.maxChars = 2000;               // soft cap (UI only; core does no validation)
      spec.flight.closeRect = kebabGlobal;
      const auto entered = promptModal(this, spec);
      if (!entered) return;
      const QString text = *entered;
      if (text == current) return;        // nothing changed
      commitRowEdit(
          id, server,
          [text](Project& p) { p.meta.description = text.toStdString(); },
          [id, text](stencil::net::ServerClient* c, qint64 version,
                     std::function<void(bool, qint64)> done) {
            c->updateProjectDescriptionAsync(
                id, text, version,
                [done](bool ok2, qint64 v, bool) { done(ok2, v); });
          },
          [text](stencil::net::ServerProject& sp) { sp.description = text; });
    };

    // "Add keywords" — the row's search keywords, comma/space separated, normalized to
    // lowercase unique words the way the browser store's setKeywords does. Empty clears.
    auto editKeywords = [this, it, kebabGlobal] {
      const QString id = it->data(Qt::UserRole).toString();
      const QString server = it->data(Qt::UserRole + 1).toString();
      QStringList current;
      if (server.isEmpty()) {
        for (const auto& p : projects_)
          if (QString::fromStdString(p.meta.id) == id) {
            for (const auto& k : p.meta.keywords) current << QString::fromStdString(k);
            break;
          }
      } else {
        for (const auto& sp : remote_)
          if (sp.id == id && sp.serverUrl == server) { current = sp.keywords; break; }
      }
      PromptSpec spec;
      spec.title = tr("Project keywords");
      spec.titleIcon = QStringLiteral("info");
      spec.message = tr("Keywords (comma or space separated):");
      spec.defaultValue = current.join(' ');
      spec.multiline = true;              // keywords are a list, not a word
      spec.flight.closeRect = kebabGlobal;
      const auto entered = promptModal(this, spec);
      if (!entered) return;
      QStringList next;
      for (const QString& raw : entered->split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts)) {
        const QString k = raw.toLower();
        if (!next.contains(k)) next << k;
      }
      if (next == current) return;          // nothing changed
      commitRowEdit(
          id, server,
          [next](Project& p) {
            p.meta.keywords.clear();
            for (const QString& k : next) p.meta.keywords.push_back(k.toStdString());
          },
          [id, next](stencil::net::ServerClient* c, qint64 version,
                     std::function<void(bool, qint64)> done) {
            c->updateProjectKeywordsAsync(
                id, next, version,
                [done](bool ok2, qint64 v, bool) { done(ok2, v); });
          },
          [next](stencil::net::ServerProject& sp) { sp.keywords = next; });
    };

    // "Set expiration" — the browser-styled expiration editor, opened OVER this window
    // (browser parity: ui/base.js `stacked`). Closing the list to edit one of its rows
    // lost the user their place, so the owner is signalled and calls setProjects().
    auto editExpiration = [this, it, kebabGlobal] {
      if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;   // local only
      const QString id = it->data(Qt::UserRole).toString();
      const core::ProjectMeta* meta = nullptr;
      for (const auto& p : projects_)
        if (QString::fromStdString(p.meta.id) == id) { meta = &p.meta; break; }
      if (!meta) return;
      ExpirationDialog exp(QString::fromStdString(meta->name), meta->expiresAt,
                           QString::fromStdString(meta->refreshPeriod), meta->autoRefresh,
                           now_, this);
      support::revealDialog(exp, nullptr, support::gestureAnchorRect(), kebabGlobal);
      if (exp.exec() != QDialog::Accepted) return;
      emit expirationRequested(id, exp.expiresAtMs(), exp.refreshPeriod(), exp.autoRefresh());
    };

    // "Clear color" only when the row HAS a custom colour (browser modal parity): with
    // none set there is nothing to clear, and "Set color" already clicks straight into
    // the picker.
    const bool hasColor = !rowColor(it).isEmpty();
    QAction* removeAct = nullptr;   // marked as the danger row once the sheet is on (below)
    QColor dangerColor;
    // Same actions, order and flat shape as the browser modal's overflow menu, which
    // groups by order alone and draws no rules (slots act on the current row).
    if (remote) {
      menu.addAction(themedIcon("folder", ico, 16), "Open from server", this,
                     &ProjectsDialog::openSelected);
      if (openInServerOk_)
        menu.addAction(themedIcon("monitor", ico, 16), "Open in another app", this,
                       [this, it, kebabGlobal] {
                         emit openInRequested(it->data(Qt::UserRole).toString(),
                                              it->data(Qt::UserRole + 1).toString(), kebabGlobal);
                       });
      menu.addAction(themedIcon("copy", ico, 16), "Copy to local", this,
                     &ProjectsDialog::makeLocalCopySelected);
      menu.addAction(themedIcon("download", ico, 16), "Move to local", this,
                     &ProjectsDialog::moveToLocalSelected);
      menu.addAction(themedIcon("palette", ico, 16), "Set color", this,
                     &ProjectsDialog::setColorSelected);
      if (hasColor)
        menu.addAction(themedIcon("x", ico, 16), "Clear color", this,
                       &ProjectsDialog::clearColorSelected);
      menu.addAction(themedIcon("flag", ico, 16), "Add keywords", this, editKeywords);
      menu.addAction(themedIcon("file-text", ico, 16), "Add description", this, editDescription);
    } else {
      menu.addAction(themedIcon("folder", ico, 16), "Open", this,
                     &ProjectsDialog::openSelected);
      menu.addAction(themedIcon("external", ico, 16), "Open in new window", this,
                     &ProjectsDialog::openSelectedInNewWindow);
      if (openInLocalOk_)
        menu.addAction(themedIcon("monitor", ico, 16), "Open in another app", this,
                       [this, it, kebabGlobal] {
                         emit openInRequested(it->data(Qt::UserRole).toString(), QString(),
                                              kebabGlobal);
                       });
      menu.addAction(themedIcon("pencil", ico, 16), "Rename", this,
                     [this] { beginInlineRename(list_->currentItem()); });
      menu.addAction(themedIcon("palette", ico, 16), "Set color", this,
                     &ProjectsDialog::setColorSelected);
      if (hasColor)
        menu.addAction(themedIcon("x", ico, 16), "Clear color", this,
                       &ProjectsDialog::clearColorSelected);
      menu.addAction(themedIcon("flag", ico, 16), "Add keywords", this, editKeywords);
      menu.addAction(themedIcon("file-text", ico, 16), "Add description", this, editDescription);
      menu.addAction(themedIcon("calendar", ico, 16), "Set expiration", this, editExpiration);
      if (haveServers) {
        menu.addAction(themedIcon("server", ico, 16), "Move to server", this,
                       &ProjectsDialog::moveToServerSelected);
        menu.addAction(themedIcon("copy", ico, 16), "Copy to server", this,
                       &ProjectsDialog::copyToServerSelected);
      }
      // Destructive: the browser's "Remove" row colours both halves in --danger
      // (.project-menu-item.is-danger), so the glyph takes the theme's red and
      // support/menuDangerRow.hpp inks the label to match.
      dangerColor = themePalette(palette().color(QPalette::Window).lightness() < 128).danger;
      removeAct = menu.addAction(themedIcon("trash", dangerColor, 16), "Remove", this,
                                 &ProjectsDialog::deleteSelected);
    }
    // The same glass shimmer every other ctx row's hover sweeps (browser
    // .project-menu-item parity; mainWindow's canvas context menu already plays it).
    support::MenuShimmer shimmer(&menu);
    // …and the rest of the treatment every other menu gets (browser projectsModal.js
    // showMenu): a compact icon+label popup fitted to its own longest label rather than
    // carrying the menu bar's wide paddings, growing out of the click and pouring back.
    gui::compactIconMenu(menu);
    // After compactIconMenu — it replaces the menu's stylesheet, and this appends to it.
    support::markDangerRow(menu, removeAct, dangerColor);
    support::revealMenu(menu, globalPos);   // grow-from-the-cursor pop
    // Visible to the slots this menu fires (deleteSelected, the open confirm) for exactly
    // as long as the popup lives — they capture it and fly their answer back into the chip.
    menuKebabRect_ = kebabGlobal;
    menu.exec(globalPos);
    menuKebabRect_ = QRect();
  }

  void ProjectsDialog::scheduleRowOpen(QListWidgetItem* it) {
    if (rowDragging_ || pressOnCheck_ || !it || it->data(Qt::UserRole).isNull()) return;
    pendingRow_ = list_->row(it);
    pendingNewWindow_ = isNewWindowMod(pressMods_);
    if (!clickTimer_) {
      clickTimer_ = new QTimer(this);
      clickTimer_->setSingleShot(true);
      connect(clickTimer_, &QTimer::timeout, this, &ProjectsDialog::fireRowOpen);
    }
    // Wait out the platform's double-click window before acting.
    clickTimer_->start(QApplication::doubleClickInterval());
  }

  void ProjectsDialog::fireRowOpen() {
    // The double click that cancelled us may already have accepted the dialog
    // (the second release re-emits itemClicked, re-arming this timer).
    if (!isVisible()) return;
    openRow(list_->item(pendingRow_), pendingNewWindow_, /*confirm=*/true);
  }

  void ProjectsDialog::openRow(QListWidgetItem* it, bool newWindow, bool confirm) {
    if (!it || it->data(Qt::UserRole).isNull()) return;
    list_->setCurrentItem(it);
    confirmOpen_ = confirm;
    // Remote rows have no new-window path (same rule as the drag-out zones and
    // the ⋯ menu) — open them here instead of doing nothing.
    const bool remote = !it->data(Qt::UserRole + 1).toString().isEmpty();
    if (newWindow && !remote) openSelectedInNewWindow();
    else openSelected();
  }

  void ProjectsDialog::openSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    selectedId_ = it->data(Qt::UserRole).toString();
    const QString server = it->data(Qt::UserRole + 1).toString();
    if (!server.isEmpty()) {  // golden remote row → fetch + open from the server
      selectedServerUrl_ = server;
      finishOpen(Action::OpenRemote, /*newWindow=*/false,
                 it->data(Qt::UserRole + 3).toString());
      return;
    }
    finishOpen(Action::Open, /*newWindow=*/false, it->data(Qt::UserRole + 3).toString());
  }

  void ProjectsDialog::openSelectedInNewWindow() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    // New-window / delete / rename / renew apply to LOCAL projects only.
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;
    selectedId_ = it->data(Qt::UserRole).toString();
    finishOpen(Action::OpenInNewWindow, /*newWindow=*/true,
               it->data(Qt::UserRole + 3).toString());
  }

  // The open-confirm sits OVER the still-open dialog (browser parity: the projects
  // modal stays under the question, Cancel returns to the list — it used to close
  // first and ask after, orphaning a cancelled open). Deferred a turn so a drag
  // release / menu click in the same turn can't dismiss the question (the
  // deleteSelected pattern). A double click (confirmOpen_ false) skips it.
  void ProjectsDialog::finishOpen(Action act, bool newWindow, const QString& name) {
    if (!confirmOpen_) {
      action_ = act;
      accept();
      return;
    }
    const QString nm = support::shortName(name.isEmpty() ? tr("Untitled") : name);
    QPointer<ProjectsDialog> self(this);
    QTimer::singleShot(0, this, [this, self, act, newWindow, nm, closeTo = menuKebabRect_] {
      if (!self) return;
      ConfirmSpec spec;
      spec.flight.closeRect = closeTo;
      spec.title = tr("Open project");
      spec.message = newWindow
          ? tr("Open \"%1\" in a new window?").arg(nm)
          : tr("Open \"%1\"? Any unsaved changes in the current window will be replaced.").arg(nm);
      spec.confirmLabel = tr("Open");
      spec.confirmIcon = QStringLiteral("folder");
      if (!confirmModal(this, spec)) return;   // cancelled — the list stays up
      if (!self) return;
      confirmOpen_ = false;   // answered here — the owner must not ask again
      action_ = act;
      accept();
    });
  }

  void ProjectsDialog::deleteSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local only
    const QString id = it->data(Qt::UserRole).toString();
    const QString nm = support::shortName(it->data(Qt::UserRole + 3).toString());
    // Confirm on the NEXT turn, over the still-open dialog: the drag-out Remove zone
    // lands here from a drag release, which dismisses a box shown in the same turn.
    QTimer::singleShot(0, this, [this, id, nm, closeTo = menuKebabRect_] {
      ConfirmSpec spec;
      spec.flight.closeRect = closeTo;
      spec.title = tr("Remove project");
      spec.message = tr("Remove \"%1\"? This cannot be undone.").arg(nm);
      spec.confirmIcon = QStringLiteral("trash");
      spec.danger = true;
      if (!confirmModal(this, spec)) return;
      // Re-found by id — the confirm ran an event loop, so a re-list may have happened.
      QListWidgetItem* row = nullptr;
      for (int i = 0; i < list_->count() && !row; ++i) {
        QListWidgetItem* c = list_->item(i);
        if (c->data(Qt::UserRole).toString() == id &&
            c->data(Qt::UserRole + 1).toString().isEmpty())
          row = c;
      }
      if (row) {
        // The row scatters in place — a painted row has no widget of its own, so its
        // RECT is what comes apart (support/disintegrateOverlay.hpp). Clipped to the
        // viewport so a part-scrolled row can't overlay the dialog chrome.
        DisintegrateOverlay::overRect(
            list_->viewport(),
            list_->visualItemRect(row).intersected(list_->viewport()->rect()),
            this, DisintegrateOverlay::Sweep::Rows, /*dust=*/true,
            DisintegrateOverlay::kDustMaxCells, DisintegrateOverlay::kMs,
            list_->palette().color(QPalette::Text));   // lifted to the row's ink
        retireRow(row);  // blank the real row at once — the snapshot is what flies
        updateBatchBar();   // …and it leaves the checked set with its own dust, not after it
      }
      emit removeRequested({{id, QString()}});  // the owner removes, then setProjects()
    });
  }

  void ProjectsDialog::moveToServerSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local rows only
    if (!connections_ || connections_->urls().isEmpty()) return;
    // Which server (browser moveToServer): the picker shell, Move as the action.
    const QString nm = support::shortName(it->data(Qt::UserRole + 3).toString());
    const QString target = pickServer(
        this, connections_->urls(),
        tr("Move \"%1\" to which server? It becomes a server-backed project.").arg(nm),
        tr("Move to server"), tr("Move"), QStringLiteral("upload"));
    if (target.isEmpty()) return;
    selectedId_ = it->data(Qt::UserRole).toString();
    selectedServerUrl_ = target;
    action_ = Action::MoveToServer;
    accept();
  }

  void ProjectsDialog::copyToServerSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local rows only
    if (!connections_ || connections_->urls().isEmpty()) return;
    const QString nm = support::shortName(it->data(Qt::UserRole + 3).toString());
    const QString target =
        pickServer(this, connections_->urls(), tr("Copy \"%1\" to which server?").arg(nm));
    if (target.isEmpty()) return;
    const QString id = it->data(Qt::UserRole).toString();
    const auto cur = std::find_if(projects_.begin(), projects_.end(),
                                  [&](const Project& p) {
                                    return QString::fromStdString(p.meta.id) == id;
                                  });
    const QString base = cur != projects_.end() ? QString::fromStdString(cur->meta.name)
                                                : QStringLiteral("Untitled");
    PromptSpec spec;
    spec.title = tr("Copy to server");
    spec.message = tr("Name for the server copy:");
    spec.confirmLabel = tr("Copy");
    spec.confirmIcon = QStringLiteral("copy");
    spec.defaultValue = base + "-copy";
    const auto name = promptModal(this, spec);
    if (!name || name->isEmpty()) return;
    selectedId_ = id;
    selectedServerUrl_ = target;
    newName_ = *name;
    action_ = Action::CopyToServer;
    accept();
  }

  void ProjectsDialog::moveToLocalSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    const QString server = it->data(Qt::UserRole + 1).toString();
    if (server.isEmpty()) return;  // server (golden) rows only
    selectedId_ = it->data(Qt::UserRole).toString();
    selectedServerUrl_ = server;
    action_ = Action::MoveToLocal;
    accept();
  }

  void ProjectsDialog::makeLocalCopySelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    const QString server = it->data(Qt::UserRole + 1).toString();
    if (server.isEmpty()) return;  // server (golden) rows only
    const QString id = it->data(Qt::UserRole).toString();
    QString base = QStringLiteral("Untitled");
    for (const auto& sp : remote_)
      if (sp.id == id && sp.serverUrl == server) { base = sp.name.isEmpty() ? base : sp.name; break; }
    PromptSpec spec;
    spec.title = tr("Copy to local");
    spec.message = tr("Name for the local copy:");
    spec.confirmLabel = tr("Copy");
    spec.confirmIcon = QStringLiteral("copy");
    spec.defaultValue = base + "-copy";
    const auto name = promptModal(this, spec);
    if (!name || name->isEmpty()) return;
    selectedId_ = id;
    selectedServerUrl_ = server;
    newName_ = *name;
    action_ = Action::MakeLocalCopy;
    accept();
  }

  // Inline rename over the row's painted name (browser projectsModal beginRename
  // parity): a live-validated input with ✓/✗, Enter saves, Esc / click-away discards.
  // Commit emits renameRequested — the dialog STAYS OPEN and the owner repaints it
  // via setProjects(), like the remove/clear-all flows.
  void ProjectsDialog::beginInlineRename(QListWidgetItem* it) {
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local rows only
    if (it->data(kDoomedRole).toBool()) return;
    closeInlineRename();
    if (clickTimer_) clickTimer_->stop();   // a rename is not an open
    const QString id = it->data(Qt::UserRole).toString();
    const QString current = it->data(Qt::UserRole + 3).toString();

    // The same uniqueness/length rules the browser's inline editor applies.
    auto store = loadedNameStore(projects_);

    const QRect vr = list_->visualItemRect(it);
    auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
    QRect nr = del ? del->nameRectFor(list_->row(it)) : QRect();
    if (nr.isEmpty()) nr = QRect(vr.left() + 80, vr.top() + 8, 200, 18);

    renameBox_ = new QWidget(list_->viewport());
    renameBox_->setObjectName("projectsRenameBox");
    auto* lay = new QHBoxLayout(renameBox_);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    auto* edit = new QLineEdit(current, renameBox_);
    edit->setObjectName("projectsRenameEdit");
    edit->setToolTip(tr("Project name"));
    lay->addWidget(edit, 1);
    // ✓/✗ are the browser's .name-edit-btn chips (green check / red cross); the shared
    // objectName carries their QSS. themedIcon feeds the app-wide iconMotion filter, so
    // hovering DRAWS the check / strikes the cross exactly like the browser's icons.
    auto* okBtn = new QToolButton(renameBox_);
    okBtn->setObjectName("projectsRenameBtn");
    okBtn->setIcon(themedIcon("check", QColor("#22c55e"), 14));
    okBtn->setIconSize(QSize(14, 14));
    okBtn->setFocusPolicy(Qt::NoFocus);
    lay->addWidget(okBtn);   // its cursor follows enabled/disabled — see makeNameValidator
    auto* cancelBtn = new QToolButton(renameBox_);
    cancelBtn->setObjectName("projectsRenameBtn");
    cancelBtn->setIcon(themedIcon("x", QColor("#ef4444"), 14));
    cancelBtn->setIconSize(QSize(14, 14));
    cancelBtn->setFocusPolicy(Qt::NoFocus);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    lay->addWidget(cancelBtn);

    // Span from the name's left edge to just short of the "⋯" strip.
    const int left = nr.left() - 4;
    const int width = std::max(140, kebabZone(vr).left() - 8 - left);
    const int h = std::max(26, nr.height() + 8);
    renameBox_->setGeometry(left, nr.center().y() - h / 2, width, h);
    // The ✓/✗ FORM from dust once the editor is up (browser markIn parity): hidden
    // before show, then revealed — their slots open under the gathering motes.
    okBtn->hide();
    cancelBtn->hide();
    renameBox_->show();
    revealControls(okBtn, true);
    revealControls(cancelBtn, true);
    edit->setFocus();
    edit->selectAll();
    edit->installEventFilter(this);   // Esc cancels, focus loss discards (see eventFilter)

    // The shared ✓-enable/tooltip validation (no rest-state tooltip on the chips —
    // user decision, browser look).
    const auto revalidate = makeNameValidator(store, edit, okBtn, id, current);
    connect(edit, &QLineEdit::textChanged, renameBox_, [revalidate](const QString&) { revalidate(); });
    revalidate();
    QPointer<ProjectsDialog> self(this);
    auto commit = [this, self, store, edit, id, current] {
      if (!self) return;
      const QString name = edit->text().trimmed();
      if (name == current) { closeInlineRename(); return; }
      if (!store->validateName(name.toStdString(), id.toStdString()).ok) return;
      closeInlineRename();
      emit renameRequested(id, name);   // the owner renames, then calls setProjects()
    };
    connect(okBtn, &QToolButton::clicked, renameBox_, commit);
    connect(cancelBtn, &QToolButton::clicked, renameBox_, [this, self] { if (self) closeInlineRename(); });
    connect(edit, &QLineEdit::returnPressed, renameBox_, commit);
  }

  void ProjectsDialog::closeInlineRename() {
    if (!renameBox_) return;
    QWidget* box = renameBox_;
    renameBox_ = nullptr;   // cleared FIRST — hiding fires the edit's FocusOut back into us
    // The ✓/✗ come apart as dust (browser markOut parity): the flight is a snapshot on
    // the dialog window, so the editor itself still goes away NOW.
    for (QToolButton* b : box->findChildren<QToolButton*>()) revealControls(b, false);
    box->hide();
    box->deleteLater();
  }

  void ProjectsDialog::scatterRows(const QSet<QString>& keys) {
    if (!list_) return;
    // A copy: retireRow prunes checked_ below, and the batch removal hands checked_ in as
    // `keys` — iterating a set the loop is emptying is a trap not worth leaving. (Implicit
    // sharing makes this free until one of them is written to.)
    const QSet<QString> want = keys;
    QList<QListWidgetItem*> doomed;
    QList<QRect> rects;   // the on-screen slice of each doomed row (scrolled-out rows: none)
    // Clip each row's rect to the viewport: a checked row scrolled out of view must not
    // drop its overlay onto the dialog chrome, nor spend the shared mote budget on
    // pixels nobody can see — only the visible slices animate.
    const QRect view = list_->viewport()->rect();
    for (int i = 0; i < list_->count(); ++i) {
      const QString key = rowKeyAt(i);
      if (key.isEmpty() || (!want.isEmpty() && !want.contains(key))) continue;
      QListWidgetItem* it = list_->item(i);
      if (!it || it->isHidden()) continue;
      doomed.append(it);
      const QRect r = list_->visualItemRect(it).intersected(view);
      if (r.width() >= 8 && r.height() >= 8) rects.append(r);
    }
    if (doomed.isEmpty()) return;
    // Every row scatters at once and all repaint each frame, so the mote budget is
    // SHARED — one row keeps the fine grain, a mass removal coarsens each (browser:
    // scatterGridFor). Only rows that actually animate share it.
    const int budget =
        std::max<int>(1, DisintegrateOverlay::kDustMaxCells / std::max(1, int(rects.size())));
    // Overlays FIRST (they snapshot the still-painted rows), then retire the lot.
    for (const QRect& r : rects)
      DisintegrateOverlay::overRect(list_->viewport(), r, this,
                                    DisintegrateOverlay::Sweep::Rows, /*dust=*/true, budget,
                                    DisintegrateOverlay::kItemMs,   // a card is read, not glanced at
                                    list_->palette().color(QPalette::Text));
    for (QListWidgetItem* it : doomed)
      retireRow(it);   // blank the real row at once; its slot outlives the dust
    // …and the bar answers NOW, beside the rows' dust, not after it: retireRow has already
    // dropped these rows from the checked set and taken their flags, so the count, the
    // buttons and Select all come apart in the SAME turn the rows do (user report: the
    // items went, and the buttons went a flight later). Connections dialog parity.
    updateBatchBar();
  }

  // Closing mid-scatter: the close flight re-photographs the dialog as it hides
  // (modalReveal), so doomed rows leave NOW and their overlays stop — otherwise the
  // motes redraw the removed rows in the shrinking ghost.
  void ProjectsDialog::done(int result) {
    // A filter fade settles NOW too — nothing half-faded survives into the close flight.
    if (filterFade_) filterFade_->finishNow();
    hideHoverPreview();   // the doomed-row sweep below may delete whatever it points to
    closeInlineRename();
    for (int i = list_->count() - 1; i >= 0; --i)
      if (list_->item(i)->data(kDoomedRole).toBool()) delete list_->takeItem(i);
    stopDustClouds(this);   // …the bar's controls' own clouds with them
    QDialog::done(result);
  }

  // The scatter animates a SNAPSHOT — blank the real row the moment it starts,
  // hold the empty slot while the dust falls, then let the item go (browser
  // parity: leaveThenRemove + beginRemoval in projectsModal.js).
  void ProjectsDialog::retireRow(QListWidgetItem* it) {
    if (!it || it->data(Qt::UserRole).isNull()) return;
    const QString key = rowKeyAt(list_->row(it));
    it->setData(kDoomedRole, true);   // the delegate paints nothing for it
    it->setFlags(Qt::NoItemFlags);    // no select/check mid-flight
    checked_.remove(key);             // …and it stops counting towards the selection bar
    QTimer::singleShot(DisintegrateOverlay::kItemMs, this, [this, key] {
      // Re-found by key: a re-list may have rebuilt the rows (fresh ones aren't doomed).
      for (int i = 0; i < list_->count(); ++i)
        if (rowKeyAt(i) == key && list_->item(i)->data(kDoomedRole).toBool()) {
          // The hover preview may still be pointing at the very row about to go.
          if (list_->item(i) == hoverItem_) hideHoverPreview();
          delete list_->takeItem(i);
          break;
        }
    });
  }

  QString ProjectsDialog::rowKeyAt(int i) const {
    QListWidgetItem* it = list_->item(i);
    if (!it || it->data(Qt::UserRole).isNull()) return {};  // placeholder / non-data row
    return it->data(Qt::UserRole + 1).toString() + "|" + it->data(Qt::UserRole).toString();
  }

  QString ProjectsDialog::rowColor(const QListWidgetItem* it) const {
    if (!it || it->data(Qt::UserRole).isNull()) return {};
    const QString id = it->data(Qt::UserRole).toString();
    const QString server = it->data(Qt::UserRole + 1).toString();
    if (!server.isEmpty()) {  // server row → read the cached record
      for (const auto& sp : remote_)
        if (sp.id == id && sp.serverUrl == server) return sp.color;
      return {};
    }
    for (const auto& p : projects_)  // local row → read the project meta
      if (QString::fromStdString(p.meta.id) == id) return QString::fromStdString(p.meta.color);
    return {};
  }

  QString ProjectsDialog::currentRowColor() const { return rowColor(list_->currentItem()); }

  // Resolve the selected row's (id, serverUrl) and emit a SetColor action with `color`
  // ("" = clear to the theme default). Shared by the set / clear colour menu entries.
  void ProjectsDialog::emitSetColor(QListWidgetItem* it, const QString& color) {
    selectedId_ = it->data(Qt::UserRole).toString();
    selectedServerUrl_ = it->data(Qt::UserRole + 1).toString();
    selectedColor_ = color;
    action_ = Action::SetColor;
    accept();
  }

  void ProjectsDialog::setColorSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    const QString cur = currentRowColor();
    const QColor seed = (!cur.isEmpty() && QColor(cur).isValid()) ? QColor(cur)
                                                                  : QColor("#7c3aed");
    // Raised from the row's ⋯ menu, so it flies like every other window that menu opens:
    // out of the pressed menu row, back into the ⋯ chip. Rows are delegate-painted, so
    // both ends are global rects; off the menu it falls back to the row's own strip.
    const QRect rowRect(list_->viewport()->mapToGlobal(list_->visualItemRect(it).topLeft()),
                        list_->visualItemRect(it).size());
    const QRect from = menuKebabRect_.isValid() ? support::gestureAnchorRect() : rowRect;
    const QColor picked =
        support::pickColorAnimated(seed, this, "Project name color", nullptr, from,
                                   {}, false, menuKebabRect_);
    if (!picked.isValid()) return;   // cancelled
    emitSetColor(it, picked.name());
  }

  void ProjectsDialog::clearColorSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    emitSetColor(it, QString());   // clear → theme default
  }

  void ProjectsDialog::createBlank() {
    action_ = Action::NewBlank;
    accept();
  }

  void ProjectsDialog::createNew() {
    const QString seed = QString::fromStdString(loadedNameStore(projects_)->defaultName());
    const auto name = promptValidatedName(this, "New Project", seed, QString(), projects_);
    if (!name) return;
    newName_ = *name;
    action_ = Action::New;
    accept();
  }

}
