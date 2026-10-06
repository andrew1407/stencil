# Desktop app — use cases

The Qt app, scenario by scenario. Build and run it from
[`desktop/README.md`](../../../desktop/README.md).

## Open the app

The window has a menu bar, a header with the logo and the project name, the toolbar
sections, the canvas with its **Blank image** card, and the points table. Appearance
follows the OS; **View ▸ Dark Theme** (`Ctrl+D`) flips it.

| Light | Dark |
|---|---|
| ![the app, light](img/editor-empty-light.png) | ![the app, dark](img/editor-empty-dark.png) |

## Start from a blank page or a link

**File ▸ Open Image…** (`Ctrl+O`) offers a local file, a URL, or a blank page in any fill
and size. `stencil --src <path|url>` and `stencil --blank` do the same from a shell.

![the open-image dialog](img/open-image-dialog.png)

| White page | Black page | From a URL |
|---|---|---|
| ![white blank](img/blank-white.png) | ![black blank](img/blank-black.png) | ![the logo from the repository](img/open-from-url.png) |

## Drop a file on the window

Drag an image or a `.stencil` project over the window and it splits in two: drop on the left
to open it and keep it in your projects, on the right to open it **incognito**, without saving
it. The half under the pointer lights up.

![an image or project dragged over the save / incognito halves](img/file-dropzones.png)

A `.json` layout gets one zone that draws it over the open picture, and a `.stc` script one
that runs it.

![a .json layout dragged over the window](img/layout-dropzone.png)

## Open a frame from a video

Choose a video instead and the dialog turns into a small player. Drag the bar under the
picture, or type a **Frame** number, to choose the frame the canvas opens:

![a local video, scrubbed to a frame](img/open-video-local.png)

Tick **Crop** and the box is drawn over the player itself, so the frame that lands on the
canvas is the one framed here — the Album / Portrait button flips the orientation:

![cropping a video frame before opening it](img/crop-video.png)

## Draw, select and edit points

**Edit ▸ Start Drawing** (`Alt+A`) places points on click; the selected line's style sits
in the bar above the canvas and every vertex is editable in the points table, in pixels and
page units.

| Light | Dark |
|---|---|
| ![a selected line](img/lines-selection-light.png) | ![the same, dark](img/lines-selection-dark.png) |

The **Lines** tab lists every line with its colour. Click a line's colour chip to pick a new
colour for it, with the canvas following as you drag; double-click the chip to give the line
the toolbar's line colour back. Double-clicking the toolbar's own line colour resets it to the
default yellow.

![picking a line's colour from the Lines tab](img/lines-swatch-picker.png)

## The canvas menu

Right-click (or `Shift+F10`) for the image, layout, copy, filter, transform, script and
assistant submenus.

![the context menu](img/context-menu.png)

**Make a copy** saves a copy of the open project — the image only, the image and its layout, or
the whole project with its colour, keywords, description and chat:

![the Make a copy submenu](img/context-menu-copy.png)

## Change the look

**View ▸ Visuals & Settings…** (`Alt+V`) sets the accent, appearance, motion style, drawing
defaults and app preferences. The logo click cycles accents; right-click it for the list.

| Settings, light | Settings, dark | Accent picker |
|---|---|---|
| ![settings](img/settings-dialog-light.png) | ![settings, dark](img/settings-dialog-dark.png) | ![accents](img/accent-picker.png) |

The theme switch wipes the new palette across the window:

![the theme swap](img/theme-swap.gif)

## Crop

**File ▸ Crop Image…** (`Ctrl+Shift+X`) crops inside a page-shaped box, album or portrait.

![the crop dialog](img/crop-dialog.png)

## Ask the assistant

**View ▸ AI Assistant** (`Alt+G`) opens the chat dock; it sits on any edge or floats.
Describe the edit, and the plan runs on the canvas; variants arrive as cards. Below, a real
turn through a collaboration server's proxy: the model framed the S and applied the sepia.

| Docked, light | Docked, dark |
|---|---|
| ![the chat dock](img/assistant-docked-light.png) | ![the chat dock, dark](img/assistant-docked-dark.png) |

![the floating chat](img/assistant-floating.png)

**View ▸ AI Assistant Settings…** (`Alt+Shift+G`) chooses the provider and model.

![assistant settings](img/assistant-settings-dialog.png)

## Run a script

**Data ▸ Stencil Script…** (`Alt+Shift+S`) edits and runs a `.stc` recipe with the
core's own highlighting; dropping a `.stc` file on the window runs it too. The canvas menu's
**Stencil Script** row opens the same editor beside the menu.

| Script window | From the canvas menu |
|---|---|
| ![the script dialog](img/script-dialog.png) | ![the script editor in the canvas menu](img/context-menu-script.png) |

A script can also arrive by a `stencil://` link, from the browser app or from VS Code. It opens
in the Script window, or, when the link asks to run it, the app shows it and asks first; a
script that came by link may open web images only.

![the question before a linked script runs](img/script-link-confirm.png)

## Projects and servers

**Project ▸ Projects…** (`Ctrl+Shift+P`) manages the auto-saved projects, local and on
servers; **Project ▸ Servers…** (`Ctrl+Shift+K`) connects to a collaboration server.

| Projects, light | Projects, dark | Servers |
|---|---|---|
| ![projects](img/projects-dialog-light.png) | ![projects, dark](img/projects-dialog-dark.png) | ![servers](img/connect-dialog.png) |

**Make a copy** is also on a project row's menu (right after **Open in another app**) and on
the toolbar's Image section. Pick what to copy, then answer the question: **Just copy** adds
the copy to the list and leaves everything else open, **Open** switches to it, **Open in new
window** opens it beside this one. **Open in incognito** opens the copy without ever saving it.
Copies are named after the project — `Kitchen plan-copy`, then `Kitchen plan-copy(1)`, and so
on. For a project that lives on a server, the copy is made on that server unless **Make a local
copy** is ticked, and only a local copy can open incognito.

![a project row's Make a copy submenu](img/projects-row-copy.png)

| A local project | A server project |
|---|---|
| ![the copy question](img/copy-dialog.png) | ![the copy question, with Make a local copy](img/copy-dialog-server.png) |

Drag a project row out of the list and the window behind it offers **Open here**, **Open in a new
window** and **Remove**; the zone under the pointer lights up and the drop decides which one runs.

| Drop zones, light | Drop zones, dark |
|---|---|
| ![dragging a project row out over the drop zones](img/projects-dropzones-light.png) | ![dragging a project row out, dark](img/projects-dropzones-dark.png) |

## Describe the project, hand it on

**Project ▸ Description…**, **Keywords…** and **Image Links…** keep the project's metadata;
**Project ▸ Open In…** sends it to the browser app or the Telegram bot.

| Description | Keywords |
|---|---|
| ![description](img/description-dialog.png) | ![keywords](img/keywords-dialog.png) |

| Image links | Open In… |
|---|---|
| ![image links](img/links-dialog.png) | ![open in](img/open-in-dialog.png) |

## Shortcuts and help

**Help ▸ Keyboard Shortcuts…** (`Alt+K`) rebinds any action live; **Help ▸ Controls &
Shortcuts Info** lists the gestures.

| Shortcuts | Help |
|---|---|
| ![keyboard shortcuts](img/shortcuts-dialog.png) | ![help](img/help-dialog.png) |
