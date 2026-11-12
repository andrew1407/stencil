<p align="center">
  <a href="https://github.com/andrew1407/stencil"><img src="https://raw.githubusercontent.com/andrew1407/stencil/v1.0.0/vscode-extension/icon.png" width="112" alt="Stencil"></a>
</p>
<h1 align="center">Stencil — MCP server</h1>
<p align="center">
  Give Claude Code, Claude Desktop or any MCP client real image tools: crop, rotate, filter, draw,<br>
  grab video frames, scrape a page's media, run <code>.stc</code> scripts and manage server projects.
</p>
<p align="center">
  <a href="https://hub.docker.com/r/andrew1407/stencil-mcp/tags"><img src="https://img.shields.io/docker/v/andrew1407/stencil-mcp?sort=semver&logo=docker&label=version" alt="version"></a>
  <img src="https://img.shields.io/docker/image-size/andrew1407/stencil-mcp?sort=semver&label=size" alt="image size">
  <a href="https://github.com/andrew1407/stencil/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-Apache--2.0-blue" alt="Apache-2.0"></a>
</p>

The server speaks MCP over **stdio** and does every pixel through the Stencil CLI, which this image
carries along with `ffmpeg` — so results match the browser and desktop editors exactly.

## Quick start

Register it with Claude Code from the folder it should work in:

```sh
claude mcp add stencil -- docker run --rm -i -v "$PWD:$PWD" -w "$PWD" andrew1407/stencil-mcp
```

Or in any client's `mcpServers` config:

```json
{
  "mcpServers": {
    "stencil": {
      "command": "docker",
      "args": ["run", "--rm", "-i", "-v", "/path/to/work:/path/to/work", "-w", "/path/to/work",
               "andrew1407/stencil-mcp"]
    }
  }
}
```

Mount the folder **at the same path** inside the container: a client hands the server its workspace
as host paths, and every write must land inside them. `-i` is required — MCP runs over stdin.

Then ask in plain words: *"Crop photo.jpg to its centre 80%, turn it right and save out.png"*,
*"Grab frame 24 of clip.mp4"*, *"Check tour.stc and tell me what it would write"*.

## Tools

| Tool | Purpose |
|---|---|
| `stencil_edit` | the whole pipeline — crop, rotate, layout, filter — with an optional preview thumbnail |
| `stencil_probe` | an image's size, format and alpha, or a video's length |
| `source_site` | download a web page's matching media into a folder |
| `stencil_prompt` | plan an edit with a configured model, validate it, run it |
| `stencil_script` · `_check` · `_plan` · `_emit` | run, check, dry-run or convert a `.stc` script |
| `stencil_projects` · `stencil_project_update` · `stencil_project_file` | list, edit and download a collaboration server's projects |

Read-only resources (the op registry, the `.stc` contract, page formats, colours, filters) and two
prompts (`write_stc_script`, `print_page_layout`) come with it.

## Environment

Pass with `-e NAME=value` in the `docker run` arguments.

| Variable | Default | Meaning |
|---|---|---|
| `STENCIL_MCP_ROOTS` | the working directory | where writes may land, when the client offers no roots |
| `STENCIL_MCP_SERVERS` | none | collaboration servers a call may reach (comma list of origins) |
| `STENCIL_MCP_SERVER_TOKENS` | none | `origin=token` pairs for those servers — never a tool parameter |
| `STENCIL_CLI_TIMEOUT_SECONDS` | `120` | per-call deadline |
| `STENCIL_LLM_PROVIDER`, `STENCIL_LLM_BASE_URL`, `STENCIL_LLM_MODEL`, `STENCIL_LLM_API_KEY` | `ollama` | the model behind `stencil_prompt` (plain-http endpoints; a key goes to loopback only) |

The `desktop` and `browser` delivery surfaces need those apps beside the server, so in a container
the results are files (the `cli` surface).

## Tags

| Tag | |
|---|---|
| `1.0.0` | this release |
| `latest` | the newest release |

Every tag is built for **linux/amd64** and **linux/arm64**.

## More

- Source and docs — [github.com/andrew1407/stencil](https://github.com/andrew1407/stencil) ·
  [MCP guide](https://github.com/andrew1407/stencil/blob/main/mcp/README.md) ·
  [changelog](https://github.com/andrew1407/stencil/blob/main/mcp/CHANGELOG.md)
- The other images — [browser](https://hub.docker.com/r/andrew1407/stencil-browser) ·
  [cli](https://hub.docker.com/r/andrew1407/stencil-cli) ·
  [server](https://hub.docker.com/r/andrew1407/stencil-server) ·
  [bot](https://hub.docker.com/r/andrew1407/stencil-bot)
