// Recolouring is VS Code's setting, not one of ours — an extension may not set token colours,
// only a theme or the user can. This command opens that setting, seeding it first with every
// row this language uses, labelled, so there is something to edit rather than an empty object.
import * as vscode from 'vscode';

import { COMMANDS } from './lib/ids.js';

const SETTING = 'editor.semanticTokenColorCustomizations';

// The values Dark Modern already resolves to, so seeding changes nothing until one is edited.
// tests/colors.test.js holds these equal to the block in README.md.
const DEFAULT_RULES = Object.freeze({
  macro: '#569cd6',
  class: '#4ec9b0',
  type: '#4ec9b0',
  keyword: '#c586c0',
  function: '#dcdcaa',
  parameter: '#9cdcfe',
  enumMember: '#4fc1ff',
  label: '#c8c8c8',
  variable: '#9cdcfe',
  property: '#9cdcfe',
  string: '#ce9178',
  number: '#b5cea8',
  operator: '#d4d4d4',
});

const isUnset = (value) => !value || Object.keys(value).length === 0;

const configureColors = async () => {
  const config = vscode.workspace.getConfiguration();
  if (isUnset(config.get(SETTING))) {
    await config.update(SETTING, { '[*]': { rules: { ...DEFAULT_RULES } } }, true);
  }
  // `edit` scrolls to the setting and opens it for editing; without support it just opens the file.
  await vscode.commands.executeCommand('workbench.action.openSettingsJson', {
    revealSetting: { key: SETTING, edit: true },
  });
};

const register = (context) => {
  const registration = vscode.commands.registerCommand(COMMANDS.configureColors, configureColors);
  context.subscriptions.push(registration);
  return registration;
};

export { DEFAULT_RULES, SETTING, configureColors, register };
