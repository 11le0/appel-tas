@echo off
cd /d "%~dp0"

where node >nul 2>nul || (echo Node.js is not installed. See README.md & pause & exit /b 1)

if not exist randy\randy.exe (
    where gcc >nul 2>nul || (echo gcc not found. Install w64devkit and add its bin folder to PATH. See README.md & pause & exit /b 1)
    echo Building Randy ^(first run only^)...
    gcc -O3 -std=gnu11 -pthread randy\randy_cli.c randy\explorer.c randy\physics.c randy\spikes.c randy\level_decoder.c -o randy\randy.exe -lm || (echo Build failed & pause & exit /b 1)
)
if not exist bruteforce.exe (
    where gcc >nul 2>nul && (
        echo Building the Brute Force Solver ^(first run only^)...
        gcc -O3 -std=gnu11 -pthread main.c physics.c level.c playerstate_json.c hashset.c expr.c spikes.c -o bruteforce.exe -lm
    )
)

cd webui
start "" http://localhost:8787
node server.js
pause
