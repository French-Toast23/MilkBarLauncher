#include "Connectivity.h"

using namespace Connectivity;

void namedPipeClass::createServer()
{
	bool pipeOpened = false;
	int tries = 0;
	HANDLE hPipeTemp;
	// One pipe per Cemu process: the launcher that started this Cemu names its pipe after our PID. With a
	// shared name, two launchers running at once made each Cemu connect to whichever pipe answered first.
	std::string pipeName = "\\\\.\\pipe\\languageConnectionPipe_" + std::to_string(GetCurrentProcessId());
	Logging::LoggerService::LogDebug("Connecting to named pipe " + pipeName);

	while (!pipeOpened)
	{
		hPipeTemp = CreateFileA(pipeName.c_str(), GENERIC_ALL, 0, nullptr, OPEN_EXISTING, 0, nullptr);

		if (hPipeTemp == INVALID_HANDLE_VALUE)
		{
			tries++;
			Sleep(10);
			continue;
		}

		pipeOpened = true;

		DWORD mode = PIPE_READMODE_MESSAGE;

		SetNamedPipeHandleState(hPipeTemp, &mode, nullptr, nullptr);

		this->hPipe = hPipeTemp;
	}
}