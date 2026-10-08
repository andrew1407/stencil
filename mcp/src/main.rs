//! Entry point: serve the Stencil MCP server over stdio. stdout is the JSON-RPC channel, so all
//! logging goes to **stderr** through plain `eprintln!`; a log line on stdout corrupts the stream.

use rmcp::transport::stdio;
use rmcp::ServiceExt;
use stencil_mcp::config::Config;
use stencil_mcp::opplan;
use stencil_mcp::pipeline::ProcessRunner;
use stencil_mcp::server::StencilServer;

/// Synchronous on purpose: `Config::load` reads the dotenv file with `setenv`, and `setenv`
/// races any other thread's `getenv`. The runtime — and its workers — start after it.
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();
    let (config, warnings) = Config::load(&args);
    for warning in &warnings {
        eprintln!("stencil-mcp: warning: {warning}");
    }
    let surfaces: Vec<&str> = config.default_surfaces.iter().map(|s| s.as_str()).collect();
    eprintln!("stencil-mcp: starting; surfaces={surfaces:?}");

    let runtime = tokio::runtime::Builder::new_multi_thread().enable_all().build()?;
    runtime.block_on(serve(config))
}

async fn serve(config: Config) -> Result<(), Box<dyn std::error::Error>> {
    // Plans are judged by the CLI's own registry copy; a skewed one is refused per call.
    tokio::spawn(async {
        if let Some(warning) = opplan::registry_skew(&ProcessRunner).await {
            eprintln!("stencil-mcp: warning: {warning}");
        }
    });
    let service = StencilServer::new(config)
        .serve(stdio())
        .await
        .map_err(|e| format!("failed to start the MCP stdio transport: {e}"))?;

    service.waiting().await?;
    Ok(())
}
