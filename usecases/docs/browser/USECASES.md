# Browser app — use cases

What the editor looks like and what you do with it, one scenario at a time. Build, run and
configure it from [`browser/README.md`](../../../browser/README.md).

## Open the editor

Serve the app (`cd browser && npm run serve`) and open <http://localhost:8080>. You get the
toolbar, an empty canvas with a **Blank image** card, and the points table. The theme
follows your OS; the moon/sun button (`Ctrl+D`) flips it.

| Light | Dark |
|---|---|
| ![the editor, light](img/editor-empty-light.png) | ![the editor, dark](img/editor-empty-dark.png) |

## Start from a blank page

1. Click **Blank image** on the empty canvas (or the Open Image button, `Ctrl+O`).
2. Pick **Blank**, choose a fill (White, Black, or any colour) and a size, then **Create blank**.
3. Press **Start** (`Alt+A`) and click on the canvas to place points; **Stop** with `Alt+S`.

![create a blank page and draw](img/create-blank.gif)

| White page | Black page |
|---|---|
| ![white blank](img/blank-white.png) | ![black blank](img/blank-black.png) |

## Open an image from a link

Open Image (`Ctrl+O`) takes a local file, a URL, or a blank page. Paste an `http(s)`
address under **URL link** and open it; here the app's own logo, fetched straight from the
repository:

![the open-image window](img/open-image-modal.png)

![an image opened from a URL](img/open-from-url.png)

## Open a frame from a video

Pick a video the same way — a local file or a link — and the window turns into a small
player. Drag the bar under the picture, or type a **Frame** number, to choose the frame the
canvas opens:

![a local video, scrubbed to a frame](img/open-video-local.png)

![the same clip opened from a link](img/open-video-url.png)

Tick **Crop** and the box is drawn over the player itself, so the frame that lands on the
canvas is the one framed here. The Album / Portrait button flips the orientation:

![cropping a video frame before opening it](img/crop-video.png)

## Draw, select and edit points

Lines are polylines or rectangles. Selecting a line in the **Lines** tab raises the
selection bar, where colour, thickness, point size and style change for that line only. The
**Points** tab lists every vertex in pixels and in page units (cm or in), editable in place.

| Light | Dark |
|---|---|
| ![a selected line and its points](img/lines-selection-light.png) | ![the same, dark](img/lines-selection-dark.png) |

## The canvas menu

Right-click the canvas (or `Shift+F10`) for everything the toolbar has, plus filters,
transforms, a script flyout and the assistant, without leaving the picture.

![the canvas context menu](img/context-menu.png)

## Change the look

**Visuals & Settings** (`Alt+V`) holds the accent colour, light/dark/system appearance,
the interface motion style (Dust, Water, Fire, Sliding, None) and the drawing defaults.
Clicking the logo cycles the accent; right-click it for the full list.

| Visuals & Settings | Accent picker |
|---|---|
| ![the visuals window](img/visuals-modal.png) | ![the accent list](img/accent-picker.png) |

The theme switch sweeps the new palette in from the button you pressed:

![the theme swap](img/theme-swap.gif)

## Crop, rotate, filter

**Crop** (`Ctrl+Shift+X`) opens a page-locked crop box; the Edit section rotates by quarter
turns and applies Black & White, Sepia, Invert, Contour or a tint. Undo and redo cover all
of it.

![the crop window](img/crop-modal.png)

## Ask the assistant

Open the chat with the sparkle button (`Alt+G`), describe the edit, and the model answers
with a plan that runs through the same operations as the toolbar. Variants come back as
cards you can open or save. The panel docks on any edge or floats. The stills below are a
real turn through a collaboration server's proxy: the model saw the picture, framed the S
and applied the sepia itself.

![an assistant turn](img/assistant-turn.gif)

| Docked right | Floating |
|---|---|
| ![the docked chat](img/assistant-docked.png) | ![the floating chat](img/assistant-floating.png) |

Point it at a model in **Assistant settings** (`Alt+Shift+G`): a local Ollama, any
OpenAI-style server, or a Stencil collaboration server that proxies its own key.

![assistant settings](img/assistant-settings-modal.png)

## Run a script

The script window (`Alt+Shift+S`) takes a `.stc` recipe, colours it as you type, names any
error by line, and runs it with `Ctrl+Enter`. Drop a `.stc` file on the page to do the same.

![typing and running a script](img/script-run.gif)

![the script window](img/script-modal.png)

A script can also arrive from somewhere else — the VS Code extension hands one over in the
page's URL fragment. It runs on the picture as the page opens, and the window shows the
source that acted.

![a script handed over from VS Code](img/script-handoff.png)

## Projects and servers

Every editor auto-saves into a local project; **Projects** (`Ctrl+Shift+P`) lists, renames,
duplicates and expires them. **Servers** (`Ctrl+Shift+K`) connects to a collaboration
server, after which projects can be shared by link and edited live with others.

| Projects | Servers |
|---|---|
| ![the projects window](img/projects-modal.png) | ![the servers window](img/connect-modal.png) |

Drag a row **out of the list** and the window behind it turns into three drop targets: **Open
here** on the left, **Open in a new tab** on the right, and **Remove** across the bottom. The one
under the pointer lights up, the row rides along under the cursor, and the drop decides the
action — dropping back on the list reorders instead.

![a project row dragged out over the drop zones](img/projects-dropzones.png)

## Describe the project

The **Description & attributes** section keeps a project's description, its keywords (what
the Projects search matches) and the image's source and resource links.

| Description | Keywords | Image links |
|---|---|---|
| ![description](img/description-modal.png) | ![keywords](img/keywords-modal.png) | ![image links](img/links-modal.png) |

## Hand a project to the desktop app

**Open In…** (`Ctrl+Shift+E`) sends the current project to the desktop app or the Telegram
bot. A link shared from a chat lands on the bounce page first, which forwards to the
`stencil://` scheme the desktop app registers and offers the download when nothing answers.

![the Open In window](img/open-in-modal.png)

| Light | Dark |
|---|---|
| ![the launch page](img/launch-page-light.png) | ![the launch page, dark](img/launch-page-dark.png) |

## Shortcuts and help

Every action is rebindable in **Keyboard Shortcuts** (`Alt+K`); **Help** (`Alt+H`) lists the
mouse and keyboard gestures. In fullscreen (`Alt+F`) the toolbar hides and slides back in
when the pointer touches the top edge.

| Shortcuts | Help |
|---|---|
| ![keyboard shortcuts](img/shortcuts-modal.png) | ![help window](img/help-modal.png) |

![the fullscreen strip](img/fullscreen-strip.png)
