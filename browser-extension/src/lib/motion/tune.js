import { PARTICLE_STYLES } from '../dustCloud.js';
import { particleStyle } from '../motionPrefs.js';
// …as dustCloud.js's code (0 = dust, the flight as tabulated).
export const styleCode = () => PARTICLE_STYLES[particleStyle()] || 0;
