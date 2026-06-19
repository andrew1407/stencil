import { StencilContextMenu } from './contextMenu.js';
import { StencilFullscreenLayer } from './fullscreenLayer.js';
import { StencilDropOverlay } from './dropOverlay.js';
import { StencilAppContainer } from './appContainer.js';
import { StencilNotifications } from './notifications.js';
import { StencilSettingsModal } from './settingsModal.js';
import { StencilVisualsModal } from './visualsModal.js';
import { StencilInfoModal } from './infoModal.js';
import { StencilProjectsModal } from './projectsModal.js';
import { StencilBlankImageModal } from './blankImageModal.js';
import { StencilLinksModal } from './linksModal.js';
import { StencilCropModal } from './cropModal.js';
import { StencilInstall } from './installButton.js';
// ── Top-level body composer (custom-element hosts, exact original body order) ──
// Importing the modules above registers every customElements.define. Each template()
// emits the host tag with markup inline, so layout() still produces the full static
// markup string (143 ids) the app and tests expect.
export const layout = () =>
  `${StencilContextMenu.template()}${StencilFullscreenLayer.template()}${StencilDropOverlay.template()}${StencilAppContainer.template()}${StencilNotifications.template()}${StencilSettingsModal.template()}${StencilVisualsModal.template()}${StencilInfoModal.template()}${StencilProjectsModal.template()}${StencilBlankImageModal.template()}${StencilLinksModal.template()}${StencilCropModal.template()}${StencilInstall.template()}`;
