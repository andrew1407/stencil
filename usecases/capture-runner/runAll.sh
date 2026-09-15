#!/usr/bin/env bash
# Regenerates every use-case image except the bot's (that one needs a Telegram login; see
# README.md). Runs the apps one after another: several at once fight over the machine.
set -euo pipefail
cd "$(dirname "$0")/../.."

node usecases/capture-runner/browser.mjs
python3 usecases/capture-runner/cli_listings.py
node usecases/capture-runner/cliRender.mjs
node usecases/capture-runner/cliTerminal.mjs
node usecases/capture-runner/browserExtension.mjs
node usecases/capture-runner/desktop.mjs
node usecases/capture-runner/vscodeExtension.mjs

echo
echo "changed images:"
git status --porcelain usecases | grep -E '\.(png|gif)$' || echo "  none"
