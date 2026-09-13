"""External Windows debug-event capture; no injected code or application breakpoints."""

import ctypes as c
from ctypes import wintypes as w
import json
import msvcrt
import os
from pathlib import Path
import struct
import subprocess
import time

import revise_phase00_r04 as h


class ExceptionRecord(c.Structure):
    _fields_ = [("code", w.DWORD), ("flags", w.DWORD), ("nested", c.c_void_p),
                ("address", c.c_void_p), ("count", w.DWORD), ("parameters", c.c_size_t * 15)]


class ExceptionInfo(c.Structure):
    _fields_ = [("record", ExceptionRecord), ("first_chance", w.DWORD)]


class EventData(c.Union):
    _fields_ = [("exception", ExceptionInfo), ("raw", c.c_byte * 160)]


class Event(c.Structure):
    _fields_ = [("code", w.DWORD), ("pid", w.DWORD), ("tid", w.DWORD), ("data", EventData)]


class ExceptionPointers(c.Structure):
    _fields_ = [("record", c.POINTER(ExceptionRecord)), ("context", c.c_void_p)]


class DumpException(c.Structure):
    _pack_ = 4  # minidumpapiset.h includes pshpack4.h, unlike DEBUG_EVENT.
    _fields_ = [("tid", w.DWORD), ("pointers", c.POINTER(ExceptionPointers)), ("client", w.BOOL)]


def probe(name, app):
    assert c.sizeof(ExceptionRecord) == 152 and c.sizeof(Event) == 176 and c.sizeof(DumpException) == 16
    helper = Path(__file__)
    retained = h.WORK / ("native-helper-" + h.sha(helper) + ".py")
    if not retained.exists():
        retained.write_bytes(helper.read_bytes())
    print(json.dumps({"native_helper_sha256": h.sha(helper), "retained_helper": str(retained)}), flush=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.WaitForDebugEventEx.argtypes = [c.POINTER(Event), w.DWORD]
    kernel.ContinueDebugEvent.argtypes = [w.DWORD, w.DWORD, w.DWORD]
    kernel.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenThread.restype = w.HANDLE
    kernel.GetThreadContext.argtypes = [w.HANDLE, c.c_void_p]
    kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.SetErrorMode(0x8003)
    executable = h.ROOT / "bin/Release" / app / (app + ".exe")
    env = os.environ.copy()
    env.update(PATH="C:\\Windows\\System32;C:\\Windows", _NO_DEBUG_HEAP="1")
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    process = subprocess.Popen([str(executable)], env=env, startupinfo=startup,
                               creationflags=subprocess.CREATE_NO_WINDOW | 2)
    print(json.dumps({"native_debug_pid": process.pid, "exe": str(executable), "exe_sha256": h.sha(executable),
                      "cwd": str(os.getcwd()), "environment_overrides": {"PATH": env["PATH"], "_NO_DEBUG_HEAP": "1"},
                      "instrumentation": "DEBUG_ONLY_THIS_PROCESS; continue loader events immediately; normal heap; no application breakpoints or symbols before failure"}), flush=True)
    before = time.monotonic()
    close_sent = False
    next_sample = before + 1
    initial_breakpoint = False
    fault_captured = False
    forced = False
    exit_code = None
    try:
        while exit_code is None:
            event = Event()
            if kernel.WaitForDebugEventEx(c.byref(event), 20):
                status = 0x10002
                raw = bytes(event.data.raw)
                elapsed = time.monotonic() - before
                if event.code == 1:
                    info = event.data.exception
                    record = info.record
                    print(json.dumps({"debug_exception": hex(record.code), "first_chance": info.first_chance,
                                      "pid": event.pid, "tid": event.tid, "elapsed_seconds": elapsed,
                                      "address": hex(record.address or 0), "flags": record.flags,
                                      "parameters": list(record.parameters)[:record.count]}), flush=True)
                    if record.code == 0x80000003 and not initial_breakpoint:
                        initial_breakpoint = True
                    else:
                        status = 0x80010001
                        if record.code == 0xc0000409 or not info.first_chance:
                            fault_captured = True
                            buffer = c.create_string_buffer(1248)
                            context = (c.addressof(buffer) + 15) & ~15
                            c.c_uint32.from_address(context + 48).value = 0x10001f
                            thread = kernel.OpenThread(8 | 0x40, False, event.tid)
                            assert thread, c.get_last_error()
                            assert kernel.GetThreadContext(thread, context), c.get_last_error()
                            kernel.CloseHandle(thread)
                            registers = {reg: hex(c.c_uint64.from_address(context + offset).value)
                                         for reg, offset in [("rax",120),("rcx",128),("rdx",136),("rbx",144),
                                                             ("rsp",152),("rbp",160),("rsi",168),("rdi",176),
                                                             ("r8",184),("r9",192),("r10",200),("r11",208),
                                                             ("r12",216),("r13",224),("r14",232),("r15",240),("rip",248)]}
                            print(json.dumps({"fault_registers": registers, "module_identities": h.prior.process_modules(process)}), flush=True)
                            dump = h.WORK / (name + ".dmp")
                            assert not dump.exists()
                            pointers = ExceptionPointers(c.pointer(record), context)
                            exception = DumpException(event.tid, c.pointer(pointers), False)
                            dbghelp = c.WinDLL("C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/dbghelp.dll", use_last_error=True)
                            dbghelp.MiniDumpWriteDump.argtypes = [w.HANDLE,w.DWORD,w.HANDLE,w.DWORD,c.POINTER(DumpException),c.c_void_p,c.c_void_p]
                            with dump.open("xb") as stream:
                                ok = dbghelp.MiniDumpWriteDump(int(process._handle), process.pid, msvcrt.get_osfhandle(stream.fileno()), 0x1866, c.byref(exception), None, None)
                            print(json.dumps({"dump_success": bool(ok), "dump_error": c.get_last_error() if not ok else 0,
                                              "dump": str(dump), "dump_sha256": h.sha(dump)}), flush=True)
                elif event.code == 5:
                    exit_code = struct.unpack_from("I", raw)[0]
                elif event.code in [3, 6]:
                    file_handle = struct.unpack_from("Q", raw)[0]
                    if file_handle:
                        kernel.CloseHandle(file_handle)
                elif event.code == 8:
                    address, unicode, length = struct.unpack_from("QHH", raw)
                    data = c.create_string_buffer(length * (2 if unicode else 1))
                    read = c.c_size_t()
                    if kernel.ReadProcessMemory(int(process._handle), address, data, len(data), c.byref(read)):
                        print(json.dumps({"debug_output": data.raw[:read.value].decode("utf-16-le" if unicode else "utf-8", errors="replace"), "tid":event.tid, "elapsed_seconds":elapsed}), flush=True)
                assert kernel.ContinueDebugEvent(event.pid, event.tid, status), c.get_last_error()
            now = time.monotonic()
            if exit_code is None and now >= next_sample:
                windows = h.prior.process_windows(process.pid)
                print(json.dumps({"elapsed_seconds": now-before, "windows": windows}), flush=True)
                next_sample = now + 1
                if not close_sent and now - before >= 60:
                    user = c.WinDLL("user32", use_last_error=True)
                    user.PostMessageW.argtypes = [w.HWND,w.UINT,w.WPARAM,w.LPARAM]
                    for window in windows:
                        if window["class"] == "SDL_app":
                            close_sent |= bool(user.PostMessageW(window["hwnd"],0x10,0,0))
                    print(json.dumps({"normal_close_request":close_sent}),flush=True)
            if exit_code is None and now - before > 90:
                forced = True
                process.kill()
        process.wait()
    finally:
        if exit_code is None and process.poll() is None:
            process.kill()
    print(json.dumps({"debugger_result":"EXCEPTION_CAPTURED" if fault_captured else "CLEAN_EXIT" if exit_code == 0 else "FAILED_EXIT",
                      "exit_code":exit_code,"exit_hex":hex(exit_code or 0),"normal_close_sent":close_sent,"forced_termination":forced}),flush=True)
    return 0 if exit_code == 0 and close_sent and not fault_captured and not forced else 1
