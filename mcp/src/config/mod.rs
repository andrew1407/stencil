//! Server configuration: which delivery surface(s) a `stencil_edit` result goes to, plus
//! the paths/URLs those surfaces need.
//!
//! Resolution order, lowest precedence first: built-in defaults < the dotenv file < process
//! env (incl. the mcpServers "env") < the `--surface` CLI arg; a per-call `surface` parameter
//! overrides it for one call. The hand-rolled dotenv loader never overrides the process env.
mod env;
mod surface;

use std::path::PathBuf;

pub use env::cli_timeout;
pub use surface::{parse_surfaces, Surface};

use env::{arg_value, default_desktop_path, env_nonempty, load_dotenv};

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct LlmEnv {
    /// `STENCIL_LLM_PROVIDER` — `ollama` (default) | `openai-compat` | `stencil-server`.
    pub provider: Option<String>,
    /// `STENCIL_LLM_BASE_URL` — defaulted per provider when unset.
    pub base_url: Option<String>,
    /// `STENCIL_LLM_MODEL` — may stay empty (provider/server default).
    pub model: Option<String>,
    /// `STENCIL_LLM_API_KEY` — `openai-compat` bearer key; optional.
    pub api_key: Option<String>,
    /// `STENCIL_LLM_SERVER_URL` — the `stencil-server` provider's endpoint.
    pub server_url: Option<String>,
    /// `STENCIL_LLM_SERVER_TOKEN` — bearer token for that server.
    pub server_token: Option<String>,
}

/// Resolved server configuration.
#[derive(Debug, Clone)]
pub struct Config {
    /// Default delivery surfaces when a tool call doesn't specify its own.
    pub default_surfaces: Vec<Surface>,
    /// Qt desktop binary path (for the `desktop` surface).
    pub desktop_path: Option<PathBuf>,
    /// Base URL of the served browser editor (for the `browser` surface).
    pub browser_url: String,
    /// Auto-open the browser launch URL with the OS opener.
    pub auto_open: bool,
    /// LLM provider settings for `stencil_prompt` (raw; resolved per call).
    pub llm: LlmEnv,
}

const DEFAULT_BROWSER_URL: &str = "http://localhost:8080";

impl Default for Config {
    fn default() -> Self {
        Config {
            default_surfaces: vec![Surface::Cli],
            desktop_path: None,
            browser_url: DEFAULT_BROWSER_URL.to_string(),
            auto_open: false,
            llm: LlmEnv::default(),
        }
    }
}

impl Config {
    /// Build the config from the dotenv file, the process environment, and `--surface <list>`
    /// in `args`, plus any non-fatal warnings (a bad surface token falls back to the default).
    pub fn load(args: &[String]) -> (Config, Vec<String>) {
        load_dotenv();

        let mut config = Config::default();
        let mut warnings: Vec<String> = Vec::new();

        // Surfaces: process env, then the --surface arg (arg wins).
        let surface_spec =
            arg_value(args, "--surface").or_else(|| std::env::var("STENCIL_SURFACES").ok());
        if let Some(spec) = surface_spec {
            match parse_surfaces(&spec) {
                Ok(surfaces) => config.default_surfaces = surfaces,
                Err(e) => warnings.push(format!("ignoring STENCIL_SURFACES/--surface: {e}")),
            }
        }

        if let Ok(path) = std::env::var("STENCIL_DESKTOP") {
            if !path.trim().is_empty() {
                config.desktop_path = Some(PathBuf::from(path));
            }
        }
        if config.desktop_path.is_none() {
            config.desktop_path = default_desktop_path();
        }

        if let Ok(url) = std::env::var("STENCIL_BROWSER_URL") {
            if !url.trim().is_empty() {
                config.browser_url = url.trim().trim_end_matches('/').to_string();
            }
        }

        if let Ok(flag) = std::env::var("STENCIL_AUTO_OPEN") {
            config.auto_open = matches!(
                flag.trim().to_ascii_lowercase().as_str(),
                "1" | "true" | "yes" | "on"
            );
        }

        // LLM settings (see `LlmEnv`); a bad provider token is worth a startup warning.
        config.llm = LlmEnv {
            provider: env_nonempty("STENCIL_LLM_PROVIDER"),
            base_url: env_nonempty("STENCIL_LLM_BASE_URL"),
            model: env_nonempty("STENCIL_LLM_MODEL"),
            api_key: env_nonempty("STENCIL_LLM_API_KEY"),
            server_url: env_nonempty("STENCIL_LLM_SERVER_URL"),
            server_token: env_nonempty("STENCIL_LLM_SERVER_TOKEN"),
        };
        if let Some(provider) = &config.llm.provider {
            if let Err(e) = crate::llm::Provider::parse(provider) {
                warnings.push(format!(
                    "STENCIL_LLM_PROVIDER: {e}; stencil_prompt calls will fail unless \
                     they override the provider"
                ));
            }
        }

        (config, warnings)
    }
}
