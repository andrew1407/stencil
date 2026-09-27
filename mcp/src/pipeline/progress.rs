//! Progress lines for the tool call that is running: the server installs a sink per call,
//! and anything below it reports into whichever sink its task is scoped to.

use std::future::Future;
use std::sync::Arc;

tokio::task_local! {
    static SINK: Sink;
}

/// Where a call's progress messages go; cloned into every task the call spawns.
#[derive(Clone)]
pub struct Sink(Arc<dyn Fn(String) + Send + Sync>);

impl Sink {
    pub fn new(report: impl Fn(String) + Send + Sync + 'static) -> Sink {
        Sink(Arc::new(report))
    }
}

/// Report one step; a no-op outside a scoped call.
pub fn report(message: impl Into<String>) {
    let _ = SINK.try_with(|sink| (sink.0)(message.into()));
}

/// The current task's sink, so a spawned task can be scoped to the same call.
pub fn current() -> Option<Sink> {
    SINK.try_with(Sink::clone).ok()
}

/// Run `work` with `sink` receiving its reports.
pub async fn scope<F: Future>(sink: Option<Sink>, work: F) -> F::Output {
    match sink {
        Some(sink) => SINK.scope(sink, work).await,
        None => work.await,
    }
}
