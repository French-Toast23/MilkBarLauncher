#include "LoggerService.h"

using namespace Logging;

std::string LoggerService::GetTimeAsString(int Format)
{

	char buffer[80];

	struct tm timeinfo;
	time_t now = time(0);
	localtime_s(&timeinfo, &now);

	if(Format == 1)
		strftime(buffer, sizeof(buffer), "%d-%m-%Y %H:%M:%S", &timeinfo);
	else if(Format == 2)
		strftime(buffer, sizeof(buffer), "%d-%m-%Y", &timeinfo);

	std::string str(buffer);

	return buffer;

}

void LoggerService::WriteToLog(std::string Message, std::string LogType, const char* caller)
{
	Logging::LogMutex.lock();

	std::string timeStr = LoggerService::GetTimeAsString(1);

	if (Logging::PidLogFile.is_open())
	{
		Logging::PidLogFile << "[" << LogType << "][" << timeStr << "]";
		if (caller != nullptr && caller[0] != '\0')
			Logging::PidLogFile << "[" << caller << "]";
		Logging::PidLogFile << " " << Message << std::endl;
		Logging::PidLogFile.flush();
	}

	if (Logging::LogFile.is_open())
	{
		Logging::LogFile << "[" << LogType << "][" << timeStr << "]";
		if (caller != nullptr && caller[0] != '\0')
			Logging::LogFile << "[" << caller << "]";
		Logging::LogFile << " " << Message << std::endl;
		Logging::LogFile.flush();
	}

	Logging::LogMutex.unlock();
}

void LoggerService::StartLoggerService()
{

	char* appdata = nullptr;
	size_t sz = 0;

	_dupenv_s(&appdata, &sz, "APPDATA");

	std::string str(appdata);

	std::string filepath = "\\BOTWM\\LatestLog.txt";

	std::ifstream file(appdata + filepath);

	if (file.good())
	{
		std::string LogsPath = "\\BOTWM\\Logs";

		std::ifstream LogsFolder(appdata + LogsPath);

		if (!LogsFolder.good())
		{
			_mkdir((appdata + LogsPath).c_str());
		}

		int counter = 0;

		do 
		{

			std::string fileDate;

			std::getline(file, fileDate);

			file.close();

			file.open(appdata + filepath);

			std::ifstream Logfile(appdata + LogsPath + "\\" + fileDate + "_" + std::to_string(counter) + ".txt");

			if (!Logfile.good())
			{

				std::ofstream OldLog((appdata + LogsPath + "\\" + fileDate + "_" + std::to_string(counter) + ".txt").c_str());

				std::string newLine;

				while (std::getline(file, newLine))
				{
					OldLog << newLine << std::endl;
				}

				OldLog.close();
				file.close();

				remove((appdata + filepath).c_str());

				break;

			}

			counter++;

		} while (true);

	}

	file.close();

	DWORD currentPid = GetCurrentProcessId();
	std::string pidFilepath = "\\BOTWM\\LatestLog_" + std::to_string(currentPid) + ".txt";
	PidLogFile.open((appdata + pidFilepath).c_str(), std::ios_base::out | std::ios_base::trunc);
	if (PidLogFile.is_open())
	{
		PidLogFile << LoggerService::GetTimeAsString(2) << " [PID: " << currentPid << "]" << std::endl;
		PidLogFile << "Logging level: Debug" << std::endl;
		PidLogFile.flush();
	}

	LogFile.open((appdata + filepath).c_str(), std::ios_base::app);
	if (LogFile.is_open())
	{
		LogFile << LoggerService::GetTimeAsString(2) << " [PID: " << currentPid << "]" << std::endl;
		LogFile << "Logging level: Debug" << std::endl;
		LogFile.flush();
	}

}

void LoggerService::LogWarning(std::string Message, const char* caller)
{
	if (LEVEL < 1)
		return;

	LoggerService::WriteToLog(Message, "WRN", caller);
}

void LoggerService::LogDebug(std::string Message, const char* caller)
{
	if (LEVEL < 2)
		return;
	LoggerService::WriteToLog(Message, "DBG", caller);
}

void LoggerService::LogInformation(std::string Message, const char* caller)
{
	LoggerService::WriteToLog(Message, "INF", caller);
}

void LoggerService::LogCritical(std::string Message, const char* caller)
{
	LoggerService::WriteToLog(Message, "CRT", caller);
}

void LoggerService::LogError(std::string Message, const char* caller)
{
	LoggerService::WriteToLog(Message, "ERR", caller);
}

void LoggerService::StartTimer(std::string timerName)
{
	if (TimerName != "")
		LogWarning("Overwriting timer: \"" + TimerName + "\". Timer results may be wrong.");

	Timer = GetTickCount();
	TimerName = timerName;
}

void LoggerService::FinishTimer()
{
	if (TimerName != "")
		LogDebug("Timer \"" + TimerName + "\" finished at: " + std::to_string(float(GetTickCount() - Timer)) + " ms.");
	else
		LogWarning("Tried to terminate a timer but no timer was set.");

	TimerName = "";
}