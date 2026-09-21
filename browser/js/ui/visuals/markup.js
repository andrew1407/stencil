// ── Markup: the Visuals & Settings modal ────────────────────────────────────
// The searchable rows of visual defaults: accent mount, motion mode, drawing colours.
import { icon } from '../icons.js';
import { MOTION_MODE_LABELS } from '../motion/motionPrefs.js';
import { SILENCE_MS_MIN, SILENCE_MS_MAX } from '../../llm/voice/settings.js';

export const visualsModalInner = () => `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('palette', { size: 18 })} Visuals &amp; Settings</h2>
                <button class="app-modal-close btn-icon-text" id="visuals-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="modal-search-bar">
                <input type="text" id="vs-search" class="modal-search" placeholder="Search settings…">
            </div>
            <div class="settings-body">
                <div class="vs-section">App appearance</div>
                <div class="vs-row"><label>Main theme</label>
                    <div id="vs-accent" class="vs-ctrl"></div>
                </div>
                <div class="vs-row"><label>Appearance</label>
                    <span class="vs-ctrl"><select id="vs-appearance">
                        <option value="system">System (follow the OS)</option>
                        <option value="light">Light</option>
                        <option value="dark">Dark</option>
                    </select></span>
                </div>
                <div class="vs-section">Motion</div>
                <div class="vs-row"><label>Drawing animation</label>
                    <span class="vs-ctrl vs-ctrl-check"><input type="checkbox" id="vs-draw-anim"></span>
                </div>
                <div class="vs-row"><label>Dim and blur behind windows</label>
                    <span class="vs-ctrl vs-ctrl-check"><input type="checkbox" id="vs-modal-backdrop"></span>
                </div>
                <div class="vs-row"><label>Interface animation</label>
                    <span class="vs-ctrl"><select id="vs-motion-mode" data-cs-skip>
                        ${MOTION_MODE_LABELS.map(([v, label]) => `<option value="${v}">${label}</option>`).join('')}
                    </select></span>
                </div>
                <div class="vs-section">Drawing defaults (applied to new lines)</div>
                <div class="vs-row"><label>Line color</label><label class="vs-ctrl vs-color"><input type="color" id="vs-line-color"><span class="vs-hex"></span></label></div>
                <div class="vs-row"><label>Line thickness</label><span class="vs-ctrl"><input type="number" id="vs-thickness" min="1" max="20"></span></div>
                <div class="vs-row"><label>Point size</label><span class="vs-ctrl"><input type="number" id="vs-point" min="1" max="30"></span></div>
                <div class="vs-row"><label>Line style</label>
                    <span class="vs-ctrl"><select id="vs-style"><option value="solid">Solid</option><option value="dashed">Dashed</option><option value="dotted">Dotted</option></select></span>
                </div>
                <div class="vs-row"><label>Area fill (new locked areas)</label><label class="vs-ctrl vs-color"><input type="color" id="vs-fill"><span class="vs-hex"></span></label></div>
                <div class="vs-section">Drawing behavior</div>
                <div class="vs-row"><label>Hold-to-draw delay (ms)</label><span class="vs-ctrl"><input type="number" id="vs-hold-delay" min="100" max="3000" step="50"></span></div>
                <div class="vs-section">Voice input</div>
                <div class="vs-row"><label data-title="How long a pause ends what you are saying and sends it — dictation in the chat and the hands-free voice chat both use it">Send after a pause of (ms)</label>
                    <span class="vs-ctrl"><input type="number" id="vs-voice-silence" min="${SILENCE_MS_MIN}" max="${SILENCE_MS_MAX}" step="100"></span></div>
                <div class="vs-section">Highlight styles</div>
                <div class="vs-row"><label>Selected line/point glow</label><label class="vs-ctrl vs-color"><input type="color" id="vs-sel-glow"><span class="vs-hex"></span></label></div>
                <div class="vs-row"><label>Point hover ring</label><label class="vs-ctrl vs-color"><input type="color" id="vs-hover-ring"><span class="vs-hex"></span></label></div>
                <div class="vs-row"><label>Point focus ring</label><label class="vs-ctrl vs-color"><input type="color" id="vs-focus-ring"><span class="vs-hex"></span></label></div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint">Changes apply live and are saved automatically.</span>
                <button id="vs-reset" class="btn-icon-text">${icon('rotate-ccw', { size: 14 })}<span>Reset All</span></button>
            </div>
        </div>
    `;
