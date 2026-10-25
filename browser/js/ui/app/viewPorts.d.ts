// Shape of viewPorts.js — the view pieces index.js hands to `new DrawingApp(viewPorts)`.
import type { shellNodes, htmlTheme, htmlAccent, htmlCustomAccent } from './shell.js';
import type { wireViewportSync } from '../bindings/viewport/viewportSync.js';
import type { InputController } from '../bindings/pointer/inputController.js';
import type { PointerController } from '../bindings/pointer/controller.js';

export interface ViewPorts {
  readonly shellNodes: typeof shellNodes;
  readonly htmlTheme: typeof htmlTheme;
  readonly htmlAccent: typeof htmlAccent;
  readonly htmlCustomAccent: typeof htmlCustomAccent;
  readonly wireViewportSync: typeof wireViewportSync;
  readonly InputController: typeof InputController;
  readonly PointerController: typeof PointerController;
}

export declare const viewPorts: ViewPorts;
