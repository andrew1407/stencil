# Stencil — Python package (`pystencil`)

A **stdlib-only** Python mirror of the browser editor's `window.stencil` facade and the
Zig [CLI](../cli/README.md)'s editing capabilities, driving the **same C++ `core/`** over
`ctypes`: load an image, crop / rotate it, apply a filter, draw a layout, save the result,
build layout JSON, and connect to a Stencil [collaboration server](../server/README.md) to
fetch, edit, and publish projects. For the project overview see the
[repository README](../README.md); for how the package is built,
[`ARCHITECTURE.md`](ARCHITECTURE.md).

```python
from pystencil import Editor

(Editor()
    .load("photo.jpg")
    .crop("x1=10% x2=90% y1=10% y2=90%")
    .rotate_right()
    .apply_filter("sepia")
    .save("out.png"))
```

## Build

Needs **Python 3.9+** and a **C++17 compiler on `PATH`** (clang or gcc); no PyPI packages.
The native library is not committed — build it once from `core/`:

```bash
# from this directory (pystencil/)
python3 build.py            # compiles core/*.cpp + cliApi.cpp → the shared lib
```

You don't have to run it by hand: the first time the package needs the core it builds it on
demand and caches the result, rebuilding whenever any source or header is newer than the
artifact. PNG and BMP are decoded natively; **JPEG input needs the Zig CLI** (`codecs.decode`
raises a `CodecError` pointing you at it).

## Test

```bash
python3 -m unittest discover -s tests
STENCIL_SKIP_NATIVE=1 python3 -m unittest discover -s tests   # no C++ compiler needed
python3 -m unittest discover -s tests -p "bench_*.py"          # opt-in benchmarks
```

The suite is hermetic — no running server, no network. Native-backed cases build the library
on demand; `STENCIL_SKIP_NATIVE=1` skips them as a band.

## Quickstart

```python
from pystencil import Editor, Layout, Line, Point

ed = Editor()
ed.load("photo.jpg")                              # path | http(s) URL | bytes | Image
ed.crop("x1=10% x2=90% y1=10% y2=90%")            # crop spec (px/cm/mm/in/%/bare-px edges)
ed.rotate_right()                                 # quarter-turn clockwise (= rotate(+1))
ed.apply_filter("sepia")                          # "bw" | "sepia" | "invert" | "contour" | "none" | a colour → duotone

# Draw a red triangle over the result (coordinates are image pixels).
ed.draw(Layout(
    image_width=ed.image_size[0], image_height=ed.image_size[1],
    lines=[Line(points=[Point(50, 50), Point(750, 50), Point(400, 550)], color="#ff0000")],
))

ed.save("out.png")                     # encode the derived view → file (format from extension)
ed.save_layout("out.json")             # write the structured layout JSON
ed.save_project("proj.stencil")        # portable project file: image + layout + metadata
Editor().open_project("proj.stencil")  # …and read it back (or on any Stencil surface)
Editor.delete_project("old.stencil")   # delete a local .stencil file from disk
```

Every edit is chainable and snapshotted, so `undo()`, `redo()` and `reset()` walk the
history (64 states deep, the same cap every surface holds).

## Public API

### `Editor` (alias `Stencil`) — the facade

| Group | Methods |
|---|---|
| Source | `load(src, *, frame=, name=, source=, resource=)`, `blank(width=, height=, color="#ffffff", page="A4")` |
| Edits (chainable) | `rotate(q)`, `rotate_left()`, `rotate_right()`, `crop(spec=None, *, x1=, y1=, x2=, y2=, album=False)`, `set_filter(mode)`, `set_filter_color(color)`, `apply_filter(mode)`, `set_page_format(name, width=, height=)`, `draw(layout)`, `apply_layout(layout)` |
| History | `undo() -> bool`, `redo() -> bool`, `reset()` |
| Render / save | `result() -> Image`, `save(path, fmt=None) -> Image`, `layout() -> Layout`, `save_layout(path=None) -> str`, `save_project(path)`, `open_project(src)` |
| Introspection | `image_size -> (w, h)`, `name -> str`, `page_format -> str`, `has_image() -> bool` |

`load` accepts a local path, an `http(s)://` URL, raw `bytes`, or an `Image`. `blank`'s
`page` is any ISO A/B/C format name (an unknown name falls back to A4). `set_page_format`
takes a format name or `"custom"` with `width`/`height` in cm (0.1–500). `crop` takes a
spec string or `x1/y1/x2/y2` edge kwargs. `draw` **appends** lines from a `Layout` / dict /
JSON string / list of `Line`; `apply_layout` **adopts** a layout's rotation + crop + filter +
lines wholesale.

### `Image` — RGBA8 buffer

```python
from pystencil import Image

img = Image.open("pic.png")                 # decode via codecs (PNG/BMP)
img = Image.decode(raw_bytes)               # sniff + decode
img = Image.blank(800, 600, (255, 0, 0, 255))
img.width, img.height, img.pixel_count
png_bytes = img.encode("png")
img.save("out.bmp")                         # format from extension, default png
clone = img.copy()
```

### `Layout` / `Line` / `Point` — structured JSON

Dataclasses whose JSON shape mirrors the browser's layout export (camelCase keys, optional
fields omitted when `None`):

```python
from pystencil import Layout, Line, Point

lay = Layout(
    image_width=800, image_height=600,
    lines=[Line(points=[Point(50, 50), Point(750, 550)],
                color="#FFFF00", thickness=2.0, point_size=4.0,
                style="solid", locked=False, fill_color="transparent")],
)
text = lay.to_json(indent=2)                # -> imageWidth/imageHeight/lines[...]
again = Layout.from_json(text)              # tolerant: missing fields → defaults
```

The optional layout fields (`imageFilter`, `filterColor`, `cropRect`, `rotationQuarters`,
`pageSize`, `customPageWidth`, `customPageHeight`, the formula trio) round-trip through
`from_dict` / `from_json` like every other client.

### `Core` / `get_core()` — the ctypes binding

The low-level surface, if you want the core transforms directly:

```python
from pystencil import get_core

core = get_core()
core.parse_color("rebeccapurple")           # -> (102, 51, 153, 255)
core.page_formats()                          # -> ["A0", "A1", ..., "C10"]
core.named_page_size("B5")                   # -> (17.6, 25.0) cm
core.default_blank_size_px(21.0, 29.7)       # -> (794, 1123) px @ 96 dpi
core.rotated_dims(800, 600, 1)               # -> (600, 800)
buf = bytearray(b"\xff\x00\x00\xff" * (w * h))   # in-place ops mutate the bytearray you pass
core.apply_filter("bw", buf, w * h)
core.apply_contour(buf, w, h)
core.rasterize_line(buf, w, h, [(10, 10), (90, 90)], color="#00ff00")
```

### `ServerConnection` / `ConnectionManager` — collaboration

A REST client (`urllib`, `Authorization: Bearer <token>`) for the collaboration server:

```python
from pystencil import ServerConnection, Editor

conn = ServerConnection("http://host:8090").connect()             # mints a token
conn = ServerConnection("http://host:8090", "<token>").connect()  # an admin-gated server
conn = ServerConnection("http://host:8090#token=<tok>").connect() # an invite link
proj = conn.create_remote_project("Shared", image=Editor().load("photo.png").result())
listing = conn.list_projects()
got = conn.get_project(proj["id"])
ed = Editor().load(conn.get_file(proj["id"], "original"))
ed.apply_filter("sepia")
conn.save_remote_project(proj["id"],
                         version=got["project"]["version"],
                         layout=ed.layout().to_dict(), image=ed.result())
```

A `ConnectionManager` holds several connections at once (`connect` / `disconnect` /
`reconnect` / `remote_projects`). Version conflicts surface as `ServerError(code="conflict")`.
Self-signed TLS is opt-in via an `ssl` context option. `conn.credential_kind` reports what the
supplied credential turned out to be (`"admin"`, `"session"`, `"none"`).

The client is REST-only, so it tracks a peer's changes by **polling**: `diff_projects(prev,
curr)` is a pure diff into `{id, kind, fields, project}` events, `poll_project_changes(previous)`
is one-shot, and `watch_projects(on_change=, interval=, stop=)` is a ready-made blocking loop —
on a single connection or aggregated across a `ConnectionManager`.

## Command line

`python -m pystencil` (installed as **`stencil-py`**) runs a one-shot pipeline or a
`/command` REPL, mirroring the [CLI](../cli/README.md):

```bash
python3 -m pystencil -i photo.jpg -c "x1=10% x2=90% y1=10% y2=90%" -r 1 --filter sepia out.png
python3 -m pystencil --blank b5 pink out.png
python3 -m pystencil --source-site https://example.com --source-filter img \
  --source-min-width 200 --source-count 10 shots/          # scrape a page's media into a directory
python3 -m pystencil --repl
```

REPL commands mirror the CLI console: `/upload`, `/source-upload` (alias `/scrape`),
`/blank`, `/format`, `/crop`, `/rotate`, `/filter`, `/apply`, `/undo`, `/redo`, `/reset`,
`/save`, `/layout`, `/connect`, `/connections`, `/fetch`, `/prompt` (alias `/p`), `/llm`
and `/chat on|off|clear`. A bare command that needs arguments lists its options. `/layout
[path]` has the same path semantics as `Editor.save_layout` and the Zig CLI.

## LLM prompts

`pystencil.llm` implements the shared [LLM contract](../contracts/llm/llm-contract.md): the
model answers with an op-plan that is strictly validated and executed through the same
`Editor` methods above. Chat-only replies come back with zero actions; each plan variant
yields one extra output image.

```python
from pystencil import Editor, Chat, LlmClient, LlmConfig, execute_op_plan

# one-shot: ask the configured provider to edit this editor's image
ed = Editor().load("photo.jpg")
reply, outputs = ed.prompt("crop 10% off every edge and give me a sepia variant")
for i, img in enumerate(outputs):
    img.save("out-%d.png" % i)

# multi-turn: bounded history + the contract's image replay rule
chat = Chat(LlmClient(LlmConfig(provider="ollama", model="llama3.2-vision")))
reply, plan = chat.send("what should I crop?", images=[("image/png", ed.result().encode("png"))])
execute_op_plan(plan, ed)

# opt-in chat persistence: save the conversation with the project
ed.save_chats = True                  # OFF by default, like every surface
ed.attach_chat(chat)
ed.save_project("proj.stencil")
chat2 = Chat.from_doc(Editor().open_project("proj.stencil").chat_doc)

# several images in ONE turn: the plan's `image` ops switch the working image,
# `save` writes <name>.stencil beside the output
shots = [("image/png", open(p, "rb").read(), p) for p in ("cat.jpg", "dog.png")]
reply, _ = Editor().prompt("make each of them b&w and save them", images=shots)
```

Configuration is `LlmConfig(...)` args with env fallback (`LlmConfig.from_env()`):

| Env key | Meaning | Default |
|---|---|---|
| `STENCIL_LLM_PROVIDER` | `ollama` \| `openai-compat` \| `stencil-server` | `ollama` |
| `STENCIL_LLM_BASE_URL` | provider endpoint (`ollama`/`openai-compat`) | `http://localhost:11434` / `http://localhost:1234/v1` |
| `STENCIL_LLM_MODEL` | model name (empty = provider/server default) | empty |
| `STENCIL_LLM_API_KEY` | Bearer key, sent on `openai-compat` only | empty |
| `STENCIL_LLM_SERVER_URL` | collaboration server proxying Anthropic (`stencil-server`) | empty |

For `stencil-server`, `LlmClient(LlmConfig(provider="stencil-server"), server=conn)` reuses a
`ServerConnection`'s URL and bearer token. Two deviations from the richer clients: the `frame`
op raises `LlmExecutionError` (no video decoding here — extract frames with the CLI/desktop
and `load()` them), and attached images are not downscaled (no resampling) — pass
reasonably-sized images. Getting a provider running:
[root README → AI assistant](../README.md#ai-assistant--setting-up-a-model).
