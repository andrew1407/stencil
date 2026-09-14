// The contributed identifiers, in one place: tests/manifest.test.js asserts package.json
// declares exactly these, so a rename cannot land on only one side.
'use strict';

const LANGUAGE_ID = 'stencil-script';
const SCOPE_NAME = 'source.stc';
const FILE_EXTENSION = '.stc';
const CONFIG_SECTION = 'stencil';

const COMMANDS = Object.freeze({
  runScript: 'stencil.runScript',
  runScriptOnImage: 'stencil.runScriptOnImage',
  checkScript: 'stencil.checkScript',
});

const SETTINGS = Object.freeze({
  cliPath: 'cliPath',
  checkOnType: 'checkOnType',
});

module.exports = { COMMANDS, CONFIG_SECTION, FILE_EXTENSION, LANGUAGE_ID, SCOPE_NAME, SETTINGS };
