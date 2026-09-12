// Command stencil-server is the Stencil collaboration server: it stores and
// shares projects and runs live multi-client edit sessions over WebSocket and
// raw TCP. It is a protocol adapter (a sibling of mcp/): it persists metadata in
// Postgres and bytes in a path-confined file store, and it never links the C++ core.
package main

import (
	"context"
	"errors"
	"fmt"
	"log"
	"os/signal"
	"sync"
	"syscall"

	"stencil/server/internal/config"
	"stencil/server/internal/filestore"
	"stencil/server/internal/httpapi"
	"stencil/server/internal/hub"
	"stencil/server/internal/service"
	"stencil/server/internal/store"
)

func main() {
	if err := run(); err != nil {
		log.Fatalf("stencil-server: %v", err)
	}
}

func run() error {
	cfg, err := config.Load()
	if err != nil {
		return err
	}
	if cfg.DatabaseURL == "" {
		return errors.New("DATABASE_URL is required")
	}

	rootCtx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	// Persistence.
	pool := store.PoolOptions{MaxConns: cfg.DBMaxConns, MinConns: cfg.DBMinConns, StatementTimeout: cfg.DBStatementTimeout}
	st, err := store.NewWithPool(rootCtx, cfg.DatabaseURL, pool)
	if err != nil {
		return err
	}
	defer st.Close()
	if err := store.Migrate(rootCtx, st.MigratePool()); err != nil {
		return err
	}

	fs, err := filestore.NewWithQuota(cfg.FilestoreRoot, cfg.StorageQuotaBytes)
	if err != nil {
		return err
	}
	if warn := filestoreWarning(cfg.FilestoreRoot); warn != "" {
		log.Print(warn)
	}

	// Event/edit bus: Redis when configured, otherwise in-process.
	b, err := openBus(rootCtx, cfg)
	if err != nil {
		return err
	}
	defer b.Close()

	// Reap expired projects on startup and on a timer (Postgres is the source of
	// truth; the sweep also drops filestore bytes and notifies clients). The
	// WaitGroup lets shutdown join it before the store/bus close.
	var sweepWG sync.WaitGroup
	startExpirySweep(rootCtx, &sweepWG, st, service.NewProjects(st, fs, nil, b, cfg.ProjectTTL), cfg.SweepInterval)

	// REST + WS + TCP. The hub is built first so the REST delete guard can consult it
	// for a project's live connection count.
	h := hub.New(rootCtx, st, b, st, hub.WithHelloLimit(cfg.HelloRatePerMin, cfg.TrustedProxies))
	deps := apiDeps(cfg, st, fs, h, b)

	tlsConf, err := newTLSConfig(cfg)
	if err != nil {
		return err
	}
	tcpLn, err := listenTCP(cfg, tlsConf)
	if err != nil {
		return err
	}
	banner := fmt.Sprintf("HTTP/WS listening on %s (tls=%v, redis=%v, filestore=%s, llm=%v)",
		cfg.ListenAddr, tlsConf != nil, cfg.RedisURL != "", fs.Root(), deps.LLM != nil)
	return serve(rootCtx, newHTTPServer(cfg, httpapi.New(deps), h, tlsConf), tcpLn, h,
		cfg.TCPAddr, banner, tlsConf != nil, stop, &sweepWG)
}
