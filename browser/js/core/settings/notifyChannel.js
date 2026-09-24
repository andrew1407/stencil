// Where a notice shows (Visuals modal / stencil.notifyChannel): 'toast', the in-app stack in
// the corner, or 'system', the browser's Notification API. App-wide under its own localStorage
// key; the sinks are in ui/shell/notifySinks.js. Desktop twin: support/notify/NotificationSink.hpp.
import { publish, EVENTS } from '../../eventBus/appBus.js';

export const NOTIFY_STORAGE_KEY = 'drawingApp_notify';
// Fired after every change, so an open dialog can restate its control.
export const NOTIFY_EVENT = EVENTS.notifyChannelChanged;

const NOTIFY_TOAST = 'toast';
const NOTIFY_SYSTEM = 'system';
export const NOTIFY_CHANNELS = Object.freeze([NOTIFY_TOAST, NOTIFY_SYSTEM]);
export const DEFAULT_NOTIFY_CHANNEL = NOTIFY_TOAST;
// One list for the desktop combo (dialogs/settings/SettingsDialogMotionRows.cpp) to mirror.
export const NOTIFY_CHANNEL_LABELS = Object.freeze([
  [NOTIFY_TOAST, 'In the app'],
  [NOTIFY_SYSTEM, 'Browser notifications'],
]);

const ls = () => (typeof localStorage !== 'undefined' ? localStorage : null);

export const normalizeNotifyChannel = (v) => {
  const s = String(v ?? '').trim().toLowerCase();
  return NOTIFY_CHANNELS.includes(s) ? s : DEFAULT_NOTIFY_CHANNEL;
};

// Bad/missing data degrades to the default.
const readNotifyChannel = () => {
  try {
    const raw = ls()?.getItem(NOTIFY_STORAGE_KEY);
    if (!raw) return DEFAULT_NOTIFY_CHANNEL;
    const saved = JSON.parse(raw);
    return normalizeNotifyChannel(saved && typeof saved === 'object' ? saved.channel : saved);
  } catch { /* storage blocked or the blob is junk — the default stands */ }
  return DEFAULT_NOTIFY_CHANNEL;
};

let channel = readNotifyChannel();

export const notifyChannel = () => channel;

// Persist and announce. An unknown value falls back to the default (the console facade
// throws before it gets here).
export const setNotifyChannel = (v) => {
  channel = normalizeNotifyChannel(v);
  try { ls()?.setItem(NOTIFY_STORAGE_KEY, JSON.stringify({ channel })); } catch { /* storage blocked — this session still honours it */ }
  publish(NOTIFY_EVENT, channel);
  return channel;
};

// Tests only: forget what was loaded and read the store again.
export const reloadNotifyChannel = () => { channel = readNotifyChannel(); return channel; };
