#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
echo "Building chat_server..."
cc -O2 -pthread -o chat_server chat.c mongoose.c
echo "Built ./chat_server"
