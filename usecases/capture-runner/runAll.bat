@echo off
rem Regenerates every use-case image except the bot's (that one needs a Telegram login; see
rem README.md). Runs the apps one after another: several at once fight over the machine.
setlocal
cd /d "%~dp0..\.."

set PY=python
where python3 >nul 2>nul && set PY=python3

node usecases\capture-runner\browser.mjs || exit /b 1
%PY% usecases\capture-runner\cli_listings.py || exit /b 1
node usecases\capture-runner\cliRender.mjs || exit /b 1
node usecases\capture-runner\cliTerminal.mjs || exit /b 1
node usecases\capture-runner\browserExtension.mjs || exit /b 1
node usecases\capture-runner\desktop.mjs || exit /b 1
node usecases\capture-runner\vscodeExtension.mjs || exit /b 1

echo.
echo changed images:
git status --porcelain usecases | findstr /R "\.png$ \.gif$" || echo   none
