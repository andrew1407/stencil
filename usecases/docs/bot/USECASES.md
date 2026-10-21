# Telegram bot — use cases

Editing from a chat. Run the bot from [`bot/README.md`](../../../bot/README.md).

## Say hello

`/start` answers with the main menu: **💬 Chat with assistant**, **❓ Help**, **ℹ️ Status**,
**🖼 Sources**, **🆕 Blank**, **🔌 Connect**, **📁 Projects**, **➕ Create** and **💾 Save**.
The bot answers the accounts its operator lets in; anyone else can still use `/start` and
`/help`, and is told "This bot isn't enabled for your account." for the rest.

![/start](img/start-menu.png)

## Send a photo

A photo, an image file or a link to an image becomes the working image. Caption it with a
command — `/crop …` or `/filter bw` — and the edit applies at once; send several photos together
with a caption and it runs on each, the results coming back as one album. **🖼 Sources** lists
the other ways in: `/url` for a single image link, `/sourcesite` to collect a page's pictures.

The reply carries the edit keyboard: **🎛 Edit**, **🎨 Filter**, **✏️ Draw**, **↩️ Undo**,
**↪️ Redo**, **💬 Chat**, **⬇️ Download**, **🧼 Reset**, **💾 Save**, **✏️ Rename** and
**📝 Describe**.

![a photo and its edit keyboard](img/photo-edit-menu.png)

## The reply buttons

**🎛 Edit**, **🎨 Filter**, **✏️ Draw** and **⬇️ Download** turn the keyboard under the picture
into their own choices, and **« Back** brings the edit keyboard back. Edit offers **↔️ Flip**,
the two quarter turns and **✂️ Crop…**; Draw offers **✏️ Draw…** (how to draw), **🩹 Undo line**
and **🧹 Clear lines**.

| Edit | Draw | Download |
|---|---|---|
| ![the Edit choices](img/menu-edit.png) | ![the Draw choices](img/menu-draw.png) | ![the Download choices](img/menu-download.png) |

**🖼 Image** sends the current picture again as a photo, **📦 Project** sends the whole project as
a `.stencil` file, and **📄 Layout JSON** — once there is an edit — sends the drawn lines as a
`.json` file. Send the `.stencil` file back later to open the project again.

## Crop, filter, draw

`/crop`, `/rotate`, `/filter` and `/blank` take the same values as the CLI's `--crop`,
`--rotate`, `--filter` and `--blank`; the buttons offer the same edits.

| `/crop x1=10% x2=90% y1=10% y2=90%` | **🎨 Filter** ▸ **⚫ B&W** |
|---|---|
| ![the cropped photo](img/crop.png) | ![the photo in black and white](img/filter-bw.png) |

![the Filter choices](img/filter-menu.png)

`/draw` is the bot's own. Its points are pixels, or percentages of the picture as the chat shows
it — after its crop and turns — so `/draw rect 20%,20% 80%,80%` frames the middle of what you
see. `line` takes two or more points and `poly` three or more, closing the shape; **🩹 Undo line**
takes the last line back and **🧹 Clear lines** all of them.

![/draw rect 20%,20% 80%,80% framing the middle of the cropped picture](img/draw-rect.png)

## Choose the pen

`/color`, `/thickness`, `/points`, `/style` (solid, dashed or dotted) and `/fill` set the pen for
the lines that come next, and `/pen` shows it. A fill paints closed shapes only.

![the pen set to a red, 3 px dashed line, then a line drawn with it](img/pen.png)

## Undo, redo, start over

**↩️ Undo** and **↪️ Redo** step through the edits one at a time, **🧼 Reset** clears every edit
and keeps the picture, and `/drop` forgets the picture itself.

![Undo takes the dashed line back](img/undo.png)

## Name it, describe it

**✏️ Rename** asks for a name and takes the next message you send as it. **📝 Describe** shows
the current description and takes the next message as the new one; send `-` to clear it. Both
carry into the project when you save it on a server.

![a name and a description given in the chat](img/rename-describe.png)

## Ask the assistant

`/prompt <text>` hands the working image and your words to the model the bot is configured
with; the plan runs and the result comes back as a photo. When the operator offers more than one
model, `/chatapi` picks the one the assistant uses.

![a /prompt reply and its result](img/prompt.png)

**💬 Chat** (or `/chat`) turns chat mode on: from then on a plain message is a prompt and the
assistant answers, editing the image, until **🚪 Chat off**. **🧹 Clear chat** forgets the
conversation.

| Chat mode on | A plain message edits the image |
|---|---|
| ![chat mode](img/chat-mode.png) | ![a chat turn](img/chat-turn.png) |

## Run a script

`/script @crop 10% ; @filter sepia ; @line (0,0) (100%,100%)` runs `.stc` statements on the
image, and a `.stc` file runs the same way when you send it. A script that draws puts its own
lines in place of the ones already on the picture.

![/script on a photo, and its result](img/script.png)

## Send a file

A `.stc` file runs as a script, a `.stencil` file opens that project, and a `.json` layout draws
its lines on the working image in place of the ones already there. A video becomes its first
frame; `/frame 30` — or a video captioned `/frame 30` — picks another.

| A `.stc` script | A `.json` layout | A `.stencil` project |
|---|---|---|
| ![a .stc file run as a script](img/upload-script.png) | ![a .json layout drawn on the photo](img/upload-layout.png) | ![a .stencil file opened](img/upload-project.png) |

![a video, its first frame, then /frame 30](img/video-frame.png)

## Status and a blank page

`/status` shows the working image, its pending edits, the active project, the pen and the
connections; `/blank b5 black` starts a page.

| `/status` | `/blank b5 black` |
|---|---|
| ![status](img/status.png) | ![a black B5 page](img/blank.png) |

## Work on a server

`/connect <url> [token]` connects to a Stencil collaboration server. `/create Kitchen plan` saves
the working image there as a new project, and from then on **💾 Save** (or `/save`) writes your
edits back to it; `/sync on` does that after every edit and pulls in what others change. Until a
server project is open, Save answers "No active server project — /fetch or /create one first."
`/projects` lists the projects as buttons, and a tap — or `/fetch <name>` — opens one. While one
is open, **🔗 Link** (a link that opens it in the desktop app), **⏳ Expiration** and
**🗑 Remove** join the keyboard.

| Saved to the server | `/projects` |
|---|---|
| ![a server project saved back, with its own row of buttons](img/server-save.png) | ![the server's projects as buttons](img/server-projects.png) |

## Arrive from another app

**Open In…** in the browser and desktop apps can hand a server project to the bot: the link
opens this chat, the bot connects to that server if it has to and opens the project — or, when it
cannot, answers with the `/connect` and `/fetch` commands to run yourself.

![a server project arriving from another app's Open In…](img/open-in-arrival.png)

## Every command

`/help` lists the commands by group — image, drawing and server — with the main menu under
them.

![/help, with the main menu under it](img/help.png)
