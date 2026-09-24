import { StencilContextMenu } from './contextMenu/contextMenu.js';
import { StencilFullscreenLayer } from './fullscreen/layer.js';
import { StencilDropOverlay } from './canvas/dropOverlay.js';
import { StencilAppContainer } from './shell/appContainer.js';
import { StencilNotifications } from './shell/notifications.js';
import { StencilSettingsModal } from './settings/modal.js';
import { StencilVisualsModal } from './visuals/modal.js';
import { StencilInfoModal } from './meta/infoModal.js';
import { StencilProjectsModal } from './projects/window/projectsModal.js';
import { StencilExpirationModal } from './meta/expirationModal.js';
import { StencilOpenImageModal } from './openImage/modal.js';
import { StencilOpenInModal } from './modal/openInModal.js';
import { StencilLinksModal } from './meta/linksModal.js';
import { StencilConnectModal } from './connect/modal.js';
import { StencilCropModal } from './modal/cropModal.js';
import { StencilConfirmModal } from './modal/confirmModal.js';
import { StencilChatPanel } from './chat/panel.js';
import { StencilLlmSettingsModal } from './llmSettings/modal.js';
import { StencilDescriptionModal } from './meta/descriptionModal.js';
import { StencilKeywordsModal } from './meta/keywordsModal.js';
import { StencilScriptModal } from './script/modal.js';
// Importing the modules registers every customElements.define; layout() concatenates each
// region's template() in this order, which is LOAD-BEARING (document body order).
const REGIONS = [
  StencilContextMenu,
  StencilFullscreenLayer,
  StencilDropOverlay,
  StencilAppContainer,
  StencilNotifications,
  StencilSettingsModal,
  StencilVisualsModal,
  StencilInfoModal,
  StencilProjectsModal,
  StencilExpirationModal,
  StencilOpenImageModal,
  StencilOpenInModal,
  StencilLinksModal,
  StencilConnectModal,
  StencilCropModal,
  StencilConfirmModal,
  // New regions append at the END only (the order above is load-bearing).
  StencilChatPanel,
  StencilLlmSettingsModal,
  StencilDescriptionModal,
  StencilKeywordsModal,
  StencilScriptModal,
];
export const layout = () => REGIONS.map((r) => r.template()).join('');
