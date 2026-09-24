"""
Loader for cat_bridge.dll -- no Mewtator/Mewjector needed.

    python cat_bridge_dev.py inject          # load the DLL into running Mewgenics
    python cat_bridge_dev.py eject           # cleanly unload it (e.g. before rebuilding)
    python cat_bridge_dev.py send LIST_CATS  # send one raw pipe command, print the JSON

The DLL (bin/cat_bridge.dll from a release, or the CMake build output; set
CAT_BRIDGE_DLL to use another) is copied to tools/.live/ before injecting,
so the original isn't locked while the game holds it. Eject runs the
exported CatBridgeShutdown first (joins the pipe thread outside the loader
lock), then FreeLibrary.

Requires 64-bit Python on Windows, plus pefile and pywin32.
"""

import ctypes
import json
import os
import shutil
import sys
from ctypes import wintypes
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
# Release zip: bin/cat_bridge.dll. Source checkout: the CMake build output.
# $CAT_BRIDGE_DLL overrides both. The newest existing one wins.
DLL_CANDIDATES = [
    ROOT / "bin" / "cat_bridge.dll",
    ROOT / "mod" / "build" / "cat_bridge" / "RelWithDebInfo" / "cat_bridge.dll",
    ROOT / "mod" / "build" / "cat_bridge" / "Release" / "cat_bridge.dll",
]
LIVE_DLL = HERE / ".live" / "cat_bridge.dll"


def built_dll():
    if os.environ.get("CAT_BRIDGE_DLL"):
        return Path(os.environ["CAT_BRIDGE_DLL"])
    existing = [p for p in DLL_CANDIDATES if p.exists()]
    return max(existing, key=lambda p: p.stat().st_mtime) if existing else DLL_CANDIDATES[0]
PROCESS_NAME = "Mewgenics.exe"
PIPE_NAME = r"\\.\pipe\cat_bridge"

PROCESS_ALL_ACCESS = 0x1F0FFF
MEM_COMMIT_RESERVE = 0x3000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04
TH32CS_SNAPPROCESS = 0x02
TH32CS_SNAPMODULE = 0x08
TH32CS_SNAPMODULE32 = 0x10
INFINITE = 0xFFFFFFFF

k32 = ctypes.WinDLL("kernel32", use_last_error=True)


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD), ("th32DefaultHeapID", ctypes.c_void_p),
        ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wintypes.DWORD), ("szExeFile", ctypes.c_wchar * 260),
    ]


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD), ("th32ModuleID", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD), ("GlblcntUsage", wintypes.DWORD),
        ("ProccntUsage", wintypes.DWORD), ("modBaseAddr", ctypes.c_void_p),
        ("modBaseSize", wintypes.DWORD), ("hModule", wintypes.HMODULE),
        ("szModule", ctypes.c_wchar * 256), ("szExePath", ctypes.c_wchar * 260),
    ]


k32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
k32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
k32.Process32FirstW.argtypes = k32.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
k32.Module32FirstW.argtypes = k32.Module32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(MODULEENTRY32W)]
k32.OpenProcess.restype = wintypes.HANDLE
k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
k32.VirtualAllocEx.restype = ctypes.c_void_p
k32.VirtualAllocEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
k32.VirtualFreeEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD]
k32.WriteProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.GetModuleHandleW.restype = wintypes.HMODULE
k32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
k32.GetProcAddress.restype = ctypes.c_void_p
k32.GetProcAddress.argtypes = [wintypes.HMODULE, ctypes.c_char_p]
k32.CreateRemoteThread.restype = wintypes.HANDLE
k32.CreateRemoteThread.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p]
k32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
k32.GetExitCodeThread.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
k32.CloseHandle.argtypes = [wintypes.HANDLE]


def die(msg):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def find_pid():
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    entry = PROCESSENTRY32W(dwSize=ctypes.sizeof(PROCESSENTRY32W))
    try:
        ok = k32.Process32FirstW(snap, ctypes.byref(entry))
        while ok:
            if entry.szExeFile.lower() == PROCESS_NAME.lower():
                return entry.th32ProcessID
            ok = k32.Process32NextW(snap, ctypes.byref(entry))
    finally:
        k32.CloseHandle(snap)
    die(f"{PROCESS_NAME} is not running")


def find_module(pid, name):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    entry = MODULEENTRY32W(dwSize=ctypes.sizeof(MODULEENTRY32W))
    try:
        ok = k32.Module32FirstW(snap, ctypes.byref(entry))
        while ok:
            if entry.szModule.lower() == name.lower():
                return entry.modBaseAddr
            ok = k32.Module32NextW(snap, ctypes.byref(entry))
    finally:
        k32.CloseHandle(snap)
    return None


def run_remote(hproc, func_addr, arg):
    thread = k32.CreateRemoteThread(hproc, None, 0, func_addr, arg, 0, None)
    if not thread:
        die(f"CreateRemoteThread failed, gle={ctypes.get_last_error()}")
    k32.WaitForSingleObject(thread, INFINITE)
    code = wintypes.DWORD()
    k32.GetExitCodeThread(thread, ctypes.byref(code))
    k32.CloseHandle(thread)
    return code.value


def kernel32_proc(name):
    # kernel32 is mapped at the same base in every process of a boot session.
    return k32.GetProcAddress(k32.GetModuleHandleW("kernel32.dll"), name)


def inject():
    dll = built_dll()
    if not dll.exists():
        die(f"{dll} not found -- download a release (bin/cat_bridge.dll) or build the mod first")
    pid = find_pid()
    if find_module(pid, LIVE_DLL.name):
        die("cat_bridge.dll is already loaded -- eject it first")

    LIVE_DLL.parent.mkdir(exist_ok=True)
    shutil.copy2(dll, LIVE_DLL)
    pdb = dll.with_suffix(".pdb")
    if pdb.exists():
        shutil.copy2(pdb, LIVE_DLL.with_suffix(".pdb"))

    path_bytes = (str(LIVE_DLL) + "\0").encode("utf-16-le")
    hproc = k32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
    if not hproc:
        die(f"OpenProcess failed, gle={ctypes.get_last_error()}")
    try:
        remote = k32.VirtualAllocEx(hproc, None, len(path_bytes), MEM_COMMIT_RESERVE, PAGE_READWRITE)
        k32.WriteProcessMemory(hproc, remote, path_bytes, len(path_bytes), None)
        result = run_remote(hproc, kernel32_proc(b"LoadLibraryW"), remote)
        k32.VirtualFreeEx(hproc, remote, 0, MEM_RELEASE)
    finally:
        k32.CloseHandle(hproc)

    if result == 0 or not find_module(pid, LIVE_DLL.name):
        die("LoadLibraryW failed inside the game")
    print(f"injected {LIVE_DLL} into pid {pid}")


def eject():
    import pefile

    pid = find_pid()
    base = find_module(pid, LIVE_DLL.name)
    if not base:
        die("cat_bridge.dll is not loaded")

    pe = pefile.PE(str(LIVE_DLL), fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
    rva = next((s.address for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name == b"CatBridgeShutdown"), None)
    pe.close()
    if rva is None:
        die("CatBridgeShutdown export missing -- DLL is from an older build")

    hproc = k32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
    if not hproc:
        die(f"OpenProcess failed, gle={ctypes.get_last_error()}")
    try:
        run_remote(hproc, base + rva, None)
        run_remote(hproc, kernel32_proc(b"FreeLibrary"), base)
    finally:
        k32.CloseHandle(hproc)

    if find_module(pid, LIVE_DLL.name):
        die("FreeLibrary returned but the module is still loaded")
    print(f"ejected from pid {pid}")


def send(command):
    import pywintypes
    import win32file

    try:
        h = win32file.CreateFile(PIPE_NAME, win32file.GENERIC_READ | win32file.GENERIC_WRITE,
                                 0, None, win32file.OPEN_EXISTING, 0, None)
    except pywintypes.error as e:
        die(f"could not open {PIPE_NAME}: {e}")
    try:
        win32file.WriteFile(h, (command + "\n").encode("utf-8"))
        buf = b""
        while b"\n" not in buf:
            _, chunk = win32file.ReadFile(h, 65536)
            if not chunk:
                break
            buf += chunk
    finally:
        win32file.CloseHandle(h)
    line = buf.split(b"\n", 1)[0].decode("utf-8")
    try:
        print(json.dumps(json.loads(line), indent=2, ensure_ascii=False))
    except json.JSONDecodeError:
        print(line)


def main():
    if ctypes.sizeof(ctypes.c_void_p) != 8:
        die("needs 64-bit Python")
    args = sys.argv[1:]
    if args[:1] == ["inject"]:
        inject()
    elif args[:1] == ["eject"]:
        eject()
    elif args[:1] == ["send"] and len(args) >= 2:
        send(" ".join(args[1:]))
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
