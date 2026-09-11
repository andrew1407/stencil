import MOTION from '../../config/motion.json' with { type: 'json' };
import { particleStyle } from '../motionPrefs.js';
import { PARTICLE_STYLES } from '../dustCloud.js';
// Every duration, count and easing below: config/motion.json, the one home for the tuning.
export const TUNE = MOTION.ui;

// The style the particles wear right now, as dustCloud.js's code (0 = dust, the flight
// as tabulated). Every cloud builder below reads it once, when the cloud is built.
export const styleCode = () => PARTICLE_STYLES[particleStyle()] || 0;
