// The Servers connect form: URL + token on Enter or Connect, and Reconnect all. A new
// row gathers in on the list's removal hold, so the connections echo cannot rebuild it.
// Desktop twin: ConnectDialog::doConnect (dialogs/connect/ConnectDialogActions.cpp).
import { notify } from '../../utils.js';
import { normalizeUrl, isInsecureRemote } from '../../net/connectionManager.js';
import { materialize, rowDustGrid } from '../motion.js';
import { batchNote } from './rules.js';

export function wireConnectForm({ urlEl, tokenEl, addBtn, reconnectBtn, list, mgr }, { hold, render }) {
  const connect = async () => {
    const url = urlEl.value.trim();
    if (!url) { notify('Enter a server URL', 'fail'); return; }
    const token = tokenEl.value.trim();
    addBtn.disabled = true;
    try {
      await mgr().connect(token ? { url, token } : url);
      urlEl.value = '';
      tokenEl.value = '';
      notify('Connected', 'ok');
      if (isInsecureRemote(normalizeUrl(url)))
        notify('Insecure connection: plaintext http — your access token and images are sent unencrypted. Use https on untrusted networks.', 'fail');
      // The new row materializes on the same hold as a removal, so the connections-changed
      // echo cannot rebuild the list mid-animation.
      const settle = hold.begin();
      render();
      materialize(list.querySelector(`[data-url="${CSS.escape(normalizeUrl(url))}"]`),
        rowDustGrid());
      await settle();
    } catch (err) {
      notify(`Could not connect — ${err.message}`, 'fail');
      // A refused credential still leaves a row (connectionManager keeps it in `_expired`)
      // and it gets the same arrival; an unreachable server leaves no row.
      let norm = '';
      try { norm = normalizeUrl(url); } catch { norm = ''; }
      if (norm && mgr().isExpired(norm)) {
        // The URL is in the list now, so leaving it typed in invites adding it twice.
        urlEl.value = '';
        tokenEl.value = '';
        const settle = hold.begin();
        render();
        materialize(list.querySelector(`[data-url="${CSS.escape(norm)}"]`), rowDustGrid());
        await settle();
      }
    } finally {
      addBtn.disabled = false;
    }
  };

  addBtn.addEventListener('click', connect);
  urlEl.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); connect(); } });
  tokenEl.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); connect(); } });
  reconnectBtn.addEventListener('click', async () => {
    try { const urls = mgr().urls; await mgr().reconnect(); notify(batchNote('Reconnected', urls), 'ok'); }
    catch (err) { notify(`Reconnect failed — ${err.message}`, 'fail'); }
    render();
  });
}
