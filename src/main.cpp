#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "nt_wrappers.h"
#include "globals.h"
#include "state_machine.h"
#include <string>

bool AttachToProcess() {
	DebugSetProcessKillOnExit(FALSE);

	return true;
}

int main(int argc, char *argv[]) {
    STARTUPINFOA si {0};
    PROCESS_INFORMATION pi {0};
    std::string ws = "";
	chronoporia::InitializeWrapperAddresses();

	for (int i=1; i<argc; i++) {
		ws += argv[i];
		ws += " ";
	}
    CreateProcessA(
		NULL,
		&ws[0],
		NULL,
		NULL,
		FALSE,
		DEBUG_ONLY_THIS_PROCESS,
		NULL,
		NULL,
		&si,
		&pi
	);

	globals::process_id = pi.dwProcessId;
	globals::process_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SUSPEND_RESUME | PROCESS_VM_READ | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_DUP_HANDLE, FALSE, pi.dwProcessId);
	globals::running = AttachToProcess();
	chronoporia::ResumeProcess();
	chronoporia::RunExecution();
    return 0;
}
