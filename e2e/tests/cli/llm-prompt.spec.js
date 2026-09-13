// CLI /prompt e2e: pipe a scripted console session into the real Zig binary
// (`--console` reads /command lines from stdin — no TTY, no docker stack) with
// the STENCIL_LLM_* env pointed at the in-process stub LLM (openai-compat wire
// shape, llm-contract.md §5/§6.2). The stub scripts a §1 op-plan; the spec
// asserts the plan EXECUTED — the saved PNG's real dimensions swapped — not just
// that a reply was printed.
import { test, expect } from '@playwright/test';
import path from 'node:path';
import { cliAvailable, runCli, pngSize } from '../../helpers/cli.js';
import { runConsole } from '../../helpers/consoleCli.js';
import { startLlmStub } from '../../helpers/llm-stub.js';

test.describe('cli /prompt (LLM assistant)', () => {
  test.skip(!cliAvailable(), 'build the CLI first: (cd cli && zig build) or set STENCIL_CLI');

  /** @type {Awaited<ReturnType<typeof startLlmStub>>} */
  let stub;

  // Ephemeral loopback port: the CLI runs on this host and dials the stub directly.
  test.beforeAll(async () => { stub = await startLlmStub(); });
  test.afterAll(async () => { await stub?.close(); });
  test.beforeEach(() => stub.reset());

  // The console reads /command lines from piped stdin (helpers/consoleCli.js); the
  // STENCIL_LLM_* env points it at the stub living in this process.
  const runScripted = (lines, cwd) => runConsole(lines, {
    cwd,
    env: {
      STENCIL_LLM_PROVIDER: 'openai-compat',
      STENCIL_LLM_BASE_URL: stub.url,
      STENCIL_LLM_MODEL: 'e2e-model',
    },
  });

  test('a scripted op-plan executes on the working image', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    // Known non-square input, made by the CLI itself (8x4, as in pipeline.spec.js).
    const input = path.join(dir, 'in.png');
    const mk = runCli(['--blank', '8', '4', 'white', input], { cwd: dir });
    expect(mk.code, mk.out).toBe(0);

    // The "model" answers with a valid §1 plan: one quarter-turn.
    stub.queue({ version: 1, reply: 'Rotated it a quarter turn.', actions: [{ op: 'rotate', dir: 'right' }] });

    const r = await runScripted([
      `/upload ${input}`,
      '/prompt rotate this image a quarter turn',
      '/save out.png',
      '/exit',
    ], dir);
    expect(r.code, r.out).toBe(0);
    expect(r.out).toContain('Rotated it a quarter turn.'); // chat reply printed
    // The plan really ran through the session ops: 8x4 → 4x8 in the written file.
    expect(pngSize(path.join(dir, 'out.png'))).toEqual({ width: 4, height: 8 });

    // Wire shape (§6.2): one POST to {base}/chat/completions with the configured
    // model, and the working image attached for vision as a data-URL content part.
    expect(stub.requests).toHaveLength(1);
    const req = stub.requests[0];
    expect(req.path).toBe('/chat/completions');
    expect(req.body.model).toBe('e2e-model');
    const turn = req.body.messages.at(-1);
    expect(turn.role).toBe('user');
    expect(Array.isArray(turn.content)).toBeTruthy();
    expect(turn.content[0]).toMatchObject({ type: 'text' });
    expect(turn.content[0].text).toContain('rotate this image a quarter turn');
    const img = turn.content.find((p) => p.type === 'image_url');
    expect(img?.image_url?.url).toMatch(/^data:image\/png;base64,/);
  });

  test('a reply with no JSON object is just chat (image untouched)', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const input = path.join(dir, 'in.png');
    const mk = runCli(['--blank', '8', '4', 'white', input], { cwd: dir });
    expect(mk.code, mk.out).toBe(0);

    stub.queue('Nice picture! Nothing to change.'); // plain text, no op-plan

    const r = await runScripted([
      `/upload ${input}`,
      '/prompt what do you think?',
      '/save out.png',
      '/exit',
    ], dir);
    expect(r.code, r.out).toBe(0);
    expect(r.out).toContain('Nice picture! Nothing to change.');
    expect(pngSize(path.join(dir, 'out.png'))).toEqual({ width: 8, height: 4 }); // unchanged
    expect(stub.requests).toHaveLength(1);
  });
});
