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
	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/httpapi"
	"stencil/server/internal/hub"
	"stencil/server/internal/ratelimit"
	"stencil/server/internal/service"
	"stencil/server/internal/store"
	"stencil/server/internal/transport"
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
	if err := migrate(rootCtx, st, cfg.FilestoreRoot, cfg.SessionQuotaBytes); err != nil {
		return err
	}

	quotas := filestore.Quotas{Total: cfg.StorageQuotaBytes, PerOwner: cfg.OwnerQuotaBytes}
	fs, err := filestore.NewWithQuotas(cfg.FilestoreRoot, quotas)
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

	// REST + WS + TCP. The hub is built first: the REST delete guard, the expiry sweep and the presence
	// heartbeat all ask it which projects are being edited live.
	transport.Configure(transport.Timeouts{WSPing: cfg.Live.WSPing, WSPongTimeout: cfg.Live.WSPongTimeout,
		TCPIdle: cfg.Live.TCPIdle, TCPWrite: cfg.Live.TCPWrite})
	eventbus.SetDropWindow(cfg.Live.BusDropWarn)
	ratelimit.SetIdleTTL(cfg.RateBucketIdle)
	h := hub.New(rootCtx, st, b, st, hubOptions(cfg)...)
	deps := apiDeps(cfg, st, fs, h, b)
	expiry := service.Expiry{Store: st, Live: h, OpTimeout: cfg.OpTimeout}

	// Presence, the expiry reaper (filestore bytes dropped, clients notified) and the filestore reconcile
	// run on timers; the WaitGroup lets shutdown join all three before the store/bus close.
	var sweepWG sync.WaitGroup
	if cfg.Presence.TTL > 0 {
		presence, err := service.NewPresence(st, h, cfg.Presence.TTL)
		if err != nil {
			return err
		}
		deps.RemoteSessions, expiry.Remote = presence, presence
		log.Printf("presence: instance %s, heartbeat %v, ttl %v", presence.Instance, cfg.Presence.Heartbeat, cfg.Presence.TTL)
		startPresence(rootCtx, &sweepWG, presence, h.LiveChanged(), cfg.Presence, cfg.OpTimeout)
	}
	startExpirySweep(rootCtx, &sweepWG, maintenance{
		projects: expiry,
		drops:    service.NewProjects(st, fs, nil, b, cfg.ProjectTTL),
		sessions: st,
		batch:    cfg.Sweep.Batch,
		workers:  cfg.Sweep.Workers,
		timeout:  cfg.OpTimeout,
	}, cfg.SweepInterval)
	startReconcile(rootCtx, &sweepWG, fs, st, cfg.OpTimeout, cfg.Sweep.Reconcile, cfg.Sweep.TmpMaxAge)

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
		cfg.TCPAddr, banner, tlsConf != nil, stop, &sweepWG, cfg.HTTP.ShutdownTimeout)
}
