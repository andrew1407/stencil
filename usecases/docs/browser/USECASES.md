# Browser app — use cases

What the editor looks like and what you do with it, one scenario at a time. Build, run and
configure it from [`browser/README.md`](../../../browser/README.md). Keyboard shortcuts are
written with Ctrl; on a Mac they use ⌘ instead.

## Open the app

Serve the app (`cd browser && npm run serve`) and open <http://localhost:8080>. You get the
toolbar, an empty canvas with a **Blank image** card, and the points table. The theme
follows your OS; the moon/sun button (`Ctrl+D`) flips it.

| Light | Dark |
|---|---|
| ![the editor, light](img/editor-empty-light.png) | ![the editor, dark](img/editor-empty-dark.png) |

## Start from a blank page, a file or a link

**Open Image** (`Ctrl+O`, or the **Blank image** card) has three tabs: **Local file**,
**URL link** and **Blank**. **Blank** takes a fill — **White**, **Black** or any colour — and a
size, then **Create blank**.

![a white page created, then three points clicked in](img/create-blank.gif)

Under **URL link**, paste an `http(s)` address and press **Preview**. **Crop** trims the picture
to the page's shape before it opens, **Incognito** opens it without ever saving it, and **Open
here** or **Open in new tab** finishes. Here, a picture straight from the repository:

![the Open Image window previewing a picture from a link](img/open-image-modal.png)

![the picture open in the editor](img/open-from-url.png)

With a picture already open, the window also offers **Replace image**: the new picture takes the
old one's place, and **Keep existing annotations** keeps the lines drawn on it.

## Drop a file on the page

Drag an image or a `.stencil` project over the page and it splits in two: drop on the left to
open it and keep it in your projects, on the right to open it **incognito**, without saving it.
The half under the pointer lights up.

![an image or project dragged over the save / incognito halves](img/file-dropzones.png)

A `.json` layout opens no image, so it gets one zone that draws the layout over the open
picture. A `.stc` script runs from either half — or, while the Script window is open, lands in
it instead.

![a .json layout dragged over the page](img/layout-dropzone.png)

## Open a frame from a video

Pick a video the same way — a local file or a link — and the window turns into a small
player. Drag the bar under the picture, or type a **Frame** number, to choose the frame the
canvas opens:

![a local video, scrubbed to a frame](img/open-video-local.png)

![the same clip opened from a link](img/open-video-url.png)

Tick **Crop** and the box is drawn over the player itself, so the frame that lands on the
canvas is the one framed here. The Album / Portrait button flips the orientation:

![cropping a video frame before opening it](img/crop-video.png)

## Draw lines and shapes

Press **Start** (`Alt+A`, or press `~` twice) and click on the picture to place points; the
same key stops, and the next run of clicks makes a new line.

![two lines clicked in point by point, then the sepia filter](img/edit-draw.gif)

The **Line** button beside **Start** switches to **Rect**: drag across the picture to sweep a
rectangle. Back on **Line**, clicking a line's first point again closes it into an area. A
selected rectangle or area gets **Fill** in the selected-line bar — a colour and its opacity,
0 to 255 — and **Unchain**, which opens it back into a line.

![a rectangle swept and filled, then a second area closed by clicking its first point again](img/draw-shapes.gif)

Selecting a line in the **Lines** tab raises the selection bar, where colour, thickness, point
size and style change for that line only. The **Points** tab lists the selected line's points in
pixels and in page units (cm or in), editable in place.

| Light | Dark |
|---|---|
| ![a selected line and the Lines tab](img/lines-selection-light.png) | ![the same, dark](img/lines-selection-dark.png) |

The **Lines** tab names each line — `Line 1`, `Line 2`… until you double-click the name and type
your own — and its eye hides the line from the canvas, dimming its row, and shows it again.

Click a line's colour swatch in the **Lines** tab to pick a new colour for it; double-click the
swatch to put it back to the toolbar's line colour. Double-clicking any other colour swatch puts
it back to its default — the toolbar's line colour to yellow, its point colour to the line's, the
tint and the blank page's fill to theirs, and the selection bar's colour to the toolbar's.

Each row also carries the line's thickness, its points' colour and their size. Double-click a
number to type a new one — Enter keeps it, Escape leaves it as it was — and click the round swatch
to pick the points' colour; double-click it to reset them to the toolbar's point colour. None of
these changes which line is selected.

Drag one colour swatch onto another to copy its colour across — the toolbar's line or point
colour onto a row in the **Lines** tab, the selection bar's onto the toolbar's, and so on. While you
drag, a swatch of the colour rides beside the pointer and every swatch that can take it glows; the
one you let go on changes, through the same path a pick would.

![the toolbar's line colour dragged onto a line's swatch](img/drag-color.gif)

On a phone, fold the controls with **Controls** and drag one finger across the picture to move
around it — a quick tap still selects, and a press held still starts a line.

![one finger panning a zoomed picture on a phone](img/touch-pan.gif)

## Filter, crop, flip and rotate

The filter list in the **Edit** section holds **B&W**, **Sepia**, **Invert**, **Contour** and
**Tint**; resting on a row previews it before you pick it, and `Alt+B` steps through them.

![the filter list, previewing Sepia](img/filter-menu.png)

**Crop** (`Ctrl+Shift+X`) opens a crop box locked to the page's shape: **Portrait** flips it to
album, **Apply Crop** keeps it. The window opens sized around the picture, as large as the screen
allows, and resizing it fits the picture to its new room. Flip (`Alt+Shift+F`) mirrors the
picture left to right and the two rotate buttons turn it by quarter turns. The lines follow a
flip and a turn, the zoom and the spot you were looking at stay put, and undo (`Ctrl+Z`) and redo
(`Ctrl+Shift+Z`) cover all of it.

![the crop window](img/crop-modal.png)

## Compare before and after

**Compare** in the **View** section shows the picture as it was: **Original**, or a split —
**Split ↔** or **Split ↕** — with a divider you drag. `Alt+O` steps through them; hold
`Alt+Shift+O` to see the original until you let go.

![a split compare: the original on the left, sepia on the right](img/compare-split.png)

## Zoom, pan and page size

The **Zoom** section fits the picture to the window (`Alt+0`), steps out and in (`Alt+↓`,
`Alt+↑`) and takes a percentage. Ctrl+wheel zooms toward the pointer (Shift for bigger steps,
Control on a Mac), Shift+drag zooms to a rectangle, and Alt+drag on an empty spot, a
middle-button drag or the arrow keys move around.

**Page** sets the page the picture is measured against — an ISO A, B or C size, or **Custom** in
cm — and the units, cm or in, the **Points** tab shows. The crop box and a blank page take the
page's shape. **𝑓(x,y)** turns on formulas that transform those page coordinates.

![the page sizes](img/page-size.png)

## The canvas menu

Right-click the picture (or `Shift+F10`) for drawing, view, style and filter controls, copying
and downloading the image and its layout, **Make a Copy**, and the **Assistant** and **Stencil
Script**, which open right beside the menu. **Transformation** holds the 𝑓(x,y) formulas.

![the canvas menu](img/context-menu.png)

**Make a Copy** copies the open project: the picture alone, the picture with its lines and
layout, or the whole project with its colour, keywords, description and chat.

![the Make a Copy submenu](img/context-menu-copy.png)

**Stencil Script** opens a script editor beside the menu, for a recipe run without leaving the
picture.

![the script editor beside the canvas menu](img/context-menu-script.png)

## Drag a toolbar icon

With a mouse or pen, an icon dragged off the toolbar carries its action to wherever you let go.
Let go back on the icon, or press Escape, and nothing changes.

An icon that opens a window — Projects, Servers, Crop, Script, Visuals & Settings and the rest —
opens it with its corner on the release point; a window already open moves there. The chat icon
raises the dock zones: drop in one to dock the chat on that side, anywhere else to float it there.

![the Projects icon dragged out, opening its window where it lands](img/drag-icon-window.gif)

Rotate, flip and **Clear All Lines** (the eraser, which then asks nothing) act only when dropped
on the picture: the canvas frame glows while you drag and brightens under the pointer.

![rotate dragged onto the canvas](img/drag-icon-canvas.gif)

Drag **+** or **−** and the zoom follows the pointer: the farther from the button, the closer in or
farther out. Drag **Fit to window** and the two light up as targets: hold it over **+** to step in,
over **−** to step out, and drop it back on its button to return to where you started.

![zoom + dragged away from the toolbar](img/drag-icon-zoom.gif)

### Clean view from the logo

Drag the logo in the header over the picture and it previews its clean view: no filter, no lines,
no points, no compare split. Move off and the picture comes back as it was; let go over it and the
clean view is applied, with the filter, **Points**, **Lines** and **Compare** controls following.

The logo resets other things too. Let go over a control and it goes back to its default: the line
colour turns yellow, the point colour takes the line's, the tint goes back to violet, **Thickness**
to 2, the filter to none, the page to A3, a formula field empties and **𝑓(x,y)** switches off.
Let go over a line — on the picture, on its row in the **Lines** tab or on the selected-line bar —
and that line takes the style the toolbar is set to. Whatever the logo would change glows while it
is over it.

![the logo dragged over a sepia picture with lines](img/drag-logo-clean.gif)

## Change the look

**Visuals & Settings** (`Alt+V`) holds the accent colour, light/dark/system appearance, the
interface motion (Dust, Water, Fire, Sliding, None), how fast it plays (**Animation speed**, 0.1
to 1), how fast the console types (**Console text speed**), notifications and the drawing
defaults. Double-click a row's title to put that setting back to its default.

Clicking the logo cycles the accent; right-click it for the whole list.

| Visuals & Settings | The logo's accent list |
|---|---|
| ![the visuals window](img/visuals-modal.png) | ![the accent list beside the logo](img/accent-picker.png) |

With **Multiple windows** on, windows like Projects or Visuals open side by side and stay open:
nothing behind them dims, and a click outside closes none of them.

The theme switch sweeps the new palette in from the button you pressed:

![the theme swap](img/theme-swap.gif)

Drag the moon/sun switch instead of clicking it and a round lens follows the pointer, showing the
page under it in the other theme. It is a preview only: wherever you let go, or on Escape, the lens
closes and the theme stays as it was.

![the theme lens dragged across the page](img/theme-lens.gif)

## Work in fullscreen

Fullscreen (`Alt+F`) hides the toolbar so the canvas fills the screen; it slides back in when
the pointer touches the top edge, and leaves again when the pointer moves away.

![the toolbar sliding in at the top edge in fullscreen, and leaving again](img/fullscreen-strip.gif)

## Ask the assistant

Open the chat with the sparkle button (`Alt+G`), describe the edit, and the model answers
with a plan that runs through the same operations as the toolbar. Other takes come back as
cards you can download or open. The panel docks on any edge or floats.

![a turn: the S framed with a dashed red rectangle, the picture warmed with sepia, two more takes as cards](img/assistant-turn.gif)

| Docked | Floating |
|---|---|
| ![the docked chat](img/assistant-docked.png) | ![the floating chat](img/assistant-floating.png) |

Point it at a model in **Assistant settings** (`Alt+Shift+G`): **Ollama**, an **OpenAI API**
server such as LM Studio or vLLM, the **Anthropic API (Claude)** with your own key — kept for this
tab only — or a **Stencil server** that proxies its own key. **Save chats with projects** keeps the
conversation with the project.

![assistant settings pointed at a local Ollama](img/assistant-settings-modal.png)

## Run a script

The script window (`Alt+Shift+S`) takes a `.stc` recipe and colours it as you type; **Run**
(`Ctrl+Enter`) runs it and names any error by its line. Drop a `.stc` file on the page to run it
at once.

![typing a script, running it, and the picture it leaves behind](img/script-run.gif)

![the script window](img/script-modal.png)

A script can also arrive by a link — the VS Code extension hands one over in the page's URL
fragment. It opens in the Script window, highlighted and ready to edit; nothing runs until you
press **Run**.

![a script that arrived by a link, waiting in the Script window](img/script-link.png)

## Save and export

**Download** (`Ctrl+Shift+D`) saves the picture and **Copy** (`Ctrl+C`) puts it on the
clipboard. Right-click either for the choices: **Current** (with the filter, lines and points),
**Original** (with neither), **Filter Only** (no lines or points) and, while a split compare is
up, **With Compare**.

![the download choices](img/export-menu.png)

**Save Project** (`Ctrl+Shift+S`) writes the picture, its lines and its settings to one
`.stencil` file — Shift+click leaves the theme out — and **Open Project** (`Ctrl+Shift+F`) reads
one back. Once a project is tied to a file, the refresh button beside them keeps the two in step,
saving each edit and picking up changes made to the file. The **Data** section copies, downloads
and uploads the layout as JSON, and `Ctrl+V` pastes a picture or a layout.

## Projects

Every editor auto-saves into a local project. **Projects** (`Ctrl+Shift+P`) lists them, the one
you opened most recently first (or by name, by last edit, or in your own order), and searches
their names and keywords. A project you leave unopened expires after 7 days unless you give it
longer; one kept forever says **never expires**.

![the projects window](img/projects-modal.png)

A row's **⋯** menu opens, renames, colours, describes and expires it, and has **Make a copy ›**
beside **Open in another app**; the toolbar's Image section has the same list behind its copy
button. Pick **Image only**, **Image and layout** or **Whole project**, and a short question
names the copy — `Kitchen plan-copy`, then `Kitchen plan-copy(1)`, and so on. **Just copy** adds it
to the list and leaves everything else as it was, **Open** switches to it, and **Open in new tab**
opens it beside this one. Tick **Open in incognito** to open a copy that is never saved; for a
project that lives on a server, **Make a local copy** chooses between a copy in this browser and
one on that server.

| The row menu | The toolbar | The question |
|---|---|---|
| ![a project row's Make a copy submenu](img/projects-row-copy.png) | ![the toolbar's copy list](img/copy-toolbar-menu.png) | ![the copy question](img/copy-modal.png) |

Drag a row **out of the list** and the window behind it turns into three drop targets: **Open
here** on the left, **Open in a new tab** on the right, and **Remove** across the bottom. The one
under the pointer lights up, the row rides along under the cursor, and the drop decides the
action — dropping back on the list reorders instead. A **⋯** beside the window's title opens that
row's menu, and releasing over an item runs it. Drop the row of the project open in this tab on
the window's **Close** button to close that project here, as **Close Current Project**
(`Alt+Shift+W`) does.

![a project row dragged out over the drop zones](img/projects-dropzones.png)

## Work together on a server

**Servers** (`Ctrl+Shift+K`) connects to a Stencil collaboration server with its URL and a
token. **Auto-connect on open** and **Sync changes to server** are on by default, and a dropped
connection reconnects by itself. Connected with the server's admin token, a row also offers an
invite link: it mints a fresh token and copies a link that connects someone else.

![the Servers window with a connection](img/connect-modal.png)

A project moves to the server from its row's **⋯** menu (**Move to server** or **Copy to
server**), or starts there through **Save to** in the Open Image window. Server projects wear a
gold outline in **Projects**, and anyone connected can open them.

![Projects with a server project among the local ones](img/projects-server.png)

Everyone who has a server project open edits the same picture, and each person's changes appear
on the others' screens by themselves.

![two people on one server project: a line drawn on the left appears on the right, a rectangle on the right appears on the left](img/coedit.gif)

## Describe the project

The **Description & attributes** section keeps a project's description, its keywords (what
the Projects search matches) and the image's source and resource links.

| Description | Keywords | Image links |
|---|---|---|
| ![description](img/description-modal.png) | ![keywords](img/keywords-modal.png) | ![image links](img/links-modal.png) |

## Hand a project to another app

**Open In…** (`Ctrl+Shift+E`) sends the current project to the desktop app — and, when a
Telegram bot is configured and the project lives on a server, to the bot. A link shared from a
chat lands on the bounce page first, which forwards to the `stencil://` scheme the desktop app
registers.

![the Open In window](img/open-in-modal.png)

![the launch page](img/launch-page.png)

## Type commands in the console

The console icon in the Data section (`Alt+Shift+T`) opens a console window beside the editor. It
blocks nothing: type `/help` for the commands, `/crop x1=10% x2=90%`, `/rotate 1`, `/filter bw`,
`/theme dark`, `/connect <url>` or `/desktop`, and the answer types in at the **Console text
speed**. A bare `/upload` opens the file picker.

![the console window beside the editor, after /help](img/console-modal.png)

## Shortcuts and help

Every action is rebindable in **Keyboard Shortcuts** (`Alt+K`); a few fixed keys are not, such as
pressing `~` twice, holding `Alt+Shift+O` and the arrow keys. **Help** (`Alt+H`) opens
**Controls & Shortcuts Info**, which lists the mouse and keyboard gestures.

| Shortcuts | Help |
|---|---|
| ![keyboard shortcuts](img/shortcuts-modal.png) | ![help window](img/help-modal.png) |
