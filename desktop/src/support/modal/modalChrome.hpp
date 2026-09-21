#pragma once
#include <QRect>
#include <QString>
#include <QVector>
#include <functional>
#include <optional>

class QBoxLayout;
class QDialog;
class QFormLayout;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLayout;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;

// The browser's shared modal shell (.settings-header/.settings-body/.vs-section/
// .settings-footer in browser/css/components.css); the QSS half lives in theme.cpp.
namespace stencil::gui {

  // Browser .app-modal: 560px.
  inline constexpr int MODAL_WIDTH = 560;
  // Public because a child that ANIMATES its slot must carry the gap itself — a layout's
  // spacing cannot be animated (projectsDialog's batch bar; controlReveal closeBarSlot).
  inline constexpr int BODY_SPACING = 10;

  struct ModalChrome {
    QVBoxLayout* root = nullptr;   // the dialog's own layout — margins 0, dividers full-bleed
    QVBoxLayout* body = nullptr;   // the padded content column (browser .settings-body)
    QPushButton* close = nullptr;  // the header pill, already wired to reject()
    QLabel* footerHint = nullptr;  // the footer's muted hint, once addModalFooter made one
  };

  // `iconName` is an iconSet glyph (empty = no glyph). The dialog must have no layout yet.
  ModalChrome installModalChrome(QDialog* dlg, const QString& iconName, const QString& title);

  // Call after the body is filled. `liveHint` keeps the label even while empty (browser #open-in-hint).
  QHBoxLayout* addModalFooter(ModalChrome& chrome, const QString& hint = QString(),
                              bool liveHint = false);
  // The footer's one-line width — what a `width:auto` browser modal opens at.
  int modalFooterLineWidth(const ModalChrome& chrome, const QHBoxLayout* footer);

  // Browser .modal-search-bar. Call before the body is filled.
  QLineEdit* addModalSearchBar(ModalChrome& chrome, const QString& placeholder);

  // Browser .settings-body. `topPad` is the browser's 14px, or 0 under a pinned table head.
  struct ModalScrollBody {
    QScrollArea* scroll = nullptr;
    QWidget* content = nullptr;
    QVBoxLayout* layout = nullptr;
  };
  ModalScrollBody makeModalScrollBody(ModalChrome& chrome, int topPad = 14);

  // Browser .vs-section.
  QLabel* modalSectionLabel(const QString& text, QWidget* parent = nullptr);
  // margin 14px 0 6px; the first one 0.
  QLabel* modalSectionLabel(const QString& text, QWidget* parent, bool first);

  // Browser .vs-row. `content` follows the label (empty = content only); `labelMinW` > 0 pins the column.
  QWidget* modalRow(QWidget* parent, const QString& label, QLayout* content,
                    int labelMinW = 0);
  QWidget* modalRow(QWidget* parent, const QString& label, QWidget* field, bool grow = false,
                    int labelMinW = 0);

  // Browser .info-empty.
  QLabel* modalEmptyLabel(const QString& text, QWidget* parent = nullptr);

  // Browser modal cap: `width` wide, 82% of the screen tall, at most 760px.
  void sizeModalTall(QDialog* dlg, int width);

  QFrame* modalDivider(QWidget* parent = nullptr);

  // Labels and rows flush LEFT (the macOS style right-aligns labels); `grow` fields fill the row (browser .vs-field).
  void alignModalForm(QFormLayout* form, bool growFields = false);

  // Browser default `button`: accent fill, white text. Danger buttons keep objectName "dangerButton".
  void makeModalCta(QPushButton* btn, const QString& iconName = QString());

  // The same shared danger face, for a row that needs its objectName for something else.
  void makeModalDanger(QPushButton* btn, const QString& iconName = QString());

  // The GO face: Run in the script editors, green rather than the accent every other CTA wears.
  void makeModalGo(QPushButton* btn, const QString& iconName = QString());

  // GLOBAL, optional flight endpoints (support/modalReveal.hpp). Browser twin: confirmModal.js `closeAnchor`.
  struct FlightAnchors {
    QRect openRect;    // invalid = the press that raised the dialog
    QRect closeRect;   // invalid = back the way it came
  };

  // Browser ui/confirmModal.js: true on Confirm, false on Cancel / Close / Escape.
  struct ConfirmSpec {
    QString title;                                  // header title
    // Browser opts.titleIcon.
    QString titleIcon = QStringLiteral("alert");
    QString message;                                // the question
    QString confirmLabel = QStringLiteral("OK");
    QString confirmIcon = QStringLiteral("check");  // the action's glyph (browser confirmIcon)
    QString cancelLabel = QStringLiteral("Cancel");
    bool danger = false;                            // destructive action → red confirm
    // Non-empty → a THIRD button between Cancel and Confirm (browser askAlt).
    QString altLabel;
    QString altIcon = QStringLiteral("plus");
    FlightAnchors flight;                           // where it grows from / shrinks into
  };
  bool confirmModal(QWidget* parent, const ConfirmSpec& spec);
  // Browser confirmModal.js askAlt.
  enum class ConfirmChoice { CANCEL, CONFIRM, ALT };
  ConfirmChoice confirmModalChoice(QWidget* parent, const ConfirmSpec& spec);

  // Browser confirmModal.js `prompt`. `multiline` = the <textarea> shape (Ctrl/⌘+Enter
  // confirms). Returns the trimmed text, or nullopt on cancel.
  struct PromptSpec {
    QString title;                                 // header title
    // Browser opts.titleIcon: the alert triangle only for a question with a consequence.
    QString titleIcon = QStringLiteral("alert");
    QString message;                               // the field's caption
    QString defaultValue;                          // pre-filled (and pre-selected) text
    QString confirmLabel = QStringLiteral("Save");
    QString confirmIcon = QStringLiteral("save");
    QString cancelLabel = QStringLiteral("Cancel");
    bool multiline = false;
    bool password = false;                         // echo dots (a token, never shown)
    int rows = 3;                                  // multiline only
    int maxChars = 0;                              // >0 caps the returned text
    // Returns why the trimmed text cannot be saved (empty = it can); disables Save and shows under the field.
    std::function<QString(const QString&)> validate;
    FlightAnchors flight;                          // where it grows from / shrinks into
  };
  std::optional<QString> promptModal(QWidget* parent, const PromptSpec& spec);

  // Browser confirmModal.js `choose`. Returns the picked value, or nullopt on cancel.
  struct ChooseOption {
    QString value;
    QString label;                                 // empty = the value itself
  };
  struct ChooseSpec {
    QString title = QStringLiteral("Choose");
    QString titleIcon = QStringLiteral("alert");
    QString message;
    QVector<ChooseOption> options;
    int currentIndex = 0;                          // pre-selected option
    QString confirmLabel = QStringLiteral("OK");
    QString confirmIcon = QStringLiteral("check");
    QString cancelLabel = QStringLiteral("Cancel");
    FlightAnchors flight;
  };
  std::optional<QString> chooseModal(QWidget* parent, const ChooseSpec& spec);

}  // namespace stencil::gui
