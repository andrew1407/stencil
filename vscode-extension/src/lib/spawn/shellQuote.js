// Quoting per shell family, because VS Code's terminal is whichever shell the user picked.
// PowerShell doubles a quote and evaluates a quoted command word; cmd.exe has no escape at
// all and needs `cd /d` to change drive; everything else is POSIX.
'use strict';

// Bytes a POSIX shell passes through untouched.
const SAFE_POSIX = /^[A-Za-z0-9_@%+=:,./-]+$/;

// cmd.exe expands %NAME% even inside quotes and splits an argument on , and =; PowerShell
// reads a leading @ as splatting and a , as a list. Anything else is quoted.
const SAFE_CMD = /^[A-Za-z0-9_@+:./-]+$/;
const SAFE_PS = /^[A-Za-z0-9_+=:./-]+$/;

/* `lead` is what PowerShell needs before a quoted command word, which it would otherwise
 * print as a string. A " cannot be escaped inside a cmd.exe quoted string and is illegal in a
 * Windows path, so it is dropped rather than left to split the argument. */
const SHELLS = Object.freeze({
  posix: Object.freeze({
    safe: SAFE_POSIX, cd: 'cd', lead: '',
    quote: (text) => `'${text.split("'").join("'\\''")}'`,
  }),
  powershell: Object.freeze({
    safe: SAFE_PS, cd: 'cd', lead: '& ',
    quote: (text) => `'${text.split("'").join("''")}'`,
  }),
  cmd: Object.freeze({
    safe: SAFE_CMD, cd: 'cd /d', lead: '',
    quote: (text) => `"${text.split('"').join('')}"`,
  }),
});

const DEFAULT_KIND = 'posix';

// Windows with nothing reported is PowerShell, the default terminal there.
const shellKind = (shell) => {
  const name = String(shell ?? '').toLowerCase();
  if (!name) return process.platform === 'win32' ? 'powershell' : DEFAULT_KIND;
  if (/(^|[\\/])(pwsh|powershell)(\.exe)?$/.test(name)) return 'powershell';
  if (/(^|[\\/])cmd(\.exe)?$/.test(name)) return 'cmd';
  return DEFAULT_KIND;
};

const shellFor = (kind) => SHELLS[kind] ?? SHELLS[DEFAULT_KIND];

module.exports = { DEFAULT_KIND, SAFE_CMD, SAFE_POSIX, SAFE_PS, SHELLS, shellFor, shellKind };
