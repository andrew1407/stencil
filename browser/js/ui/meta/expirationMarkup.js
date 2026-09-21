import { icon } from '../icons.js';

export const expirationModalInner = () => `
        <div class="app-modal exp-modal">
            <div class="settings-header">
                <h2>${icon('calendar', { size: 18 })} Project expiration</h2>
                <button class="app-modal-close btn-icon-text" id="expiration-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <div class="exp-project" id="expiration-project"></div>
                <div class="vs-row exp-keep-row">
                    <label><input type="checkbox" id="expiration-keep"> Keep forever (never expires)</label>
                </div>
                <div class="vs-row" id="expiration-period-row">
                    <label for="expiration-period">Expires in</label>
                    <span class="exp-period-controls">
                        <select id="expiration-period"></select>
                        <button id="expiration-refresh" class="btn-icon-text" data-title="Set the expiration to now + the selected period">${icon('refresh', { size: 14 })}<span>Refresh</span></button>
                    </span>
                </div>
                <div class="vs-row" id="expiration-auto-row">
                    <label><input type="checkbox" id="expiration-auto"> Refresh expiration each time the project is opened</label>
                </div>
                <div class="exp-calendar" id="expiration-calendar">
                    <div class="exp-cal-head">
                        <button class="btn-icon" id="expiration-prev" data-title="Previous month">${icon('chevron-left', { size: 16 })}</button>
                        <span class="exp-cal-title" id="expiration-cal-title"></span>
                        <button class="btn-icon" id="expiration-next" data-title="Next month">${icon('chevron-right', { size: 16 })}</button>
                    </div>
                    <div class="exp-cal-grid" id="expiration-cal-grid"></div>
                </div>
                <div class="exp-legend">
                    <span class="exp-legend-item"><span class="exp-swatch exp-swatch-today"></span>Today: <b id="expiration-today"></b></span>
                    <span class="exp-legend-item"><span class="exp-swatch exp-swatch-expiry"></span>Expires: <b id="expiration-when"></b></span>
                </div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint">Past dates can’t be chosen. Expiration is local to this browser.</span>
                <button id="expiration-save" class="btn-icon-text">${icon('check')}<span>Save</span></button>
            </div>
        </div>
    `;
