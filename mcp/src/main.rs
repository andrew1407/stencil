//! Entry point: serve the Stencil MCP server over stdio.
//!
//! IMPORTANT: stdout is the JSON-RPC channel for the stdio transport. All logging therefore
//! goes to **stderr** — writing logs to stdout would corrupt the protocol stream.

use anyhow::Context;
use rmcp::transport::stdio;
use rmcp::ServiceExt;
use stencil_mcp::config::Config;
use stencil_mcp::server::StencilServer;
use tracing_subscriber::EnvFilter;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new("info")),
        )
        .with_writer(std::io::stderr)
        .with_ansi(false)
        .init();

    // Resolve config from .env + process env + the --surface arg.
    let args: Vec<String> = std::env::args().collect();
    let (config, warnings) = Config::load(&args);
    for warning in &warnings {
        tracing::warn!("{warning}");
    }
    tracing::info!(
        surfaces = ?config.default_surfaces.iter().map(|s| s.as_str()).collect::<Vec<_>>(),
        "starting stencil-mcp server"
    );

    let service = StencilServer::new(config)
        .serve(stdio())
        .await
        .context("failed to start the MCP stdio transport")?;

    service.waiting().await?;
    Ok(())
}
