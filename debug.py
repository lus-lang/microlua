import sys
import subprocess
import os
import time

# --- AUTO-CONFIGURE LLDB PATH ---
# Ask lldb where its python module is and add it to sys.path
try:
    # This runs 'lldb -P' to get the path (usually inside Xcode.app)
    lldb_python_path = subprocess.check_output(['xcrun', 'lldb', '-P']).decode("utf-8").strip()
    if lldb_python_path not in sys.path:
        sys.path.append(lldb_python_path)
except subprocess.CalledProcessError:
    print("Error: Could not find lldb Python path. Is Xcode command line tools installed?")
    sys.exit(1)

import lldb

# --- CONFIGURATION ---
# Usage: python3 debug.py [--timeout N] [exe [args...]]
# Defaults preserve the original behavior (REPL + /tmp/test_while.lua).
EXE_PATH = "./builddir/mlua"
ARGS = ["/tmp/test_while.lua"]
TIMEOUT_SECONDS = 2  # How long to wait before interrupting

_argv = sys.argv[1:]
if _argv and _argv[0] == "--timeout":
    TIMEOUT_SECONDS = int(_argv[1])
    _argv = _argv[2:]
if _argv:
    EXE_PATH = _argv[0]
    ARGS = _argv[1:]
# ----------------------

def run_debugger():
    # 1. Initialize Debugger
    debugger = lldb.SBDebugger.Create()
    debugger.SetAsync(True)  # Async mode to allow timeout
    
    # 2. Create Target
    if not os.path.exists(EXE_PATH):
        print(f"Error: Executable not found at '{EXE_PATH}'")
        sys.exit(1)

    target = debugger.CreateTarget(EXE_PATH)
    if not target.IsValid():
        print(f"Error: Could not create LLDB target for '{EXE_PATH}'")
        sys.exit(1)

    # 3. Setup Launch Info
    launch_info = lldb.SBLaunchInfo(ARGS)
    launch_info.SetLaunchFlags(lldb.eLaunchFlagDisableASLR)
    launch_info.AddOpenFileAction(0, "/dev/null", True, False)
    
    # 4. Launch the Process
    error = lldb.SBError()
    print(f"Launching: {EXE_PATH} {' '.join(ARGS)}")
    process = target.Launch(launch_info, error)

    if not error.Success():
        print(f"Failed to launch: {error}")
        sys.exit(1)

    # 5. Handle initial SIGSTOP and continue
    listener = debugger.GetListener()
    event = lldb.SBEvent()
    
    # Wait for initial stop
    if listener.WaitForEvent(5, event):
        if process.GetState() == lldb.eStateStopped:
            process.Continue()
    
    # 6. Wait for timeout then interrupt
    print(f"Waiting {TIMEOUT_SECONDS} seconds for infinite loop...")
    time.sleep(TIMEOUT_SECONDS)
    
    state = process.GetState()
    if state == lldb.eStateRunning:
        print("Process still running - interrupting to get backtrace...")
        process.Stop()
        time.sleep(0.5)  # Give it time to stop
    
    state = process.GetState()
    if state == lldb.eStateStopped:
        print("\n*** PROCESS INTERRUPTED - BACKTRACE ***")
        for thread in process:
            if thread.GetStopReason() != lldb.eStopReasonNone:
                print(f"\nThread {thread.GetThreadID()}:")
                for frame in thread:
                    fn_name = frame.GetFunctionName() or "<unknown>"
                    print(f"  #{frame.GetFrameID()}: {fn_name}")
        process.Kill()
        sys.exit(1)
    elif state == lldb.eStateExited:
        exit_code = process.GetExitStatus()
        print(f"Process exited with status {exit_code}")
        sys.exit(exit_code)

if __name__ == "__main__":
    run_debugger()