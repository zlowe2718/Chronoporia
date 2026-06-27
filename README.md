# Overview
A Windows time-travel debugger (TTD) aimed at letting the user step backwards through code to diagnose bugs, races, and heisenbugs. Chronoporia creates a child process that runs the user's code allowing full access to the child process, so that it can capture memory, threads, dlls, and non-deterministic events.  The ultimate goal is to have a fully featured language agnostic debugger that not only allows reverse stepping, but also accurately replicate heisenbugs and data races spanning from multi-threaded programs.

## WIP
Currently in progress and tested on Windows 11 25H2 and requires c++23.  Other Windows versions may not work currently due to Window's C API structs changing between versions.

Current code allows for simple deterministic python scripts to successfully revert back to any snapshot and replay execution.

## Execution
Chronoporia operates in a few main execution phases:

**Recording** - Takes a snapshot of the current program memory then listens for and records non-determinstic events

**Time Restore** - Resets memory, dlls, and thread contexts (WIP: and handles) back to the closest snapshot 

**Reconstruction** - Starts from reconstructed memory at the most recent coarse snapshot and takes more frequent snapshots (dubbed microsnapshots). When a non-determinstic event is reached such as NtCreateThread or NtQueryPerformanceCounter this phase should etiher inject the values necessary to recreate the event correctly (in the case of NtCreateThread) or stub the function and return the non-deterministic return value (in the case of NtQueryPerformanceCounter).  Once all micro snapshots are taken this phase then transitions to the debugger phase.

**Debugger (WIP)** - This phase will be responsible for actually stepping forwards and backwards in the code.  This phase will be responsible for a few different ideas.  This phase will let the user drive the actual debugger (via cli or later from DAP).  To be language agnostic chronoporia will use a plugin system so that anyone can make a dll that tells chronoporia how to make, set, and analyze breakpoints for their language. This phase should also be able to show (via cli or later in a GUI or code editor extension) the snapshots, events, and how they are ordered.  This should also have a setting to "preview" a snapshot to see the current stack trace, and local variables of the current frame (no deeper than one frame most likely).  This will also hook into the same plugin system to tell chronoporia how to analyze the byte data.  I.e. c++ can interpret byte data as structs, while to view cpython data as python values (and not c values) you would need to analyze the PyObject structure.

# Build and Run
1. Run `cmake -B build -S .` to build the msvc solution
2. Run `cmake --build build --config Debug` to build the project
3. This will place chronoporia.exe in the test_scripts\python folder
4. Run `chronoporia.exe <python_path> <python_file>` to run a python file

# Usage
Chronoporia will run the python file for a specified time then revert back to program start and start the reconstruction phase.  After all snapshots the program will then transition to the debugger phase.  The debugger phase currently has three commands.

### Snapshot
* To list all snapshots run `snapshot --list` 
* To revert to a snapshot run `snapshot --run_id=<x> --run_seq=<x>`

### Detach
* To detach chronoporia from the program run `detach`

### Resume
* To resume the program from a suspended state run `resume`