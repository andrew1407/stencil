package main

// Boot wiring split out of main(): the REST dependency set, the opt-in LLM
// proxy, and the warnings an operator must see before the listeners come up.

import (
	"context"
	"fmt"
	"log"
	"path/filepath"
	"strings"

	"stencil/server/internal/bus"
	"stencil/server/internal/config"
	"stencil/server/internal/filestore"
	"stencil/server/internal/httpapi"
	"stencil/server/internal/hub"
	"stencil/server/internal/llm"
	"stencil/server/internal/redisbus"
	"stencil/server/internal/store"
)

// apiDeps assembles the REST handler's dependency set and logs the auth posture
// it implies — the one thing an operator must read before traffic arrives.
func apiDeps(cfg config.Config, st *store.Store, fs *filestore.Store, h *hub.Hub, b bus.Bus) httpapi.Deps {
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
		TrustedProxies:  cfg.TrustedProxies,
		OpTimeout:       cfg.OpTimeout,
	}
	if cfg.AuthOpen {
		log.Printf("WARNING: AUTH_OPEN=1 — token issuance is OPEN: anyone who can reach this server " +
			"gets full workspace access (projects, chat transcripts, the LLM proxy). Use only on trusted networks.")
	} else if cfg.AdminTokenGenerated {
		// Printed exactly once, at boot: issuance is closed by default now, so a
		// dev without ADMIN_TOKEN needs this to mint session tokens.
		log.Printf("auth: ADMIN_TOKEN not set — generated for this run: %s", cfg.AdminToken)
	}
	configureLLM(cfg, &deps) // opt-in proxy, or a log line saying why not
	return deps
}

// configureLLM attaches the proxy to deps when a provider is fully configured,
// and otherwise logs why it stays off.
func configureLLM(cfg config.Config, deps *httpapi.Deps) {
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
}

// filestoreWarning returns the boot warning for a relative FILESTORE_ROOT (""
// when absolute): it resolves against the working directory, so in a container
// with no mounted volume the stored bytes vanish with the container.
func filestoreWarning(root string) string {
	if root == "" || filepath.IsAbs(root) {
		return ""
	}
	return fmt.Sprintf("WARNING: FILESTORE_ROOT %q is relative — it resolves against the working "+
		"directory, so in a container with no mounted volume the stored image bytes are lost on restart. "+
		"Set an absolute path.", root)
}

// openBus returns a Redis-backed bus when REDIS_URL is set, else an in-process
// bus (single-instance deployments).
func openBus(ctx context.Context, cfg config.Config) (bus.Bus, error) {
	if cfg.RedisURL == "" {
		return bus.NewInProc(), nil
	}
	return redisbus.NewWithOptions(ctx, cfg.RedisURL, redisbus.Options{PoolSize: cfg.Redis.PoolSize,
		DialTimeout: cfg.Redis.DialTimeout, IOTimeout: cfg.Redis.IOTimeout})
}
