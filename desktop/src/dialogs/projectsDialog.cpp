#include "../support/searchCombo.hpp"
#include "projectsDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "projectDragZones.hpp"
#include "projectsStore.hpp"
#include "reorderableListWidget.hpp"
#include "../app/scrollReveal.hpp"  // revealOpacityForItem (scroll edge fade)
#include "../support/disintegrateOverlay.hpp"  // deleted rows come apart
#include "../support/displayName.hpp"          // shortName for the remove confirm
#include "../support/dissolveEffect.hpp"      // scroll-edge grain dissolve
#include "../support/filterFade.hpp"          // filtered-out rows fade + collapse
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
#include <QMessageBox>
#include <QColor>
#include <QFont>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QPolygonF>
#include <QPushButton>
#include <QScreen>
#include <QSize>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <limits>
#include <QVariant>
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
    constexpr int kHoverPreviewPx = 267;
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

    // Modal name prompt with live validation (mirrors the browser's inline rename):
    // OK enabled only when the trimmed name is non-empty, ≤80 chars, and unique
    // (excluding `exceptId`); its tooltip shows the reason. nullopt on cancel.
    std::optional<QString> promptValidatedName(QWidget* parent, const QString& title,
                                               const QString& initial,
                                               const QString& exceptId,
                                               const std::vector<Project>& projects) {
      core::ProjectsStore store;
      std::vector<core::ProjectMeta> metas;
      for (const auto& p : projects) metas.push_back(p.meta);
      store.load(metas);

      QDialog d(parent);
      d.setWindowTitle(title);
      auto* lay = new QVBoxLayout(&d);
      lay->addWidget(new QLabel("Project name:", &d));
      auto* edit = new QLineEdit(initial, &d);
      edit->selectAll();
      lay->addWidget(edit);
      auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
      auto* okBtn = box->button(QDialogButtonBox::Ok);
      okBtn->setText(QString::fromUtf8("✓ Save"));
      box->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("✗ Cancel"));
      lay->addWidget(box);

      auto revalidate = [&]() {
        const auto res =
            store.validateName(edit->text().trimmed().toStdString(), exceptId.toStdString());
        okBtn->setEnabled(res.ok);
        okBtn->setToolTip(res.ok ? QStringLiteral("Save name")
                                 : QString::fromStdString(res.reason));
      };
      QObject::connect(edit, &QLineEdit::textChanged, &d, [&](const QString&) { revalidate(); });
      QObject::connect(box, &QDialogButtonBox::accepted, &d, &QDialog::accept);
      QObject::connect(box, &QDialogButtonBox::rejected, &d, &QDialog::reject);
      revalidate();
      if (d.exec() != QDialog::Accepted) return std::nullopt;
      return edit->text().trimmed();
    }

    // The "⋯" kebab strip — shared by the delegate (paint) and eventFilter (hit-test).
    QRect kebabZone(const QRect& rowRect) {
      const int w = 30;
      return QRect(rowRect.right() - w, rowRect.top(), w, rowRect.height());
    }

    constexpr int kBadgeGap = 8;   // between the origin badge and the kebab strip

    // UserRole+7: "doomed" — this row's removal scatter is playing. The delegate
    // paints NOTHING for it (the overlay animates a snapshot; the row must not keep
    // painting underneath); the slot stays until retireRow drops the item.
    constexpr int kDoomedRole = Qt::UserRole + 7;

    // The origin badge's footprint, so the row text can stop before it (paintRow reserves
    // this, the badge paint below lays it out). Zero for placeholder rows, which have none.
    int originBadgeWidth(const QStyleOptionViewItem& o, const QModelIndex& idx) {
      if (idx.data(Qt::UserRole).isNull()) return 0;
      const QString server = idx.data(Qt::UserRole + 1).toString();
      const QString text = !server.isEmpty()
                               ? server
                               : (idx.data(Qt::UserRole + 6).toBool() ? QStringLiteral(".stencil")
                                                                      : QStringLiteral("this computer"));
      QFont bf(o.font);
      bf.setPointSizeF(std::max(8.0, o.font.pointSizeF() - 1.5));
      return 13 + 4 + QFontMetrics(bf).horizontalAdvance(text) + kBadgeGap;
    }

    // Paints a rounded golden outline around server (shared) rows — the desktop analogue
    // of the browser's `.project-remote` border — plus a vertical "⋯" kebab on every
    // real row (the browser modal's per-row "more actions" button).
    class ProjectRowDelegate : public QStyledItemDelegate {
     public:
      using QStyledItemDelegate::QStyledItemDelegate;
      // Cap the row width to the viewport so long "<name> — <url>" labels ELIDE instead of
      // forcing a horizontal scrollbar (which clipped the row at narrow window widths).
      QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        if (const auto* av = qobject_cast<const QAbstractItemView*>(opt.widget))
          s.setWidth(av->viewport()->width());
        // A row leaving the filtered set collapses its slot (support/filterFade), so the
        // rows below it close the gap instead of jumping once it disappears.
        s.setHeight(filterHeight(s.height(), filterPresenceOf(idx)));
        return s;
      }
      // The whole row — background, text, badges — rides one painter opacity, so
      // nothing can fade out of step.
      void paint(QPainter* p, const QStyleOptionViewItem& opt,
                 const QModelIndex& idx) const override {
        if (idx.data(kDoomedRole).toBool()) return;   // scatter plays over the held-open slot
        // A filter fade rides one painter opacity over the whole row. Distinct from the
        // grain above on purpose: excluded is not deleted.
        const double fo = filterOpacity(filterPresenceOf(idx));
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

      void paintRow(QPainter* p, const QStyleOptionViewItem& opt,
                    const QModelIndex& idx) const {
        // Real rows: let the base paint the background/selection/icon but NOT the text, so we can
        // colour just the project NAME (status/custom colour) while the trailing "— <url>" /
        // metadata keeps the default text colour. Placeholder rows draw normally.
        QStyleOptionViewItem o(opt);
        initStyleOption(&o, idx);
        const QString full = o.text;
        const bool realRow = !idx.data(Qt::UserRole).isNull();
        QStyle* st = o.widget ? o.widget->style() : QApplication::style();
        if (!realRow) {
          QStyledItemDelegate::paint(p, o, idx);  // placeholder rows: default rendering
        } else {
          // Draw bg/selection/icon WITHOUT the text via the style directly. (Calling
          // QStyledItemDelegate::paint re-runs initStyleOption internally, re-adding the text we
          // cleared — which double-drew it as a ghosted overlay.) Then we paint the name+rest.
          QStyleOptionViewItem bg(o);
          bg.text.clear();
          st->drawControl(QStyle::CE_ItemViewItem, &bg, p, o.widget);
        }
        if (realRow && !full.isEmpty()) {
          // The exact text rect the base would use (after the icon), minus the kebab strip.
          // Room for the 30px kebab strip AND the origin badge painted below it.
          const QRect tr = st->subElementRect(QStyle::SE_ItemViewItemText, &o, o.widget)
                               .adjusted(kThumbTextGap, 0, -34 - originBadgeWidth(o, idx), 0);
          const QString name = idx.data(Qt::UserRole + 3).toString();
          const QString rest = (!name.isEmpty() && full.startsWith(name)) ? full.mid(name.size())
                                                                          : QString();
          const QString nameStr = name.isEmpty() ? full : name;
          const bool sel = o.state & QStyle::State_Selected;
          const QColor def = o.palette.color(sel ? QPalette::HighlightedText : QPalette::Text);
          QColor nameCol = idx.data(Qt::UserRole + 4).value<QColor>();
          if (sel || !nameCol.isValid()) nameCol = def;   // selection forces highlighted text
          const QFontMetrics fm(o.font);
          p->save();
          p->setFont(o.font);
          const QString nameDraw = fm.elidedText(nameStr, Qt::ElideRight, tr.width());
          p->setPen(nameCol);
          p->drawText(tr, Qt::AlignVCenter | Qt::TextSingleLine, nameDraw);
          const int nameW = fm.horizontalAdvance(nameDraw);
          if (!rest.isEmpty() && nameDraw == name && nameW < tr.width()) {
            const QRect rr = tr.adjusted(nameW, 0, 0, 0);
            p->setPen(def);
            p->drawText(rr, Qt::AlignVCenter | Qt::TextSingleLine,
                        fm.elidedText(rest, Qt::ElideRight, rr.width()));
          }
          p->restore();
        }
        // Origin outline: golden for a server (shared) row, bronze for a .stencil file-origin
        // row (mirrors the browser's `.project-remote` / `.project-file` borders).
        const bool remote = !idx.data(Qt::UserRole + 1).toString().isEmpty();
        const bool fileOrigin = idx.data(Qt::UserRole + 6).toBool();
        if (remote || fileOrigin) {
          p->save();
          p->setRenderHint(QPainter::Antialiasing, true);
          p->setBrush(Qt::NoBrush);
          p->setPen(QPen(QColor(remote ? "#d4a017" : "#c1783c"), 2));
          p->drawRoundedRect(QRectF(opt.rect).adjusted(1, 1, -1, -1), 6, 6);
          p->restore();
        }
        // Origin badge: glyph + WORD, on the row's right — `server` (gold) with the
        // address, `file-text` (bronze) with ".stencil", or `monitor` (grey) with
        // "this computer". Port of the browser's inline origin badges (projectsModal.js).
        if (realRow) {
          const QString gname = remote ? QStringLiteral("server")
                                       : (fileOrigin ? QStringLiteral("file-text") : QStringLiteral("monitor"));
          const QColor gcol = remote ? QColor("#d4a017") : (fileOrigin ? QColor("#c1783c") : QColor("#9aa4b2"));
          // "browser" on the web; here the local store IS this computer, so it says so.
          const QString gtext = remote ? idx.data(Qt::UserRole + 1).toString()
                                       : (fileOrigin ? QStringLiteral(".stencil")
                                                     : QStringLiteral("this computer"));
          if (hasIcon(gname)) {
            const int gs = 13;
            QFont bf(o.font);
            bf.setPointSizeF(std::max(8.0, o.font.pointSizeF() - 1.5));
            const QFontMetrics bfm(bf);
            const QRect zone = kebabZone(opt.rect);
            const int textW = bfm.horizontalAdvance(gtext);
            const int badgeW = gs + 4 + textW;
            const int bx = zone.left() - kBadgeGap - badgeW;
            const int by = opt.rect.center().y();
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            themedIcon(gname, gcol, gs).paint(p, QRect(bx, by - gs / 2, gs, gs));
            p->setFont(bf);
            p->setPen(gcol);
            p->drawText(QRect(bx + gs + 4, opt.rect.top(), textW, opt.rect.height()),
                        Qt::AlignVCenter | Qt::TextSingleLine, gtext);
            p->restore();
          }
        }
        // Kebab dots only on real (selectable) rows — skip "No projects yet" /
        // "Loading…" placeholders, which carry no project id.
        if (idx.data(Qt::UserRole).isNull()) return;
        const QRect zone = kebabZone(opt.rect);
        const bool active = opt.state & (QStyle::State_Selected | QStyle::State_MouseOver);
        QColor dot = opt.palette.color(active ? QPalette::HighlightedText
                                              : QPalette::PlaceholderText);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setPen(Qt::NoPen);
        p->setBrush(dot);
        const int cx = zone.center().x();
        const int cy = zone.center().y();
        for (int dy = -6; dy <= 6; dy += 6)
          p->drawEllipse(QPointF(cx, cy + dy), 1.6, 1.6);
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
                                 core::UnitFormat unit,
                                 QWidget* parent)
      : QDialog(parent), projects_(projects), now_(now), unit_(unit),
        connections_(connections), thumbs_(thumbs) {
    setWindowTitle("Projects");
    // Wide enough that the "<name> — <url>" labels fit without eliding early.
    setMinimumSize(760, 320);
    resize(900, 560);

    // Most-recently-updated first, matching the browser store ordering.
    std::sort(projects_.begin(), projects_.end(),
              [](const Project& a, const Project& b) {
                return a.meta.updatedAt > b.meta.updatedAt;
              });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("<b>Saved projects</b>", this));

    // Filter row: a compact "Show:" dropdown (All / Local / all-servers / a specific connected
    // server) + a name search box that takes the remaining width (mirrors the browser modal).
    {
      auto* frow = new QHBoxLayout;
      frow->addWidget(new QLabel("Show:", this));
      filter_ = new SearchComboBox(this, /*searchable=*/false);
      filter_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
      filter_->setMaximumWidth(260);
      filter_->setToolTip("Filter the list: all, local only, or a specific server");
      rebuildFilterOptions();
      frow->addWidget(filter_);                       // natural width — no stretch
      frow->addSpacing(8);
      // Sort mode (mirrors the browser modal): the data role carries the mode key.
      frow->addWidget(new QLabel("Sort:", this));
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
      frow->addSpacing(8);
      // Search-mode (what the search box matches): name+keywords (default), names, keywords.
      searchModeCombo_ = new SearchComboBox(this, /*searchable=*/false);
      searchModeCombo_->setToolTip("What the search box matches");
      searchModeCombo_->addItem(tr("Name + keywords"), QStringLiteral("common"));
      searchModeCombo_->addItem(tr("Names only"), QStringLiteral("names"));
      searchModeCombo_->addItem(tr("Keywords only"), QStringLiteral("keywords"));
      frow->addWidget(searchModeCombo_);
      frow->addSpacing(8);
      search_ = new QLineEdit(this);
      search_->setPlaceholderText(tr("Search projects…"));
      search_->setClearButtonEnabled(true);
      search_->setToolTip("Filter projects by name (case-insensitive)");
      frow->addWidget(search_, 1);                    // the search field takes the remaining width
      // Select/deselect every row in the CURRENT filtered view (browser: `selectables`).
      selectAllBtn_ = new QPushButton(tr("Select all"), this);
      selectAllBtn_->setObjectName("projectsSelectAll");
      selectAllBtn_->setToolTip("Select every project in the current filtered view");
      frow->addWidget(selectAllBtn_);
      connect(selectAllBtn_, &QPushButton::clicked, this, &ProjectsDialog::toggleSelectAll);
      layout->addLayout(frow);
      connect(filter_, &QComboBox::currentIndexChanged, this, [this](int) { applyFilter(); });
      connect(sortCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        g_projectsSortMode = sortCombo_->currentData().toString();
        refresh();
      });
      connect(searchModeCombo_, &QComboBox::currentIndexChanged, this, [this](int) { applyFilter(); });
      connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { applyFilter(); });
    }

    // Batch-select toolbar — appears once one or more rows are checked. Direction buttons
    // show by selection homogeneity (all-local → To/Server-copy; all-server → To/Local-copy).
    {
      batchBar_ = new QWidget(this);
      auto* bh = new QHBoxLayout(batchBar_);
      bh->setContentsMargins(0, 0, 0, 0);
      batchCount_ = new QLabel("0 selected", this);
      bh->addWidget(batchCount_);
      bh->addStretch(1);
      // Browser batch-bar parity: accent-filled actions, danger-filled Remove.
      batchToServer_ = new QPushButton(QString::fromUtf8("⇧ To server"), this);
      batchToServer_->setToolTip("Move the checked local projects to a server");
      batchToServer_->setObjectName("primaryButton");
      batchCopyServer_ = new QPushButton(QString::fromUtf8("⧉ Server copy"), this);
      batchCopyServer_->setToolTip("Copy the checked local projects to a server");
      batchCopyServer_->setObjectName("primaryButton");
      batchToLocal_ = new QPushButton(QString::fromUtf8("⇩ To local"), this);
      batchToLocal_->setToolTip("Move the checked server projects to local storage");
      batchToLocal_->setObjectName("primaryButton");
      batchCopyLocal_ = new QPushButton(QString::fromUtf8("⧉ Local copy"), this);
      batchCopyLocal_->setToolTip("Copy the checked server projects to local storage");
      batchCopyLocal_->setObjectName("primaryButton");
      auto* batchRemove = new QPushButton("Remove selected", this);
      batchRemove->setToolTip("Remove the checked projects");
      batchRemove->setObjectName("dangerButton");
      auto* batchClear = new QPushButton("Clear", this);
      batchClear->setToolTip("Clear the current checkbox selection");
      batchClear->setObjectName("primaryButton");
      bh->addWidget(batchToServer_);
      bh->addWidget(batchCopyServer_);
      bh->addWidget(batchToLocal_);
      bh->addWidget(batchCopyLocal_);
      bh->addWidget(batchRemove);
      bh->addWidget(batchClear);
      batchBar_->setVisible(false);
      layout->addWidget(batchBar_);
      connect(batchToServer_, &QPushButton::clicked, this, [this] { runBatch(Action::BatchMoveToServer); });
      connect(batchCopyServer_, &QPushButton::clicked, this, [this] { runBatch(Action::BatchCopyToServer); });
      connect(batchToLocal_, &QPushButton::clicked, this, [this] { runBatch(Action::BatchMoveToLocal); });
      connect(batchCopyLocal_, &QPushButton::clicked, this, [this] { runBatch(Action::BatchCopyToLocal); });
      connect(batchRemove, &QPushButton::clicked, this, [this] { runBatch(Action::BatchRemove); });
      connect(batchClear, &QPushButton::clicked, this, [this] { checked_.clear(); refresh(); });
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
    list_->setSpacing(6);  // vertical gaps so rows read as separate cards
    // Rows fit the viewport (the delegate clamps their width + elides) — never scroll sideways.
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Golden outline (not fill) around shared rows + the per-row "⋯" kebab,
    // mirroring the browser modal.
    list_->setItemDelegate(new ProjectRowDelegate(list_));
    // Hover-magnify + kebab clicks: track moves over the viewport to pop a larger
    // preview, and catch left-clicks on the "⋯" zone (handled in eventFilter).
    list_->viewport()->setMouseTracking(true);
    list_->viewport()->installEventFilter(this);
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

    // Row-open gestures (see confirmRequested() in the header for the mapping).
    connect(list_, &QListWidget::itemClicked, this, &ProjectsDialog::scheduleRowOpen);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
      // THE crux: kill the pending single-click open before it can raise the
      // confirmation, so the dialog never flashes on the way to a double click.
      if (clickTimer_) clickTimer_->stop();
      if (pressOnCheck_) return;   // double-tapping the checkbox never opens
      openRow(it, isNewWindowMod(pressMods_), /*confirm=*/false);
    });
    // Return/Enter on the focused row opens it like a single click (confirms).
    // Consumed so it can't also trigger the dialog's default button.
    list_->installEventFilter(this);
    installEventFilter(this);   // own deactivation → hide the hover preview
    connect(list_, &QListWidget::itemChanged, this, &ProjectsDialog::onItemChanged);

    // Bottom row: only the create actions + Close. Per-row actions (Open / Rename / Renew /
    // To-server / copy / Delete …) live on the "⋯" kebab + right-click menu, and multi-row
    // actions on the batch toolbar above — so they're not duplicated here.
    auto* row = new QHBoxLayout;
    auto* newBtn = new QPushButton("New Project", this);
    newBtn->setToolTip("Create a new empty project from the current canvas");
    auto* blankBtn = new QPushButton("🖼 Blank Image", this);
    blankBtn->setToolTip("Create a blank image (white, black, or any color) to draw on");
    // "Clear All" only ever wipes local projects. When a server is connected, label it
    // "Clear All Local" so the button matches the actual (local-only) removal; the label is
    // kept in step reactively via ConnectionManager::changed() (see below).
    auto* clearAllBtn = new QPushButton("Clear All", this);
    clearAllBtn_ = clearAllBtn;
    clearAllBtn->setObjectName("dangerButton");  // red danger styling (mirrors the browser modal)
    clearAllBtn->setToolTip("Remove all local projects (server projects are not affected)");
    auto* closeBtn = new QPushButton("Close", this);
    closeBtn->setToolTip("Close this dialog");
    row->addWidget(newBtn);
    row->addWidget(blankBtn);
    row->addStretch(1);
    row->addWidget(clearAllBtn);
    row->addWidget(closeBtn);
    layout->addLayout(row);

    connect(newBtn, &QPushButton::clicked, this, &ProjectsDialog::createNew);
    connect(blankBtn, &QPushButton::clicked, this, &ProjectsDialog::createBlank);
    connect(clearAllBtn, &QPushButton::clicked, this, [this] {
      // Confirm HERE, parented to this dialog: the question then sits ON TOP of the still
      // open Projects window. Closing first and asking afterwards left the user answering
      // about a list they could no longer see.
      const int n = static_cast<int>(projects_.size());
      if (n == 0) return;
      if (QMessageBox::question(
              this, "Clear all projects",
              QString("Are you sure? This removes all %1 local project(s) and cannot be undone. "
                      "Server projects are not affected.")
                  .arg(n),
              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
      scatterRows();              // the whole list comes apart before it empties
      emit clearAllRequested();   // the owner removes them, then calls setProjects()
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

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

  bool ProjectsDialog::eventFilter(QObject* obj, QEvent* ev) {
    // The hover preview is a ToolTip window: switching window or app delivers the
    // list no Leave, and the popup would float on over whatever came to the front.
    if (hoverPreview_ && obj == this &&
        (ev->type() == QEvent::WindowDeactivate || ev->type() == QEvent::ApplicationDeactivate)) {
      hoverPreview_->hide();
    }
    // Alt pressed/released while the preview is up: re-scale it in place from the
    // source pixmap it carries (held = 2x, released = back to the glance size).
    if (hoverPreview_ && hoverPreview_->isVisible() &&
        (ev->type() == QEvent::KeyPress || ev->type() == QEvent::KeyRelease) &&
        static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Alt) {
      const QPixmap src = hoverPreview_->property("srcPixmap").value<QPixmap>();
      if (!src.isNull()) {
        const int edge = (ev->type() == QEvent::KeyPress) ? kHoverPreviewPx * 2 : kHoverPreviewPx;
        hoverPreview_->setPixmap(
            src.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        hoverPreview_->adjustSize();
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
        // that were down for the press that produces them.
        pressMods_ = me->modifiers();
        const QPoint vpos = me->position().toPoint();
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
      if (ev->type() == QEvent::MouseMove) {
        const QPoint vpos = static_cast<QMouseEvent*>(ev)->position().toPoint();
        QListWidgetItem* it = list_->itemAt(vpos);
        const QPixmap src = it ? it->data(Qt::UserRole + 2).value<QPixmap>() : QPixmap();
        // Magnify only while over the icon cell (the left edge of the row), so the
        // preview doesn't pop up across the whole row's text.
        bool overIcon = false;
        if (it && !src.isNull()) {
          const QRect vr = list_->visualItemRect(it);
          const QRect iconCell(vr.left(), vr.top(),
                               list_->iconSize().width() + 8, vr.height());
          overIcon = iconCell.contains(vpos);
        }
        if (overIcon) {
          if (!hoverPreview_) {
            hoverPreview_ = new QLabel(this, Qt::ToolTip);
            hoverPreview_->setStyleSheet(
                "QLabel{background:#1e1e1e;border:2px solid #d4a017;"
                "border-radius:8px;padding:4px;}");
          }
          // Alt HELD doubles the glance (chat HoverPreview / browser parity). The
          // SOURCE rides along so the Alt toggle below can re-scale without a move.
          const int edge =
              (QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier)
                  ? kHoverPreviewPx * 2
                  : kHoverPreviewPx;
          hoverPreview_->setProperty("srcPixmap", src);
          hoverPreview_->setPixmap(
              src.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation));
          hoverPreview_->adjustSize();
          // Down-right of the cursor, flipped/clamped to stay on-screen.
          const QPoint cur = static_cast<QMouseEvent*>(ev)->globalPosition().toPoint();
          QScreen* s = QGuiApplication::screenAt(cur);
          const QRect scr = (s ? s : QGuiApplication::primaryScreen())->availableGeometry();
          const QSize sz = hoverPreview_->size();
          QPoint gp = cur + QPoint(18, 18);
          if (gp.x() + sz.width() > scr.right()) gp.setX(cur.x() - 18 - sz.width());
          if (gp.y() + sz.height() > scr.bottom()) gp.setY(scr.bottom() - sz.height());
          if (gp.y() < scr.top()) gp.setY(scr.top());
          hoverPreview_->move(gp);
          hoverPreview_->show();
        } else if (hoverPreview_) {
          hoverPreview_->hide();
        }
      } else if (ev->type() == QEvent::Leave && hoverPreview_) {
        hoverPreview_->hide();
      }
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
    list_->clear();
    const core::ProjectsStore store;  // pure helpers only; reads meta, no state

    // Multi-line row tooltip, in order: image size + orientation, total drawn-line
    // length in the active unit (local rows only; `lineLenCm` 0 omits the row),
    // the description when set, and the origin note.
    auto rowTooltip = [&](int w, int h, double lineLenCm, const QString& description,
                          const QString& origin) {
      QStringList lines;
      if (w > 0 && h > 0)
        lines << QString("%1x%2 px   ·   %3").arg(w).arg(h).arg(
            h >= w ? QStringLiteral("portrait") : QStringLiteral("landscape"));
      if (lineLenCm > 0)
        lines << QString("Line: %1 %2")
                     .arg(lineLenCm * unit_.factor, 0, 'f', 1)
                     .arg(QString::fromStdString(unit_.label));
      if (!description.isEmpty()) lines << QString("Description: %1").arg(description);
      lines << origin;
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
      it->setToolTip(rowTooltip(pr.meta.imageW, pr.meta.imageH, pr.meta.lineLengthCm,
                                QString::fromStdString(pr.meta.description),
                                pr.meta.fromFile ? QStringLiteral("Opened from a .stencil project file")
                                                 : QStringLiteral("Stored on this computer")));
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
      it->setToolTip(rowTooltip(sp.imageW, sp.imageH, 0.0, sp.description,
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
    batchBar_->setVisible(n > 0);
    if (batchCount_) batchCount_->setText(tr("%1 selected").arg(n));
    const bool haveServers = connections_ && !connections_->urls().isEmpty();
    const auto dir = batchDirectionsFor(locals, remotes, haveServers);
    if (batchToServer_) batchToServer_->setVisible(dir.toServer);
    if (batchCopyServer_) batchCopyServer_->setVisible(dir.toServer);
    if (batchToLocal_) batchToLocal_->setVisible(dir.toLocal);
    if (batchCopyLocal_) batchCopyLocal_->setVisible(dir.toLocal);
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
    selectAllBtn_->setVisible(any);
    selectAllBtn_->setText(allFilteredChecked() ? tr("Deselect all") : tr("Select all"));
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
      const QStringList urls = connections_->urls();
      QString target = urls.first();
      if (urls.size() > 1) {
        bool ok = false;
        target = QInputDialog::getItem(this, tr("To server"),
                                       tr("Move/copy the selected projects to which server?"),
                                       urls, 0, false, &ok);
        if (!ok || target.isEmpty()) return;
      }
      selectedServerUrl_ = target;
    }
    if (act == Action::BatchRemove) {
      // Confirm HERE (like Clear All): the question sits over the still-open list, the
      // owner removes on removeRequested and repaints via setProjects() — no close.
      if (QMessageBox::question(
              this, "Remove projects",
              QString("Remove %1 selected project(s)? Server projects are deleted from "
                      "the server.")
                  .arg(batchItems_.size()),
              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
      scatterRows(checked_);   // they come apart on the way out
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
    updateSelectAll();   // the filtered view IS the select-all pool
  }

  void ProjectsDialog::showRowMenu(QListWidgetItem* it, const QPoint& globalPos) {
    if (!it || it->data(Qt::UserRole).isNull()) return;
    const bool remote = !it->data(Qt::UserRole + 1).toString().isEmpty();
    const QColor ico = palette().color(QPalette::WindowText);
    const bool haveServers = connections_ && !connections_->urls().isEmpty();
    QMenu menu(this);

    // "Description…" — edit the row's free-text description inline (no accept()/close), mirroring the
    // colour edit but persisting straight to the local registry (fileStore) or the server
    // (updateProjectDescriptionAsync), then refreshing the list + its tooltip. An empty value clears.
    auto editDescription = [this, it] {
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
      bool ok = false;
      QString text = QInputDialog::getMultiLineText(this, tr("Project description"),
                                                    tr("Description (leave empty to clear):"),
                                                    current, &ok);
      if (!ok) return;
      text = text.trimmed().left(2000);   // soft-cap ~2000 chars (UI only; core does no validation)
      if (text == current) return;        // nothing changed
      if (server.isEmpty()) {
        // Local row: update the registry copy + persist, then refresh the list/tooltip.
        for (auto& p : projects_)
          if (QString::fromStdString(p.meta.id) == id) { p.meta.description = text.toStdString(); break; }
        fileStore::saveProjects(projects_);
        refresh();
      } else {
        // Server row: guarded PUT ("" clears). On success update the cached record so the tooltip
        // reflects it immediately (until the next live re-list), then refresh.
        stencil::net::ServerClient* c = connections_ ? connections_->find(server) : nullptr;
        if (!c) return;
        qint64 version = 0;
        for (const auto& sp : remote_)
          if (sp.id == id && sp.serverUrl == server) { version = sp.version; break; }
        QPointer<ProjectsDialog> self(this);
        c->updateProjectDescriptionAsync(
            id, text, version,
            [this, self, id, server, text](bool ok2, qint64 newVersion, bool) {
              if (!self || !ok2) return;
              for (auto& sp : remote_)
                if (sp.id == id && sp.serverUrl == server) {
                  sp.description = text;
                  sp.version = newVersion;
                  break;
                }
              refresh();
            });
      }
    };

    // Same actions/order as the browser modal's overflow menu (slots act on the current row).
    if (remote) {
      menu.addAction(themedIcon("folder", ico, 16), "Open from server", this,
                     &ProjectsDialog::openSelected);
      menu.addAction(themedIcon("copy", ico, 16), "Copy to local…", this,
                     &ProjectsDialog::makeLocalCopySelected);
      menu.addAction(themedIcon("download", ico, 16), "Move to local", this,
                     &ProjectsDialog::moveToLocalSelected);
      menu.addSeparator();
      menu.addAction(themedIcon("image", ico, 16), "Set color…", this,
                     &ProjectsDialog::setColorSelected);
      menu.addAction(themedIcon("x", ico, 16), "Clear color", this,
                     &ProjectsDialog::clearColorSelected);
      menu.addAction(themedIcon("file-text", ico, 16), "Description…", this, editDescription);
    } else {
      menu.addAction(themedIcon("folder", ico, 16), "Open", this,
                     &ProjectsDialog::openSelected);
      menu.addAction(themedIcon("external", ico, 16), "Open in new window", this,
                     &ProjectsDialog::openSelectedInNewWindow);
      menu.addAction(themedIcon("pencil", ico, 16), "Rename", this,
                     &ProjectsDialog::renameSelected);
      menu.addAction(themedIcon("calendar", ico, 16), "Expiration…", this,
                     &ProjectsDialog::expirationSelected);
      if (haveServers) {
        menu.addAction(themedIcon("server", ico, 16), "Move to server", this,
                       &ProjectsDialog::moveToServerSelected);
        menu.addAction(themedIcon("copy", ico, 16), "Copy to server…", this,
                       &ProjectsDialog::copyToServerSelected);
      }
      menu.addSeparator();
      menu.addAction(themedIcon("image", ico, 16), "Set color…", this,
                     &ProjectsDialog::setColorSelected);
      menu.addAction(themedIcon("x", ico, 16), "Clear color", this,
                     &ProjectsDialog::clearColorSelected);
      menu.addAction(themedIcon("file-text", ico, 16), "Description…", this, editDescription);
      menu.addSeparator();
      // Destructive: a red trash glyph echoes the browser's red "Remove" item.
      menu.addAction(themedIcon("trash", QColor("#dc3545"), 16), "Remove", this,
                     &ProjectsDialog::deleteSelected);
    }
    menu.exec(globalPos);
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
      action_ = Action::OpenRemote;
      accept();
      return;
    }
    action_ = Action::Open;
    accept();
  }

  void ProjectsDialog::openSelectedInNewWindow() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    // New-window / delete / rename / renew apply to LOCAL projects only.
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;
    selectedId_ = it->data(Qt::UserRole).toString();
    action_ = Action::OpenInNewWindow;
    accept();
  }

  void ProjectsDialog::deleteSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local only
    const QString id = it->data(Qt::UserRole).toString();
    const QString nm = support::shortName(it->data(Qt::UserRole + 3).toString());
    // Confirm on the NEXT turn, over the still-open dialog: the drag-out Remove zone
    // lands here from a drag release, which dismisses a box shown in the same turn.
    QTimer::singleShot(0, this, [this, id, nm] {
      if (!confirmYesNo(this, "Remove project",
                        QString("Remove \"%1\"? This cannot be undone.").arg(nm)))
        return;
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
            this, DisintegrateOverlay::Sweep::Rows, /*dust=*/true);
        retireRow(row);  // blank the real row at once — the snapshot is what flies
      }
      emit removeRequested({{id, QString()}});  // the owner removes, then setProjects()
    });
  }

  void ProjectsDialog::moveToServerSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local rows only
    if (!connections_ || connections_->urls().isEmpty()) return;
    const QStringList urls = connections_->urls();
    QString target = urls.first();
    if (urls.size() > 1) {  // pick which server to store it on
      bool ok = false;
      target = QInputDialog::getItem(this, "Move to server",
                                     "Store this project on which server? The local copy "
                                     "will be removed.",
                                     urls, 0, false, &ok);
      if (!ok || target.isEmpty()) return;
    }
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
    const QStringList urls = connections_->urls();
    QString target = urls.first();
    if (urls.size() > 1) {
      bool ok = false;
      target = QInputDialog::getItem(this, "Copy to server",
                                     "Copy this project to which server?", urls, 0, false, &ok);
      if (!ok || target.isEmpty()) return;
    }
    const QString id = it->data(Qt::UserRole).toString();
    const auto cur = std::find_if(projects_.begin(), projects_.end(),
                                  [&](const Project& p) {
                                    return QString::fromStdString(p.meta.id) == id;
                                  });
    const QString base = cur != projects_.end() ? QString::fromStdString(cur->meta.name)
                                                : QStringLiteral("Untitled");
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Copy to server", "Name for the server copy:",
                                               QLineEdit::Normal, base + "-copy", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    selectedId_ = id;
    selectedServerUrl_ = target;
    newName_ = name.trimmed();
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
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Copy to local", "Name for the local copy:",
                                               QLineEdit::Normal, base + "-copy", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    selectedId_ = id;
    selectedServerUrl_ = server;
    newName_ = name.trimmed();
    action_ = Action::MakeLocalCopy;
    accept();
  }

  void ProjectsDialog::renameSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local only
    const QString id = it->data(Qt::UserRole).toString();
    const auto cur = std::find_if(projects_.begin(), projects_.end(),
                                  [&](const Project& p) {
                                    return QString::fromStdString(p.meta.id) == id;
                                  });
    const QString old = cur != projects_.end()
                            ? QString::fromStdString(cur->meta.name)
                            : QString();
    const auto name = promptValidatedName(this, "Rename Project", old, id, projects_);
    if (!name) return;
    selectedId_ = id;
    newName_ = *name;
    action_ = Action::Rename;
    accept();
  }

  void ProjectsDialog::expirationSelected() {
    auto* it = list_->currentItem();
    if (!it || it->data(Qt::UserRole).isNull()) return;
    if (!it->data(Qt::UserRole + 1).toString().isEmpty()) return;  // local only
    selectedId_ = it->data(Qt::UserRole).toString();
    action_ = Action::Expiration;
    accept();
  }

  void ProjectsDialog::scatterRows(const QSet<QString>& keys) {
    if (!list_) return;
    QList<QListWidgetItem*> doomed;
    QList<QRect> rects;   // the on-screen slice of each doomed row (scrolled-out rows: none)
    // Clip each row's rect to the viewport: a checked row scrolled out of view must not
    // drop its overlay onto the dialog chrome, nor spend the shared mote budget on
    // pixels nobody can see — only the visible slices animate.
    const QRect view = list_->viewport()->rect();
    for (int i = 0; i < list_->count(); ++i) {
      const QString key = rowKeyAt(i);
      if (key.isEmpty() || (!keys.isEmpty() && !keys.contains(key))) continue;
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
                                    DisintegrateOverlay::Sweep::Rows, /*dust=*/true, budget);
    for (QListWidgetItem* it : doomed)
      retireRow(it);   // blank the real row at once; its slot outlives the dust
  }

  // Closing mid-scatter: the close flight re-photographs the dialog as it hides
  // (modalReveal), so doomed rows leave NOW and their overlays stop — otherwise the
  // motes redraw the removed rows in the shrinking ghost.
  void ProjectsDialog::done(int result) {
    // A filter fade settles NOW too — nothing half-faded survives into the close flight.
    if (filterFade_) filterFade_->finishNow();
    for (int i = list_->count() - 1; i >= 0; --i)
      if (list_->item(i)->data(kDoomedRole).toBool()) delete list_->takeItem(i);
    for (QWidget* fx : findChildren<QWidget*>(QString::fromLatin1(DisintegrateOverlay::kObjectName))) {
      fx->hide();   // excluded from the ghost's render immediately; deleted safely after
      fx->deleteLater();
    }
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
    QTimer::singleShot(DisintegrateOverlay::kMs, this, [this, key] {
      // Re-found by key: a re-list may have rebuilt the rows (fresh ones aren't doomed).
      for (int i = 0; i < list_->count(); ++i)
        if (rowKeyAt(i) == key && list_->item(i)->data(kDoomedRole).toBool()) {
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

  QString ProjectsDialog::currentRowColor() const {
    auto* it = list_->currentItem();
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
    // Rows are delegate-painted (no swatch widget), so the origin is the row's rect —
    // the strip whose ⋯ menu / right-click opened this — passed as the GLOBAL fallback.
    const QRect rowRect(list_->viewport()->mapToGlobal(list_->visualItemRect(it).topLeft()),
                        list_->visualItemRect(it).size());
    const QColor picked =
        support::pickColorAnimated(seed, this, "Project name color", nullptr, rowRect);
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
    core::ProjectsStore store;
    std::vector<core::ProjectMeta> metas;
    for (const auto& p : projects_) metas.push_back(p.meta);
    store.load(metas);
    const QString seed = QString::fromStdString(store.defaultName());
    const auto name = promptValidatedName(this, "New Project", seed, QString(), projects_);
    if (!name) return;
    newName_ = *name;
    action_ = Action::New;
    accept();
  }

}
