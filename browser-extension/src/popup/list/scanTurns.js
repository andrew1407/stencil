// The newest scan owns the panel's list: starting one makes every earlier scan stale, so a
// slower tab's result never overwrites what a later scan (a tab switch, a re-scan) drew.
export const createScanTurns = () => {
  let latest = 0;
  return {
    begin() {
      const mine = ++latest;
      return () => mine === latest;
    },
  };
};
