#include "quill/LogMacros.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "nt_wrappers.h"
#include "globals.h"
#include "state_machine.h"
#include <string>
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/sinks/ConsoleSink.h>

bool AttachToProcess() {
	DebugSetProcessKillOnExit(FALSE);

	return true;
}

void StartLogger() {
    quill::Backend::start();

    auto sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink");
    globals::logger = quill::Frontend::create_or_get_logger("root", sink);

#ifdef _DEBUG
    globals::logger->set_log_level(quill::LogLevel::Debug);
#endif
}

// TODO: Edit the CreateProcess call to run the program as a separate executable (in a separate window)
int main(int argc, char *argv[]) {
	StartLogger();

    STARTUPINFOA si {0};
	si.cb = sizeof(STARTUPINFOA);

    PROCESS_INFORMATION pi {0};
    std::string ws = "";
	chronoporia::InitializeWrapperAddresses();

	for (int i=1; i<argc; i++) {
		ws += argv[i];
		ws += " ";
	}
    BOOL process_created = CreateProcessA(
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
	if (process_created == 0) {
		LOG_ERROR(globals::logger, "Could not create child process");
		return 1;
	}
	LOG_INFO(globals::logger, "Child Process Created");

	globals::process_id = pi.dwProcessId;
	globals::process_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SUSPEND_RESUME | PROCESS_VM_READ | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_DUP_HANDLE, FALSE, pi.dwProcessId);
	globals::running = AttachToProcess();
	chronoporia::ResumeProcess();
	chronoporia::RunExecution();
    return 0;
}
