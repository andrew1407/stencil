package main

// Listener wiring split out of main(): the one TLS config both ports share, the
// HTTP/WS server, the raw-TCP edit listener, and the run-until-signal drain.

import (
	"context"
	"crypto/tls"
	"errors"
	"log"
	"net"
	"net/http"
	"sync"
	"time"

	"stencil/server/internal/config"
	"stencil/server/internal/httpapi"
	"stencil/server/internal/hub"
)

// newTLSConfig loads the certificate securing BOTH the HTTP/WS port and the raw-TCP
// edit channel, so the live-edit transport is encryptable too — not just
// REST/WS. TLS is opt-in via TLS_CERT/TLS_KEY; without them the server runs
// plaintext (intended only behind a trusted proxy or on localhost).
func newTLSConfig(cfg config.Config) (*tls.Config, error) {
	if cfg.TLSCert == "" || cfg.TLSKey == "" {
		return nil, nil
	}
	cert, err := tls.LoadX509KeyPair(cfg.TLSCert, cfg.TLSKey)
	if err != nil {
		return nil, err
	}
	return &tls.Config{Certificates: []tls.Certificate{cert}, MinVersion: tls.VersionTLS12}, nil
}

// newHTTPServer mounts REST + WS + healthz behind the CORS policy.
func newHTTPServer(cfg config.Config, api *httpapi.API, h *hub.Hub, tlsConf *tls.Config) *http.Server {
	mux := http.NewServeMux()
	api.Register(mux)
	mux.Handle("/ws", h.WSHandler())
	mux.HandleFunc("GET /healthz", func(rw http.ResponseWriter, _ *http.Request) {
		rw.WriteHeader(http.StatusOK)
		_, _ = rw.Write([]byte("ok"))
	})
	return &http.Server{
		Addr:              cfg.ListenAddr,
		Handler:           httpapi.CORS(cfg.CORSOrigins)(mux),
		ReadHeaderTimeout: 10 * time.Second,
		// 5m fits a 32 MiB upload on a slow link and an LLM proxy call
		// (LLM_TIMEOUT_SECONDS, default 120s). WS conns are hijacked on upgrade
		// (deadlines cleared), so live edit sessions are unaffected.
		ReadTimeout:  5 * time.Minute,
		WriteTimeout: 5 * time.Minute,
		IdleTimeout:  2 * time.Minute,
		TLSConfig:    tlsConf,
	}
}

// listenTCP opens the raw-TCP edit listener (NDJSON) for the desktop and CLI
// clients, wrapped in the same certificate as HTTP/WS when TLS is configured.
func listenTCP(cfg config.Config, tlsConf *tls.Config) (net.Listener, error) {
	ln, err := net.Listen("tcp", cfg.TCPAddr)
	if err != nil {
		return nil, err
	}
	if tlsConf != nil {
		ln = tls.NewListener(ln, tlsConf)
	}
	return ln, nil
}

// serve runs both listeners until a signal arrives or HTTP fails, then drains in
// order: the sweep goroutine, TCP accepts, every live edit conn, then HTTP.
func serve(ctx context.Context, srv *http.Server, tcpLn net.Listener, h *hub.Hub, tcpAddr, banner string, tlsOn bool, stop func(), sweepWG *sync.WaitGroup) error {
	go func() {
		log.Printf("TCP edit listener on %s (tls=%v)", tcpAddr, tlsOn)
		_ = h.ServeListener(tcpLn)
	}()
	errCh := make(chan error, 1)
	go func() {
		log.Print(banner)
		if tlsOn {
			// Cert/key already loaded into TLSConfig.Certificates.
			errCh <- srv.ListenAndServeTLS("", "")
		} else {
			errCh <- srv.ListenAndServe()
		}
	}()

	select {
	case <-ctx.Done():
		log.Println("shutting down")
	case err := <-errCh:
		if err != nil && !errors.Is(err, http.ErrServerClosed) {
			return err
		}
	}

	shutdownCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	stop()            // cancel rootCtx so the expiry-sweep goroutine winds down
	sweepWG.Wait()    // join it before run()'s deferred st.Close()/b.Close() fire
	_ = tcpLn.Close() // stop accepting new TCP editors; ServeListener now drains
	// Cancel every live edit connection so their handlers unwind: TCP Reads (now
	// ctx-aware) return and ServeListener's wg.Wait() completes, and hijacked
	// WebSocket editors (which Shutdown cannot close) release so Shutdown can
	// finish instead of blocking until the timeout.
	h.CloseAll()
	return srv.Shutdown(shutdownCtx)
}
