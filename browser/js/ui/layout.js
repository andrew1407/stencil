import { StencilContextMenu } from './contextMenu.js';
import { StencilFullscreenLayer } from './fullscreenLayer.js';
import { StencilDropOverlay } from './dropOverlay.js';
import { StencilAppContainer } from './appContainer.js';
import { StencilNotifications } from './notifications.js';
import { StencilSettingsModal } from './settingsModal.js';
import { StencilVisualsModal } from './visualsModal.js';
import { StencilInfoModal } from './infoModal.js';
import { StencilProjectsModal } from './projectsModal.js';
import { StencilExpirationModal } from './expirationModal.js';
import { StencilOpenImageModal } from './openImage/modal.js';
import { StencilOpenInModal } from './openInModal.js';
import { StencilLinksModal } from './linksModal.js';
import { StencilConnectModal } from './connectModal.js';
import { StencilCropModal } from './cropModal.js';
import { StencilConfirmModal } from './confirmModal.js';
import { StencilInstall } from './installButton.js';
import { StencilChatPanel } from './chatPanel.js';
import { StencilLlmSettingsModal } from './llmSettingsModal.js';
import { StencilDescriptionModal } from './descriptionModal.js';
import { StencilKeywordsModal } from './keywordsModal.js';
import { StencilScriptModal } from './scriptModal.js';
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
