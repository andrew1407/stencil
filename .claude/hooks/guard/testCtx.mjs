// The deterministic context the guard's tests share, so no case depends on the machine.

export const ctx = {
  repoRoot: '/repo',
  homeDir: '/home/user',
  allowedOrigins: ['localhost', '127.0.0.1', '0.0.0.0', '::1', 'host.docker.internal'],
};
