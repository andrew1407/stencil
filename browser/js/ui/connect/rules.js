// The Servers modal's pure rules: which rows a credential filter keeps and how a batch
// outcome is worded. Shared by the list, the batch bar and the connect form.
// Desktop twin of the filter: dialogs/connect/ConnectDialogFilter.cpp.

// all | admin | non-admin; an admin credential is one that can mint session tokens.
export const matchesConnFilter = (conn, mode) => {
  if (mode === 'admin') return conn?.credentialKind === 'admin';
  if (mode === 'non-admin') return conn?.credentialKind !== 'admin';
  return true;
};

// One server is named outright, several are counted (desktop parity).
export const batchNote = (verb, urls) =>
  (urls.length === 1 ? `${verb} to ${urls[0]}` : `${verb} ${urls.length} servers`);
