"""CPU affinity for weak-CPU simulation (TASK-011 step 6, docs/MTVU-PLAN.md section 12).

`--cpu-cores N` limits the game process to N logical CPUs. The CPUs are chosen to use distinct physical cores
first (one logical CPU per core, the lowest index), and SMT siblings only when N is larger than the core count.
The affinity is set on the process after it starts (SetProcessAffinityMask), so the first few milliseconds of
startup may run on every CPU; the result record says so. Only performance modes may use it.
"""
import ctypes
import os
from ctypes import wintypes

RELATION_PROCESSOR_CORE = 0
ERROR_INSUFFICIENT_BUFFER = 122
PROCESS_SET_INFORMATION = 0x0200
PROCESS_QUERY_INFORMATION = 0x0400
# One processor group holds up to 64 logical CPUs; this launcher pins inside group 0 only.
GROUP_CPU_LIMIT = 64


def validate_cpu_cores(value, logical_count):
    """Returns the requested core count or raises ValueError (1 .. logical_count)."""
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError('--cpu-cores takes an integer')
    if value < 1 or value > logical_count:
        raise ValueError(f'--cpu-cores must be between 1 and {logical_count} (logical CPUs on this machine)')
    return value


def select_logical_cpus(physical_cores, requested):
    """Chooses `requested` logical CPUs from physical cores (each a list of logical CPU indices).

    One CPU per physical core first, in core order (the lowest index of each core); then the remaining SMT siblings
    in the same order. Returns the sorted indices. Raises ValueError when the request does not fit.
    """
    cores = [sorted(core) for core in physical_cores if core]
    total = sum(len(core) for core in cores)
    if requested < 1 or requested > total:
        raise ValueError(f'cannot select {requested} logical CPUs from {total}')
    first_threads = [core[0] for core in cores]
    sibling_threads = sorted(cpu for core in cores for cpu in core[1:])
    chosen = (first_threads + sibling_threads)[:requested]
    return sorted(chosen)


def affinity_mask(cpus):
    """Affinity bit mask for logical CPU indices in group 0."""
    if not cpus:
        raise ValueError('an affinity mask needs at least one CPU')
    mask = 0
    for cpu in cpus:
        if cpu < 0 or cpu >= GROUP_CPU_LIMIT:
            raise ValueError(f'logical CPU {cpu} is outside processor group 0')
        mask |= 1 << cpu
    return mask


class _LogicalProcessorInformation(ctypes.Structure):
    # Only ProcessorMask and Relationship are read; the union that follows is not needed.
    _fields_ = [('ProcessorMask', ctypes.c_size_t), ('Relationship', ctypes.c_int), ('_union', ctypes.c_ubyte * 16)]


def physical_core_topology():
    """Logical CPU indices grouped by physical core, from GetLogicalProcessorInformation on Windows.

    Other platforms (and any failure) fall back to one core per logical CPU, which keeps the selection valid.
    """
    count = os.cpu_count() or 1
    fallback = [[cpu] for cpu in range(count)]
    if os.name != 'nt':
        return fallback
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel32.GetLogicalProcessorInformation.argtypes = [ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD)]
    kernel32.GetLogicalProcessorInformation.restype = wintypes.BOOL
    length = wintypes.DWORD(0)
    if kernel32.GetLogicalProcessorInformation(None, ctypes.byref(length)):
        return fallback
    if ctypes.get_last_error() != ERROR_INSUFFICIENT_BUFFER or length.value == 0:
        return fallback
    entries = (_LogicalProcessorInformation * (length.value // ctypes.sizeof(_LogicalProcessorInformation)))()
    if not kernel32.GetLogicalProcessorInformation(entries, ctypes.byref(length)):
        return fallback
    cores = []
    for entry in entries:
        if entry.Relationship != RELATION_PROCESSOR_CORE:
            continue
        mask = int(entry.ProcessorMask)
        cores.append([bit for bit in range(GROUP_CPU_LIMIT) if mask >> bit & 1])
    return cores or fallback


def apply_process_affinity(pid, mask):
    """Sets the affinity mask of a running process (Windows). Raises OSError on failure."""
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.SetProcessAffinityMask.argtypes = [wintypes.HANDLE, ctypes.c_size_t]
    kernel32.SetProcessAffinityMask.restype = wintypes.BOOL
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel32.OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, False, pid)
    if not handle:
        raise OSError(f'OpenProcess failed for pid {pid} (error {ctypes.get_last_error()})')
    try:
        if not kernel32.SetProcessAffinityMask(handle, mask):
            raise OSError(f'SetProcessAffinityMask failed for pid {pid} (error {ctypes.get_last_error()})')
    finally:
        kernel32.CloseHandle(handle)


def plan_affinity(requested, physical_cores=None):
    """Full selection for a requested core count: validated count, CPU list, mask and topology summary."""
    cores = physical_cores if physical_cores is not None else physical_core_topology()
    logical_count = sum(len(core) for core in cores)
    validate_cpu_cores(requested, logical_count)
    cpus = select_logical_cpus(cores, requested)
    return {
        'requested_cores': requested,
        'logical_cpus': logical_count,
        'physical_cores': len([core for core in cores if core]),
        'cpus': cpus,
        'mask': affinity_mask(cpus),
    }
