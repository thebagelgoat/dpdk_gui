#!/usr/bin/env bash
# Start the DPDK Packet Broker
# Backend requires sudo; frontend can run as your normal user.
#
# Usage:
#   Terminal 1: sudo python3 -m uvicorn main:app --host 127.0.0.1 --port 8000
#   Terminal 2: npm run dev  (inside frontend/)
#
# Or run both together with this script (also requires sudo):
#   sudo ./start.sh
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKEND_DIR="${PROJECT_DIR}/backend"
FRONTEND_DIR="${PROJECT_DIR}/frontend"

# Check Python deps
sudo python3 -c "import fastapi, uvicorn, pydantic, psutil" 2>/dev/null || {
    echo "ERROR: Python packages missing. Run:"
    echo "  sudo pip install --break-system-packages fastapi 'uvicorn[standard]' pydantic psutil aiofiles websockets"
    exit 1
}

# Check npm
command -v npm >/dev/null || { echo "ERROR: npm not found. Run: sudo apt install nodejs npm"; exit 1; }

# Check engine binary
ENGINE="${PROJECT_DIR}/engine/dpdk_engine"
[[ -f "$ENGINE" ]] || { echo "ERROR: engine not built. Run: ./build.sh"; exit 1; }

cleanup() {
    echo ""
    echo "Shutting down..."
    kill "$BACKEND_PID" "$FRONTEND_PID" 2>/dev/null || true
    wait 2>/dev/null || true
    echo "Done."
}
trap cleanup SIGINT SIGTERM

# Start backend (sudo python3 so the engine subprocess inherits root)
echo "Starting backend (port 8000)..."
cd "${BACKEND_DIR}"
sudo python3 -m uvicorn main:app --host 127.0.0.1 --port 8000 &
BACKEND_PID=$!

# Start frontend (no sudo needed — just a Vite dev server)
echo "Starting frontend (port 5173)..."
cd "${FRONTEND_DIR}"
npm run dev &
FRONTEND_PID=$!

echo ""
echo "============================================"
echo "  DPDK Packet Broker running"
echo "  Open: http://localhost:5173"
echo "  Press Ctrl+C to stop"
echo "============================================"

wait
