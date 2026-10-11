#!/bin/sh
set -e
cd "$(dirname "$0")"

command -v node >/dev/null 2>&1 || { echo "Node.js is not installed. See README.md"; exit 1; }
command -v make >/dev/null 2>&1 || { echo "make is not installed. See README.md"; exit 1; }

if [ ! -x randy/randy ] || [ ! -x bruteforce ]; then
    echo "Building (first run only)..."
    make
fi

cd webui
exec node server.js
