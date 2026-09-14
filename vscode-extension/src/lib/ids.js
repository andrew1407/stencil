// The contributed identifiers, in one place: tests/manifest.test.js asserts package.json
// declares exactly these, so a rename cannot land on only one side.
'use strict';

const LANGUAGE_ID = 'stencil-script';
const SCOPE_NAME = 'source.stc';
const FILE_EXTENSION = '.stc';
const CONFIG_SECTION = 'stencil';

// The saved project file: contributed for its icon and JSON grammar, registered by no code.
const PROJECT_LANGUAGE_ID = 'stencil-project';
const PROJECT_SCOPE_NAME = 'source.stencil-project';
const PROJECT_FILE_EXTENSION = '.stencil';

const COMMANDS = Object.freeze({
  runScript: 'stencil.runScript', runScriptOnImage: 'stencil.runScriptOnImage',
  checkScript: 'stencil.checkScript', configureColors: 'stencil.configureColors',
});

const SETTINGS = Object.freeze({
  cliPath: 'cliPath', checkOnType: 'checkOnType', highlighting: 'highlighting',
  completion: 'completion', hover: 'hover', colors: 'colors',
});

module.exports = { COMMANDS, CONFIG_SECTION, FILE_EXTENSION, LANGUAGE_ID, PROJECT_FILE_EXTENSION,
  PROJECT_LANGUAGE_ID, PROJECT_SCOPE_NAME, SCOPE_NAME, SETTINGS };
