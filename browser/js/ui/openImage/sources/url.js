// The Open-Image window's URL tab: the address, and the explicit Preview that fetches it.
import { StencilElement, hostTag, define } from '../../base.js';
import { icon } from '../../icons.js';
import { notify } from '../../../utils.js';

/** A half-typed URL never triggers a fetch: the explicit Preview button is gated on this. */
export const isPreviewableUrl = (v) => {
  if (!v) return false;
  try { return /^(https?:|data:|blob:)$/i.test(new URL(v).protocol); } catch { return false; }
};

export class StencilOiUrlSource extends StencilElement {
  // Preview is explicit (button / Enter) after validation — not on every keystroke — so a
  // half-typed URL never spins up a fetch.
  static inner() {
    return `
                <div class="oi-panel" id="oi-panel-url" data-panel="url" style="display:none">
                    <div class="vs-row vs-field"><label data-title="Load an image or video straight from the web">URL</label><input type="url" id="open-image-url" placeholder="https://… (image or video)"><button id="open-image-url-preview" class="btn-icon-text" type="button" data-title="Load a preview of this URL" disabled>${icon('image', { size: 14 })}<span>Preview</span></button></div>
                </div>`;
  }

  static template() {
    return hostTag('stencil-oi-url-source', 'style="display:contents"', StencilOiUrlSource.inner());
  }

  get url() { return this.$('open-image-url').value.trim(); }
  /** The control the tab hands the caret to. */
  get field() { return this.$('open-image-url'); }

  reset() { this.field.value = ''; this.#syncButton(); }

  #syncButton() { this.$('open-image-url-preview').disabled = !isPreviewableUrl(this.url); }

  #request() {
    if (!isPreviewableUrl(this.url)) {
      notify('Enter a valid image or video URL (http/https or data:).', 'fail');
      return;
    }
    this.emit('preview-request', { url: this.url });
  }

  wire() {
    const field = this.field;
    // Editing the URL retires the preview without taking it off the screen; the window decides.
    field.addEventListener('input', () => {
      this.#syncButton();
      this.emit('source-change', { kind: 'url', url: this.url });
    });
    field.addEventListener('keydown', (e) => {
      if (e.key !== 'Enter') return;
      e.preventDefault();
      this.#request();
    });
    this.$('open-image-url-preview').addEventListener('click', () => this.#request());
  }
}

define('stencil-oi-url-source', StencilOiUrlSource);
