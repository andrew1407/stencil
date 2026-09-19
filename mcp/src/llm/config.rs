//! Providers & configuration (contract §5).

use serde_json::Value;

use crate::config::LlmEnv;

/// The `providers` table of the canonical cross-surface asset, embedded at compile time.
static PROVIDERS: std::sync::LazyLock<Value> = std::sync::LazyLock::new(|| {
    let asset: Value =
        serde_json::from_str(include_str!("../../../browser/js/config/llm/providers.json"))
            .expect("canonical browser/js/config/llm/providers.json is not valid JSON");
    asset["providers"].clone()
});

/// The three providers every Stencil client supports.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Provider {
    /// Native Ollama chat (`POST {baseUrl}/api/chat`).
    Ollama,
    /// OpenAI-compatible servers — LM Studio, llama.cpp, vLLM … (`POST {baseUrl}/chat/completions`).
    OpenaiCompat,
    /// A Stencil collaboration server proxying Anthropic (`POST {serverUrl}/llm/chat`).
    StencilServer,
}

impl Provider {
    /// Parse a provider token (trimmed, case-insensitive).
    pub fn parse(token: &str) -> Result<Provider, String> {
        match token.trim().to_ascii_lowercase().as_str() {
            "ollama" => Ok(Provider::Ollama),
            "openai-compat" => Ok(Provider::OpenaiCompat),
            "stencil-server" => Ok(Provider::StencilServer),
            other => Err(format!(
                "unknown LLM provider '{other}' (expected: ollama, openai-compat, stencil-server)"
            )),
        }
    }

    pub fn as_str(self) -> &'static str {
        match self {
            Provider::Ollama => "ollama",
            Provider::OpenaiCompat => "openai-compat",
            Provider::StencilServer => "stencil-server",
        }
    }

    /// The contract's per-provider default `baseUrl` (§5), from the embedded providers.json.
    /// `stencil-server` carries `null` — its endpoint is the configured server URL.
    pub fn default_base_url(self) -> Option<&'static str> {
        PROVIDERS[self.as_str()]["defaultBaseUrl"].as_str()
    }
}

/// Resolved provider configuration for one call: the `STENCIL_LLM_*` environment (already
/// layered with `.env` by `config.rs`) plus the per-call tool overrides.
#[derive(Debug, Clone)]
pub struct LlmConfig {
    pub provider: Provider,
    /// For `ollama`/`openai-compat`; trailing slash trimmed. Unused for `stencil-server`.
    pub base_url: String,
    /// May be empty (provider/server default).
    pub model: String,
    /// `openai-compat` only; empty = no `Authorization` header.
    pub api_key: String,
    /// `stencil-server` only.
    pub server_url: String,
    /// `stencil-server` only; empty = no `Authorization` header.
    pub server_token: String,
}

impl LlmConfig {
    /// Resolve the provider config from the environment. `model` is the ONLY per-call override:
    /// the key travels with the endpoint, so a caller-chosen host would exfiltrate it.
    pub fn resolve(env: &LlmEnv, model: Option<&str>) -> Result<LlmConfig, String> {
        let token = env.provider.as_deref().unwrap_or("ollama");
        let provider = Provider::parse(token)?;

        let base_url = env
            .base_url
            .as_deref()
            .or(provider.default_base_url())
            .unwrap_or_default();

        let server_url = env.server_url.as_deref().unwrap_or_default();
        if provider == Provider::StencilServer && server_url.trim().is_empty() {
            return Err(
                "the stencil-server provider needs STENCIL_LLM_SERVER_URL set to a Stencil \
                 collaboration server URL"
                    .to_string(),
            );
        }

        Ok(LlmConfig {
            provider,
            base_url: base_url.trim().trim_end_matches('/').to_string(),
            model: model.or(env.model.as_deref()).unwrap_or_default().to_string(),
            api_key: env.api_key.clone().unwrap_or_default(),
            server_url: server_url.trim().trim_end_matches('/').to_string(),
            server_token: env.server_token.clone().unwrap_or_default(),
        })
    }
}
