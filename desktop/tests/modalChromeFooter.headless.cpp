// confirmModal's own glyph, OpenInDialog's Telegram fallback, and addModalFooter's wrap.
#include "modalChromeParts.hpp"

namespace modalchrome {

  void checkIconsAndFooter(QWidget& host) {
  // ── confirmModal titleIcon ──
  {
    ConfirmSpec spec;
    spec.title = "Project keywords";
    spec.titleIcon = "info";
    spec.message = "Keep?";
    bool glyph = false;
    whenModal("stencilConfirmModal", [&](QDialog* m) {
      // The header's glyph label sits before the title label, carrying a pixmap.
      for (QLabel* l : m->findChildren<QLabel*>())
        if (!l->pixmap().isNull()) glyph = true;
      m->accept();
    });
    check(confirmModal(&host, spec), "confirm: accept resolves true");
    check(glyph, "confirm: the header wears the caller's glyph");
  }

  // ── OpenInDialog: the Telegram fallback stays in the dialog ──
  {
    // A host too long for the 64-char payload.
    const QString longUrl = "https://stencil-collaboration-server.a-very-long-subdomain."
                            "example-organisation.example.com:8443";
    OpenInDialog dlg(&host, /*serverProject=*/true, longUrl, /*browser=*/true,
                     /*telegram=*/true, /*incognito=*/false, "proj-abcdef");
    int fallbacks = 0;
    QObject::connect(&dlg, &OpenInDialog::telegramFallback, [&fallbacks] { ++fallbacks; });
    dlg.show();
    pumpFor(20);
    QLabel* hint = dlg.findChild<QLabel*>("modalFooterHint");
    check(hint && hint->text().isEmpty(), "open-in: the footer hint starts empty");
    check(!dlg.fallbackShown(), "open-in: no fallback row before Telegram is asked");
    QPushButton* tg = btnByText(&dlg, "Telegram bot");
    check(tg != nullptr, "open-in: finds the Telegram button");
    if (tg) tg->click();
    pumpFor(20);
    check(dlg.isVisible(), "open-in: an overlong link keeps the dialog open");
    check(dlg.fallbackShown(), "open-in: …and shows the fallback row");
    check(dlg.fallbackCommands() == "/connect " + longUrl + "\n/fetch proj-abcdef",
          "open-in: the row carries the two bot commands");
    check(hint && hint->text() ==
                      "The link is too long for Telegram — open the bot and paste these commands.",
          "open-in: the footer hint says why, in the browser's words");
    check(fallbacks == 1, "open-in: the owner is told to open the bot chat");
    check(dlg.findChild<QPushButton*>("openInFallbackCopy") != nullptr,
          "open-in: a copy chip sits beside the commands");
    dlg.close();
  }
  {
    OpenInDialog dlg(&host, true, "http://localhost:8090", true, true, false, "p1");
    dlg.show();
    pumpFor(20);
    if (QPushButton* tg = btnByText(&dlg, "Telegram bot")) tg->click();
    pumpFor(20);
    check(!dlg.isVisible() && dlg.result() == QDialog::Accepted &&
              dlg.getOutcome() == OpenInDialog::Outcome::TELEGRAM,
          "open-in: a link that fits accepts with the Telegram outcome");
    check(!dlg.fallbackShown(), "open-in: …with no fallback row");
  }

  // ── addModalFooter: the wrap ──
  {
    QDialog dlg(&host);
    ModalChrome chrome = installModalChrome(&dlg, "info", "Wrap");
    chrome.body->addWidget(new QLabel("body", &dlg));
    QHBoxLayout* footer = addModalFooter(
        chrome, "Drag to move · drag a corner to resize (aspect locked to the page).");
    QLabel* hint = chrome.footerHint;
    auto* a = new QPushButton("Portrait", &dlg);
    auto* b = new QPushButton("Cancel", &dlg);
    auto* c = new QPushButton("Apply Crop", &dlg);
    footer->addWidget(a);
    footer->addWidget(b);
    footer->addWidget(c);
    const int gap = footer->spacing();
    const int buttons = a->minimumSizeHint().width() + b->minimumSizeHint().width() +
                        c->minimumSizeHint().width() + gap * 2;
    const int pads = 18 * 2 + 2;   // the stack's padding + the root's 1px insets
    const auto inDlg = [&dlg](QWidget* w) { return QRect(w->mapTo(&dlg, QPoint(0, 0)), w->size()); };
    const auto sameRow = [&](QWidget* x, QWidget* y) {
      return inDlg(x).center().y() >= inDlg(y).top() && inDlg(x).center().y() <= inDlg(y).bottom();
    };
    dlg.resize(pads + buttons + gap + 400, 200);
    dlg.show();
    pumpFor(60);   // the 0-timer reservation + the first apply
    check(footer->indexOf(hint) == 0 && footer->count() == 4,
          "footer: with room the hint leads the buttons' row");
    check(sameRow(hint, a) && inDlg(hint).left() < inDlg(a).left() && sameRow(a, c),
          "footer: hint left, buttons to its right on the one row");
    check(dlg.minimumWidth() >= pads + buttons && dlg.minimumWidth() < pads + buttons + 110,
          "footer: the window reserves the buttons' width, never the hint's");

    // Squeeze until the hint's 110px basis no longer fits beside the buttons.
    dlg.resize(pads + buttons + gap + 60, 200);
    pumpFor(60);
    check(footer->indexOf(hint) < 0 && footer->itemAt(0)->spacerItem() != nullptr &&
              footer->count() == 4,
          "wrap: the hint leaves the row and a stretch leads the buttons");
    check(hint->alignment() == (Qt::AlignLeft | Qt::AlignVCenter),
          "wrap: the hint on its own line stays left-aligned");
    check(inDlg(hint).left() == 19 && inDlg(hint).width() == dlg.width() - pads,
          "wrap: …spanning the full width from the left pad");
    check(inDlg(a).top() > inDlg(hint).bottom() && sameRow(a, b) && sameRow(b, c),
          "wrap: the buttons sit on one line under the hint");
    check(inDlg(c).right() == inDlg(hint).right() && inDlg(a).left() > inDlg(hint).left(),
          "wrap: …packed to the right edge");

    // Narrower than the buttons themselves (the popover shape): they wrap onto further
    // right-packed lines in their order, none cut off.
    dlg.setMinimumWidth(0);
    dlg.resize(pads + a->minimumSizeHint().width() + gap + b->minimumSizeHint().width() + 2, 260);
    pumpFor(60);
    check(sameRow(a, b) && inDlg(c).top() > inDlg(b).bottom(),
          "lines: the third button drops to a line of its own");
    check(inDlg(b).right() == inDlg(hint).right() && inDlg(c).right() == inDlg(hint).right(),
          "lines: every line packs right");
    check(dlg.rect().contains(inDlg(a)) && dlg.rect().contains(inDlg(b)) &&
              dlg.rect().contains(inDlg(c)),
          "lines: nothing is clipped at the edge");
    check(footer->count() == 3 && footer->indexOf(a) == 1 && footer->indexOf(b) == 2,
          "lines: the first line is still the actions row");

    // Wide again: one row, the hint back in front, the extra line gone.
    dlg.resize(pads + buttons + gap + 400, 200);
    pumpFor(60);
    check(footer->indexOf(hint) == 0 && footer->indexOf(a) == 1 && footer->indexOf(b) == 2 &&
              footer->indexOf(c) == 3 && footer->count() == 4,
          "unwrap: hint, then the buttons in their order, no stretch left behind");
    check(sameRow(hint, a) && sameRow(a, c), "unwrap: everything on the one row again");
    dlg.close();
  }

  {
    // The hover sweep is clipped to the control's own rounded shape: rendered on black mid-sweep, the
    // pill's corners stay untouched while its middle lights up — a plain fillRect spills across them.
    std::printf("the hover shimmer's rounded clip:\n");
    QDialog dlg;
    ModalChrome c = installModalChrome(&dlg, QString(), "Shimmer");
    dlg.resize(320, 200);
    dlg.show();
    pumpFor(40);
    QEvent enter(QEvent::Enter);
    QCoreApplication::sendEvent(c.close, &enter);
    pumpFor(120);
    auto* ov = c.close->findChild<QWidget*>(QStringLiteral("shimmerOverlay"));
    check(ov && ov->property("sweepProgress").toDouble() > 0, "the pill is mid-sweep");
    QImage shot(c.close->size(), QImage::Format_ARGB32_Premultiplied);
    shot.fill(Qt::black);
    if (ov) {
      QPainter p(&shot);
      ov->render(&p, QPoint(), QRegion(), QWidget::DrawChildren);
    }
    const int h = shot.height();
    check(qGray(shot.pixel(0, 0)) == 0 && qGray(shot.pixel(0, h - 1)) == 0,
          "the leading corners take no light — the band is inside the pill's radius");
    bool lit = false;
    for (int x = 0; x < shot.width() && !lit; ++x) lit = qGray(shot.pixel(x, h / 2)) > 0;
    check(lit, "…while its middle line does");
    dlg.close();
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");

  }

}  // namespace modalchrome
