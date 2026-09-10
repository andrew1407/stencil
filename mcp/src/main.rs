//! Entry point: serve the Stencil MCP server over stdio.
//!
//! IMPORTANT: stdout is the JSON-RPC channel for the stdio transport. All logging therefore
//! goes to **stderr** — plain `eprintln!`, no logging crate — because writing logs to stdout
//! would corrupt the protocol stream.

use rmcp::transport::stdio;
use rmcp::ServiceExt;
use stencil_mcp::config::Config;
use stencil_mcp::server::StencilServer;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Resolve config from the dotenv file + process env + the --surface arg.
    let args: Vec<String> = std::env::args().collect();
    let (config, warnings) = Config::load(&args);
    for warning in &warnings {
        eprintln!("stencil-mcp: warning: {warning}");
    }
    let surfaces: Vec<&str> = config.default_surfaces.iter().map(|s| s.as_str()).collect();
    eprintln!("stencil-mcp: starting; surfaces={surfaces:?}");

    let service = StencilServer::new(config)
        .serve(stdio())
        .await
        .map_err(|e| format!("failed to start the MCP stdio transport: {e}"))?;

    service.waiting().await?;
    Ok(())
}
