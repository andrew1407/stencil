#pragma once
#include <QString>

class QBoxLayout;
class QDialog;
class QFormLayout;
class QFrame;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

// The browser's shared modal shell (.settings-header / .settings-body /
// .vs-section / .settings-footer in browser/css/components.css), as Qt layout
// scaffolding: a glyph + bold title with an outlined "✕ Close" pill over a
// full-bleed hairline, a padded body the dialog fills, and a footer row under a
// second hairline. The QSS half lives in theme.cpp (#modalTitle,
// #modalClosePill, #modalDivider, #modalSection, #modalFooterHint).
namespace stencil::gui {

  // The browser's shared modal width (.app-modal: 560px) — every chrome-shelled
  // dialog sizes itself against it.
  inline constexpr int kModalWidth = 560;

  struct ModalChrome {
    QVBoxLayout* root = nullptr;   // the dialog's own layout — margins 0, dividers full-bleed
    QVBoxLayout* body = nullptr;   // the padded content column (browser .settings-body)
    QPushButton* close = nullptr;  // the header pill, already wired to reject()
  };

  // Install the header + body scaffolding on a dialog that has no layout yet.
  // `iconName` is an iconSet glyph (empty = no glyph).
  ModalChrome installModalChrome(QDialog* dlg, const QString& iconName, const QString& title);

  // Footer under a full-bleed divider: a muted hint on the left (empty = none)
  // and a stretch, so callers just append their buttons. Call after the body is
  // filled — it lands below everything added so far.
  QHBoxLayout* addModalFooter(ModalChrome& chrome, const QString& hint = QString());

  // A tracked, uppercase, muted section caption (browser .vs-section).
  QLabel* modalSectionLabel(const QString& text, QWidget* parent = nullptr);

  // A 1px hairline in the theme's border colour.
  QFrame* modalDivider(QWidget* parent = nullptr);

  // Browser-style form geometry: labels and rows flush LEFT (the macOS style
  // otherwise right-aligns labels and centres the whole form), and optionally
  // text fields growing to the full row width (browser .vs-field).
  void alignModalForm(QFormLayout* form, bool growFields = false);

  // Mark a footer button as the browser's default accent-filled CTA (browser
  // `button` default: accent fill, white text) and give it the named glyph in
  // white. Danger buttons keep objectName "dangerButton" instead.
  void makeModalCta(QPushButton* btn, const QString& iconName = QString());

  // The browser's confirm dialog (ui/confirmModal.js): the same chrome shell —
  // alert glyph + title + Close pill over a hairline, the question in the body,
  // and a footer with Cancel + the named action as accent CTAs (danger = red).
  // Modal; true on Confirm, false on Cancel / Close / Escape. Replaces the native
  // QMessageBox question wherever the browser shows its styled modal instead.
  struct ConfirmSpec {
    QString title;                                  // header title
    QString message;                                // the question
    QString confirmLabel = QStringLiteral("OK");
    QString confirmIcon = QStringLiteral("check");  // the action's glyph (browser confirmIcon)
    QString cancelLabel = QStringLiteral("Cancel");
    bool danger = false;                            // destructive action → red confirm
    // Non-empty → a THIRD button between Cancel and Confirm (browser askAlt: two
    // real answers plus a way out, each one click).
    QString altLabel;
    QString altIcon = QStringLiteral("plus");
  };
  bool confirmModal(QWidget* parent, const ConfirmSpec& spec);
  // The askAlt variant: Cancel / <alt> / <confirm> (browser confirmModal.js askAlt).
  enum class ConfirmChoice { Cancel, Confirm, Alt };
  ConfirmChoice confirmModalChoice(QWidget* parent, const ConfirmSpec& spec);

}  // namespace stencil::gui
