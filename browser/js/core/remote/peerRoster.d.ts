export declare class PeerRoster {
  /** True when the peer was not counted (new, or back after it was forgotten); resets its misses. */
  see(peerId: string): boolean;
  /** Drop a departed peer from every map at once. */
  forget(peerId: string): void;
  /** A null activeId clears the entry. */
  setActive(peerId: string, activeId: string | null): void;
  /** A falsy session clears the entry. */
  setIncognito(peerId: string, session: { name: string; updatedAt: number } | null): void;
  /** One ping round: forgets every peer but `self` silent for more than `misses` rounds; true if any went. */
  sweep(self: string, misses: number): boolean;
  tabCount(): { count: number; youAreOnly: boolean };
  activeIds(): string[];
  incognitoSessions(): { name: string; updatedAt: number }[];
}
