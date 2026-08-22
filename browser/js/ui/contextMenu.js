import { StencilElement, hostTag, define } from './base.js';
import { notify, setRadioGroup, formatCombo, supportsShareFiles, isTouchLike, pointInRect } from '../utils.js';
import { hotkeys } from '../core/hotkeys.js';
import { icon } from './icons.js';
import { loadLlmSettings, serverBearerToken } from '../llm/llmSettings.js';
import { probeProvider } from '../llm/llmClient.js';
import { MAX_ATTACHMENTS } from '../llm/chatController.js';
import {
  sharedChatController, peekChatController, runLoggedChatTurn, closedTurnToast, queueAttachments,
  cacheProbe, cachedProbe, probeStatusClass,
  chatLog, onChatLog, clearSharedConversation, requeueRowAttachments, chatTurnInFlight,
} from '../llm/chatSession.js';
import {
  renderChatLog, chatAttachmentChips, wireInputSizer, wireChatSuggestions, wireChatMoreMenu,
  chatSuggestionsHtml, chatComposerActionsHtml, syncComposerControls, wireChatComposer,
  notifyAttachmentsChanged, CHAT_ATTACHMENTS_EVENT, wireChatRowMenu, chatRowMenuOpen,
} from './chatView.js';
import { menuPopOrigin, surfaceIn, surfaceOut, settleSurface, motionReduced } from './motion.js';
// ── Component: custom right-click context menu ──────────────────
const SUBMENU_HIDE_DELAY_MS = 180; // grace period before a submenu closes on mouseleave
const LIVE_SYNC_INTERVAL_MS = 120; // poll cadence to reflect external state while the menu is open
const TINT_DEBOUNCE_MS = 80;       // debounce custom-tint recolor+save while dragging the picker
const ASSIST_SCROLL_GRACE_MS = 900; // window in which an assistant-caused scroll can't close the menu

// ── Assistant: an ordinary submenu parent whose flyout IS a chat ─────────────
// A compact chat on the SAME conversation as the panel (one controller per app —
// js/llm/chatSession.js). Exists ONLY when a provider is configured; an unreachable
// one still shows it — the failure surfaces in the reply, exactly like the panel.
export const assistantEnabled = (settings) => (settings?.provider ?? 'none') !== 'none';

// The entry's markup, unconditionally (syncAssistant gates and builds it). It sits
// directly ABOVE the drawing items with no separator of its own, so gating it off
// leaves the menu's original separator set untouched — nothing dangles.
export const assistantItemHtml = () => `
        <!-- Assistant submenu: the flyout is a compact chat (chat panel's conversation) -->
        <div class="ctx-item" id="ctx-assist-menu">
            <span class="ctx-icon">${icon('sparkle')}</span><span class="ctx-label">Assistant</span><span class="ctx-arrow">${icon('chevron-right', { size: 12 })}</span>
            <div class="ctx-sub ctx-assist-sub" id="ctx-assist-sub">
                <div class="ctx-assist" id="ctx-assist">
                    <div class="ctx-assist-transcript" id="ctx-assist-transcript">
                        <div class="chat-empty">
                            ${chatSuggestionsHtml()}
                        </div>
                    </div>
                    <div class="ctx-assist-attachments" id="ctx-assist-attachments"></div>
                    <div class="ctx-assist-row">
                        <div class="ctx-assist-inputcol">
                            <div class="ctx-assist-sizer" id="ctx-assist-sizer"></div>
                            <textarea id="ctx-assist-input" rows="2" placeholder="Ask the assistant… (Enter sends, Shift+Enter newline)"></textarea>
                        </div>
                        ${chatComposerActionsHtml({
    prefix: 'ctx-assist',
    actionsClass: 'ctx-assist-actions',
    gearClass: 'ctx-assist-config',
    gearTitle: 'Assistant settings — provider &amp; model',
  })}
                    </div>
                </div>
            </div>
        </div>`;

export class StencilContextMenu extends StencilElement {
  static inner() {
    return `
        <!-- Fit zoom to window — FIRST: the most-reached-for entry, and a plain item, so it
             is a single click with no submenu to traverse. -->
        <div class="ctx-item" id="ctx-fit-window"><span class="ctx-icon">${icon('fit')}</span><span class="ctx-label">Fit to Window</span><span class="ctx-hotkey" data-hk="resetZoom">Alt+0</span></div>
        <!-- Image / Layout submenu -->
        <div class="ctx-item" id="ctx-layout-menu">
            <span class="ctx-icon">${icon('folder')}</span><span class="ctx-label">Image / Layout</span><span class="ctx-arrow">${icon('chevron-right', { size: 12 })}</span>
            <div class="ctx-sub" id="ctx-layout-sub">
                <div class="ctx-sub-label">Image</div>
                <div class="ctx-item ctx-sub-item" id="ctx-copy-img"><span class="ctx-icon">${icon('copy')}</span><span class="ctx-label">Copy Image</span><span class="ctx-hotkey" data-hk="copyImage">Ctrl+C</span></div>
                <div class="ctx-item ctx-sub-item" id="ctx-paste-img"><span class="ctx-icon">${icon('paste')}</span><span class="ctx-label">Paste Image</span><span class="ctx-hotkey" data-hk="paste">Ctrl+V</span></div>
                <div class="ctx-item ctx-sub-item" id="ctx-dl-img"><span class="ctx-icon">${icon('download')}</span><span class="ctx-label">Download Image</span></div>
                <div class="ctx-item ctx-sub-item" id="ctx-share-img" style="display:none;"><span class="ctx-icon">${icon('share')}</span><span class="ctx-label">Share Image</span></div>
                <div class="ctx-sep"></div>
                <div class="ctx-sub-label">Layout (JSON)</div>
                <div class="ctx-item ctx-sub-item" id="ctx-copy-layout"><span class="ctx-icon">${icon('copy')}</span><span class="ctx-label">Copy Layout</span><span class="ctx-hotkey" data-hk="copyLayout">Ctrl+Alt+C</span></div>
                <div class="ctx-item ctx-sub-item" id="ctx-paste-layout"><span class="ctx-icon">${icon('paste')}</span><span class="ctx-label">Paste Layout</span><span class="ctx-hotkey" data-hk="paste">Ctrl+V</span></div>
                <div class="ctx-item ctx-sub-item" id="ctx-dl-layout"><span class="ctx-icon">${icon('file-text')}</span><span class="ctx-label">Download Layout</span></div>
                <div class="ctx-item ctx-sub-item" id="ctx-ul-layout"><span class="ctx-icon">${icon('upload')}</span><span class="ctx-label">Upload Layout</span></div>
            </div>
        </div>
        <!-- Fullscreen toggle -->
        <div class="ctx-item" id="ctx-fullscreen"><span class="ctx-icon">${icon('maximize')}</span><span class="ctx-label" id="ctx-fs-label">Enter Fullscreen</span><span class="ctx-hotkey" data-hk="fullscreen">Alt+F</span></div>
        <div class="ctx-sep"></div>
        <!-- Drawing (the Assistant entry is built above this group by syncAssistant) -->
        <div class="ctx-item" id="ctx-draw-toggle"><span class="ctx-icon">${icon('play', { size: 14 })}</span><span class="ctx-label" id="ctx-draw-label">Start Drawing</span><span class="ctx-hotkey" id="ctx-draw-hotkey" data-hk="startDraw">Alt+A</span></div>
        <div class="ctx-item" id="ctx-drawmode-toggle"><span class="ctx-icon">${icon('rect')}</span><span class="ctx-label" id="ctx-drawmode-label">Switch to Rectangle Drawing</span></div>
        <div class="ctx-item" id="ctx-draw-rect"><span class="ctx-icon">${icon('rect-filled')}</span><span class="ctx-label">Draw Rectangle (instant)</span></div>
        <div class="ctx-sep"></div>
        <!-- Toggles -->
        <div class="ctx-item" id="ctx-show-points"><span class="ctx-check" id="ctx-chk-points">${icon('check', { size: 14 })}</span><span class="ctx-label">Show Points</span><span class="ctx-hotkey" data-hk="togglePoints">Alt+P</span></div>
        <div class="ctx-item" id="ctx-show-lines"><span class="ctx-check" id="ctx-chk-lines">${icon('check', { size: 14 })}</span><span class="ctx-label">Show Lines</span><span class="ctx-hotkey" data-hk="toggleLines">Alt+L</span></div>
        <div class="ctx-item" id="ctx-clear-lines"><span class="ctx-icon">${icon('eraser')}</span><span class="ctx-label">Clear All Lines</span><span class="ctx-hotkey" data-hk="clearAllLines">Alt+W</span></div>
        <div class="ctx-sep"></div>
        <!-- Style submenu -->
        <div class="ctx-item" id="ctx-style-menu">
            <span class="ctx-icon">${icon('palette')}</span><span class="ctx-label">Style</span><span class="ctx-arrow">${icon('chevron-right', { size: 12 })}</span>
            <div class="ctx-sub" id="ctx-style-sub">
                <div class="ctx-row">
                    <label>Point Size</label>
                    <input type="number" class="ctx-num" id="ctx-point-size" min="1" max="30">
                </div>
                <div class="ctx-row">
                    <label>Line Thickness</label>
                    <input type="number" class="ctx-num" id="ctx-thickness" min="1" max="20">
                </div>
                <div class="ctx-sub-label">Line Style</div>
                <div class="ctx-radio-group" id="ctx-style-radios">
                    <label class="ctx-radio-item"><input type="radio" name="ctxLineStyle" value="solid"> Solid</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxLineStyle" value="dashed"> Dashed</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxLineStyle" value="dotted"> Dotted</label>
                </div>
            </div>
        </div>
        <!-- Filter submenu -->
        <div class="ctx-item" id="ctx-filter-menu">
            <span class="ctx-icon">${icon('image')}</span><span class="ctx-label">Image Filter</span><span class="ctx-hotkey" data-hk="cycleFilter">Alt+B</span><span class="ctx-arrow">${icon('chevron-right', { size: 12 })}</span>
            <div class="ctx-sub" id="ctx-filter-sub">
                <div class="ctx-sub-label">Filter</div>
                <div class="ctx-radio-group" id="ctx-filter-radios">
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="none"> None</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="bw"> Black &amp; White</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="sepia"> Sepia</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="invert"> Invert</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="contour"> Contour</label>
                    <label class="ctx-radio-item"><input type="radio" name="ctxFilter" value="custom"> Custom Tint</label>
                </div>
                <div id="ctx-tint-row">
                    <label>Tint Color</label>
                    <input type="color" class="ctx-color" id="ctx-tint-color">
                </div>
            </div>
        </div>
        <!-- Transformation submenu -->
        <div class="ctx-item" id="ctx-transform-menu">
            <span class="ctx-icon">${icon('function')}</span><span class="ctx-label">Transformation</span><span class="ctx-arrow">${icon('chevron-right', { size: 12 })}</span>
            <div class="ctx-sub" id="ctx-transform-sub">
                <div class="ctx-sub-label">Coordinate Formulas</div>
                <label class="ctx-checkbox-item"><input type="checkbox" id="ctx-allow-formulas"> Allow Formulas</label>
                <div id="ctx-formula-inputs" style="display:none;padding:5px 14px 8px;">
                    <div style="margin-top:4px;display:flex;flex-direction:column;gap:5px;">
                        <div style="display:flex;align-items:center;gap:6px;">
                            <label style="font-size:12px;color:var(--text-muted);min-width:36px;font-weight:normal;">x(x)=</label>
                            <input type="text" id="ctx-formula-x" placeholder="e.g. x + 9" style="width:140px;font-family:monospace;font-size:12px;padding:3px 6px;border:1px solid var(--border-main);border-radius:4px;background:var(--input-bg);color:var(--input-text);">
                        </div>
                        <div style="display:flex;align-items:center;gap:6px;">
                            <label style="font-size:12px;color:var(--text-muted);min-width:36px;font-weight:normal;">y(y)=</label>
                            <input type="text" id="ctx-formula-y" placeholder="e.g. (y-7)*4" style="width:140px;font-family:monospace;font-size:12px;padding:3px 6px;border:1px solid var(--border-main);border-radius:4px;background:var(--input-bg);color:var(--input-text);">
                        </div>
                        <div id="ctx-formula-error" style="font-size:11px;color:var(--danger);display:none;align-items:center;gap:5px;">${icon('alert', { size: 13 })} Invalid formula</div>
                    </div>
                </div>
            </div>
        </div>
        <!-- Tooltip submenu -->
        <div class="ctx-item" id="ctx-tooltip-menu">
            <span class="ctx-icon">${icon('message')}</span><span class="ctx-label">Tooltip</span><span class="ctx-arrow">${icon('chevron-right', { size: 12 })}</span>
            <div class="ctx-sub" id="ctx-tooltip-sub">
                <label class="ctx-checkbox-item"><input type="checkbox" id="ctx-tt-enabled" checked> Show Tooltips</label>
                <div class="ctx-sep"></div>
                <div class="ctx-sub-label">Show in Tooltip</div>
                <label class="ctx-checkbox-item"><input type="checkbox" id="ctx-tt-page" checked> Page (cm)</label>
                <label class="ctx-checkbox-item"><input type="checkbox" id="ctx-tt-screen" checked> Screen (px)</label>
                <label class="ctx-checkbox-item"><input type="checkbox" id="ctx-tt-coords" checked> To Edge (cm)</label>
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-context-menu', 'id="ctx-menu"', StencilContextMenu.inner()); }

  wire(app) {
    const menu = document.getElementById('ctx-menu');
    const canvas = document.getElementById('canvas');
    const viewport = document.getElementById('canvas-viewport');

    // ── Submenu management ──────────────────────────────────────
    let subHideTimer = null;
    let activeSub = null;
    let activeSubItem = null;

    const closeAllSubs = () => {
      clearTimeout(subHideTimer);
      document.querySelectorAll('#ctx-menu .ctx-sub.ctx-sub-visible').forEach(s => s.classList.remove('ctx-sub-visible'));
      document.querySelectorAll('#ctx-menu .ctx-item.ctx-open-sub').forEach(i => i.classList.remove('ctx-open-sub'));
      activeSub = null;
      activeSubItem = null;
      subShownPointer = null;   // nothing placed ⇒ no "pointer idle since" reference
    };

    const positionSub = (item, sub) => {
      // The menu's entry pop (animations.css) keeps a live transform for ~140ms, and a
      // transformed ancestor becomes the containing block for our position:fixed
      // flyouts — one placed during the pop lands off-target. A quick hover beats the
      // animation, so finish the (purely cosmetic) pop first.
      try { for (const a of menu.getAnimations?.() || []) a.finish(); } catch { /* no WAAPI */ }
      // Render off-screen to measure, then place correctly
      sub.style.left = '-9999px';
      sub.style.top = '-9999px';
      sub.classList.add('ctx-sub-visible');
      item.classList.add('ctx-open-sub');

      const ir = item.getBoundingClientRect();
      const sw = sub.offsetWidth;
      const sh = sub.offsetHeight;
      const vw = window.innerWidth;
      const vh = window.innerHeight;

      let left = ir.right + 2;
      if (left + sw > vw - 6) left = ir.left - sw - 2;
      if (left < 4) left = 4;

      let top = ir.top;
      if (top + sh > vh - 6) top = vh - sh - 6;
      if (top < 4) top = 4;

      sub.style.left = left + 'px';
      sub.style.top = top  + 'px';
      subShownPointer = { ...lastPointer };
    };

    const repositionActiveSub = () => {
      if (activeSub && activeSubItem && activeSub.classList.contains('ctx-sub-visible'))
        positionSub(activeSubItem, activeSub);
    };

    // A flyout may declare itself "engaged" (a chat mid-typing, a running turn) via a
    // `_keepOpen` predicate: a stray mouseleave must NOT yank it away then. Only the
    // hover-out paths honour it — hovering ANOTHER submenu parent still closes it.
    const keepSubOpen = (sub) => !!sub._keepOpen?.();

    // Pointer bookkeeping for the hide timers: a mouseleave does NOT always mean the
    // user left — an item that MOVES under a stationary cursor (the menu's entry pop)
    // fires one with no user motion at all, which must not shut a just-opened flyout.
    let lastPointer = { x: -1, y: -1 };
    let subShownPointer = null;   // where the pointer was when the flyout was placed
    const samplePointer = e => { lastPointer = { x: e.clientX, y: e.clientY }; };
    // Captured mouseover/mouseout too — they precede the non-bubbling mouseenter/
    // mouseleave the wiring reacts to; tracking moves alone would leave a stale position
    // at decision time. Bound only while the menu is open (openAt ↔ closeMenu).
    const setPointerTracking = (on) => {
      for (const type of ['mousemove', 'mouseover', 'mouseout']) {
        if (on) document.addEventListener(type, samplePointer, true);
        else document.removeEventListener(type, samplePointer, true);
      }
    };
    const pointerOver = (el) => pointInRect(lastPointer.x, lastPointer.y, el.getBoundingClientRect());
    // The pointer has not MOVED since the open flyout was placed ⇒ hover events arriving
    // now are layout-induced (items sliding under a still cursor), not the user's.
    // Compared by position, not clock: the shift can land in the placement's millisecond.
    const pointerIdle = () => !!subShownPointer
      && subShownPointer.x === lastPointer.x && subShownPointer.y === lastPointer.y;

    // Hide a submenu — unless the cursor never moved since it opened (a layout-induced
    // mouseleave: re-place it and keep it), the cursor is still on it or its parent, or
    // it's the assistant flyout the user is busy in.
    const hideSub = (item, sub) => {
      if (keepSubOpen(sub)) return;
      if (pointerIdle() && sub.classList.contains('ctx-sub-visible')) {
        positionSub(item, sub);   // the item moved, not the user — follow it
        return;
      }
      if (pointerOver(item) || pointerOver(sub)) return;
      sub.classList.remove('ctx-sub-visible');
      item.classList.remove('ctx-open-sub');
      if (activeSub === sub) { activeSub = null; activeSubItem = null; }
    };

    const closeActiveSub = () => {
      if (!activeSub || keepSubOpen(activeSub) || pointerIdle()) return;
      activeSub.classList.remove('ctx-sub-visible');
      if (activeSubItem) activeSubItem.classList.remove('ctx-open-sub');
      activeSub = null;
      activeSubItem = null;
    };

    // Items WITHOUT a submenu close any open one on hover.
    const wirePlainItem = (item) => {
      item.addEventListener('mouseenter', () => {
        clearTimeout(subHideTimer);
        closeActiveSub();
      });
    };

    // Hover-open + grace-period hide for one submenu parent. Extracted so the
    // assistant entry — which may be built after wire() runs — gets the SAME wiring
    // as the static parents (Image / Layout, Style, Image Filter, …).
    const wireSubmenu = (item, sub) => {
      // ResizeObserver: reposition whenever submenu content changes size (the chat
      // flyout grows with the conversation). Only the FLYOUT moves — never the menu.
      new ResizeObserver(() => {
        if (sub === activeSub && sub.classList.contains('ctx-sub-visible')) positionSub(item, sub);
      }).observe(sub);

      item.addEventListener('mouseenter', () => {
        clearTimeout(subHideTimer);
        // A neighbour sliding under a stationary cursor must not steal the open flyout.
        if (pointerIdle() && activeSub && activeSub !== sub) return;
        // Phones/touch: the assistant entry is a plain item there — no flyout.
        if (item.dataset.noSub === '1') { closeActiveSub(); return; }
        // Close other open subs (this is the one path that also closes the assistant
        // flyout while it's engaged: opening a sibling submenu wins, as everywhere).
        document.querySelectorAll('#ctx-menu .ctx-sub.ctx-sub-visible').forEach(s => {
          if (s !== sub) s.classList.remove('ctx-sub-visible');
        });
        document.querySelectorAll('#ctx-menu .ctx-item.ctx-open-sub').forEach(i => {
          if (i !== item) i.classList.remove('ctx-open-sub');
        });
        positionSub(item, sub);
        activeSub = sub;
        activeSubItem = item;
      });

      item.addEventListener('mouseleave', e => {
        if (sub.contains(e.relatedTarget)) return;
        subHideTimer = setTimeout(() => {
          if (activeSub === sub) hideSub(item, sub);
        }, SUBMENU_HIDE_DELAY_MS);
      });

      sub.addEventListener('mouseenter', () => clearTimeout(subHideTimer));
      sub.addEventListener('mouseleave', e => {
        if (item.contains(e.relatedTarget)) return;
        subHideTimer = setTimeout(() => hideSub(item, sub), SUBMENU_HIDE_DELAY_MS);
      });
    };

    // Wire hover handlers — only for TOP-LEVEL items so nested ones inside
    // a submenu don't accidentally close their parent submenu on hover.
    menu.querySelectorAll(':scope > .ctx-item').forEach(item => {
      const sub = item.querySelector(':scope > .ctx-sub');
      if (sub) wireSubmenu(item, sub);
      else wirePlainItem(item);
    });

    // ── Live-sync timer ─────────────────────────────────────────
    // Keeps ctx menu state current while it's open (hotkeys may change state)
    let syncInterval = null;
    // Where the menu grew from — the click. Kept so the close pours back into it.
    let openPoint = null;

    const closeMenu = () => {
      // Into the very point it grew out of (js/ui/motion.js) — measured while it is
      // still on screen, and only if it IS: closeMenu is also the idle teardown.
      if (menu.classList.contains('ctx-open') && !motionReduced()) surfaceOut(menu, openPoint);
      else settleSurface(menu);
      menu.classList.remove('ctx-open');
      closeAllSubs();
      setPointerTracking(false);
      clearInterval(syncInterval);
      syncInterval = null;
    };

    const syncState = () => {
      if (!app) return;
      document.getElementById('ctx-draw-label').textContent =
        app.isDrawing ? 'Stop Drawing' : 'Start Drawing';
      document.getElementById('ctx-draw-toggle').querySelector('.ctx-icon').innerHTML =
        app.isDrawing ? icon('stop', { size: 14 }) : icon('play', { size: 14 });

      const dmLabel = document.getElementById('ctx-drawmode-label');
      if (dmLabel) dmLabel.textContent = app.drawMode === 'rect'
        ? 'Switch to Line Drawing' : 'Switch to Rectangle Drawing';
      const dmIcon = document.getElementById('ctx-drawmode-toggle').querySelector('.ctx-icon');
      if (dmIcon) dmIcon.innerHTML = app.drawMode === 'rect'
        ? icon('swap') : icon('rect');

      // Toggle a row's disabled state and explain it via the native title tooltip:
      // the base label when enabled, base + "— reason" when off.
      const gate = (id, off, base, reason) => {
        const el = document.getElementById(id);
        if (!el) return;
        el.classList.toggle('ctx-disabled', off);
        el.title = off ? `${base}\n— ${reason}` : base;
      };

      const hasLines = app.lines && app.lines.length > 0;
      gate('ctx-dl-layout', !hasLines, 'Download Layout', 'Draw at least one line to export');
      gate('ctx-copy-layout', !hasLines, 'Copy Layout', 'Draw at least one line to copy');

      const hasImage = !!app.image;
      gate('ctx-copy-img', !hasImage, 'Copy Image', 'Load an image first');
      gate('ctx-dl-img', !hasImage, 'Download Image', 'Load an image first');
      // Share item is rendered only where Web Share files are supported (see wire()).
      gate('ctx-share-img', !hasImage, 'Share Image', 'Load an image first');
      // Paste-layout needs an image too (matching paste handler behavior)
      gate('ctx-paste-layout', !hasImage, 'Paste Layout', 'Load an image first');

      // Refresh hotkey hint text in case shortcuts were remapped
      hotkeys.updateCtxHints();
      // The draw hotkey span carries data-hk="startDraw"; override it after
      // updateCtxHints so it reflects the start/stop binding for the live state.
      document.getElementById('ctx-draw-hotkey').textContent =
        formatCombo(hotkeys.get(app.isDrawing ? 'stopDraw' : 'startDraw'), hotkeys.isMac);

      // Checkmarks
      document.getElementById('ctx-chk-points').innerHTML = app.showPoints ? icon('check', { size: 14 }) : '';
      document.getElementById('ctx-chk-lines').innerHTML = app.showLines  ? icon('check', { size: 14 }) : '';

      // Style sub values
      document.getElementById('ctx-point-size').value = app.pointSize;
      document.getElementById('ctx-thickness').value = app.thickness;
      setRadioGroup('ctxLineStyle', app.style);

      // Filter sub values
      setRadioGroup('ctxFilter', app.imageFilter);
      document.getElementById('ctx-tint-color').value = app.filterColor || '#7c3aed';
      const tintRow = document.getElementById('ctx-tint-row');
      tintRow.classList.toggle('ctx-tint-visible', app.imageFilter === 'custom');

      // Fullscreen label
      const isFS = document.body.classList.contains('fullscreen-mode');
      document.getElementById('ctx-fs-label').textContent = isFS ? 'Exit Fullscreen' : 'Enter Fullscreen';
      document.getElementById('ctx-fullscreen').querySelector('.ctx-icon').innerHTML = icon('maximize');

      // Tooltip checkboxes
      document.getElementById('ctx-tt-enabled').checked = app.tooltipEnabled;
      document.getElementById('ctx-tt-page').checked = app.tooltipShowPage;
      document.getElementById('ctx-tt-screen').checked = app.tooltipShowScreen;
      document.getElementById('ctx-tt-coords').checked = app.tooltipShowCoords;
      // Formula checkbox
      document.getElementById('ctx-allow-formulas').checked = app.allowFormulas;
      document.getElementById('ctx-formula-inputs').style.display = app.allowFormulas ? 'block' : 'none';
    };

    const menuIsOpen = () => menu.classList.contains('ctx-open');

    // Assistant turn state, read by the scroll-close guard below: while the chat is
    // answering (and for a grace window after), the canvas viewport scrolls caused by
    // the executing plan must NOT dismiss the menu the user is chatting in.
    let assistSending = false;
    let assistBusyUntil = 0;
    const assistantBusy = () => assistSending || Date.now() < assistBusyUntil;

    // Clamp the menu into the viewport, ONCE per open. Deliberately NOT re-run while
    // open: moving the root menu under a stationary cursor fires mouseleave on the
    // hovered item, closing its flyout — the chat lives in a FLYOUT for exactly this
    // reason (its own observer grows it without moving the menu).
    const placeMenu = (x, y) => {
      const mw = menu.offsetWidth;
      const mh = menu.offsetHeight;
      const vw = window.innerWidth;
      const vh = window.innerHeight;
      const left = Math.max(4, Math.min(x, vw - mw - 6));
      const top = Math.max(4, Math.min(y, vh - mh - 6));
      menu.style.left = left + 'px';
      menu.style.top = top + 'px';
      // The entry pop (animations.css menuPop) grows out of the click point.
      menu.style.transformOrigin = menuPopOrigin(x, y, { left, top, width: mw, height: mh });
    };

    const openAt = (x, y) => {
      openPoint = { x, y };
      closeAllSubs();
      // Track the pointer only while open; seed it with the click point so the
      // idle checks never compare against a stale position from a previous open.
      setPointerTracking(true);
      lastPointer = { x, y };
      // Build/hide the Assistant entry and pick its mode (flyout vs plain) BEFORE
      // measuring — the menu is re-evaluated per open, so a provider change or a
      // window resize between opens is picked up.
      syncAssistant();
      menu.style.left = '-9999px'; menu.style.top = '-9999px';
      menu.classList.add('ctx-open');
      placeMenu(x, y);
      // …and it forms out of that same point as dust (js/ui/motion.js). Opacity only —
      // a live transform on the menu would make it the containing block for its
      // position:fixed flyouts, which is the very trap menuPop's comment names.
      if (!motionReduced()) surfaceIn(menu, openPoint);
      // Start live-sync so hotkey changes reflect immediately
      clearInterval(syncInterval);
      syncInterval = setInterval(() => {
        if (menu.classList.contains('ctx-open')) syncState();
        else { clearInterval(syncInterval); syncInterval = null; }
      }, LIVE_SYNC_INTERVAL_MS);
    };

    [canvas, viewport].forEach(el => {
      el.addEventListener('contextmenu', e => {
        if (!app.image) return;
        e.preventDefault();
        syncState();
        openAt(e.clientX, e.clientY);
      });
    });

    // Close on outside click / Escape / scroll; everything INSIDE the menu keeps it
    // open, including the assistant chat. Escape always closes, even from the chat
    // input — a chat input you can't Escape out of is the surprising option.
    document.addEventListener('mousedown', e => {
      if (!menu.contains(e.target)) closeMenu();
    });
    // Capture phase, consumed only while open: Escape closes the menu and reaches
    // no other app handler. A chat row menu floating on top goes first (its own
    // capture closer swallows the key).
    document.addEventListener('keydown', e => {
      if (e.code !== 'Escape' || !menuIsOpen()) return;
      if (chatRowMenuOpen()) return;
      e.stopPropagation();
      closeMenu();
    }, true);
    document.addEventListener('scroll', e => {
      // Scrolling the chat transcript (or the menu itself, on a short screen) is not
      // a page scroll — only the latter dismisses the menu.
      if (e.target && e.target.nodeType && menu.contains(e.target)) return;
      // Nor is the canvas viewport re-scrolling because a PLAN just replaced/rotated
      // the image: those scrolls are the assistant's, not the user's.
      if (assistantBusy()) return;
      closeMenu();
    }, true);

    // ── Actions ──

    document.getElementById('ctx-copy-img').addEventListener('click', () => {
      closeMenu(); app.export.copyImageToClipboard();
    });

    document.getElementById('ctx-dl-img').addEventListener('click', () => {
      closeMenu(); app.export.saveImage();
    });

    // Share image — only revealed where the Web Share API can share files.
    const shareItem = document.getElementById('ctx-share-img');
    if (shareItem && supportsShareFiles()) shareItem.style.display = '';
    shareItem?.addEventListener('click', () => {
      closeMenu(); app.export.shareImage();
    });

    document.getElementById('ctx-dl-layout').addEventListener('click', () => {
      closeMenu(); app.export.downloadJSON();
    });

    document.getElementById('ctx-ul-layout').addEventListener('click', () => {
      closeMenu(); document.getElementById('upload-json').click();
    });

    document.getElementById('ctx-copy-layout').addEventListener('click', () => {
      closeMenu(); app.export.copyLayoutToClipboard();
    });

    document.getElementById('ctx-paste-img').addEventListener('click', async () => {
      closeMenu();
      try {
        const items = await navigator.clipboard.read();
        for (const item of items) {
          const imgType = item.types.find(t => t.startsWith('image/'));
          if (imgType) {
            if (app.image && !(await app.confirm('Replace current image with pasted image?', { title: 'Replace image', confirmIcon: 'paste' }))) {
              notify('Image paste canceled', 'fail');
              return;
            }
            const blob = await item.getType(imgType);
            const file = new File([blob], 'pasted-image.png', { type: imgType });
            app.loadImageFromFile(file);
            notify('Image pasted from clipboard', 'ok');
            return;
          }
        }
        notify('No image found in clipboard', 'fail');
      } catch (err) {
        notify('Clipboard read failed: ' + (err.message || err), 'fail');
      }
    });

    document.getElementById('ctx-paste-layout').addEventListener('click', async () => {
      closeMenu();
      try {
        const text = await navigator.clipboard.readText();
        if (!text) { notify('Clipboard is empty', 'fail'); return; }
        let data = null;
        try {
          data = JSON.parse(text);
        } catch {
          /* not JSON — left as null, the guard below notifies the user */
        }
        if (!data || !Array.isArray(data.lines)) {
          notify('Clipboard does not contain a layout JSON', 'fail');
          return;
        }
        app.export.applyPastedLayout(data);
      } catch (err) {
        notify('Clipboard read failed: ' + (err.message || err), 'fail');
      }
    });

    document.getElementById('ctx-draw-toggle').addEventListener('click', () => {
      closeMenu();
      if (app.isDrawing) app.stopDrawingMode();
      else if (app.image) app.startDrawingMode();
    });

    document.getElementById('ctx-drawmode-toggle').addEventListener('click', () => {
      closeMenu();
      app.setDrawMode(app.drawMode === 'rect' ? 'line' : 'rect');
      app.storage.save();
      notify('Drawing mode: ' + (app.drawMode === 'rect' ? 'Rectangle' : 'Line'), 'info');
    });

    // Instant draw rectangle: switch to rect mode and start drawing right away.
    // If a line is selected, the next rect connects to it (one-shot).
    document.getElementById('ctx-draw-rect').addEventListener('click', () => {
      closeMenu();
      if (!app.image) { notify('Load an image first', 'fail'); return; }
      app.setDrawMode('rect');
      app.startDrawingMode(); // continues the selected line if one is selected
      notify('Drag to draw a rectangle', 'info');
    });

    document.getElementById('ctx-show-points').addEventListener('click', () => {
      app.showPoints = !app.showPoints;
      const cb = document.getElementById('show-points');
      if (cb) cb.checked = app.showPoints;
      app.renderer.redraw(); app.storage.save();
      document.getElementById('ctx-chk-points').innerHTML = app.showPoints ? icon('check', { size: 14 }) : '';
    });

    document.getElementById('ctx-show-lines').addEventListener('click', () => {
      app.showLines = !app.showLines;
      const cb = document.getElementById('show-lines');
      if (cb) cb.checked = app.showLines;
      app.renderer.redraw(); app.storage.save();
      document.getElementById('ctx-chk-lines').innerHTML = app.showLines ? icon('check', { size: 14 }) : '';
    });

    document.getElementById('ctx-clear-lines').addEventListener('click', () => {
      closeMenu();
      app.clearAllLines();
    });

    // Point size — live on input, commit on change
    document.getElementById('ctx-point-size').addEventListener('input', e => {
      const v = parseInt(e.target.value);
      if (!isNaN(v) && v >= 1 && v <= 30) {
        app.pointSize = v;
        const inp = document.getElementById('point-size');
        if (inp) inp.value = v;
        app.renderer.redraw();
      }
    });
    document.getElementById('ctx-point-size').addEventListener('change', e => {
      const v = Math.max(1, Math.min(30, parseInt(e.target.value) || app.pointSize));
      e.target.value = v; app.pointSize = v;
      const inp = document.getElementById('point-size');
      if (inp) inp.value = v;
      app.renderer.redraw(); app.storage.save();
    });

    // Line thickness — live on input, commit on change
    document.getElementById('ctx-thickness').addEventListener('input', e => {
      const v = parseInt(e.target.value);
      if (!isNaN(v) && v >= 1 && v <= 20) {
        app.thickness = v;
        const inp = document.getElementById('line-thickness');
        if (inp) inp.value = v;
        app.renderer.redraw();
      }
    });
    document.getElementById('ctx-thickness').addEventListener('change', e => {
      const v = Math.max(1, Math.min(20, parseInt(e.target.value) || app.thickness));
      e.target.value = v; app.thickness = v;
      const inp = document.getElementById('line-thickness');
      if (inp) inp.value = v;
      app.renderer.redraw(); app.storage.save();
    });

    document.querySelectorAll('input[name="ctxLineStyle"]').forEach(r => {
      r.addEventListener('change', () => {
        app.style = r.value;
        const sel = document.getElementById('line-style');
        if (sel) sel.value = r.value;
        app.renderer.redraw(); app.storage.save();
      });
    });

    document.querySelectorAll('input[name="ctxFilter"]').forEach(r => {
      r.addEventListener('change', () => {
        app.imageFilter = r.value;
        const sel = document.getElementById('image-filter');
        if (sel) sel.value = r.value;
        const mainPicker = document.getElementById('filter-color');
        if (mainPicker) mainPicker.style.display = (r.value === 'custom') ? 'inline-block' : 'none';
        document.getElementById('ctx-tint-row').classList.toggle('ctx-tint-visible', r.value === 'custom');
        app.renderer.redraw(); app.storage.save();
      });
    });

    let ctxTintTimer = null;
    document.getElementById('ctx-tint-color').addEventListener('input', e => {
      app.filterColor = e.target.value;
      const mainPicker = document.getElementById('filter-color');
      if (mainPicker) mainPicker.value = e.target.value;
      clearTimeout(ctxTintTimer);
      ctxTintTimer = setTimeout(() => {
        if (app.imageFilter === 'custom') { app.renderer.redraw(); app.storage.save(); }
      }, TINT_DEBOUNCE_MS);
    });

    document.getElementById('ctx-fullscreen').addEventListener('click', () => {
      closeMenu();
      if (typeof app.toggleFullscreen === 'function') app.toggleFullscreen();
    });

    document.getElementById('ctx-fit-window').addEventListener('click', () => {
      closeMenu();
      if (app && app.zoomPan && typeof app.zoomPan.fitToWindow === 'function') app.zoomPan.fitToWindow();
    });

    document.getElementById('ctx-tt-enabled').addEventListener('change', e => {
      app.tooltipEnabled = e.target.checked;
      if (!app.tooltipEnabled) app.tooltipMgr.hide();
      app.storage.save();
    });
    document.getElementById('ctx-tt-page').addEventListener('change', e => {
      app.tooltipShowPage = e.target.checked;
      app.storage.save();
    });
    document.getElementById('ctx-tt-screen').addEventListener('change', e => {
      app.tooltipShowScreen = e.target.checked;
      app.storage.save();
    });
    document.getElementById('ctx-tt-coords').addEventListener('change', e => {
      app.tooltipShowCoords = e.target.checked;
      app.storage.save();
    });

    // Transformation submenu: formulas
    const ctxSyncFormulaUI = checked => {
      const fi = document.getElementById('formula-inputs');
      const ctxFi = document.getElementById('ctx-formula-inputs');
      const mainCb = document.getElementById('allow-formulas');
      if (fi) fi.style.display = checked ? 'inline-flex' : 'none';
      if (ctxFi) ctxFi.style.display = checked ? 'block' : 'none';
      if (mainCb) mainCb.checked = checked;
    };
    document.getElementById('ctx-allow-formulas').addEventListener('change', e => {
      app.allowFormulas = e.target.checked;
      ctxSyncFormulaUI(e.target.checked);
      if (!e.target.checked) { app.formulaX = ''; app.formulaY = ''; app.settings.showFormulaError(false); }
      app.settings.refreshFormulaCoords();
      app.storage.save();
    });
    // Same debounced commit as the toolbar pair, mirroring the other way — one wiring, so
    // both entry points flag errors, persist and sync to peers identically.
    app.settings.wireFormulaInputs({
      x: 'ctx-formula-x', y: 'ctx-formula-y', mirrorX: 'formula-x', mirrorY: 'formula-y',
    });
    menu.addEventListener('mousedown', e => e.stopPropagation());

    // ── Assistant entry: a submenu parent whose flyout is a chat ────────────
    // The flyout is NOT a list of .ctx-items, so none of the menu's "activate →
    // closeMenu()" wiring applies: typing, sending, stopping and executing a plan
    // all leave the menu (and the flyout) open.
    let assistWired = false;
    const wireAssistant = () => {
      const item = document.getElementById('ctx-assist-menu');
      const flyout = document.getElementById('ctx-assist-sub');
      const transcript = document.getElementById('ctx-assist-transcript');
      const input = document.getElementById('ctx-assist-input');
      const sendBtn = document.getElementById('ctx-assist-send');
      const attachBtn = document.getElementById('ctx-assist-attach-btn');
      const attachInput = document.getElementById('ctx-assist-attach-input');
      const gearBtn = document.getElementById('ctx-assist-settings-btn');
      const statusDot = document.getElementById('ctx-assist-status-dot');
      const attachList = document.getElementById('ctx-assist-attachments');
      if (!item || !flyout || !transcript || !input || !sendBtn) return;

      let turnAbort = null;
      // The shared send ↔ Stop swap (the panel's contract).
      const updateControls = () => syncComposerControls({ sendBtn, attachBtn, input }, assistSending,
        { attachFull: (peekChatController(app)?.attachments.length ?? 0) >= MAX_ATTACHMENTS });

      // ── Attachments: feed the SAME shared controller, so a file queued here rides
      // the next turn from either surface (both rows repaint on the change event). ──
      const renderAttachments = () => chatAttachmentChips(attachList, peekChatController(app));
      window.addEventListener(CHAT_ATTACHMENTS_EVENT, renderAttachments);
      renderAttachments();

      // ── Settings gear: opens the assistant settings modal through the panel's own
      // gear (one modal, one wiring) — and CLOSES the menu first, because a modal
      // behind a popup menu is unusable. Its dot mirrors the shared probe. ──
      const setDot = (probe) => { statusDot.className = `conn-status conn-status-${probeStatusClass(probe)}`; };
      const refreshDot = () => {
        const settings = loadLlmSettings();
        const known = cachedProbe(settings);
        if (known) { setDot(known); return; }
        setDot(null);   // amber while in flight
        probeProvider(settings, { getToken: (url) => serverBearerToken(app, url) })
          .then((probe) => { cacheProbe(settings, probe); setDot(probe); })
          .catch(() => setDot({ ok: false }));
      };
      gearBtn.addEventListener('click', () => {
        closeMenu();
        document.getElementById('chat-settings-btn')?.click();
      });
      // Refresh the dot when the flyout is about to open (free when the panel already
      // probed — see the shared cache). No dot to paint in plain mode.
      item.addEventListener('mouseenter', () => { if (item.dataset.noSub !== '1') refreshDot(); });
      // "Engaged" = typing in the flyout, resizing its composer, or a running turn —
      // the hover-out timers must not yank the chat away then (a SIBLING submenu
      // parent still closes it). keepSubOpen consults this predicate.
      let resizing = false;
      // The row menu floats on the body OVER the flyout, so while it shows the
      // pointer reads as "left" — that must not close the chat under it.
      flyout._keepOpen = () => assistSending || resizing || chatRowMenuOpen() || flyout.contains(document.activeElement);

      // ── Resizable composer: the panel's slider strip. The flyout is a fixed-height
      // column, so growing the input takes room from the transcript; re-place it anyway
      // (its height can hit the viewport clamp) — the ROOT menu is never re-placed. ──
      wireInputSizer(document.getElementById('ctx-assist-sizer'), input, {
        hold: (on) => { resizing = on; },
        onDrag: () => { if (flyout.classList.contains('ctx-sub-visible')) positionSub(item, flyout); },
      });

      // ── Transcript: the SHARED row log, exactly like the panel's — both surfaces
      // render the same rows in the same order, and this one shows the history that
      // happened while the menu was closed. ──
      const paint = () => renderChatLog(transcript, chatLog(), {
        onConfigure: closeMenu,   // the CTA opens the settings modal; the menu must go
        // An expired collaboration-server session is fixed in Connections, not in the
        // provider settings — and the menu closes for that modal just the same.
        onReconnect: () => { closeMenu(); document.getElementById('connect-btn')?.click(); },
        // §11: this surface renders the SAME shared log, so its choice cards must answer
        // too — otherwise a card shown here would be inert while the panel's works.
        onAskSubmit: (answer) => { runTurn(answer).catch(() => { /* shown in the transcript */ }); },
        // Retry on a failed turn: the SAME text through the normal send path.
        onRetry: (text) => {
          // Never on top of a running turn — panel parity, and the shared flag catches
          // one started from the panel too (that is what logged the prompt twice).
          if (assistSending || chatTurnInFlight()) return;
          // The failed turn's attachments ride the retry too (panel parity).
          peekChatController(app)?.requeueLastTurnAttachments?.();
          runTurn(text).catch(() => { /* shown in the transcript */ });
        },
      });
      onChatLog(paint);
      paint();
      // Anything clicked in here (send, a chip, "open as the working image") may make
      // the editor relayout — hold the scroll grace open across it.
      flyout.addEventListener('click', () => { assistBusyUntil = Date.now() + ASSIST_SCROLL_GRACE_MS; });
      // Clicking the parent opens the flyout too (hover is the primary affordance,
      // like the other parents) and drops the caret into the input.
      item.addEventListener('click', (e) => {
        if (flyout.contains(e.target)) return;
        if (item.dataset.noSub === '1') {
          // Phones/touch: no flyout here — hand over to the (modal) chat panel.
          closeMenu();
          openChatPanel();
          return;
        }
        if (!flyout.classList.contains('ctx-sub-visible')) {
          positionSub(item, flyout);
          activeSub = flyout;
          activeSubItem = item;
        }
        input.focus();
      });

      // The shared logged-turn frame; only the deltas below are this surface's own.
      const runTurn = async (text) => {
        assistSending = true;
        updateControls();
        await runLoggedChatTurn(sharedChatController(app), text, {
          settings: loadLlmSettings(),
          begin: (abort) => { turnAbort = abort; },
          // A turn that lands after the menu was dismissed must not vanish silently.
          onResult: (res) => {
            // The SAME balloon the panel builds — and a way back: reopening this flyout
            // would need the menu at its old point, so the click opens the docked
            // panel, which holds the very same conversation.
            const toast = closedTurnToast(res);
            if (!menuIsOpen() && toast) notify(toast.text, toast.type, { onClick: () => app.chat?.open() });
          },
          cleanup: () => {
            assistSending = false;
            // The plan's last relayout can still be settling — keep the grace window
            // open a moment past the turn.
            assistBusyUntil = Date.now() + ASSIST_SCROLL_GRACE_MS;
            turnAbort = null;
            updateControls();
            renderAttachments();          // the turn consumed the queue…
            notifyAttachmentsChanged();   // …so the panel's row drops them too
          },
        });
      };

      // Escape bubbles out of the composer on purpose (the document listener closes
      // the menu); every other key stays inside. Global hotkeys ignore typing targets.
      wireChatMoreMenu('ctx-assist', document, { onOpen: () => {
        updateControls();
        const clr = document.getElementById('ctx-assist-clear');
        if (clr) clr.disabled = !!transcript.querySelector('.chat-empty');
      } });   // attach / clear / settings behind the …
      // Clear from THIS surface clears the shared conversation, exactly like the
      // panel's own item (one controller, one log — both surfaces repaint).
      document.getElementById('ctx-assist-clear')?.addEventListener('click', () => {
        if (assistSending) return;
        clearSharedConversation(app);
      });
      wireChatComposer({ input, sendBtn, attachBtn, attachInput }, {
        isSending: () => assistSending,
        abort: () => turnAbort?.abort(),
        submit: (text) => { updateControls(); runTurn(text); },
        attachFiles: async (files) => {
          await queueAttachments(sharedChatController(app), files,
            (err) => notify(`Attachment failed — ${err.message}`, 'fail'));
          renderAttachments();
          notifyAttachmentsChanged();
        },
        onInput: updateControls,
      });
      // Suggestion chips prefill the input (editable before sending) — the same shared
      // delegated wiring as the panel, so a rebuilt empty state stays clickable.
      wireChatSuggestions(transcript, (prompt) => {
        input.value = prompt;
        updateControls();
        input.focus();
      });
      // Right-click on a transcript row: the SHARED row menu (panel parity).
      // Insert appends into THIS flyout's composer; Resend re-queues the row's
      // original attachments and re-sends through the same runTurn path.
      wireChatRowMenu(transcript, {
        onInsert: (text) => {
          input.value = input.value ? `${input.value}\n${text}` : text;
          updateControls();
          input.focus();
        },
        onResend: (text, attachments) => {
          if (assistSending || chatTurnInFlight()) return;
          requeueRowAttachments(sharedChatController(app), attachments);
          renderAttachments();
          notifyAttachmentsChanged();
          runTurn(text).catch(() => { /* shown in the transcript */ });
        },
      });
      updateControls();
    };

    // The phone/touch fallback: close the menu and open the chat panel (a full-screen
    // modal there), which continues the very same conversation.
    const openChatPanel = () => {
      if (typeof app?.chat?.open === 'function') app.chat.open();
      else document.getElementById('chat-btn')?.click();
    };

    // Show/hide (and, if the provider was configured after load, BUILD) the Assistant
    // entry, and pick its mode. Runs on every open and on LLM-settings changes, so the
    // assistant switching on/off — and a resize between opens — needs no reload.
    const syncAssistant = () => {
      const on = assistantEnabled(loadLlmSettings());
      let item = document.getElementById('ctx-assist-menu');
      if (on && !item) {
        // Built directly above the Drawing group and wired exactly like the static
        // parents (those were wired in the loop at the top of wire()).
        const anchor = document.getElementById('ctx-draw-toggle');
        if (anchor) anchor.insertAdjacentHTML('beforebegin', assistantItemHtml());
        else menu.insertAdjacentHTML('beforeend', assistantItemHtml());
        item = document.getElementById('ctx-assist-menu');
        const sub = item?.querySelector(':scope > .ctx-sub');
        if (item && sub) wireSubmenu(item, sub);
      }
      if (!item) return;
      // Built once, then only hidden — so the menu session's transcript survives a
      // provider switch (and the wiring is never duplicated). It owns no separator,
      // so hiding it leaves the menu's original grouping exactly as it was.
      item.style.display = on ? '' : 'none';
      if (on && !assistWired) { assistWired = true; wireAssistant(); }
      // Plain (no flyout) on phones and coarse pointers: no caret, click opens the
      // panel (the app-wide touch rule, utils.js — a hover flyout needs a hover).
      const plain = isTouchLike();
      item.dataset.noSub = plain ? '1' : '0';
      item.classList.toggle('ctx-assist-plain', plain);
      item.title = plain ? 'Assistant — opens the chat panel' : '';
      if (plain && document.getElementById('ctx-assist-sub')?.classList.contains('ctx-sub-visible')) {
        closeAllSubs();   // a resize while open collapses the flyout instead of stranding it
      }
    };
    window.addEventListener('stencil:llm-settings-changed', syncAssistant);
    // A resize WHILE the menu is open re-evaluates the mode (and drops a flyout that
    // no longer fits) — the next open re-evaluates anyway.
    window.addEventListener('resize', () => { if (menuIsOpen()) syncAssistant(); });
    syncAssistant();

    // Format the data-hk shortcut spans (hardcoded "Ctrl+C"/"Alt+J" in the markup) right away
    // so macOS shows ⌘/⌥ from the very first paint — not only after the menu is first opened
    // (which is when syncState's poll would otherwise be the first to call updateCtxHints).
    hotkeys.updateCtxHints();
  }
}
define('stencil-context-menu', StencilContextMenu);
