// One Servers row: grip, batch checkbox, status + URL label, the admin badge and the
// invite / reconnect / disconnect actions. The list (./list.js) owns the state it reads.
// Desktop twin: dialogs/connect/ConnectDialogRow.cpp + ConnectDialogRowActions.cpp.
import { notify } from '../../utils.js';
import { icon } from '../icons.js';
import { isExpiredSession } from '../../net/connectionManager.js';

export function connectRow(url, cm, { app, mgr, selected, drag, updateBatchBar, render, confirmDisconnect }) {
  const row = document.createElement('div');
  row.className = 'connect-row';
  row.dataset.url = url;
  const grip = document.createElement('span');
  grip.className = 'connect-grip';
  grip.dataset.title = 'Drag to reorder · drag out of the modal to disconnect';
  grip.textContent = '⋮⋮';
  drag.attach(row, url);
  row.appendChild(grip);
  const cb = document.createElement('input');
  cb.type = 'checkbox';
  cb.className = 'connect-select';
  cb.checked = selected.has(url);
  cb.dataset.title = 'Select for batch action';
  cb.addEventListener('change', () => {
    if (cb.checked) selected.add(url); else selected.delete(url);
    row.classList.toggle('connect-selected', cb.checked);
    updateBatchBar();
  });
  if (cb.checked) row.classList.add('connect-selected');
  const conn = cm.get(url);
  const status = conn ? (conn.status || 'connected') : 'error';
  const statusText = { connected: 'Connected', connecting: 'Connecting…', error: 'Disconnected — not reachable',
    disconnected: 'Disconnected', expired: 'Session expired — reconnect to sign in again' }[status] || status;
  const expired = status === 'expired';
  if (expired) row.classList.add('connect-expired');
  const isAdmin = conn?.credentialKind === 'admin';
  if (isAdmin) row.classList.add('connect-admin');
  const label = document.createElement('span');
  label.className = 'connect-url';
  label.dataset.title = `${statusText} — ${url}`;
  label.innerHTML = `<span class="conn-status conn-status-${status}" data-title="${statusText}"></span>${icon('server', { size: 14 })}<span>${url}</span>`;
  // Its own row child: .connect-url ellipsises a long URL and would clip the badge.
  let badge = null;
  if (isAdmin) {
    badge = document.createElement('span');
    badge.className = 'connect-admin-badge';
    badge.dataset.title = 'Admin credential — this connection can mint session tokens (invite links)';
    badge.innerHTML = `${icon('lock', { size: 12 })}<span>Admin</span>`;
  }
  // The same icon-only button in every state (desktop Connect parity). On an expired
  // session it first asks for a fresh session, then for a token (the admin token works too).
  const recon = document.createElement('button');
  recon.className = 'connect-reconnect-one btn-icon';
  recon.dataset.title = expired ? 'Sign in to this server again' : 'Reconnect this server';
  recon.innerHTML = icon('refresh', { size: 15 });
  recon.addEventListener('click', async () => {
    recon.disabled = true;
    try {
      await mgr().reconnectOne(url, expired ? '' : undefined);
      notify(`Reconnected to ${url}`, 'ok');
    } catch (err) {
      if (!isExpiredSession(err)) notify(`Reconnect failed — ${err.message}`, 'fail');
      else {
        const token = await app.prompt(
          `${url} refused the saved session. Paste an access token — or the server's `
          + 'admin token, which mints a fresh session for you.',
          { title: 'Session expired', confirmLabel: 'Reconnect', confirmIcon: 'link',
            // Reconnect needs a token: without one the answer was ignored.
            validate: (v) => (v ? '' : 'Paste a token to reconnect') });
        if (token) {
          try { await mgr().reconnectOne(url, String(token).trim()); notify(`Reconnected to ${url}`, 'ok'); }
          catch (e2) { notify(`Reconnect failed — ${e2.message}`, 'fail'); }
        }
      }
    } finally { recon.disabled = false; }
    render();
  });
  const disc = document.createElement('button');
  disc.className = 'connect-disconnect danger btn-icon';
  disc.dataset.title = 'Disconnect (and forget) this server';
  disc.innerHTML = icon('trash', { size: 15 });
  disc.addEventListener('click', () => confirmDisconnect(url));
  // Their own flex box, so the row's space-between cannot fling them apart.
  const actions = document.createElement('div');
  actions.className = 'connect-actions';
  // Invite: mint a session token and copy `<url>#token=…`; only an admin credential can mint.
  if (conn?.connected && conn.credentialKind === 'admin') {
    const invite = document.createElement('button');
    invite.className = 'connect-invite btn-icon';
    invite.dataset.title = 'Copy an invite link (mints a fresh session token)';
    invite.innerHTML = icon('link', { size: 15 });
    invite.addEventListener('click', async () => {
      invite.disabled = true;
      try {
        const link = await conn.mintInvite();
        await navigator.clipboard.writeText(link);
        notify('Invite link copied', 'ok');
      } catch (err) {
        notify(`Invite failed — ${err.message}`, 'fail');
      } finally { invite.disabled = false; }
    });
    actions.append(invite);
  }
  actions.append(recon, disc);
  row.append(cb, label, ...(badge ? [badge] : []), actions);
  return row;
}
