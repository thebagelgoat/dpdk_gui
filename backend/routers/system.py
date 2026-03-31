from __future__ import annotations
import os
import psutil
from fastapi import APIRouter

router = APIRouter()

NIC_ADDRESSES = ["0000:00:10.0", "0000:00:10.1"]


def _nic_info(pci_addr: str) -> dict:
    driver = "none"
    driver_path = f"/sys/bus/pci/devices/{pci_addr}/driver"
    if os.path.islink(driver_path):
        driver = os.path.basename(os.readlink(driver_path))

    dpdk_ready = driver in ("vfio-pci", "uio_pci_generic", "igb_uio")

    vendor = ""
    device = ""
    try:
        with open(f"/sys/bus/pci/devices/{pci_addr}/vendor") as f:
            vendor = f.read().strip()
        with open(f"/sys/bus/pci/devices/{pci_addr}/device") as f:
            device = f.read().strip()
    except OSError:
        pass

    return {
        "pci_address": pci_addr,
        "driver": driver,
        "dpdk_ready": dpdk_ready,
        "vendor": vendor,
        "device": device,
    }


def _hugepage_info() -> dict:
    total = 0
    free = 0
    size_kb = 0
    try:
        with open("/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages") as f:
            total = int(f.read().strip())
        with open("/sys/kernel/mm/hugepages/hugepages-2048kB/free_hugepages") as f:
            free = int(f.read().strip())
        size_kb = 2048
    except OSError:
        pass
    return {
        "total": total,
        "free": free,
        "used": total - free,
        "size_kb": size_kb,
        "total_mb": total * size_kb // 1024,
    }


@router.get("/")
async def system_info() -> dict:
    cpu_count = psutil.cpu_count(logical=True)
    cpu_percent = psutil.cpu_percent(interval=None, percpu=True)
    mem = psutil.virtual_memory()

    return {
        "cpu": {
            "count": cpu_count,
            "available_dpdk_cores": [1, 2, 3],
            "percent_per_core": cpu_percent,
        },
        "memory": {
            "total_mb": mem.total // (1024 * 1024),
            "available_mb": mem.available // (1024 * 1024),
            "used_percent": mem.percent,
        },
        "hugepages": _hugepage_info(),
        "nics": [_nic_info(addr) for addr in NIC_ADDRESSES],
        "iommu_enabled": "intel_iommu=on" in open("/proc/cmdline").read()
            if os.path.exists("/proc/cmdline") else False,
    }
