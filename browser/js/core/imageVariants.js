// File-name suffix + clipboard/toast label per export variant. Read by the download
// (core/exportService.js) and by the clipboard copy (core/clipboardExport.js), so the
// two can never name the same variant differently.
export const VARIANT_META = {
    current:  { suffix: '',          copyLabel: 'Image copied to clipboard' },
    original: { suffix: '-original', copyLabel: 'Original image copied to clipboard' },
    tint:     { suffix: '-tint',     copyLabel: 'Tinted image copied to clipboard' },
    split:    { suffix: '-split',    copyLabel: 'Split image copied to clipboard' },
};
