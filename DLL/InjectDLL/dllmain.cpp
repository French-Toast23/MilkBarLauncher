#define _WINSOCKAPI_
#include <vector>
#include <Windows.h>
#include <string>
#include <iostream>
#include "dllmain_Variables.h"
#include "dllmain_Functions.h"
#include "Connectivity.h"
#include "Memory.h"
#include "LoggerService.h"

using namespace Main;

////////////////// Players definitions //////////////////

Memory::Link_class* Main::Link = new Memory::Link_class();
Memory::World_class* Main::World = new Memory::World_class();

Memory::OtherPlayer_class* Main::Jugador1 = new Memory::OtherPlayer_class(1);
Memory::OtherPlayer_class* Main::Jugador2 = new Memory::OtherPlayer_class(2);
Memory::OtherPlayer_class* Main::Jugador3 = new Memory::OtherPlayer_class(3);
Memory::OtherPlayer_class* Main::Jugador4 = new Memory::OtherPlayer_class(4);

Memory::OtherPlayer_class* Main::Jugadores[] = { Jugador1, Jugador2, Jugador3, Jugador4 };

Memory::BombSyncer* Main::BombSync = new Memory::BombSyncer();

////////////////// Parameter initialization //////////////////

uint64_t Main::baseAddr = Memory::getBaseAddress();
uint64_t Main::s_LocalLinkAddr = 0;

std::vector < std::vector<float> > Main::Jugador1Queue = { {}, {}, {} };
std::vector < std::vector<float> > Main::Jugador2Queue = { {}, {}, {} };
std::vector < std::vector<float> > Main::Jugador3Queue = { {}, {}, {} };
std::vector < std::vector<float> > Main::Jugador4Queue = { {}, {}, {} };
std::vector < std::vector<float> > Main::JugadoresQueues[] = {Jugador1Queue, Jugador2Queue, Jugador3Queue, Jugador4Queue};

Connectivity::namedPipeClass* Main::namedPipe = new Connectivity::namedPipeClass();
Connectivity::Client* Main::client = new Connectivity::Client();

std::shared_mutex WeaponChangeMutex;
std::shared_mutex BombExplodeMutex;

float Main::targetFPS = 1000;
float Main::serializationRate = 20;
float Main::ping = 0;

int Main::playerNumber = 0;
bool Main::IsProp = false;
bool Main::IsPropHuntStopped = true;
bool Main::HidePlayer32 = true;
std::vector<byte> Main::FoundPlayers = {};
std::string Main::serverName = "";
std::vector<bool> Main::ConnectedPlayers = { false, false, false, false };

DWORD Main::t0 = GetTickCount();
DWORD Main::t1 = GetTickCount();

std::string Main::serverData = "";

bool Main::isEnemySync = false;
bool Main::isGlyphSync = false;
bool Main::isQuestSync = false;
bool Main::isHvsSR = false;
bool Main::isDeathSwap = false;

int Main::GlyphUpdateTime = 60;
int Main::GlyphDistance = 250;

std::vector<std::string> Main::questServerSettings;

bool Main::isPaused = true;

std::vector<float> Main::oldLocations[] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

HMODULE myhModule;

bool Main::QuestSyncReady = false;


DWORD __stdcall EjectThread(LPVOID lpParameter) {
    Sleep(100);
    FreeLibraryAndExitThread(myhModule, 0);
}

bool startServerLoop()
{
    Logging::LoggerService::LogInformation("startServerLoop: Spawning mainServerLoop thread...");
    HANDLE hThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)Main::mainServerLoop, 0, 0, 0);
    if (hThread)
    {
        CloseHandle(hThread);
        return true;
    }
    Logging::LoggerService::LogError("startServerLoop: CreateThread failed: " + std::to_string(GetLastError()));
    return false;
}

void readInstruction()
{
    DWORD read = 0;
    bool started = false;

    while (!started)
    {
        TCHAR chBuff[BUFF_SIZE] = { 0 };
        TCHAR responsePositive[BUFF_SIZE] = "Succeeded";
        TCHAR responseNegative[BUFF_SIZE] = "Failed";
        bool response = false;

        BOOL readSuccess = ReadFile(namedPipe->hPipe, chBuff, (BUFF_SIZE - 1) * sizeof(TCHAR), &read, nullptr);
        if (!readSuccess || read == 0)
        {
            Sleep(50);
            continue;
        }

        chBuff[read] = '\0';
        Logging::LoggerService::LogInformation(std::string("Received pipe instruction: ") + chBuff);

        if (strstr(chBuff, "!connect"))
        {
            response = Main::connectToServer(chBuff);
            Logging::LoggerService::LogInformation(response ? "connectToServer: Succeeded" : "connectToServer: Failed");
        }
        else if (strstr(chBuff, "!startServerLoop"))
        {
            Logging::LoggerService::LogInformation("Start server loop requested...");
            if (!started)
            {
                response = true;
                started = true;
            }
            else
            {
                exit(1);
            }
        }

        DWORD written = 0;
        if (response)
        {
            WriteFile(namedPipe->hPipe, responsePositive, BUFF_SIZE * sizeof(TCHAR), &written, nullptr);
        }
        else
        {
            WriteFile(namedPipe->hPipe, responseNegative, BUFF_SIZE * sizeof(TCHAR), &written, nullptr);
        }
    }

    startServerLoop();
}

DWORD WINAPI WorkerThread(LPVOID lpParam)
{
    Sleep(500);
    Logging::LoggerService::StartLoggerService();
    Logging::LoggerService::LogInformation("InjectDLL WorkerThread started (PID: " + std::to_string(GetCurrentProcessId()) + ")");
    Main::SetupAssemblyPatches();
    namedPipe->createServer();
    readInstruction();
    return 0;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}