import MOTION from '../../config/motion.json' with { type: 'json' };
import { particleStyle } from '../motionPrefs.js';
import { PARTICLE_STYLES } from '../dust/dustCloud.js';
// config/motion.json is the one home for the tuning.
export const TUNE = MOTION.ui;

// The particle style as dustCloud.js's code (0 = dust); read once per cloud, when built.
export const styleCode = () => PARTICLE_STYLES[particleStyle()] || 0;
