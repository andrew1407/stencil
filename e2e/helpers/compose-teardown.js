// Playwright globalTeardown — tear the stack down only when explicitly asked
// (E2E_STACK_DOWN=1). Local runs keep the DB warm between iterations; CI discards
// the VM, so it doesn't need this. Clean up manually with:
//   docker compose down -v
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const COMPOSE_FILE = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../docker-compose.yml');

export default async function globalTeardown() {
  if (process.env.E2E_STACK !== '1' || process.env.E2E_STACK_DOWN !== '1') return;
  console.log('[e2e stack] docker compose down -v …');
  execFileSync('docker', ['compose', '-f', COMPOSE_FILE, 'down', '-v'], { stdio: 'inherit' });
}
