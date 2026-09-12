export declare class PeerRoster {
  see(peerId: string): void;
  /** Drop a departed peer from every map at once. */
  forget(peerId: string): void;
  /** A null activeId clears the entry. */
  setActive(peerId: string, activeId: string | null): void;
  /** A falsy session clears the entry. */
  setIncognito(peerId: string, session: { name: string; updatedAt: number } | null): void;
  tabCount(): { count: number; youAreOnly: boolean };
  activeIds(): string[];
  incognitoSessions(): { name: string; updatedAt: number }[];
}
