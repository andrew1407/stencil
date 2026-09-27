// The guard's rules, dispatched by tool name: decide() maps one PreToolUse payload to allow /
// ask / deny, and safeDecide() turns any internal error into an ask. Loaded by ../guard.mjs.

import fs from 'node:fs';
import os from 'node:os';
import process from 'node:process';
import { bashDecision } from './bash.mjs';
import { fileDecision, searchDecision } from './files.mjs';
import { evaluateScriptDecision, uploadFileDecision } from './page.mjs';
import { navigationDecision, webFetchDecision, webSearchDecision } from './urls.mjs';
import { allow, ask } from './verdict.mjs';

const LOCAL_ORIGINS = ['localhost', '127.0.0.1', '0.0.0.0', '::1', 'host.docker.internal'];

export function defaultCtx() {
  const repoRoot = process.env.CLAUDE_PROJECT_DIR || process.cwd();
  return { repoRoot, homeDir: os.homedir(), allowedOrigins: LOCAL_ORIGINS, exists: fs.existsSync };
}

export function decide(payload, ctx = defaultCtx()) {
  const tool = payload && payload.tool_name;
  const input = (payload && payload.tool_input) || {};
  switch (tool) {
    case 'Bash':
      return bashDecision(input.command || '', ctx);
    case 'Read':
    case 'Edit':
    case 'Write':
    case 'NotebookEdit':
      return fileDecision(tool, input, ctx);
    case 'Grep':
    case 'Glob':
      return searchDecision(tool, input, ctx);
    case 'mcp__chrome-devtools__evaluate_script':
      return evaluateScriptDecision(input, ctx);
    case 'mcp__chrome-devtools__upload_file':
      return uploadFileDecision(input, ctx);
    case 'mcp__chrome-devtools__navigate_page':
    case 'mcp__chrome-devtools__new_page':
      return navigationDecision(input, ctx);
    case 'WebFetch':
      return webFetchDecision(input, ctx);
    case 'WebSearch':
      return webSearchDecision(input);
    default:
      return allow(); // anything else — unrestricted
  }
}

// Fail-closed wrapper: any exception inside decide() becomes an `ask` whose prompt
// explains why, so an internal guard bug can never turn into a silent allow.
export function safeDecide(payload, ctx) {
  try {
    return ctx === undefined ? decide(payload) : decide(payload, ctx);
  } catch (err) {
    return ask(`guard hook hit an internal error (failing closed): ${err && err.message ? err.message : err}`);
  }
}
