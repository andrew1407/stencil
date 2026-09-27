// How DrawingApp takes on methods it does not write in its class body: the view seam
// (drawingApp.js VIEW_SEAM, the ui/ functions the core and the facade reach through the app) and
// the DOM-free mixin (app/editing.js). Every core function is imported and called by its callers.

// Written like class methods: non-enumerable, and named for the stack trace.
const define = (proto, name, fn) => {
  Object.defineProperty(fn, 'name', { value: name });
  Object.defineProperty(proto, name, { value: fn, writable: true, configurable: true });
};

// name → function(app, ...args); every target's own defaults stand in for an omitted argument.
export const installDelegates = (proto, functions) => {
  for (const [name, fn] of Object.entries(functions))
    define(proto, name, function (...args) { return fn(this, ...args); });
};

// A class whose methods belong on the app: copied across as they are, `this` and all.
export const installMethods = (proto, ...classes) => {
  for (const cls of classes)
    for (const name of Object.getOwnPropertyNames(cls.prototype))
      if (name !== 'constructor') Object.defineProperty(proto, name, Object.getOwnPropertyDescriptor(cls.prototype, name));
};
