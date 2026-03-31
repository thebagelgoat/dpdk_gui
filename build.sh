#!/usr/bin/env bash
# Compile the DPDK engine binary
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE_DIR="${PROJECT_DIR}/engine"

# Verify DPDK dev headers are installed
if ! pkg-config --modversion libdpdk >/dev/null 2>&1; then
    echo "ERROR: libdpdk-dev not installed. Run sudo ./setup.sh first."
    exit 1
fi

DPDK_VER=$(pkg-config --modversion libdpdk)
echo "Building dpdk_engine (DPDK ${DPDK_VER})..."

mkdir -p "${ENGINE_DIR}/modules"
cd "${ENGINE_DIR}"

make clean 2>/dev/null || true
make -j"$(nproc)"

echo ""
echo "Build complete:"
ls -lh dpdk_engine
echo ""
echo "To set runtime capabilities (run once as root after build):"
echo "  sudo setcap cap_net_admin,cap_ipc_lock+ep ${ENGINE_DIR}/dpdk_engine"
