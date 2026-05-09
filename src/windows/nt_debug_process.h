// #pragma once
// #define WIN32_LEAN_AND_MEAN
// #include <Windows.h>

// namespace chronoporia {
//     bool AttachToProcess();
//     void DebugLoop();
//     DWORD HandleDebugException(const DEBUG_EVENT* debug_event);
//     DWORD HandleBreakpoint(const DEBUG_EVENT *debug_event);

//     // Places permanent breakpoints at all non-det functions and creates their trampoline
//     void SetupNonDetCapture();
// }