// Command stencil-server is the Stencil collaboration server: it stores and
// shares projects and runs live multi-client edit sessions over WebSocket and
// raw TCP. It is a protocol adapter (a sibling of mcp/): it persists metadata in
// Postgres and bytes in a secured file store, and it never links the C++ core.
package main

import (
	"context"
	"crypto/tls"
	"errors"
	"log"
	"net"
	"net/http"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"stencil/server/internal/bus"
	"stencil/server/internal/config"
	"stencil/server/internal/filestore"
	"stencil/server/internal/httpapi"
	"stencil/server/internal/hub"
	"stencil/server/internal/redisbus"
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
	st, err := store.New(rootCtx, cfg.DatabaseURL)
	if err != nil {
		return err
	}
	defer st.Close()
	if err := store.Migrate(rootCtx, st.MigratePool()); err != nil {
		return err
	}

	fs, err := filestore.New(cfg.FilestoreRoot)
	if err != nil {
		return err
	}

	// Event/edit bus: Redis when configured, otherwise in-process.
	b, err := openBus(rootCtx, cfg.RedisURL)
	if err != nil {
		return err
	}
	defer b.Close()

	// Reap expired projects on startup and on a timer (Postgres is the source of
	// truth; the sweep also drops filestore bytes and notifies clients). The
	// WaitGroup lets shutdown join the sweep before the store/bus are closed, so it
	// never touches a closed pool.
	var sweepWG sync.WaitGroup
	startExpirySweep(rootCtx, &sweepWG, st, fs, b, cfg.SweepInterval)

	// REST + WS + TCP. The hub is built first so the REST delete guard can consult it
	// for a project's live connection count.
	h := hub.New(rootCtx, st, b, st)
	api := httpapi.New(httpapi.Deps{
		Projects:     st,
		Sessions:     st,
		Files:        fs,
		LiveSessions: h,
		Bus:          b,
		TokenTTL:     cfg.TokenTTL,
		ProjectTTL:   cfg.ProjectTTL,
		MaxBodyBytes: cfg.MaxBodyBytes,
		AdminToken:   cfg.AdminToken,
	})

	mux := http.NewServeMux()
	api.Register(mux)
	mux.Handle("/ws", h.WSHandler())
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte("ok"))
	})

	httpSrv := &http.Server{
		Addr:              cfg.ListenAddr,
		Handler:           httpapi.CORS(cfg.CORSOrigins)(mux),
		ReadHeaderTimeout: 10 * time.Second,
	}

	// One shared TLS config (loaded once) secures both the HTTP/WS port and the
	// raw-TCP edit channel, so the live-edit transport is encryptable too — not
	// just REST/WS. TLS is opt-in via TLS_CERT/TLS_KEY; without them the server
	// runs plaintext (intended only behind a trusted proxy or on localhost).
	var tlsConf *tls.Config
	if cfg.TLSCert != "" && cfg.TLSKey != "" {
		cert, err := tls.LoadX509KeyPair(cfg.TLSCert, cfg.TLSKey)
		if err != nil {
			return err
		}
		tlsConf = &tls.Config{
			Certificates: []tls.Certificate{cert},
			MinVersion:   tls.VersionTLS12,
		}
		httpSrv.TLSConfig = tlsConf
	}

	// Raw-TCP edit listener (NDJSON) for the desktop and CLI clients. When TLS
	// is configured it is wrapped so the live-edit channel is encrypted with the
	// same certificate as HTTP/WS.
	tcpLn, err := net.Listen("tcp", cfg.TCPAddr)
	if err != nil {
		return err
	}
	if tlsConf != nil {
		tcpLn = tls.NewListener(tcpLn, tlsConf)
	}
	go func() {
		log.Printf("TCP edit listener on %s (tls=%v)", cfg.TCPAddr, tlsConf != nil)
		_ = h.ServeListener(tcpLn)
	}()

	// Serve HTTP/WS until a signal arrives.
	errCh := make(chan error, 1)
	go func() {
		log.Printf("HTTP/WS listening on %s (tls=%v, redis=%v, filestore=%s)", cfg.ListenAddr, tlsConf != nil, cfg.RedisURL != "", fs.Root())
		if tlsConf != nil {
			// Cert/key already loaded into TLSConfig.Certificates.
			errCh <- httpSrv.ListenAndServeTLS("", "")
		} else {
			errCh <- httpSrv.ListenAndServe()
		}
	}()

	select {
	case <-rootCtx.Done():
		log.Println("shutting down")
	case err := <-errCh:
		if err != nil && !errors.Is(err, http.ErrServerClosed) {
			return err
		}
	}

	shutdownCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	stop()            // cancel rootCtx so the expiry-sweep goroutine winds down
	sweepWG.Wait()    // join it before the deferred st.Close()/b.Close() run
	_ = tcpLn.Close() // stop accepting new TCP editors; ServeListener now drains
	// Cancel every live edit connection so their handlers unwind: TCP Reads (now
	// ctx-aware) return and ServeListener's wg.Wait() completes, and hijacked
	// WebSocket editors (which Shutdown cannot close) release so Shutdown can
	// finish instead of blocking until the timeout.
	h.CloseAll()
	return httpSrv.Shutdown(shutdownCtx)
}

// openBus returns a Redis-backed bus when redisURL is set, else an in-process
// bus (single-instance deployments).
func openBus(ctx context.Context, redisURL string) (bus.Bus, error) {
	if redisURL == "" {
		return bus.NewInProc(), nil
	}
	return redisbus.New(ctx, redisURL)
}
