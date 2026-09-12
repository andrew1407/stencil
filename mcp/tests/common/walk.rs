//! Per-case reporting for the corpus walkers. A `harness = false` target builds a [`Walk`],
//! registers one case per fixture, and every case reports under its own name — so a red
//! corpus names the case instead of one opaque walk. The output, the name filters and the
//! exit code mirror libtest's, so `cargo test` reads the same either way.

use std::panic::{self, AssertUnwindSafe};
use std::sync::Mutex;
use std::time::Instant;

/// The failing case's location and message, filled by the panic hook.
static LAST_PANIC: Mutex<Option<String>> = Mutex::new(None);

type Case<'a> = (String, Box<dyn FnOnce() + 'a>);

#[derive(Default)]
pub struct Walk<'a> {
    cases: Vec<Case<'a>>,
}

/// libtest's flags that take a separate value; their value is consumed, never a filter.
const VALUED: [&str; 7] = [
    "--format",
    "--test-threads",
    "--logfile",
    "--color",
    "--shuffle-seed",
    "--skip",
    "-Z",
];

struct Args {
    filters: Vec<String>,
    skips: Vec<String>,
    exact: bool,
    list: bool,
    /// `--ignored` asks for the ignored tests only; a corpus case is never `#[ignore]`d.
    ignored_only: bool,
}

fn args() -> Args {
    let mut out =
        Args { filters: Vec::new(), skips: Vec::new(), exact: false, list: false, ignored_only: false };
    let mut argv = std::env::args().skip(1);
    while let Some(arg) = argv.next() {
        match arg.as_str() {
            "--exact" => out.exact = true,
            "--list" => out.list = true,
            "--ignored" => out.ignored_only = true,
            "--skip" => out.skips.extend(argv.next()),
            a if VALUED.contains(&a) => {
                argv.next();
            }
            a if a.starts_with('-') => {}
            _ => out.filters.push(arg),
        }
    }
    out
}

fn selected(name: &str, args: &Args) -> bool {
    let matches = |p: &String| if args.exact { name == p } else { name.contains(p.as_str()) };
    (args.filters.is_empty() || args.filters.iter().any(matches)) && !args.skips.iter().any(matches)
}

/// Record the panic message instead of dumping a backtrace per failing case.
fn capture_panics() {
    panic::set_hook(Box::new(|info| {
        let at = info.location().map(|l| format!("{}:{}", l.file(), l.line())).unwrap_or_default();
        let payload = info.payload();
        let message = payload
            .downcast_ref::<&str>()
            .map(|s| (*s).to_string())
            .or_else(|| payload.downcast_ref::<String>().cloned())
            .unwrap_or_else(|| "panicked".to_string());
        *LAST_PANIC.lock().expect("the panic slot") = Some(format!("at {at}\n{message}"));
    }));
}

impl<'a> Walk<'a> {
    pub fn new() -> Self {
        Self::default()
    }

    /// Register one case. Names are paths (`ollama/<case>`) so a filter can select a family.
    pub fn case(&mut self, name: impl Into<String>, body: impl FnOnce() + 'a) {
        self.cases.push((name.into(), Box::new(body)));
    }

    /// Run every selected case, report per case, and exit 101 if any failed.
    pub fn run(self) -> ! {
        let args = args();
        if args.list {
            for (name, _) in &self.cases {
                println!("{name}: test");
            }
            println!("\n{} tests, 0 benchmarks", self.cases.len());
            std::process::exit(0);
        }
        let total = self.cases.len();
        let picked: Vec<Case> =
            if args.ignored_only { Vec::new() } else { self.cases.into_iter().filter(|(n, _)| selected(n, &args)).collect() };
        let filtered = total - picked.len();

        println!("\nrunning {} tests", picked.len());
        capture_panics();
        let started = Instant::now();
        let mut failures: Vec<(String, String)> = Vec::new();
        for (name, body) in picked {
            *LAST_PANIC.lock().expect("the panic slot") = None;
            match panic::catch_unwind(AssertUnwindSafe(body)) {
                Ok(()) => println!("test {name} ... ok"),
                Err(_) => {
                    println!("test {name} ... FAILED");
                    let why = LAST_PANIC.lock().expect("the panic slot").take();
                    failures.push((name, why.unwrap_or_else(|| "panicked".into())));
                }
            }
        }
        let _ = panic::take_hook();
        let failed = failures.len();
        let verdict = if failed == 0 { "ok" } else { "FAILED" };
        if failed > 0 {
            println!("\nfailures:\n");
            for (name, why) in &failures {
                println!("---- {name} ----\n{why}\n");
            }
            println!("failures:");
            for (name, _) in &failures {
                println!("    {name}");
            }
        }
        println!(
            "\ntest result: {verdict}. {} passed; {failed} failed; 0 ignored; 0 measured; \
             {filtered} filtered out; finished in {:.2}s\n",
            total - filtered - failed,
            started.elapsed().as_secs_f64()
        );
        std::process::exit(i32::from(failed > 0) * 101)
    }
}
