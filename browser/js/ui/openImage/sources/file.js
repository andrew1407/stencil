// The Open-Image window's Local file tab: the chooser and the name it shows. It owns the pick
// and announces it; what the window then previews is the window's business.
import { StencilElement, hostTag, define } from '../../base.js';
import { icon } from '../../icons.js';
import MEDIA_TYPES from '../../../config/mediaTypes.json' with { type: 'json' };

export class StencilOiFileSource extends StencilElement {
  // The chooser is OURS, not the platform's: a native file input's button cannot hold an
  // inline glyph, so it could never mime the folder the way every other control mimes its
  // action. The input stays (hidden) as the one that actually picks the file. Desktop twin:
  // the Choose File CTA and its read-only path field, joined into one control.
  static inner() {
    return `
                <div class="oi-panel" id="oi-panel-file" data-panel="file">
                    <div class="vs-row"><label>Choose</label>
                        <span class="oi-file">
                            <button type="button" id="open-image-choose" class="btn-icon-text oi-file-btn">${icon('folder', { size: 14 })}<span>Choose File</span></button>
                            <span class="oi-file-name" id="open-image-file-name">No file chosen</span>
                            <input type="file" id="open-image-file" accept="${MEDIA_TYPES.accept.imageOrVideo}" hidden>
                        </span>
                    </div>
                </div>`;
  }

  static template() {
    return hostTag('stencil-oi-file-source', 'style="display:contents"', StencilOiFileSource.inner());
  }

  get file() { const i = this.$('open-image-file'); return (i && i.files && i.files[0]) || null; }
  /** The control the tab hands the caret to. */
  get field() { return this.$('open-image-choose'); }

  reset() { this.$('open-image-file').value = ''; this.#showName(); }

  // The hidden input holds the pick; this span is what the reader sees.
  #showName() {
    const picked = this.file;
    const name = this.$('open-image-file-name');
    name.textContent = picked ? picked.name : 'No file chosen';
    name.classList.toggle('is-empty', !picked);
  }

  wire() {
    const input = this.$('open-image-file');
    // Bound to the NAME, not the box, or a click on the button would open the picker twice
    // (desktop twin: clickActivates on the dialog's path field).
    for (const el of [this.field, this.$('open-image-file-name')]) {
      el.addEventListener('click', () => input.click());
    }
    input.addEventListener('change', () => {
      this.#showName();
      this.emit('source-change', { kind: 'file', file: this.file });
    });
  }
}

define('stencil-oi-file-source', StencilOiFileSource);
