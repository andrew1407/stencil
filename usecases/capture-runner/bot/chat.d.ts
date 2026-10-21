import type { Page } from 'playwright';
import type { ShotRunner } from '../lib/shot/runner.js';

export interface ChatWaits {
  loginPollMs: number;
  loginMaxMs: number;
  replyMs: number;
  promptMs: number;
  settleMs: number;
  menuMs: number;
  maxShotHeight: number;
}

/** How a reply is recognised: `expect` new incoming messages, and the `notice` line gone. */
export interface ReplyWait { expect?: number; notice?: RegExp | null; timeout?: number }

export interface BotChat {
  startIfNew(): Promise<boolean>;
  send(text: string, wait?: ReplyWait): Promise<void>;
  tap(label: string, reveals?: string | null, wait?: ReplyWait): Promise<void>;
  upload(file: string, opts?: ReplyWait & { kind?: 'media' | 'file'; caption?: string }): Promise<void>;
  newestText(): Promise<string>;
  shot(runner: ShotRunner, name: string, keep?: number): Promise<void>;
}

export function openBotChat(page: Page, opts: { bot: string; waits: ChatWaits; profile: string }): Promise<BotChat>;
