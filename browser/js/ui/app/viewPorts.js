// The view pieces a DrawingApp is built over: the shell's nodes and html state, the viewport sync and
// the pointer controllers. index.js (the composition root) hands them to `new DrawingApp(viewPorts)`,
// so core/ names none of them.
import { shellNodes, htmlTheme, htmlAccent, htmlCustomAccent } from './shell.js';
import { wireViewportSync } from '../bindings/viewport/viewportSync.js';
import { InputController } from '../bindings/pointer/inputController.js';
import { PointerController } from '../bindings/pointer/controller.js';

export const viewPorts = Object.freeze({
  shellNodes, htmlTheme, htmlAccent, htmlCustomAccent, wireViewportSync, InputController, PointerController,
});
