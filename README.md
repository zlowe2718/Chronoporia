# Overview
A Windows time-travel debugger (TTD) aimed at letting the user step backwards through code to diagnose bugs, races, and heisenbugs. Chronoporia creates a child process that runs the user's code allowing full access to the child process, so that it can capture memory, threads, dlls, and non-deterministic events.  The ultimate goal is to have a fully featured language agnostic debugger that not only allows reverse stepping, but also quick jumping between different lines in code and across threads.

## WIP
Currently in progress.  Current code allows for simple deterministic python scripts to successfully revert back to the beginning and replay execution.

## Execution
Chronoporia operates in a few main execution phases:

**Recording** - Takes a snapshot of the current program memory then listens for and records non-determinstic events

**Time Restore** - Resets memory, dlls, and thread contexts back to the closest snapshot 

**Reconstruction (WIP)** - Starts from reconstructed memory and records line events for a specified language like c++ or python (will be a plugin based system).  These line events will record dirty memory from the previous memory event by means of Guard Pages and capture current thread contexts.  When a non-determinstic event is reached such as NtCreateThread or NtQueryPerformanceCounter this phase should etiher inject the values necessary to recreate the event correctly (in the case of NtCreateThread) or stub the function and return the non-deterministic return value (in the case of NtQueryPerformanceCounter).  Once the program catches up to the state of the code was before this phase should then transition to the debugger phase.

**Debugger (WIP)** - This phase will be responsible for actually stepping forwards and backwards in the code.  Ideally in this phase no code will actually need to be run allowing for quick jumping around in time.  This phase should also have a global execution tree showing stack traces per thread as well as an interleaved version with all the threads.  This phase should also have a plugin system that allows different languages to interpret the memory in a way that makes sense.  I.e. c++ can interpret byte data as structs, while to view cpython data you would need to analyze the PyObject structure.