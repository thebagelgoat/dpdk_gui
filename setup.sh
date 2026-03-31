#!/usr/bin/env bash
# DPDK Packet Broker - System Setup Script
# Run as root: sudo ./setup.sh
set -euo pipefail

RED='\033[0;31m'; YELLOW='\033[1;33m'; GREEN='\033[0;32m'; NC='\033[0m'
info()    { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NIC0="0000:00:10.0"
NIC1="0000:00:10.1"

[[ $EUID -eq 0 ]] || error "Run as root: sudo $0"

# ============================================================
# Step 1: Install system packages
# ============================================================
info "Step 1/9: Installing system packages..."
apt-get update -qq
apt-get install -y \
    build-essential meson ninja-build pkg-config \
    dpdk dpdk-dev libdpdk-dev \
    libjansson-dev libpcap-dev libnuma-dev \
    python3-pip \
    nodejs npm \
    pciutils kmod iproute2 \
    libcap2-bin \
    2>/dev/null || true

# Install Python packages system-wide so sudo python3 can find them
pip install --break-system-packages \
    fastapi "uvicorn[standard]" pydantic psutil aiofiles websockets \
    2>/dev/null || true

# Verify critical packages
pkg-config --modversion libdpdk >/dev/null 2>&1 || error "libdpdk-dev not installed properly"
info "DPDK version: $(pkg-config --modversion libdpdk)"

# ============================================================
# Step 2: IOMMU / GRUB configuration
# ============================================================
info "Step 2/9: Checking IOMMU configuration..."
IOMMU_READY=false
NEEDS_REBOOT=false

if grep -q "intel_iommu=on" /proc/cmdline 2>/dev/null; then
    info "IOMMU already enabled in kernel cmdline."
    IOMMU_READY=true
else
    warn "IOMMU not enabled. Adding intel_iommu=on iommu=pt to GRUB..."
    if [[ -f /etc/default/grub ]]; then
        # Check if already added (but not active yet — pending reboot)
        if grep -q "intel_iommu=on" /etc/default/grub; then
            warn "GRUB already updated. Reboot required to activate."
            NEEDS_REBOOT=true
        else
            # Add to GRUB_CMDLINE_LINUX_DEFAULT
            sed -i 's/^GRUB_CMDLINE_LINUX_DEFAULT="\(.*\)"/GRUB_CMDLINE_LINUX_DEFAULT="\1 intel_iommu=on iommu=pt"/' /etc/default/grub
            update-grub 2>/dev/null || grub-mkconfig -o /boot/grub/grub.cfg 2>/dev/null || warn "grub update failed — update manually"
            warn "GRUB updated. A reboot is required to enable IOMMU."
            NEEDS_REBOOT=true
        fi
    else
        warn "/etc/default/grub not found. Please manually add 'intel_iommu=on iommu=pt' to your kernel cmdline."
        NEEDS_REBOOT=true
    fi
fi

# ============================================================
# Step 3: Configure hugepages (512 × 2MB = 1 GB)
# ============================================================
info "Step 3/9: Configuring hugepages..."
TARGET_HUGE=512

CURRENT_HUGE=$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages 2>/dev/null || echo 0)
if [[ "$CURRENT_HUGE" -ge "$TARGET_HUGE" ]]; then
    info "Hugepages already configured: ${CURRENT_HUGE} × 2MB"
else
    info "Allocating ${TARGET_HUGE} × 2MB hugepages..."
    echo "$TARGET_HUGE" > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

    ALLOCATED=$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages)
    if [[ "$ALLOCATED" -lt "$TARGET_HUGE" ]]; then
        warn "Only allocated ${ALLOCATED}/${TARGET_HUGE} hugepages. Memory may be fragmented."
        warn "Try rebooting and running setup.sh again."
    else
        info "Allocated ${ALLOCATED} × 2MB hugepages ($(( ALLOCATED * 2 )) MB total)"
    fi

    # Persist across reboots
    cat > /etc/sysctl.d/90-dpdk-hugepages.conf << EOF
vm.nr_hugepages = ${TARGET_HUGE}
EOF
fi

# Mount hugetlbfs if not mounted
mkdir -p /dev/hugepages
if ! mountpoint -q /dev/hugepages; then
    mount -t hugetlbfs nodev /dev/hugepages -o pagesize=2M
    info "Mounted hugetlbfs at /dev/hugepages"
fi

# Add to fstab for persistence
if ! grep -q "hugetlbfs" /etc/fstab; then
    echo "nodev /dev/hugepages hugetlbfs pagesize=2M 0 0" >> /etc/fstab
    info "Added hugetlbfs to /etc/fstab"
fi

# ============================================================
# Step 4: Load uio_pci_generic kernel module
# ============================================================
info "Step 4/9: Loading uio_pci_generic kernel module..."

modprobe uio
modprobe uio_pci_generic
info "Loaded uio and uio_pci_generic modules"

# Persist module loading across reboots
cat > /etc/modules-load.d/dpdk.conf << 'EOF'
uio
uio_pci_generic
EOF

# ============================================================
# Step 5: Bind NICs to VFIO (or uio_pci_generic)
# ============================================================
info "Step 5/9: Binding NICs to DPDK driver..."

bind_nic() {
    local pci_addr="$1"
    local target_driver="uio_pci_generic"

    # Detect current driver
    local driver_path="/sys/bus/pci/devices/${pci_addr}/driver"
    local current_driver="none"
    if [[ -L "$driver_path" ]]; then
        current_driver=$(basename "$(readlink "$driver_path")")
    fi

    if [[ "$current_driver" == "$target_driver" ]]; then
        info "$pci_addr already bound to $target_driver"
        return 0
    fi

    if [[ ! -d /sys/bus/pci/drivers/uio_pci_generic ]]; then
        warn "uio_pci_generic not available for $pci_addr — modprobe may have failed"
        return 0
    fi

    # Unbind from current driver
    if [[ "$current_driver" != "none" ]]; then
        echo "$pci_addr" > "/sys/bus/pci/drivers/${current_driver}/unbind" 2>/dev/null || true
        info "Unbound $pci_addr from $current_driver"
    fi

    # Bind to uio_pci_generic
    local vendor device
    vendor=$(sed 's/0x//' /sys/bus/pci/devices/"${pci_addr}"/vendor)
    device=$(sed 's/0x//' /sys/bus/pci/devices/"${pci_addr}"/device)

    echo "${vendor} ${device}" > /sys/bus/pci/drivers/uio_pci_generic/new_id 2>/dev/null || true
    echo "$pci_addr" > /sys/bus/pci/drivers/uio_pci_generic/bind 2>/dev/null \
        || warn "Could not bind $pci_addr to uio_pci_generic"

    local new_driver="none"
    if [[ -L "/sys/bus/pci/devices/${pci_addr}/driver" ]]; then
        new_driver=$(basename "$(readlink "/sys/bus/pci/devices/${pci_addr}/driver")")
    fi
    info "Bound $pci_addr → $new_driver (was: $current_driver)"
}

bind_nic "$NIC0"
bind_nic "$NIC1"

# ============================================================
# Step 6: udev rule for persistent NIC binding
# ============================================================
info "Step 6/9: Writing udev rule for persistent uio_pci_generic binding..."
cat > /etc/udev/rules.d/99-dpdk-uio.rules << 'EOF'
# Automatically bind Intel I350 NICs to uio_pci_generic on boot
ACTION=="add", SUBSYSTEM=="pci", ATTR{vendor}=="0x8086", ATTR{device}=="0x1521", \
    RUN+="/bin/sh -c 'echo %k > /sys/bus/pci/drivers/igb/unbind 2>/dev/null; \
    echo 8086 1521 > /sys/bus/pci/drivers/uio_pci_generic/new_id 2>/dev/null; \
    echo %k > /sys/bus/pci/drivers/uio_pci_generic/bind 2>/dev/null'"
EOF
udevadm control --reload-rules 2>/dev/null || true

# ============================================================
# Step 7: Install Node.js frontend dependencies
# ============================================================
info "Step 7/9: Installing frontend dependencies..."
FRONTEND_DIR="${PROJECT_DIR}/frontend"
if [[ -f "${FRONTEND_DIR}/package.json" ]]; then
    cd "${FRONTEND_DIR}"
    npm install --silent
    info "Frontend dependencies installed"
else
    warn "frontend/package.json not found — skipping npm install"
fi

# ============================================================
# Step 8: Compile the DPDK engine
# ============================================================
info "Step 8/9: Compiling DPDK engine..."
if [[ -f "${PROJECT_DIR}/build.sh" ]]; then
    cd "${PROJECT_DIR}"
    bash build.sh
else
    warn "build.sh not found — skipping engine compilation"
fi

# ============================================================
# Step 9: Remove any stale setcap from engine binary (running as root via sudo)
# ============================================================
info "Step 9/9: Clearing file capabilities from dpdk_engine (using sudo instead)..."
ENGINE_BIN="${PROJECT_DIR}/engine/dpdk_engine"
if [[ -f "$ENGINE_BIN" ]]; then
    setcap -r "$ENGINE_BIN" 2>/dev/null || true
    info "File capabilities cleared — engine runs as root via sudo"
else
    warn "dpdk_engine binary not found — run build.sh first"
fi

# ============================================================
# Summary
# ============================================================
echo ""
echo "=============================================="
echo -e "${GREEN}        DPDK Packet Broker Setup${NC}"
echo "=============================================="
ALLOC=$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages 2>/dev/null || echo 0)
echo "  Hugepages    : ${ALLOC} × 2MB = $(( ALLOC * 2 )) MB"

for nic in "$NIC0" "$NIC1"; do
    drv="none"
    [[ -L "/sys/bus/pci/devices/${nic}/driver" ]] && drv=$(basename "$(readlink "/sys/bus/pci/devices/${nic}/driver")")
    echo "  NIC ${nic}  : driver=${drv}"
done

dpdk_ver=$(pkg-config --modversion libdpdk 2>/dev/null || echo "unknown")
echo "  DPDK version : ${dpdk_ver}"
echo "  CPU cores    : $(nproc) (cores 1-3 available for DPDK)"

if [[ "$NEEDS_REBOOT" == "true" ]]; then
    echo ""
    echo -e "${YELLOW}  NOTE: GRUB was updated (intel_iommu) but this is not required${NC}"
    echo -e "${YELLOW}  for uio_pci_generic — no reboot needed.${NC}"
fi
echo "=============================================="
echo ""
info "To start the application:"
echo "  1. cd backend && python3 -m uvicorn main:app --host 127.0.0.1 --port 8000"
echo "  2. cd frontend && npm run dev"
echo "  3. Open http://localhost:5173"
