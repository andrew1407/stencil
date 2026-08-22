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
	"strings"
	"sync"
	"syscall"
	"time"

	"stencil/server/internal/bus"
	"stencil/server/internal/config"
	"stencil/server/internal/filestore"
	"stencil/server/internal/httpapi"
	"stencil/server/internal/hub"
	"stencil/server/internal/llm"
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

	fs, err := filestore.NewWithQuota(cfg.FilestoreRoot, cfg.StorageQuotaBytes)
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
	deps := httpapi.Deps{
		Projects:        st,
		Sessions:        st,
		Files:           fs,
		LiveSessions:    h,
		Bus:             b,
		TokenTTL:        cfg.TokenTTL,
		ProjectTTL:      cfg.ProjectTTL,
		MaxBodyBytes:    cfg.MaxBodyBytes,
		AdminToken:      cfg.AdminToken,
		AuthOpen:        cfg.AuthOpen,
		AuthRatePerMin:  cfg.AuthRatePerMin,
		WriteRatePerMin: cfg.WriteRatePerMin,
	}
	if cfg.AuthOpen {
		log.Printf("WARNING: AUTH_OPEN=1 — token issuance is OPEN: anyone who can reach this server " +
			"gets full workspace access (projects, chat transcripts, the LLM proxy). Use only on trusted networks.")
	} else if cfg.AdminTokenGenerated {
		// Printed exactly once, at boot: issuance is closed by default now, so a
		// dev without ADMIN_TOKEN needs this to mint session tokens.
		log.Printf("auth: ADMIN_TOKEN not set — generated for this run: %s", cfg.AdminToken)
	}
	// The proxy is opt-in: unconfigured, /llm/* reports disabled
	// (info: enabled=false; chat: 503 llmDisabled).
	llmBase := cfg.LLMBaseURL
	if llmBase == "" {
		llmBase = llm.DefaultBaseURL(cfg.LLMProvider)
	}
	llmKey := llm.ResolveKey(cfg.LLMProvider, cfg.LLMAPIKey, cfg.AnthropicKey)
	if llmKey != "" {
		// Shape only — never key material: enough to spot a truncated paste, a
		// stray quote/space, or an edit that landed in the wrong .env.
		log.Printf("llm: key loaded (len %d, sk-ant prefix: %v, clean: %v)", len(llmKey),
			strings.HasPrefix(llmKey, "sk-ant-"), llmKey == strings.TrimSpace(llmKey))
	}
	if llmKey == "" && cfg.AnthropicKey != "" {
		log.Printf("llm: ANTHROPIC_API_KEY is set but LLM_PROVIDER is %q — it is NOT sent to a "+
			"non-Anthropic upstream. Use LLM_API_KEY for that provider's own key.", cfg.LLMProvider)
	}
	_, reason := llm.Enablement(cfg.LLMProvider, cfg.LLMAPIKey, cfg.AnthropicKey)
	switch reason {
	case llm.DisabledUnknownProvider:
		log.Printf("llm: unknown LLM_PROVIDER %q — the proxy stays DISABLED "+
			"(known: anthropic, ollama, openai-compat)", cfg.LLMProvider)
	case llm.DisabledMissingKey:
		// Not an error — the proxy is opt-in — but say so, or "llm=false" on the
		// listen line is the only clue and it reads like a failure.
		log.Printf("llm: no LLM_API_KEY configured for provider %q — the proxy is OFF "+
			"(GET /llm/info reports enabled=false).", cfg.LLMProvider)
	default:
		// Token issuance is always gated now (ADMIN_TOKEN set or boot-generated),
		// so a configured provider can safely enable the proxy.
		deps.LLM = llm.New(cfg.LLMProvider, llmBase, llmKey, cfg.LLMModel,
			cfg.LLMMaxTokens, cfg.LLMTimeout)
		deps.LLMRatePerMin = cfg.LLMRatePerMin
		deps.LLMMaxInFlight = cfg.LLMMaxInFlight
	}
	api := httpapi.New(deps)

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
		// 5m fits a 32 MiB upload on a slow link and an LLM proxy call
		// (LLM_TIMEOUT_SECONDS, default 120s). WS conns are hijacked on upgrade
		// (deadlines cleared), so live edit sessions are unaffected.
		ReadTimeout:  5 * time.Minute,
		WriteTimeout: 5 * time.Minute,
		IdleTimeout:  2 * time.Minute,
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
		log.Printf("HTTP/WS listening on %s (tls=%v, redis=%v, filestore=%s, llm=%v)", cfg.ListenAddr, tlsConf != nil, cfg.RedisURL != "", fs.Root(), deps.LLM != nil)
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
