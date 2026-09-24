// The Notifications row: a two-way select, toast or system. Picking the browser's own asks for
// its permission from this click; refused, the row says so and stays in the app.
import { notify } from '../../utils.js';
import { enhanceSelect } from '../control/customSelect.js';
import { subscribe } from '../../eventBus/appBus.js';
import { notifyChannel, NOTIFY_EVENT, DEFAULT_NOTIFY_CHANNEL } from '../../core/settings/notifyChannel.js';
import { SystemSink } from '../shell/notifySinks.js';

const REFUSED = Object.freeze({
  unsupported: 'This browser has no notifications of its own — staying in the app',
  denied: 'Browser notifications are blocked for this site — staying in the app',
});

export const wireNotifyRow = (app, requestPermission = SystemSink.requestPermission) => {
  const select = document.getElementById('vs-notify-channel');
  enhanceSelect(select);
  const sync = () => { select.value = notifyChannel(); };
  select.addEventListener('change', async () => {
    const want = select.value;
    if (want === 'system') {
      const answer = await requestPermission();
      if (answer !== 'granted') {
        notify(REFUSED[answer] || REFUSED.denied, 'fail');
        sync();
        return;
      }
    }
    app.settings.setNotifyChannel(want);
  });
  subscribe(NOTIFY_EVENT, sync);
  return { populate: sync, reset: () => app.settings.setNotifyChannel(DEFAULT_NOTIFY_CHANNEL) };
};
