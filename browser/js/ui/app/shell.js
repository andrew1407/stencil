// The editor shell DrawingApp stands on: the fixed nodes it holds and the theme <html> carries
// (data-theme, data-accent and the inline --accent of a custom accent).
export const shellNodes = () => ({
  canvas: document.getElementById('canvas'),
  tooltip: document.getElementById('tooltip'),
  coordinatesBody: document.getElementById('coordinates-body'),
});

export const htmlTheme = () => (document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light');
export const htmlAccent = () => document.documentElement.getAttribute('data-accent');
// '' when a named preset is active.
export const htmlCustomAccent = () => document.documentElement.style.getPropertyValue('--accent').trim();
