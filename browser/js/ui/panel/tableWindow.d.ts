// The table shape of a RowWindow: striped <tr> rows in the panel's #coord-body scroller.
import type { RowWindow, RowWindowOptions } from './rowWindow.js';

export type TableWindowOptions = Omit<RowWindowOptions, 'shape'> & { columns: number };

export declare const makeTableWindow: (body: HTMLTableSectionElement, opts: TableWindowOptions) => RowWindow;
