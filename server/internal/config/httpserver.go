// HTTP-side tunables: the server's own timeouts, the shutdown drain, the Retry-After hints and the
// default page of GET /projects.
package config

import (
	"fmt"
	"time"

	"stencil/server/internal/validate"
)

// HTTPOptions bounds the HTTP/WS listener and what its answers promise.
type HTTPOptions struct {
	ReadHeaderTimeout time.Duration // HTTP_READ_HEADER_TIMEOUT_SECONDS
	ReadTimeout       time.Duration // HTTP_READ_TIMEOUT_SECONDS: fits a 32 MiB upload on a slow link
	WriteTimeout      time.Duration // HTTP_WRITE_TIMEOUT_SECONDS: fits an LLM proxy call
	IdleTimeout       time.Duration // HTTP_IDLE_TIMEOUT_SECONDS
	ShutdownTimeout   time.Duration // SHUTDOWN_TIMEOUT_SECONDS: the drain after a signal
	RetryAfter        time.Duration // RETRY_AFTER_SECONDS: a rate-limited answer (a bucket refills per minute)
	BusyRetryAfter    time.Duration // LLM_BUSY_RETRY_AFTER_SECONDS: /llm/chat over the in-flight cap
	ProjectsPageSize  int           // PROJECTS_PAGE_SIZE: rows in a GET /projects that names no ?limit=; 0 = all
}

const (
	defaultReadHeaderTimeout = 10 * time.Second
	defaultReadTimeout       = 5 * time.Minute
	defaultWriteTimeout      = 5 * time.Minute
	defaultIdleTimeout       = 2 * time.Minute
	defaultShutdownTimeout   = 10 * time.Second
	defaultRetryAfter        = 60 * time.Second
	defaultBusyRetryAfter    = 5 * time.Second
	// Every shipped client walks nextCursor to the end, so this bounds one response, never the list.
	defaultProjectsPageSize = 100
)

func loadHTTP(get getter, cfg *Config) error {
	h := &cfg.HTTP
	var err error
	for _, d := range []struct {
		dst *time.Duration
		key string
		def time.Duration
	}{
		{&h.ReadHeaderTimeout, "HTTP_READ_HEADER_TIMEOUT_SECONDS", defaultReadHeaderTimeout},
		{&h.ReadTimeout, "HTTP_READ_TIMEOUT_SECONDS", defaultReadTimeout},
		{&h.WriteTimeout, "HTTP_WRITE_TIMEOUT_SECONDS", defaultWriteTimeout},
		{&h.IdleTimeout, "HTTP_IDLE_TIMEOUT_SECONDS", defaultIdleTimeout},
		{&h.ShutdownTimeout, "SHUTDOWN_TIMEOUT_SECONDS", defaultShutdownTimeout},
		{&h.RetryAfter, "RETRY_AFTER_SECONDS", defaultRetryAfter},
		{&h.BusyRetryAfter, "LLM_BUSY_RETRY_AFTER_SECONDS", defaultBusyRetryAfter},
	} {
		if *d.dst, err = duration(get, d.key, d.def, time.Second, 1); err != nil {
			return err
		}
	}
	// The goodbye writes run inside the drain (loadLive ran first), so one may not outlast all of it.
	if cfg.Live.NoticeTimeout > h.ShutdownTimeout {
		return fmt.Errorf("config: HUB_NOTICE_TIMEOUT_SECONDS (%v) must be at most SHUTDOWN_TIMEOUT_SECONDS (%v)",
			cfg.Live.NoticeTimeout, h.ShutdownTimeout)
	}
	h.ProjectsPageSize, err = positiveInt(get, "PROJECTS_PAGE_SIZE", defaultProjectsPageSize, 0)
	if err == nil && h.ProjectsPageSize > validate.MaxListLimit {
		err = fmt.Errorf("config: PROJECTS_PAGE_SIZE %d is over the page cap %d", h.ProjectsPageSize, validate.MaxListLimit)
	}
	return err
}
