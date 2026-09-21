import { PARTICLE_STYLES } from '../dust/cloud.js';
import { particleStyle } from '../prefs/motionPrefs.js';
// …as cloud.js's code (0 = dust, the flight as tabulated).
export const styleCode = () => PARTICLE_STYLES[particleStyle()] || 0;
