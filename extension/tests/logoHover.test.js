// The header logo's hover — pulse + levitate + accent shine + orbiting rays — is a
// browser-app twin (browser/css/animations.css logoPulse/logoRaysSpin/logoRaysShimmer).
// The rays live on a ::before of a .logo-wrap span because an inline <svg> can't host
// pseudo-elements, so every host page must carry the wrapper: forget it on one and that
// surface silently loses the whole hover. Assert the five hosts stay in lockstep and the
// shared sheet keeps the effect (and its reduced-motion fallback) intact.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const HOSTS = {
  popup: 'src/popup/popup.html',
  sidepanel: 'src/sidepanel/sidepanel.html',
  devtools: 'src/devtools/panel.html',
  options: 'src/options/options.html',
  crop: 'src/crop/crop.html',
};

const html = Object.fromEntries(
  Object.entries(HOSTS).map(([name, path]) => [name, readFileSync(new URL('../' + path, import.meta.url), 'utf8')]));

for (const [name, path] of Object.entries(HOSTS)) {
  test(`${name} (${path}) wraps its logo for the hover ray ring`, () => {
    // The wrap must open immediately before the logo element it hosts the rays for.
    assert.match(html[name], /<span class="logo-wrap">\s*<(?:svg|img)[^>]*class="logo"/,
      `${path} must wrap its .logo in <span class="logo-wrap"> (lib/animations.css rays)`);
    assert.ok(html[name].includes('lib/animations.css'),
      `${path} must link lib/animations.css or the wrap does nothing`);
  });
}

test('the shared sheet carries the pulse, the rays, and the same beat as the browser app', () => {
  const css = readFileSync(new URL('../src/lib/animations.css', import.meta.url), 'utf8');
  // Pulse + levitate + accent shine on the 1.2s beat (browser logoPulse twin).
  assert.match(css, /@keyframes logoPulse/);
  assert.match(css, /\.logo-wrap \.logo:hover \{ transform: scale\(1\.08\); animation: logoPulse 1\.2s ease-in-out infinite; \}/);
  const pulse = /@keyframes logoPulse \{([\s\S]*?)\n\}/.exec(css)[1];
  assert.match(pulse, /translateY\(-2px\) scale\(1\.12\)/, 'the peak levitates and swells');
  assert.match(pulse, /drop-shadow\([^)]*var\(--accent\) 80%/, 'the glow is the accent, brightest at the peak');
  // Ray ring: slow revolution, opacity shimmering on the pulse's own beat.
  assert.match(css, /@keyframes logoRaysSpin/);
  assert.match(css, /@keyframes logoRaysShimmer/);
  assert.match(css, /\.logo-wrap:hover::before \{\n\s*animation: logoRaysSpin 8s linear infinite,\n\s*logoRaysShimmer 1\.2s ease-in-out infinite;/);
  const ring = /\.logo-wrap::before \{([\s\S]*?)\n\}/.exec(css)[1];
  assert.match(ring, /repeating-conic-gradient/, 'the spokes are an accent conic gradient');
  assert.match(ring, /opacity: 0;/, 'hidden at rest');
  assert.match(ring, /z-index: -1;/, 'behind the mark');
  assert.match(ring, /pointer-events: none;/, 'never widens the hover/drop target');
  // Reduced motion keeps the plain scale highlight and drops the ring entirely.
  assert.match(css, /@media \(prefers-reduced-motion: reduce\) \{\n\s*\.logo-wrap \.logo:hover \{ animation: none; \}/);
  assert.match(css, /\.logo-wrap::before \{ content: none; \}/);
});
