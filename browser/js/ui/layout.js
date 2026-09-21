import { StencilContextMenu } from './contextMenu/contextMenu.js';
import { StencilFullscreenLayer } from './fullscreen/fullscreenLayer.js';
import { StencilDropOverlay } from './dropOverlay.js';
import { StencilAppContainer } from './appContainer.js';
import { StencilNotifications } from './notifications.js';
import { StencilSettingsModal } from './settingsModal.js';
import { StencilVisualsModal } from './visuals/visualsModal.js';
import { StencilInfoModal } from './meta/infoModal.js';
import { StencilProjectsModal } from './projectsModal.js';
import { StencilExpirationModal } from './meta/expirationModal.js';
import { StencilOpenImageModal } from './openImage/modal.js';
import { StencilOpenInModal } from './openInModal.js';
import { StencilLinksModal } from './meta/linksModal.js';
import { StencilConnectModal } from './connect/connectModal.js';
import { StencilCropModal } from './cropModal.js';
import { StencilConfirmModal } from './modal/confirmModal.js';
import { StencilInstall } from './installButton.js';
import { StencilChatPanel } from './chat/chatPanel.js';
import { StencilLlmSettingsModal } from './llmSettings/llmSettingsModal.js';
import { StencilDescriptionModal } from './meta/descriptionModal.js';
import { StencilKeywordsModal } from './meta/keywordsModal.js';
import { StencilScriptModal } from './script/scriptModal.js';
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
  StencilInstall,
  // New regions append at the END only (the order above is load-bearing).
  StencilChatPanel,
  StencilLlmSettingsModal,
  StencilDescriptionModal,
  StencilKeywordsModal,
  StencilScriptModal,
];
export const layout = () => REGIONS.map((r) => r.template()).join('');
