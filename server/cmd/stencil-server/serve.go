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

	"stencil/server/internal/auth"
	"stencil/server/internal/config"
	"stencil/server/internal/httpapi"
	"stencil/server/internal/hub"
)

// newTLSConfig loads the certificate securing BOTH the HTTP/WS port and the raw-TCP edit channel. TLS is
// opt-in via TLS_CERT/TLS_KEY; without them the server runs plaintext (trusted proxy or localhost only).
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
	mux.Handle(auth.WSRoute, h.WSHandler())
	mux.HandleFunc("GET /healthz", func(rw http.ResponseWriter, _ *http.Request) {
		rw.WriteHeader(http.StatusOK)
		_, _ = rw.Write([]byte("ok"))
	})
	// WS conns are hijacked on upgrade, so the read/write timeouts never cut a live edit session.
	return &http.Server{
		Addr:              cfg.ListenAddr,
		Handler:           httpapi.CORS(cfg.CORSOrigins)(mux),
		ReadHeaderTimeout: cfg.HTTP.ReadHeaderTimeout,
		ReadTimeout:       cfg.HTTP.ReadTimeout,
		WriteTimeout:      cfg.HTTP.WriteTimeout,
		IdleTimeout:       cfg.HTTP.IdleTimeout,
		TLSConfig:         tlsConf,
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

// serve runs both listeners until a signal arrives or HTTP fails, then drains within drain, in order: the
// sweep goroutines, TCP accepts, every live edit conn, HTTP, then the sessions' in-flight saves.
func serve(ctx context.Context, srv *http.Server, tcpLn net.Listener, h *hub.Hub, tcpAddr, banner string, tlsOn bool, stop func(), sweepWG *sync.WaitGroup, drain time.Duration) error {
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

	if err := waitForTermination(ctx, errCh); err != nil {
		return err
	}

	shutdownCtx, cancel := context.WithTimeout(context.Background(), drain)
	defer cancel()
	stop()            // cancel rootCtx so the expiry-sweep goroutine winds down
	sweepWG.Wait()    // join it before run()'s deferred st.Close()/b.Close() fire
	_ = tcpLn.Close() // stop accepting new TCP editors; ServeListener now drains
	// Notice, then cancel, every live edit connection so their handlers unwind: ctx-aware TCP Reads return
	// and hijacked WebSocket editors (which Shutdown cannot close) release, so Shutdown finishes.
	h.CloseAll()
	err := srv.Shutdown(shutdownCtx)
	// Every run loop still commits and announces the saves its worker holds; only then do their store
	// contexts end, and run()'s deferred bus and pool closes follow.
	if derr := h.Drain(shutdownCtx); derr != nil {
		log.Printf("shutdown: live sessions still saving at the drain deadline: %v", derr)
	}
	h.Close()
	return err
}

// waitForTermination blocks until a signal cancels ctx or the HTTP server
// fails; a clean ErrServerClosed is not a failure.
func waitForTermination(ctx context.Context, errCh <-chan error) error {
	select {
	case <-ctx.Done():
		log.Println("shutting down")
		return nil
	case err := <-errCh:
		if err != nil && !errors.Is(err, http.ErrServerClosed) {
			return err
		}
		return nil
	}
}
