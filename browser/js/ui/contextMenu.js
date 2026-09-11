import { StencilElement, hostTag, define } from './base.js';
import { notify, setRadioGroup, setHtml, formatCombo, supportsShareFiles, isTouchLike, pointInRect, hasAnyLines, isTypingTarget, onWindowResize } from '../utils.js';
import { hotkeys } from '../core/hotkeys.js';
import { icon } from './icons.js';
import { attachVoiceDust } from './voiceDust.js';
import { loadLlmSettings, serverBearerToken } from '../llm/llmSettings.js';
import { probeProvider } from '../llm/llmClient.js';
import { MAX_ATTACHMENTS } from '../llm/chatController.js';
import {
  sharedChatController, peekChatController, runLoggedChatTurn, closedTurnToast, queueAttachments, ATTACHMENT_CAP_NOTICE,
  cacheProbe, cachedProbe, probeStatusClass,
  chatLog, onChatLog, clearSharedConversation, requeueRowAttachments, chatTurnInFlight,
} from '../llm/chatSession.js';
import {
  renderChatLog, chatAttachmentChips, wireInputSizer, wireChatSuggestions, wireChatMoreMenu, wireChatSideToggle,
  chatSuggestionsHtml, chatComposerActionsHtml, syncComposerControls, wireChatComposer, wireComposerVoice,
  notifyAttachmentsChanged, CHAT_ATTACHMENTS_EVENT, wireChatRowMenu, chatRowMenuOpen,
} from './chatView.js';
import { menuPopOrigin, surfaceIn, surfaceOut, settleSurface, motionReduced, revealControls,
         SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from './motion.js';
import { setChecked, swapCheckGlyph } from './controlSwap.js';
import { wireAltPreview, hideExportPreview, clearAltPreviewHover } from './exportPreview.js';
import { keysHtml } from './tipContent.js';
import { ctxArrow } from './ctxArrow.js';
import { assistantEnabled, assistantItemHtml } from './ctxAssistantItem.js';
import { wireCtxAssistant } from './ctxAssistant.js';
import { subscribe, EVENTS } from '../bus/appBus.js';
import { EXPORT_VARIANTS, EXPORT_VARIANT_LABELS, EXPORT_VARIANT_ICONS,
         exportVariantState } from './exportVariants.js';
export { assistantEnabled, assistantItemHtml };
// Keyboard navigation lives in ctxKeyboard.js; its three helpers stay reachable here.
export { ctxKeyStep, CTX_NAV_KEYS, ctxFocusables } from './ctxKeyboard.js';
import { wireCtxKeyboard } from './ctxKeyboard.js';
import { contextMenuInner } from './contextMenuMarkup.js';
import { createCtxNav } from './contextMenuNav.js';

// ── Component: custom right-click context menu ──────────────────
const SUBMENU_HIDE_DELAY_MS = 180; // grace period before a submenu closes on mouseleave
const LIVE_SYNC_INTERVAL_MS = 120; // poll cadence to reflect external state while the menu is open
const TINT_DEBOUNCE_MS = 80;       // debounce custom-tint recolor+save while dragging the picker
const ASSIST_SCROLL_GRACE_MS = 900; // window in which an assistant-caused scroll can't close the menu

export class StencilContextMenu extends StencilElement {
  static inner() { return contextMenuInner(); }
  static template() { return hostTag('stencil-context-menu', 'id="ctx-menu"', StencilContextMenu.inner()); }

  wire(app) {
    const menu = document.getElementById('ctx-menu');
    const canvas = document.getElementById('canvas');
    const viewport = document.getElementById('canvas-viewport');

    // ── Submenu navigation: opening, placing, hover-grace hiding — ui/contextMenuNav.js ──
    const {
      closeSub, closeAllSubs, positionSub, repositionActiveSub, hideSub, closeActiveSub,
      wireSubmenu, wirePlainItem, setPointerTracking, activeSub, setActiveSub,
      bindKbItem, setKbItem, setLastPointer,
    } = createCtxNav({ menu });

    // Keyboard navigation (desktop QMenu parity) — ui/ctxKeyboard.js. It walks the
    // same open-flyout state the hover path owns, so that is passed in, not copied.
    // Every callback is a thunk: menuIsOpen and friends are `const`s declared further
    // down wire(), so reading them at this call site would hit the temporal dead zone.
    bindKbItem(wireCtxKeyboard({
      menu,
      menuIsOpen: () => menuIsOpen(),
      chatRowMenuOpen: () => chatRowMenuOpen(),
      closeSub: (sub) => closeSub(sub),
      positionSub: (item, sub) => positionSub(item, sub),
      activeSub,
      setActiveSub,
    }).setKbItem);

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

    // The assistant flyout's own teardown (its dictation, wired later) rides every close.
    let onMenuClose = () => {};
    const closeMenu = () => {
      onMenuClose();
      // Into the very point it grew out of (js/ui/motion.js) — measured while it is
      // still on screen, and only if it IS: closeMenu is also the idle teardown.
      if (menu.classList.contains('ctx-open') && !motionReduced()) surfaceOut(menu, openPoint, { ms: SURFACE_MENU_OUT_MS });
      else settleSurface(menu);
      menu.classList.remove('ctx-open');
      closeAllSubs();
      setKbItem(null);
      setPointerTracking(false);
      clearInterval(syncInterval);
      syncInterval = null;
      hideExportPreview();
      clearAltPreviewHover();
    };

    // This menu polls syncState every 120ms while open (syncInterval below); the shared
    // setHtml (utils.js) writes only on a real change, so a hovered row's glyph is never
    // rebuilt under the pointer (which would loop its once-per-hover CSS animation).
    const syncState = () => {
      if (!app) return;
      document.getElementById('ctx-draw-label').textContent =
        app.isDrawing ? 'Stop Drawing' : 'Start Drawing';
      setHtml(document.getElementById('ctx-draw-toggle').querySelector('.ctx-icon'),
        app.isDrawing ? icon('stop', { size: 14 }) : icon('play', { size: 14 }));

      // A row whose action isn't available right now is HIDDEN, not greyed out — the
      // context menu carries no tooltip of its own (item 5) to explain a disabled row,
      // so a grey one just read as broken. Hiding removes the row instead of leaving a
      // dead one to puzzle over; the menu is rebuilt fresh (syncState) on every open and
      // live-synced while it's up, so a row reappears the moment its action is possible.
      const gate = (id, off) => {
        const el = document.getElementById(id);
        if (!el) return;
        el.style.display = off ? 'none' : '';
      };

      const hasLines = hasAnyLines(app);
      gate('ctx-dl-layout', !hasLines);
      gate('ctx-copy-layout', !hasLines);
      gate('ctx-clear-lines', !hasLines);

      const hasImage = !!app.image;
      gate('ctx-copy-img', !hasImage);
      gate('ctx-dl-img', !hasImage);
      // Variant-row availability and the primary-combo owner come from the shared
      // registry (exportVariants.js — the "With Compare is a FOURTH row" rule included).
      const variantState = exportVariantState(app);
      for (const v of EXPORT_VARIANTS) {
        gate(`ctx-copy-img-${v}`, !variantState.show[v]);
        gate(`ctx-dl-img-${v}`, !variantState.show[v]);
      }
      const splitPrimary = variantState.primary === 'split';
      const copyCombo = keysHtml(formatCombo(hotkeys.get('copyImage'), hotkeys.isMac), hotkeys.isMac);
      const dlCombo = keysHtml(formatCombo(hotkeys.get('saveImage'), hotkeys.isMac), hotkeys.isMac);
      setHtml(document.getElementById('ctx-copy-img-split-hk'), splitPrimary ? copyCombo : '');
      setHtml(document.getElementById('ctx-copy-img-current-hk'), splitPrimary ? '' : copyCombo);
      setHtml(document.getElementById('ctx-dl-img-split-hk'), splitPrimary ? dlCombo : '');
      setHtml(document.getElementById('ctx-dl-img-current-hk'), splitPrimary ? '' : dlCombo);
      // Share item is rendered only where Web Share files are supported (see wire()).
      gate('ctx-share-img', !hasImage);
      // Paste-layout needs an image too (matching paste handler behavior)
      gate('ctx-paste-layout', !hasImage);

      // Refresh hotkey hint text in case shortcuts were remapped
      hotkeys.updateCtxHints();
      // The draw hotkey span carries data-hk="startDraw"; override it after
      // updateCtxHints so it reflects the start/stop binding for the live state.
      const drawCombo = hotkeys.get(app.isDrawing ? 'stopDraw' : 'startDraw');
      setHtml(document.getElementById('ctx-draw-hotkey'),
        keysHtml(formatCombo(drawCombo, hotkeys.isMac), hotkeys.isMac));

      // Checkmarks
      setHtml(document.getElementById('ctx-chk-points'), app.showPoints ? icon('check', { size: 14 }) : '');
      setHtml(document.getElementById('ctx-chk-lines'), app.showLines  ? icon('check', { size: 14 }) : '');

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
      setHtml(document.getElementById('ctx-fullscreen').querySelector('.ctx-icon'), icon(isFS ? 'minimize' : 'maximize'));

      // Tooltip checkboxes
      document.getElementById('ctx-tt-enabled').checked = app.tooltipEnabled;
      document.getElementById('ctx-tt-page').checked = app.tooltipShowPage;
      document.getElementById('ctx-tt-screen').checked = app.tooltipShowScreen;
      document.getElementById('ctx-tt-coords').checked = app.tooltipShowCoords;
      // Formula checkbox
      document.getElementById('ctx-allow-formulas').checked = app.allowFormulas;
      revealControls(document.getElementById('ctx-formula-inputs'), app.allowFormulas, 'block');
    };

    const menuIsOpen = () => menu.classList.contains('ctx-open');

    // Assistant turn state, read by the scroll-close guard below: while the chat is
    // answering (and for a grace window after), the canvas viewport scrolls caused by
    // the executing plan must NOT dismiss the menu the user is chatting in.
    let assistSending = false;
    let assistBusyUntil = 0;
    // Assigned by wireCtxAssistant below; openAt only runs long after wire() has.
    let syncAssistant = () => {};
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
      // The entry pop (animations/overlays.css menuPop) grows out of the click point.
      menu.style.transformOrigin = menuPopOrigin(x, y, { left, top, width: mw, height: mh });
    };

    const openAt = (x, y) => {
      openPoint = { x, y };
      closeAllSubs();
      setKbItem(null);
      // Track the pointer only while open; seed it with the click point so the
      // idle checks never compare against a stale position from a previous open.
      setPointerTracking(true);
      setLastPointer(x, y);
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
      if (!motionReduced()) surfaceIn(menu, openPoint, { ms: SURFACE_MENU_IN_MS });
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
        // On macOS Ctrl+click IS the secondary click, so the Alt+Ctrl pull-out drag fires
        // this too. Alt with it means the gesture, not a menu: swallow the event, while a
        // plain Ctrl+click still gets one.
        e.preventDefault();
        if (e.altKey) return;
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

    // Copy Image / Download Image are now nested submenu parents — wire their
    // flyouts explicitly (the top-level loop above only wires DIRECT menu children).
    wireSubmenu(document.getElementById('ctx-copy-img'), document.getElementById('ctx-copy-img-sub'));
    wireSubmenu(document.getElementById('ctx-dl-img'), document.getElementById('ctx-dl-img-sub'));

    // Each variant row: click copies/downloads its OWN literal variant, Alt+hover
    // previews it — 'split' (With Compare, hidden unless comparing) and 'current' are
    // independent rows now, each always dispatching its own variant.
    const IMG_ACTIONS = [
      ['ctx-copy-img-', v => app.export.copyImageToClipboard(v)],
      ['ctx-dl-img-', v => app.export.saveImage(v)],
    ];
    for (const [prefix, run] of IMG_ACTIONS) {
      for (const v of EXPORT_VARIANTS) {
        const item = document.getElementById(`${prefix}${v}`);
        item.addEventListener('click', () => { closeMenu(); run(v); });
        wireAltPreview(item, app, v);
      }
    }

    // Share image — only revealed where the Web Share API can share files.
    const shareItem = document.getElementById('ctx-share-img');
    if (shareItem && supportsShareFiles()) shareItem.style.display = '';
    // shareImage() FIRST, closeMenu() after: the Web Share API needs the call to land
    // squarely inside the click's user-activation window, and the menu's own close
    // flight (surfaceOut, js/ui/motion.js) is dead weight ahead of it that every other
    // browser doesn't have to clear — the toolbar's plain #share-image button (no menu
    // to close) calls shareImage() alone. Same order here removes that one difference.
    shareItem?.addEventListener('click', () => {
      app.export.shareImage(); closeMenu();
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
              notify('Image paste canceled', 'info');
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

    // Instant draw line / rectangle: switch to that mode and start drawing right away.
    // If a line is selected, the next shape connects to it (one-shot).
    document.getElementById('ctx-draw-line').addEventListener('click', () => {
      closeMenu();
      if (!app.image) { notify('Load an image first', 'fail'); return; }
      app.setDrawMode('line');
      app.startDrawingMode(); // continues the selected line if one is selected
      notify('Drag to draw a line', 'info');
    });

    document.getElementById('ctx-draw-rect').addEventListener('click', () => {
      closeMenu();
      if (!app.image) { notify('Load an image first', 'fail'); return; }
      app.setDrawMode('rect');
      app.startDrawingMode(); // continues the selected line if one is selected
      notify('Drag to draw a rectangle', 'info');
    });

    document.getElementById('ctx-show-points').addEventListener('click', () => {
      app.showPoints = !app.showPoints;
      setChecked(document.getElementById('show-points'), app.showPoints);
      app.renderer.redraw(); app.storage.save();
      swapCheckGlyph(document.getElementById('ctx-chk-points'), app.showPoints ? icon('check', { size: 14 }) : '');
    });

    document.getElementById('ctx-show-lines').addEventListener('click', () => {
      app.showLines = !app.showLines;
      setChecked(document.getElementById('show-lines'), app.showLines);
      app.renderer.redraw(); app.storage.save();
      swapCheckGlyph(document.getElementById('ctx-chk-lines'), app.showLines ? icon('check', { size: 14 }) : '');
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

    // Transformation submenu: formulas — the shared sync (settingsController.js), so the
    // toolbar pill's .on class stays in step too.
    document.getElementById('ctx-allow-formulas').addEventListener('change', e => {
      app.allowFormulas = e.target.checked;
      app.settings.syncFormulaUI(e.target.checked);
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

    // The Assistant entry (a submenu parent whose flyout IS a chat) lives in
    // ctxAssistant.js; it drives the menu's own state through this narrow host.
    ({ syncAssistant } = wireCtxAssistant(app, {
      menu,
      menuIsOpen, closeMenu, closeAllSubs, positionSub, wireSubmenu,
      sending: () => assistSending,
      setSending: (v) => { assistSending = v; },
      bumpBusy: () => { assistBusyUntil = Date.now() + ASSIST_SCROLL_GRACE_MS; },
      setActiveSub,
      setOnMenuClose: (fn) => { onMenuClose = fn; },
    }));

    // Format the data-hk shortcut spans (hardcoded "Ctrl+C"/"Alt+J" in the markup) right away
    // so macOS shows ⌘/⌥ from the very first paint — not only after the menu is first opened
    // (which is when syncState's poll would otherwise be the first to call updateCtxHints).
    hotkeys.updateCtxHints();
  }
}
define('stencil-context-menu', StencilContextMenu);
