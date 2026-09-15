// The contributed identifiers, in one place: tests/manifest.test.js asserts package.json
// declares exactly these, so a rename cannot land on only one side.
'use strict';

const LANGUAGE_ID = 'stencil-script';
const SCOPE_NAME = 'source.stc';
const FILE_EXTENSION = '.stc';
const CONFIG_SECTION = 'stencil';

// The JavaScript flavour: a file that drives the browser app's `window.stencil` facade.
// A plain .js opts in with USE_MARKER on its first line, the way a .stc says `@use`.
const JS_LANGUAGE_ID = 'stencil-js';
const JS_SCOPE_NAME = 'source.stcjs';
const JS_FILE_EXTENSION = '.stcjs';
const USE_MARKER = '// @use stencil';

// The instance the web commands open, when the setting names none.
const DEFAULT_WEB_URL = 'https://andrew1407.github.io/stencil/';

// The saved project file: contributed for its icon and JSON grammar, registered by no code.
const PROJECT_LANGUAGE_ID = 'stencil-project';
const PROJECT_SCOPE_NAME = 'source.stencil-project';
const PROJECT_FILE_EXTENSION = '.stencil';

const COMMANDS = Object.freeze({
  runScript: 'stencil.runScript', runScriptOnImage: 'stencil.runScriptOnImage',
  checkScript: 'stencil.checkScript', configureColors: 'stencil.configureColors',
  openInWeb: 'stencil.openInWeb', openInWebIncognito: 'stencil.openInWebIncognito',
  runInWebConsole: 'stencil.runInWebConsole', runSelectionInWebConsole: 'stencil.runSelectionInWebConsole',
  openImageInWeb: 'stencil.openImageInWeb', addTypings: 'stencil.addTypings',
});

const SETTINGS = Object.freeze({
  cliPath: 'cliPath', checkOnType: 'checkOnType', checkOnSave: 'checkOnSave',
  highlighting: 'highlighting', completion: 'completion', hover: 'hover', colors: 'colors',
  webUrl: 'webUrl', webBrowser: 'webBrowser', webInlineImages: 'webInlineImages',
});

module.exports = { COMMANDS, CONFIG_SECTION, DEFAULT_WEB_URL, FILE_EXTENSION, JS_FILE_EXTENSION,
  JS_LANGUAGE_ID, JS_SCOPE_NAME, LANGUAGE_ID, PROJECT_FILE_EXTENSION, PROJECT_LANGUAGE_ID,
  PROJECT_SCOPE_NAME, SCOPE_NAME, SETTINGS, USE_MARKER };
