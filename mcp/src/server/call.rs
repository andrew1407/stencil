//! The one wrapper every tool call runs through: it fixes the call's roots, scopes its
//! progress reports to the client's token, and drops the call the moment the client
//! cancels it — which kills every CLI child the call started (`kill_on_drop`).

use std::future::Future;
use std::time::Duration;

use rmcp::model::{CallToolResult, ProgressNotificationParam};
use rmcp::service::RequestContext;
use rmcp::{ErrorData as McpError, RoleServer};

use super::roots;
use super::tools::err_result;
use crate::confine::Roots;
use crate::pipeline::progress::{self, Sink};

/// How long a finished call waits for its last progress notifications to leave.
const DRAIN: Duration = Duration::from_secs(1);

/// Resolve the call's roots into its extensions, then run `call` under cancellation with
/// its progress routed to the client.
pub async fn decorate<F, Fut>(
    mut context: RequestContext<RoleServer>,
    fallback: &Roots,
    call: F,
) -> Result<CallToolResult, McpError>
where
    F: FnOnce(RequestContext<RoleServer>) -> Fut,
    Fut: Future<Output = Result<CallToolResult, McpError>>,
{
    let roots = roots::resolve(&context.peer, fallback).await;
    context.extensions.insert(roots);
    let cancel = context.ct.clone();
    let (sink, forwarder) = match progress_sink(&context) {
        Some((sink, forwarder)) => (Some(sink), Some(forwarder)),
        None => (None, None),
    };

    let result = tokio::select! {
        biased;
        _ = cancel.cancelled() => Ok(err_result("the call was cancelled by the client".into())),
        result = progress::scope(sink, call(context)) => result,
    };
    if let Some(forwarder) = forwarder {
        let _ = tokio::time::timeout(DRAIN, forwarder).await;
    }
    result
}

/// A sink forwarding each report as a `notifications/progress` in order, when the request
/// carried a progress token. The forwarder ends once every clone of the sink is dropped.
fn progress_sink(
    context: &RequestContext<RoleServer>,
) -> Option<(Sink, tokio::task::JoinHandle<()>)> {
    let token = context.meta.get_progress_token()?;
    let peer = context.peer.clone();
    let (sender, mut receiver) = tokio::sync::mpsc::unbounded_channel::<String>();
    let forwarder = tokio::spawn(async move {
        let mut step = 0.0;
        while let Some(message) = receiver.recv().await {
            step += 1.0;
            let mut param = ProgressNotificationParam::new(token.clone(), step);
            param.message = Some(message);
            let _ = peer.notify_progress(param).await;
        }
    });
    let sink = Sink::new(move |message| {
        let _ = sender.send(message);
    });
    Some((sink, forwarder))
}
