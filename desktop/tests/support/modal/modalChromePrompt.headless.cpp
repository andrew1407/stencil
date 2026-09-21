// chooseModal, and promptModal's password and live-validation modes.
#include "modalChromeParts.hpp"

namespace modalchrome {

  void checkChooseAndPrompt(QWidget& host) {
  // ── chooseModal ──
  {
    ChooseSpec spec;
    spec.title = "Choose server";
    spec.message = "Copy to which server?";
    spec.confirmIcon = "server";
    spec.options = {{"http://a:8090", ""}, {"", "Local (this computer)"}, {"http://b:8090", ""}};
    QComboBox* seen = nullptr;
    int count = 0;
    QString firstLabel, secondLabel;
    bool okIsDefault = false;
    whenModal("stencilChooseModal", [&](QDialog* m) {
      seen = m->findChild<QComboBox*>("modalChooseSelect");
      if (seen) {
        count = seen->count();
        firstLabel = seen->itemText(0);
        secondLabel = seen->itemText(1);
        seen->setCurrentIndex(2);
      }
      QPushButton* ok = btnByText(m, "OK");
      okIsDefault = ok && ok->isDefault();
      if (ok) ok->click();
    });
    const auto picked = chooseModal(&host, spec);
    check(seen != nullptr, "choose: the picker row carries a select");
    check(count == 3, "choose: one option per entry");
    check(firstLabel == "http://a:8090", "choose: a label-less option shows its value");
    check(secondLabel == "Local (this computer)", "choose: a labelled option shows its label");
    check(okIsDefault, "choose: OK is the default button (Enter confirms)");
    check(picked && *picked == "http://b:8090", "choose: OK returns the picked VALUE");
  }
  {
    ChooseSpec spec;
    spec.message = "Where?";
    spec.options = {{"x", "X"}, {"y", "Y"}};
    whenModal("stencilChooseModal", [&](QDialog* m) {
      auto* sel = m->findChild<QComboBox*>("modalChooseSelect");
      if (sel) { sel->setCurrentIndex(1); pressEnter(sel); }
    });
    const auto picked = chooseModal(&host, spec);
    check(picked && *picked == "y", "choose: Enter on the select confirms");
  }
  {
    ChooseSpec spec;
    spec.message = "Where?";
    spec.options = {{"x", "X"}};
    whenModal("stencilChooseModal", [&](QDialog* m) {
      if (QPushButton* c = btnByText(m, "Cancel")) c->click();
    });
    check(!chooseModal(&host, spec).has_value(), "choose: Cancel hands back nothing");
    whenModal("stencilChooseModal", [&](QDialog* m) { m->reject(); });
    check(!chooseModal(&host, spec).has_value(), "choose: Escape/Close hands back nothing");
  }

  // ── promptModal: password ──
  {
    PromptSpec spec;
    spec.title = "Session expired";
    spec.message = "Paste an access token";
    spec.confirmLabel = "Reconnect";
    spec.confirmIcon = "link";
    spec.password = true;
    bool dots = false;
    whenModal("stencilPromptModal", [&](QDialog* m) {
      auto* line = m->findChild<QLineEdit*>("modalPromptLine");
      dots = line && line->echoMode() == QLineEdit::Password;
      if (line) { line->setText("  tok-123 "); pressEnter(line); }
    });
    const auto token = promptModal(&host, spec);
    check(dots, "prompt: password mode echoes dots");
    check(token && *token == "tok-123", "prompt: the trimmed token comes back on Enter");
  }

  // ── promptModal: live validation ──
  {
    PromptSpec spec;
    spec.title = "New Project";
    spec.message = "Project name:";
    spec.defaultValue = "taken";
    spec.validate = [](const QString& s) {
      if (s.isEmpty()) return QString("Name cannot be empty");
      if (s == "taken") return QString("A project with this name already exists");
      return QString();
    };
    bool deadAtOpen = false, reasonShown = false, enterIgnored = false, liveAfterFix = false,
         reasonGone = false;
    QString reasonText, tip;
    whenModal("stencilPromptModal", [&](QDialog* m) {
      auto* line = m->findChild<QLineEdit*>("modalPromptLine");
      auto* reason = m->findChild<QLabel*>("modalPromptReason");
      QPushButton* save = btnByText(m, "Save");
      deadAtOpen = save && !save->isEnabled();
      reasonShown = reason && reason->isVisible();
      reasonText = reason ? reason->text() : QString();
      tip = save ? save->toolTip() : QString();
      pressEnter(line);
      pumpFor(20);
      enterIgnored = m->isVisible();
      line->setText("fresh");
      liveAfterFix = save && save->isEnabled();
      reasonGone = reason && !reason->isVisible();
      pressEnter(line);
    });
    const auto name = promptModal(&host, spec);
    check(deadAtOpen, "prompt: Save is dead while the default value is not saveable");
    check(reasonShown && reasonText == "A project with this name already exists",
          "prompt: the reason shows under the field");
    check(tip == reasonText, "prompt: …and is Save's tooltip");
    check(enterIgnored, "prompt: Enter does nothing while invalid");
    check(liveAfterFix && reasonGone, "prompt: a saveable name enables Save and clears the reason");
    check(name && *name == "fresh", "prompt: Enter then saves it");
  }

  }

}  // namespace modalchrome
