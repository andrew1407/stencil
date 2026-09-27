//! `stencil_project_update`, `stencil_project_file` and `stencil_projects`' `files` through a
//! real `tools/call`: the refusals need no CLI; the rest run the real CLI's `--project-update`,
//! `--project-file` and `--project-files` against a scripted loopback server and self-skip
//! without it — only the fields given, the version guard, a download confined to the roots.

mod common;
use common::dispatch::{payload_of, text_of, wire, Harness};
use common::e2e::cli_present;
use common::rest::{harness, ok, reply, rooted, scripted};

use std::time::Duration;

use serde_json::json;

fn session() -> String {
    ok(json!({ "sessionId": "s1", "expiresAt": 1 }))
}

#[tokio::test]
async fn an_update_sends_only_the_fields_given_guarded_by_the_current_version() {
    if !cli_present() {
        return;
    }
    let record = json!({ "id": "p_a_b", "name": "Plans v2", "keywords": ["floor"],
                         "color": "#ff0000", "version": 5, "ownerSession": "s-internal" });
    let (origin, requests) = scripted(vec![
        session(),
        ok(json!({ "project": { "id": "p_a_b", "name": "Plans", "version": 4 }, "layout": null })),
        ok(record),
    ]);
    let args = json!({ "id": "p_a_b", "name": "Plans v2", "keywords": ["floor"], "color": "red" });
    let result = harness(&origin, Some("tok")).call("stencil_project_update", args).await.unwrap();

    assert_eq!(wire(&result)["isError"], false, "{}", text_of(&result));
    assert_eq!(payload_of(&result)["project"], json!({ "id": "p_a_b", "name": "Plans v2",
        "createdAt": 0, "updatedAt": 0, "hasImage": false, "imageW": 0, "imageH": 0,
        "keywords": ["floor"], "color": "#ff0000", "version": 5 }));
    assert!(text_of(&result).contains("now version 5"), "{}", text_of(&result));
    assert!(requests.recv().unwrap().contains("authorization: bearer tok\r\n"));
    assert!(requests.recv().unwrap().starts_with("get /projects/p_a_b http/1.1"));
    let put = requests.recv().unwrap();
    assert!(put.starts_with("put /projects/p_a_b http/1.1"), "{put}");
    let sent = r##"{"name":"Plans v2","keywords":["floor"],"color":"#ff0000","version":4}"##;
    assert!(put.ends_with(sent), "{put}");
}

#[tokio::test]
async fn a_stale_version_is_the_servers_conflict_and_nothing_is_read_first() {
    if !cli_present() {
        return;
    }
    let stale = r#"{"code":"conflict","message":"the project changed since that version"}"#;
    let conflict = reply(409, stale);
    let (origin, requests) = scripted(vec![session(), conflict]);
    let args = json!({ "id": "p_a_b", "description": "", "if_version": 2 });
    let result = harness(&origin, Some("tok")).call("stencil_project_update", args).await.unwrap();

    assert_eq!(wire(&result)["isError"], true);
    let message = text_of(&result);
    assert!(message.contains("rejected the update of p_a_b (409)"), "{message}");
    assert!(message.contains("the project changed since that version"), "{message}");
    requests.recv().unwrap();
    let put = requests.recv().unwrap();
    assert!(put.starts_with("put ") && put.ends_with(r#"{"description":"","version":2}"#), "{put}");
}

#[tokio::test]
async fn a_read_with_files_lists_each_kind_the_project_stores() {
    if !cli_present() {
        return;
    }
    let missing = || reply(404, r#"{"code":"not_found","message":"no file of that kind"}"#);
    let gif = || reply(200, "GIF89a\u{1}\u{0}\u{1}\u{0};");
    let project = json!({ "project": { "id": "p_a_b", "name": "Plans", "version": 3,
                                       "originalPath": "p/o.gif" }, "layout": { "lines": [] } });
    let mut replies = vec![session(), ok(project), gif(), missing(), missing()];
    replies.push(ok(json!({ "messages": [] })));
    replies.extend([missing(), gif()]);
    replies.extend((3..=8).map(|_| missing()));
    let (origin, requests) = scripted(replies);
    let args = json!({ "id": "p_a_b", "files": true });
    let result = harness(&origin, Some("tok")).call("stencil_projects", args).await.unwrap();

    assert_eq!(wire(&result)["isError"], false, "{}", text_of(&result));
    let project = &payload_of(&result)["projects"][0];
    assert_eq!(project["files"], json!([{ "kind": "original", "format": "gif" },
        { "kind": "chat", "format": "json" }, { "kind": "variant2", "format": "gif" }]));
    assert!(project.get("originalPath").is_none(), "{project}");
    assert!(text_of(&result).contains("  - variant2 (gif)"), "{}", text_of(&result));
    let asked: Vec<String> = requests.try_iter().skip(2).collect();
    assert_eq!(asked.len(), 12, "one probe per kind");
    assert!(asked.iter().all(|r| r.contains("range: bytes=0-63\r\n")), "{asked:?}");
    assert!(asked[11].starts_with("get /projects/p_a_b/files/variant8 http/1.1"));
}

#[tokio::test]
async fn a_download_lands_inside_the_root_and_never_clobbers_unasked() {
    if !cli_present() {
        return;
    }
    let root = tempfile::tempdir().unwrap();
    let bytes = "GIF89a\u{1}\u{0}\u{1}\u{0};";
    let (origin, requests) = scripted(vec![session(), reply(200, bytes)]);
    let h = rooted(&origin, Some("tok"), root.path());
    let args = json!({ "id": "p_a_b", "kind": "result", "output": "r.gif" });
    let result = h.call("stencil_project_file", args.clone()).await.unwrap();

    assert_eq!(wire(&result)["isError"], false, "{}", text_of(&result));
    let written = root.path().join("r.gif");
    assert_eq!(payload_of(&result), json!({ "server": origin, "id": "p_a_b", "kind": "result",
        "path": written.to_string_lossy(), "bytes": bytes.len(), "format": "gif" }));
    assert_eq!(std::fs::read(&written).unwrap(), bytes.as_bytes());
    requests.recv().unwrap();
    assert!(requests.recv().unwrap().starts_with("get /projects/p_a_b/files/result http/1.1"));

    let again = h.call("stencil_project_file", args).await.unwrap();
    assert_eq!(wire(&again)["isError"], true);
    assert!(text_of(&again).contains("--no-clobber"), "{}", text_of(&again));
    assert!(requests.recv_timeout(Duration::from_millis(300)).is_err(), "it fetched anyway");
}

#[tokio::test]
async fn the_project_writes_refuse_what_they_cannot_mean_unasked() {
    let root = tempfile::tempdir().unwrap();
    let h = rooted("http://127.0.0.1:9", None, root.path());
    let text = |result: rmcp::model::CallToolResult| text_of(&result);
    let file = |extra: serde_json::Value| {
        let mut args = json!({ "id": "p_a_b", "kind": "original", "output": "o.png" });
        args.as_object_mut().unwrap().extend(extra.as_object().unwrap().clone());
        h.call("stencil_project_file", args)
    };
    let outside = text(file(json!({ "output": "/elsewhere/o.png" })).await.unwrap());
    assert!(outside.contains("outside the allowed roots"), "{outside}");
    assert!(text(file(json!({ "id": "../admin" })).await.unwrap()).contains("is not a project id"));
    let dashed = text(file(json!({ "output": "-o.png" })).await.unwrap());
    assert!(dashed.contains("must not start with '-'"), "{dashed}");

    let update = |args| h.call("stencil_project_update", args);
    assert!(text(update(json!({ "id": "p_a_b" })).await.unwrap()).contains("nothing to change"));
    let elsewhere = json!({ "server": "http://attacker.test", "id": "p_a_b", "name": "x" });
    assert!(text(update(elsewhere).await.unwrap()).contains("is not an allowed server"));

    let files = h.call("stencil_projects", json!({ "files": true })).await.unwrap();
    assert!(text(files).contains("pass its `id`"));
    assert!(Harness::new().call("stencil_project_update", json!({ "id": "p_a_b", "name": "x" }))
        .await
        .map(|r| text_of(&r).contains("none is configured"))
        .unwrap());
}
