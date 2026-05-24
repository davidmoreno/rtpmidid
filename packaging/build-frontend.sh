#!/bin/bash
set -e

# Build web UI assets into frontend/dist (mounted at /frontend).
# Runs in a native-architecture container; output is architecture-independent.

if [ ! -f /frontend/package.json ]; then
    echo "ERROR: /frontend/package.json not found (mount source frontend/ at /frontend)"
    exit 1
fi

cd /frontend
npm ci
npm run build

if [ ! -f /frontend/dist/index.html ]; then
    echo "ERROR: frontend build did not produce dist/index.html"
    exit 1
fi

echo "Web UI assets built in frontend/dist"
