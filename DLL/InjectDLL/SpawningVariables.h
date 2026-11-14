#pragma once

#include <shared_mutex>
#include "KeyCodeActor.h"
#include "BotwEdit.h"
#include <map>
#include "ActorData.h"
#include "Memory.h"
#include "dllmain_Variables.h"
#include "Entities.h"
#include "Game.h"

// This stuff here was yoinked from BetterVR
// -----------------------------------------
	extern union FPR_t {
		double fpr;
		struct
		{
			double fp0;
			double fp1;
		};
		struct
		{
			uint64_t guint;
		};
		struct
		{
			uint64_t fp0int;
			uint64_t fp1int;
		};
	};

	extern struct PPCInterpreter_t {
		uint32_t instructionPointer;
		uint32_t gpr[32];
		FPR_t fpr[32];
		uint32_t fpscr;
		uint8_t crNew[32]; // 0 -> bit not set, 1 -> bit set (upper 7 bits of each byte must always be zero) (cr0 starts at index 0, cr1 at index 4 ..)
		uint8_t xer_ca;  // carry from xer
		uint8_t LSQE;
		uint8_t PSE;
		// thread remaining cycles
		int32_t remainingCycles; // if this value goes below zero, the next thread is scheduled
		int32_t skippedCycles; // if remainingCycles is set to zero to immediately end thread execution, this value holds the number of skipped cycles
		struct
		{
			uint32_t LR;
			uint32_t CTR;
			uint32_t XER;
			uint32_t UPIR;
			uint32_t UGQR[8];
		}sprNew;
	};
	// -----------------------------------------

	typedef void (*osLib_registerHLEFunctionType)(const char* libraryName, const char* functionName, void(*osFunction)(PPCInterpreter_t* hCPU));
	typedef uint32_t (*osLib_getHLEFunctionAddressType)(const char* libraryName, const char* functionName);


#pragma pack(1)
	extern struct TransferableData { // This is reversed compared to the gfx pack because we read as big endian.
		int f_r10;
		int f_r9;
		int f_r8;
		int f_r7;
		int f_r6;
		int f_r5;
		int f_r4;
		int f_r3;

		int ringPtr;

		int fnAddr;

		byte bytepadding[2];
		bool interceptRegisters;

		bool enabled;
	};

#pragma pack(1)
	extern struct InstanceData {
		char name[152]; // We'll allocate all unused storage for use for name storage.. just in case of a really long actor name
		uint8_t actorStorage[104];
	};

	extern struct QueueActor {
		float PosX;
		float PosY;
		float PosZ;
		std::string Name;
	};

// ---------------------------------------------------------------------------------
// This is an example function call.
// - Feel free to expand / change -
// Note: This does not take into account stuff like actually setting desired params.
// ---------------------------------------------------------------------------------
std::map<char, std::vector<KeyCodeActor>> keyCodeMap;

std::shared_mutex keycode_mutex;
std::shared_mutex queue_mutex;
std::shared_mutex data_mutex;

std::map<char, bool> prevKeyStateMap; // Used for key press logic - keeps track of previous key state

std::vector<QueueActor> queuedActors;

static uint32_t s_startTrnsDataGuestAddr = 0;
static uint32_t s_storageStartGuestAddr = 0;
static uint32_t s_storageEndGuestAddr = 0;
static uint8_t s_cachedActorStorage[104] = { 0 };
static bool s_hasCachedActorStorage = false;

MemoryInstance* memInstance;

bool isSetup = false;

void setup(PPCInterpreter_t* hCPU, uint32_t startTrnsData, uint64_t baseAddress) {

	TransferableData trnsData;
	data_mutex.lock(); //////////////////////////////////////////////////
	memInstance->memory_readMemoryBE(startTrnsData, &trnsData, baseAddress); // Just make sure to intercept stuff..
	trnsData.interceptRegisters = true;
	memInstance->memory_writeMemoryBE(startTrnsData, trnsData, baseAddress);
	data_mutex.unlock(); //===============================================

	isSetup = true;
}

void queueActor(int playerNumber, float Position[3])
{
	if (queuedActors.size() > 0)
		for (int i = 0; i < queuedActors.size(); i++)
			if (queuedActors[i].Name == "Jugador" + std::to_string(playerNumber))
				return;

	//for (int j = queuedActors.size() - 1; j >= 0; j--)
	//{
	//	if (queuedActors[j].Name == "Jugador" + std::to_string(playerNumber))
	//	{
	//		return;
	//	}
	//}

	QueueActor queueActor;
	std::string actorName = "Jugador";
	actorName.append(std::to_string(playerNumber));
	queueActor.Name = actorName;
	queueActor.PosX = Position[0];
	queueActor.PosY = Position[1];
	queueActor.PosZ = Position[2];

	queuedActors.push_back(queueActor);
	Logging::LoggerService::LogInformation("Queued player actor '" + actorName + "' at (" + std::to_string(Position[0]) + ", " + std::to_string(Position[1]) + ", " + std::to_string(Position[2]) + "). Queue size: " + std::to_string(queuedActors.size()), __FUNCTION__);
}

void queueActor(std::string actorName, float Position[3])
{
	QueueActor queueActor;
	queueActor.Name = actorName;
	queueActor.PosX = Position[0];
	queueActor.PosY = Position[1];
	queueActor.PosZ = Position[2];

	queuedActors.push_back(queueActor);
	Logging::LoggerService::LogInformation("Queued actor '" + actorName + "' at (" + std::to_string(Position[0]) + ", " + std::to_string(Position[1]) + ", " + std::to_string(Position[2]) + "). Queue size: " + std::to_string(queuedActors.size()), __FUNCTION__);
}

void setupActor(PPCInterpreter_t* hCPU, TransferableData& trnsData, InstanceData& instData, uint32_t startRingBuffer, uint32_t endRingBuffer, uint64_t baseAddress) {
	QueueActor qAct = queuedActors[0];


	// Lets set any data that our params will reference:
	// -------------------------------------------------

	// Copy needed data over to our own storage to not override actor stuff
	data_mutex.lock_shared(); //////////////////////////////////////////////////
	memInstance->memory_readMemoryBE(trnsData.f_r7, &instData.actorStorage, baseAddress);
	data_mutex.unlock_shared(); //==============================================

	int actorStorageLocation = trnsData.ringPtr + sizeof(instData) - sizeof(instData.name) - sizeof(instData.actorStorage);
	int mubinLocation = trnsData.ringPtr + sizeof(instData) - sizeof(instData.name) - sizeof(instData.actorStorage) + (7 * 4); // The MubinIter lives inside the actor btw

	// Set actor pos to stored pos
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (15 * 4)], &qAct.PosX, sizeof(float));
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (16 * 4)], &qAct.PosY, sizeof(float));
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (17 * 4)], &qAct.PosZ, sizeof(float));

	// We want to make sure there's a fairly high traverseDist
	float traverseDist = 0.f; // Hmm... this kinda proves this isn't really used
	short traverseDistInt = (short)traverseDist;
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (18 * 4)], &traverseDist, sizeof(float));
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (37 * 2)], &traverseDistInt, sizeof(short));

	int null = 0;
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (11 * 4)], &null, sizeof(int)); // mLinkData

	// Might as well null out some other things
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (9 * 4)], &null, sizeof(int)); // mData
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (10 * 4)], &null, sizeof(int)); // mProc
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (7 * 4)], &null, sizeof(int)); // idk what this is
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (3 * 4)], &null, sizeof(int)); // or this, either



	// Not sure what these are, but they helps with traverseDist issues
	int traverseDistFixer = 0x043B0000;
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (2 * 4)], &traverseDistFixer, sizeof(int));
	int traverseDistFixer2 = 0x00000016;
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (1 * 4)], &traverseDistFixer2, sizeof(int));


	// Oh, and the HashId as well
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (14 * 4)], &null, sizeof(int));

	// And we can make mRevivalGameDataFlagHash an invalid handle
	int invalid = -1;
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (12 * 4)], &invalid, sizeof(int));
	// And whatever this is, too
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (13 * 4)], &invalid, sizeof(int));

	// We can also get rid of this junk
	memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (8 * 4)], &invalid, sizeof(int));




	// Set name!
	{ // Copy to name string storage... reversed
		int pos = sizeof(instData.name) - 1;
		for (char const& c : qAct.Name) {
			memcpy(instData.name + pos, &c, 1);
			pos--;
		}
		uint8_t nullByte = 0;
		memcpy(instData.name + pos, &nullByte, 1); // Null terminate!
	}

	// -------------------------------------------------

	// Set registers for params and stuff
	int nameLocation = trnsData.ringPtr + sizeof(instData) - sizeof(instData.name);
	hCPU->gpr[3] = trnsData.f_r3;
	hCPU->gpr[4] = nameLocation;
	hCPU->gpr[5] = trnsData.f_r5 ? trnsData.f_r5 : 0xFFFFFFFF;
	hCPU->gpr[6] = mubinLocation;
	hCPU->gpr[7] = actorStorageLocation;
	hCPU->gpr[8] = 0;
	hCPU->gpr[9] = 1;
	hCPU->gpr[10] = 0;

	trnsData.f_r4 = nameLocation;
	trnsData.f_r5 = trnsData.f_r5 ? trnsData.f_r5 : 0xFFFFFFFF;
	trnsData.f_r6 = mubinLocation;
	trnsData.f_r7 = actorStorageLocation;
	trnsData.f_r8 = 0;
	trnsData.f_r9 = 1;
	trnsData.f_r10 = 0;
	trnsData.fnAddr = 0x037b6040; // Address to call to

	trnsData.enabled = true; // This tells the assembly patch to trigger one function call

	trnsData.interceptRegisters = false; // We don't want to intercept *this* function call

	std::stringstream ss;
	ss << "setupActor: Invoking createActor_ for '" << qAct.Name << "' at ("
	   << qAct.PosX << ", " << qAct.PosY << ", " << qAct.PosZ << ") fn=0x037b6040 ringPtr=0x"
	   << std::hex << trnsData.ringPtr << " actorStorage=0x" << actorStorageLocation
	   << " mubin=0x" << mubinLocation;
	Logging::LoggerService::LogInformation(ss.str(), __FUNCTION__);

	// Write our actor data!
	data_mutex.lock(); ////////////////////////////////////////////////////
	memInstance->memory_writeMemoryBE(trnsData.ringPtr, instData, baseAddress);
	data_mutex.unlock(); //================================================

	trnsData.ringPtr += sizeof(InstanceData); // Move our ring ptr to the next slot!
	if (trnsData.ringPtr >= endRingBuffer) // If we're at the end of the ring....
trnsData.ringPtr = startRingBuffer; // move to the start!

// Gotta remove this actor from the queue!
queuedActors.erase(queuedActors.begin());
}

void mainFn(PPCInterpreter_t* hCPU, uint32_t startTrnsData, uint32_t startRingBuffer, uint32_t endRingBuffer, uint64_t baseAddress) {
	if (!hCPU) return;
	if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;

	if (baseAddress == 0 || !memInstance || startTrnsData < 0x1000)
		return;

	s_startTrnsDataGuestAddr = startTrnsData;
	s_storageStartGuestAddr = startRingBuffer;
	s_storageEndGuestAddr = endRingBuffer;

	if (!isSetup) {
		setup(hCPU, startTrnsData, baseAddress);
		return;
	}

	data_mutex.lock_shared();
	TransferableData trnsData;
	memInstance->memory_readMemoryBE(startTrnsData, &trnsData, baseAddress);

	uint32_t startInstData = trnsData.ringPtr;
	InstanceData instData;
	memInstance->memory_readMemoryBE(startInstData, &instData, baseAddress);
	data_mutex.unlock_shared();

	trnsData.interceptRegisters = true;

	queue_mutex.lock();
	if (queuedActors.size() >= 1) {
		setupActor(hCPU, trnsData, instData, startRingBuffer, endRingBuffer, baseAddress);
	}
	queue_mutex.unlock();

	data_mutex.lock();
	memInstance->memory_writeMemoryBE(startTrnsData, trnsData, baseAddress);
	data_mutex.unlock();
}

void mainFn(PPCInterpreter_t* hCPU) {
	if (!hCPU) return;
	if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;
	static bool s_loggedMainFn = false;
	if (!s_loggedMainFn) {
		s_loggedMainFn = true;
		Logging::LoggerService::LogInformation("mainFn HLE invoked by Cemu.");
	}
	if (Main::baseAddr == 0) return;
	mainFn(hCPU, hCPU->gpr[3], hCPU->gpr[4], hCPU->gpr[5], Main::baseAddr);
}

enum Weather : uint32_t
{
	BlueSky,
	Cloudy,
	Rain,
	HeavyRain,
	Snow,
	HeavySnow,
	Thunderstorm,
	ThunderRain,
	BlueSkyRain
};

Weather newWeather = (Weather)0;

void ProcessActorSpawnsInWeather(PPCInterpreter_t* hCPU, uint32_t mapAddr)
{
	if (!hCPU || Main::baseAddr == 0 || !memInstance || mapAddr == 0)
		return;

	if (s_startTrnsDataGuestAddr == 0)
	{
		s_startTrnsDataGuestAddr = 0x01808020;
		s_storageStartGuestAddr = 0x01800020;
		s_storageEndGuestAddr = 0x01808020;
	}

	try
	{
		std::unique_lock<std::shared_mutex> qLock(queue_mutex, std::try_to_lock);
		if (!qLock.owns_lock() || queuedActors.empty())
			return;

		// Check if previous spawn is still pending execution in patch_Weather.asm
		uint32_t spawnPending = Memory::read_bigEndian4Bytes(Main::baseAddr + mapAddr + 12);
		if (spawnPending != 0)
			return;

		data_mutex.lock();
		TransferableData trnsData;
		memInstance->memory_readMemoryBE(s_startTrnsDataGuestAddr, &trnsData, Main::baseAddr);

		if (trnsData.f_r3 == 0 || (trnsData.f_r7 == 0 && !s_hasCachedActorStorage))
		{
			if (!trnsData.interceptRegisters)
			{
				trnsData.interceptRegisters = true;
				memInstance->memory_writeMemoryBE(s_startTrnsDataGuestAddr, trnsData, Main::baseAddr);
			}
			data_mutex.unlock();
			return;
		}

		QueueActor qAct = queuedActors[0];

		InstanceData instData;
		if (!s_hasCachedActorStorage && trnsData.f_r7 != 0)
		{
			memInstance->memory_readMemoryBE(trnsData.f_r7, &instData.actorStorage, Main::baseAddr);
			memcpy(s_cachedActorStorage, instData.actorStorage, sizeof(s_cachedActorStorage));
			s_hasCachedActorStorage = true;
		}
		else if (s_hasCachedActorStorage)
		{
			memcpy(instData.actorStorage, s_cachedActorStorage, sizeof(instData.actorStorage));
		}

		int actorStorageLocation = trnsData.ringPtr + sizeof(instData) - sizeof(instData.name) - sizeof(instData.actorStorage);
		int mubinLocation = trnsData.ringPtr + sizeof(instData) - sizeof(instData.name) - sizeof(instData.actorStorage) + (7 * 4);

		// Set actor pos to stored pos
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (15 * 4)], &qAct.PosX, sizeof(float));
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (16 * 4)], &qAct.PosY, sizeof(float));
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (17 * 4)], &qAct.PosZ, sizeof(float));

		float traverseDist = 0.f;
		short traverseDistInt = (short)traverseDist;
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (18 * 4)], &traverseDist, sizeof(float));
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (37 * 2)], &traverseDistInt, sizeof(short));

		int null = 0;
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (11 * 4)], &null, sizeof(int));
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (9 * 4)], &null, sizeof(int));
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (10 * 4)], &null, sizeof(int));
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (7 * 4)], &null, sizeof(int));
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (3 * 4)], &null, sizeof(int));

		int traverseDistFixer = 0x043B0000;
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (2 * 4)], &traverseDistFixer, sizeof(int));
		int traverseDistFixer2 = 0x00000016;
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (1 * 4)], &traverseDistFixer2, sizeof(int));

		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (14 * 4)], &null, sizeof(int));

		int invalid = -1;
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (12 * 4)], &invalid, sizeof(int));
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (13 * 4)], &invalid, sizeof(int));
		memcpy(&instData.actorStorage[sizeof(instData.actorStorage) - (8 * 4)], &invalid, sizeof(int));

		int pos = sizeof(instData.name) - 1;
		for (char const& c : qAct.Name)
		{
			memcpy(instData.name + pos, &c, 1);
			pos--;
		}
		uint8_t nullByte = 0;
		memcpy(instData.name + pos, &nullByte, 1);

		memInstance->memory_writeMemoryBE(trnsData.ringPtr, instData, Main::baseAddr);

		int nameLocation = trnsData.ringPtr + sizeof(instData) - sizeof(instData.name);

		Memory::write_bigEndian4Bytes(Main::baseAddr + mapAddr + 16, trnsData.f_r3);
		Memory::write_bigEndian4Bytes(Main::baseAddr + mapAddr + 20, nameLocation);
		Memory::write_bigEndian4Bytes(Main::baseAddr + mapAddr + 24, trnsData.f_r5 ? trnsData.f_r5 : 0xFFFFFFFF);
		Memory::write_bigEndian4Bytes(Main::baseAddr + mapAddr + 28, mubinLocation);
		Memory::write_bigEndian4Bytes(Main::baseAddr + mapAddr + 32, actorStorageLocation);
		Memory::write_bigEndian4Bytes(Main::baseAddr + mapAddr + 36, 0);
		Memory::write_bigEndian4Bytes(Main::baseAddr + mapAddr + 40, 1);
		Memory::write_bigEndian4Bytes(Main::baseAddr + mapAddr + 44, 0);

		Memory::write_bigEndian4Bytes(Main::baseAddr + mapAddr + 12, 1);

		trnsData.ringPtr += sizeof(InstanceData);
		if (s_storageEndGuestAddr != 0 && trnsData.ringPtr >= (int)s_storageEndGuestAddr)
			trnsData.ringPtr = s_storageStartGuestAddr;

		memInstance->memory_writeMemoryBE(s_startTrnsDataGuestAddr, trnsData, Main::baseAddr);
		data_mutex.unlock();

		std::stringstream ss;
		ss << "ProcessActorSpawnsInWeather: Dispatched createActor_ for '" << qAct.Name << "' at ("
		   << qAct.PosX << ", " << qAct.PosY << ", " << qAct.PosZ << ") nameAddr=0x"
		   << std::hex << nameLocation << " actorStorage=0x" << actorStorageLocation
		   << " mubin=0x" << mubinLocation << " r3=0x" << trnsData.f_r3;
		Logging::LoggerService::LogInformation(ss.str(), __FUNCTION__);

		queuedActors.erase(queuedActors.begin());
	}
	catch (...) {}
}

void WeatherFn(PPCInterpreter_t* hCPU) {
	if (!hCPU) return;
	if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;
	static bool s_loggedWeather = false;
	if (!s_loggedWeather) {
		s_loggedWeather = true;
		Logging::LoggerService::LogInformation("WeatherFn HLE invoked by Cemu.");
	}

	if (Main::baseAddr == 0 || !memInstance)
		return;

	if (Game::GameInstance && Game::GameInstance->World)
	{
		Game::GameInstance->World->LocalWeather = (int)(Weather)hCPU->gpr[26];
		newWeather = (Weather)Game::GameInstance->World->Weather;

		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 0, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 1, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 2, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 3, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 4, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 5, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 6, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 7, (char)newWeather, Main::baseAddr);
		memInstance->memory_writeMemoryBE(hCPU->gpr[3] + 8, (char)newWeather, Main::baseAddr);
	}

	ProcessActorSpawnsInWeather(hCPU, hCPU->gpr[3]);
}

int LastCreated = 0;
bool started = false;

void OnActorCreate(PPCInterpreter_t* hCPU)
{
	if (!hCPU) return;
	if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;

	if (Main::baseAddr == 0)
		Main::baseAddr = Memory::getBaseAddress();

	if (Main::baseAddr == 0 || hCPU->gpr[3] < 0x1000)
		return;

	std::string name = Memory::read_string(Main::baseAddr + hCPU->gpr[3] + 0x10, 100, __FUNCTION__);
	if (name.empty())
		return;

	static int s_actorLogCount = 0;
	if (s_actorLogCount < 25 || name.rfind("GameROMPlayer") != std::string::npos || name.rfind("Jugador", 0) == 0)
	{
		s_actorLogCount++;
		Logging::LoggerService::LogInformation("OnActorCreate: actor '" + name + "' at 0x" + std::to_string(hCPU->gpr[3]));
	}

	std::vector<std::string> BombChoices = { "CustomRemoteBomb", "CustomRemoteBomb2", "CustomRemoteBombCube", "CustomRemoteBombCube2" };

	if (name.rfind("Enemy_Bokoblin_Junior") != std::string::npos)
	{
		std::stringstream stream;
		stream << "Bokoblin: " << std::hex << hCPU->gpr[3];
		Logging::LoggerService::LogDebug(stream.str());
	}

	if (name.rfind("GameROMPlayer") != std::string::npos)
	{
		std::stringstream stream;
		stream << "Link detected at: 0x" << std::hex << hCPU->gpr[3];
		Logging::LoggerService::LogInformation(stream.str());
		Main::s_LocalLinkAddr = hCPU->gpr[3];
		if (Game::GameInstance)
		{
			Game::GameInstance->setAddress(hCPU->gpr[3]);
			if (started)
			{
				try
				{
					std::map<int, MemoryAccess::LocalInstance::FlagAddresses> Flags = Game::GameInstance->scanPlayerFlags();
					for (auto& kv : Flags)
					{
						if (Instances::PlayerList.count(kv.first) && Instances::PlayerList[kv.first])
						{
							Instances::PlayerList[kv.first]->UpdateFlags(kv.second);
						}
					}
					Logging::LoggerService::LogInformation("Re-scanned player flags after Link re-creation.", __FUNCTION__);
				}
				catch (...) {}
			}
		}
	}

	if (name.rfind("Jugador", 0) == 0)
	{
		try
		{
			int spawnedPlayer = std::stoi(name.substr(7));

			std::stringstream stream;
			stream << "Spawned player " << spawnedPlayer << " at: " << std::hex << Main::baseAddr + hCPU->gpr[3] << ". Setting up addresses.";
			Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);

			if (Instances::PlayerList.find(spawnedPlayer) != Instances::PlayerList.end() && Instances::PlayerList[spawnedPlayer])
			{
				Instances::PlayerList[spawnedPlayer]->setAddress(hCPU->gpr[3]);
				if (Instances::PlayerList[spawnedPlayer]->Equipment)
					Instances::PlayerList[spawnedPlayer]->Equipment->SetWeapons(hCPU->gpr[3]);
				if (Instances::PlayerList[spawnedPlayer]->Bumii)
					Instances::PlayerList[spawnedPlayer]->Bumii->setAddress(hCPU->gpr[3]);

				Logging::LoggerService::LogInformation("Player " + std::to_string(spawnedPlayer) + " setup correctly.", __FUNCTION__);
			}
		}
		catch (...) {}
	}

	if (name.rfind("Enemy_", 0) == 0)
	{
		if (Game::GameInstance && Game::GameInstance->EnemyService)
			Game::GameInstance->EnemyService->UpdateEnemyAddress(Main::baseAddr + hCPU->gpr[3], true);
	}

	if (!started || !Game::GameInstance) return;

	if (name == "RemoteBomb" && Game::GameInstance->Bomb)
	{
		Game::GameInstance->Bomb->setAddress(hCPU->gpr[3], __FUNCTION__);
		Game::GameInstance->Bomb->changeState(Normal);
	}
	else if (name == "RemoteBomb2" && Game::GameInstance->Bomb2)
	{
		Game::GameInstance->Bomb2->setAddress(hCPU->gpr[3], __FUNCTION__);
		Game::GameInstance->Bomb2->changeState(Normal);
	}
	else if (name == "RemoteBombCube" && Game::GameInstance->BombCube)
	{
		Game::GameInstance->BombCube->setAddress(hCPU->gpr[3], __FUNCTION__);
		Game::GameInstance->BombCube->changeState(Normal);
	}
	else if (name == "RemoteBombCube2" && Game::GameInstance->BombCube2)
	{
		Game::GameInstance->BombCube2->setAddress(hCPU->gpr[3], __FUNCTION__);
		Game::GameInstance->BombCube2->changeState(Normal);
	}

	int BombType = -1;
	for (int i = 0; i < 4; i++)
	{
		if (name == BombChoices[i])
			BombType = i;
	}

	if (BombType == -1)
		return;

	for (int j = 1; j < 33; j++)
	{
		if (Instances::PlayerList.find(j) == Instances::PlayerList.end() || !Instances::PlayerList[j] || !Instances::PlayerList[j]->connected)
			continue;

		BombAccess* Bomb = nullptr;
		if (BombType == 0)
			Bomb = Instances::PlayerList[j]->Bomb;
		else if (BombType == 1)
			Bomb = Instances::PlayerList[j]->Bomb2;
		else if (BombType == 2)
			Bomb = Instances::PlayerList[j]->BombCube;
		else if (BombType == 3)
			Bomb = Instances::PlayerList[j]->BombCube2;

		if (Bomb && Bomb->getStatus() == Processing)
		{
			Bomb->setAddress(hCPU->gpr[3], __FUNCTION__);
			Bomb->changeState(Normal);
			std::stringstream stream;
			stream << "Assigned " << name << " to player " << std::to_string(j) << " at: " << std::hex << hCPU->gpr[3];
			Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);
			return;
		}
	}
}

void OnActorErase(PPCInterpreter_t* hCPU)
{
	if (!hCPU) return;
	if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;

	if (Main::baseAddr == 0)
		Main::baseAddr = Memory::getBaseAddress();

	if (Main::baseAddr == 0 || hCPU->gpr[3] < 0x1000)
		return;

	std::string name = Memory::read_string(Main::baseAddr + hCPU->gpr[3] + 0x10, 100, __FUNCTION__);
	if (name.empty())
		return;

	if (name.rfind("GameROMPlayer") != std::string::npos)
	{
		Logging::LoggerService::LogInformation("Link actor erased.", __FUNCTION__);
		Main::s_LocalLinkAddr = 0;
		if (Game::GameInstance)
			Game::GameInstance->setAddress(0);
	}

	std::vector<std::string> BombChoices = {"CustomRemoteBomb", "CustomRemoteBomb2", "CustomRemoteBombCube", "CustomRemoteBombCube2"};

	if (name.rfind("Jugador", 0) == 0) {
		try
		{
			int spawnedPlayer = std::stoi(name.substr(7));
			if (Instances::PlayerList.find(spawnedPlayer) != Instances::PlayerList.end() && Instances::PlayerList[spawnedPlayer])
			{
				if (Instances::PlayerList[spawnedPlayer]->Status)
					Instances::PlayerList[spawnedPlayer]->Status->set(0, __FUNCTION__);
				Instances::PlayerList[spawnedPlayer]->setAddress(0);
				std::stringstream stream;
				stream << "Player " << spawnedPlayer << " erased.";
				Logging::LoggerService::LogDebug(stream.str(), __FUNCTION__);
			}
		}
		catch (...) {}
	}

	if (name.rfind("Enemy_", 0) == 0)
	{
		if (Game::GameInstance && Game::GameInstance->EnemyService)
			Game::GameInstance->EnemyService->UpdateEnemyAddress(Main::baseAddr + hCPU->gpr[3], false);
	}

	if (!started || !Game::GameInstance) return;

	if (name == "RemoteBomb" && Game::GameInstance->Bomb)
	{
		Game::GameInstance->Bomb->setAddress(0, __FUNCTION__);
		Game::GameInstance->Bomb->changeState(Cancelled);
	}
	else if (name == "RemoteBomb2" && Game::GameInstance->Bomb2)
	{
		Game::GameInstance->Bomb2->setAddress(0, __FUNCTION__);
		Game::GameInstance->Bomb2->changeState(Cancelled);
	}
	else if (name == "RemoteBombCube" && Game::GameInstance->BombCube)
	{
		Game::GameInstance->BombCube->setAddress(0, __FUNCTION__);
		Game::GameInstance->BombCube->changeState(Cancelled);
	}
	else if (name == "RemoteBombCube2" && Game::GameInstance->BombCube2)
	{
		Game::GameInstance->BombCube2->setAddress(0, __FUNCTION__);
		Game::GameInstance->BombCube2->changeState(Cancelled);
	}

	int BombType = -1;
	for (int i = 0; i < 4; i++)
	{
		if (name == BombChoices[i])
			BombType = i;
	}

	if (BombType == -1)
		return;

	for (int j = 1; j < 33; j++)
	{
		if (Instances::PlayerList.find(j) == Instances::PlayerList.end() || !Instances::PlayerList[j] || !Instances::PlayerList[j]->connected)
			continue;

		BombAccess* Bomb = nullptr;
		if (BombType == 0)
			Bomb = Instances::PlayerList[j]->Bomb;
		else if (BombType == 1)
			Bomb = Instances::PlayerList[j]->Bomb2;
		else if (BombType == 2)
			Bomb = Instances::PlayerList[j]->BombCube;
		else if (BombType == 3)
			Bomb = Instances::PlayerList[j]->BombCube2;

		if (Bomb && Bomb->BaseAddr == hCPU->gpr[3])
		{
			Bomb->setAddress(0, __FUNCTION__);
			Bomb->changeState(Deallocated);
			std::stringstream stream;
			stream << "Bomb " << name << " of player " << std::to_string(j) << " despawned.";
			Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);
			return;
		}
	}
}

void remoteBomb_onAICalc(PPCInterpreter_t* hCPU)
{
	if (!hCPU) return;
	if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;

	if (Main::baseAddr == 0 || !memInstance || hCPU->gpr[3] < 0x1000)
		return;

	unsigned int aiPtr = hCPU->gpr[3];
	unsigned int actPtr = 0;
	std::vector<std::string> BombChoices = { "CustomRemoteBomb", "CustomRemoteBomb2", "CustomRemoteBombCube", "CustomRemoteBombCube2" };

	memInstance->memory_readMemoryBE(aiPtr, &actPtr, Main::baseAddr);
	if (actPtr == 0) return;

	std::string name = Memory::read_string(Main::baseAddr + actPtr + 0x10, 50, __FUNCTION__);

	auto b1 = Memory::read_bytes(aiPtr + 0x78 + Main::baseAddr, 1, __FUNCTION__);
	auto b2 = Memory::read_bytes(aiPtr + 0x9C + Main::baseAddr, 1, __FUNCTION__);
	bool explode = (!b1.empty() && b1[0] == 0x1);
	bool cancel = (!b2.empty() && b2[0] == 0x1);

	if ((explode || cancel) && Game::GameInstance)
	{
		if (name == "RemoteBomb" && Game::GameInstance->Bomb)
		{
			Game::GameInstance->Bomb->setAddress(0, __FUNCTION__);
			Game::GameInstance->Bomb->changeState(explode ? Exploded : Cancelled);
		}
		else if (name == "RemoteBomb2" && Game::GameInstance->Bomb2)
		{
			Game::GameInstance->Bomb2->setAddress(0, __FUNCTION__);
			Game::GameInstance->Bomb2->changeState(explode ? Exploded : Cancelled);
		}
		else if (name == "RemoteBombCube" && Game::GameInstance->BombCube)
		{
			Game::GameInstance->BombCube->setAddress(0, __FUNCTION__);
			Game::GameInstance->BombCube->changeState(explode ? Exploded : Cancelled);
		}
		else if (name == "RemoteBombCube2" && Game::GameInstance->BombCube2)
		{
			Game::GameInstance->BombCube2->setAddress(0, __FUNCTION__);
			Game::GameInstance->BombCube2->changeState(explode ? Exploded : Cancelled);
		}
	}

	int BombType = -1;
	for (int i = 0; i < 4; i++)
	{
		if (name == BombChoices[i])
			BombType = i;
	}

	if (BombType == -1)
		return;

	for (int j = 1; j < 33; j++)
	{
		if (Instances::PlayerList.find(j) == Instances::PlayerList.end() || !Instances::PlayerList[j] || !Instances::PlayerList[j]->connected)
			continue;

		BombAccess* Bomb = nullptr;
		if (BombType == 0)
			Bomb = Instances::PlayerList[j]->Bomb;
		else if (BombType == 1)
			Bomb = Instances::PlayerList[j]->Bomb2;
		else if (BombType == 2)
			Bomb = Instances::PlayerList[j]->BombCube;
		else if (BombType == 3)
			Bomb = Instances::PlayerList[j]->BombCube2;

		if (!Bomb || Bomb->getStatus() == Deallocated)
			continue;

		if (Bomb->BaseAddr == actPtr)
		{
			BombStatus status = Bomb->getStatus();
			if (status == Exploded)
			{
				Memory::write_byte(aiPtr + 0x78 + Main::baseAddr, 0x1, __FUNCTION__);
				Bomb->changeState(Deallocated);
				std::stringstream stream;
				stream << "Bomb " << name << " of player " << std::to_string(j) << " exploded.";
				Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);
				Bomb->setAddress(0, __FUNCTION__);
				return;
			}
			else if (status == Cancelled)
			{
				Memory::write_byte(aiPtr + 0x9C + Main::baseAddr, 0x1, __FUNCTION__);
				Bomb->changeState(Deallocated);
				std::stringstream stream;
				stream << "Bomb " << name << " of player " << std::to_string(j) << " erased.";
				Logging::LoggerService::LogInformation(stream.str(), __FUNCTION__);
				Bomb->setAddress(0, __FUNCTION__);
				return;
			}
		}
	}
}

void remoteBomb_onAICalc2(PPCInterpreter_t* hCPU)
{
	if (!hCPU) return;
	if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;

	std::vector<std::string> RealBombChoices = { "RemoteBomb", "RemoteBomb2", "RemoteBombCube", "RemoteBombCube2" };
	
	unsigned int aiPtr = hCPU->gpr[3];
	unsigned int actPtr;
	memInstance->memory_readMemoryBE(aiPtr, &actPtr, Main::baseAddr);

	std::string name = Memory::read_string(Main::baseAddr + actPtr + 0x10, 50, __FUNCTION__);

	bool explode = Memory::read_bytes(aiPtr + 0x78 + Main::baseAddr, 1, __FUNCTION__)[0] == 0x1 ? true : false;
	bool cancel = Memory::read_bytes(aiPtr + 0x9C + Main::baseAddr, 1, __FUNCTION__)[0] == 0x1 ? true : false;

	Main::BombSync->BombExplodeMutex.lock();

	for (int i = 0; i < 4; i++)
	{
		if (name.rfind(RealBombChoices[i], 0) == 0)
		{

			if (explode)
			{
				Main::BombSync->BombMutex.lock();
				Main::BombSync->Bombs[name] = 0;
				Main::BombSync->BombMutex.unlock();
			}
			else if (cancel)
			{
				Main::BombSync->BombMutex.lock();
				Main::BombSync->Bombs[name] = -1;
				Main::BombSync->BombMutex.unlock();
			}
		}
	}

	Main::BombSync->BombExplodeMutex.unlock();

	Main::BombSync->BombMutex.lock();
	for (int i = 0; i < Main::BombSync->BombsToExplode.size(); i++)
		if (Main::BombSync->BombsToExplode[i] == actPtr)
		{
			Memory::write_byte(aiPtr + 0x78 + Main::baseAddr, 0x1, __FUNCTION__);
			Main::BombSync->BombsToExplode.erase(Main::BombSync->BombsToExplode.begin() + i);
		}
	
	for (int i = 0; i < Main::BombSync->BombsToClear.size(); i++)
		if (Main::BombSync->BombsToClear[i] == actPtr)
		{
			Memory::write_byte(aiPtr + 0x9C + Main::baseAddr, 0x1, __FUNCTION__);
			Main::BombSync->BombsToClear.erase(Main::BombSync->BombsToClear.begin() + i);
		}
	Main::BombSync->BombMutex.unlock();
}

void timemgr_OnInit(PPCInterpreter_t* hCPU)
{
	if (!hCPU) return;
	if (hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;

	if (Main::baseAddr == 0 || !Game::GameInstance || !Game::GameInstance->World)
		return;

	Game::GameInstance->World->ReadableTime = new BigEndian<float>(hCPU->gpr[3] + 0x98 + Main::baseAddr, __FUNCTION__);
	Game::GameInstance->World->WritableTime = new BigEndian<float>(hCPU->gpr[3] + 0xA0 + Main::baseAddr, __FUNCTION__);
}

void dummySelfTestFn(PPCInterpreter_t* hCPU)
{
	if (hCPU && hCPU->sprNew.LR >= 0x01000000 && hCPU->sprNew.LR < 0x10000000)
		hCPU->instructionPointer = hCPU->sprNew.LR;
}

void init() {
	static bool s_initialized = false;
	if (s_initialized) return;

	HMODULE hCemu = GetModuleHandleA(NULL);
	if (!hCemu) hCemu = GetModuleHandleA("Cemu.exe");
	if (!hCemu)
	{
		Logging::LoggerService::LogError("init(): GetModuleHandle Cemu.exe failed!");
		return;
	}

	osLib_registerHLEFunctionType osLib_registerHLEFunction = (osLib_registerHLEFunctionType)GetProcAddress(hCemu, "osLib_registerHLEFunction");
	if (!osLib_registerHLEFunction)
	{
		Logging::LoggerService::LogError("init(): GetProcAddress osLib_registerHLEFunction failed!");
		return;
	}

	// Self-test: register dummy function to verify index lookup offset (+0x10) per Claude Spec §2.3 #5
	osLib_registerHLEFunction("selftest", "dummy", static_cast<void (*) (PPCInterpreter_t*)>(&dummySelfTestFn));
	osLib_getHLEFunctionAddressType osLib_getHLEFunctionAddress = (osLib_getHLEFunctionAddressType)((char*)osLib_registerHLEFunction + 0x10);
	uint32_t dummyIdx = osLib_getHLEFunctionAddress("selftest", "dummy");
	if (dummyIdx == 0 || dummyIdx > 0xFFFF)
	{
		std::stringstream ss;
		ss << "init(): Self-test FAILED! Lookup returned invalid index 0x" << std::hex << dummyIdx
		   << ". Offset +0x10 is invalid for this Cemu build!";
		Logging::LoggerService::LogError(ss.str());
		return;
	}
	Logging::LoggerService::LogInformation("init(): Self-test PASSED (dummyIdx=0x" + std::to_string(dummyIdx) + "). HLE offset verified.");

	osLib_registerHLEFunction("spawnactors", "fnCallMain", static_cast<void (*) (PPCInterpreter_t*)>(&mainFn));
	osLib_registerHLEFunction("multiplayer", "WeatherSync", static_cast<void (*) (PPCInterpreter_t*)>(&WeatherFn));
	osLib_registerHLEFunction("ukl_actorinterceptor", "OnActorCreate", static_cast<void (*) (PPCInterpreter_t*)>(&OnActorCreate));
	osLib_registerHLEFunction("ukl_actorinterceptor", "OnActorDeleteLater", static_cast<void (*) (PPCInterpreter_t*)>(&OnActorErase));
	osLib_registerHLEFunction("ukl_actorinterceptor", "OnActorErase", static_cast<void (*) (PPCInterpreter_t*)>(&OnActorErase));
	osLib_registerHLEFunction("ukl_remotebombaiinterceptor", "OnCalc", &remoteBomb_onAICalc);
	osLib_registerHLEFunction("ukl_timemgrinterceptor", "OnInit", &timemgr_OnInit);

	s_initialized = true;
	Logging::LoggerService::LogInformation("init(): All HLE interceptors registered successfully.");
}

static void BindSlot(uint64_t slotHost, uint32_t idx, const char* name)
{
	if (idx == 0 || idx > 0xFFFF) return; // rejects 0, > 0xFFFF, and 0xFFFFFFFF (unregistered)
	int op = (int)(0x04000000u | idx);
	int current = Memory::read_bigEndian4Bytes(slotHost, __FUNCTION__);
	if (current != op)
	{
		Memory::write_bigEndian4Bytes(slotHost, op, __FUNCTION__);
		int readback = Memory::read_bigEndian4Bytes(slotHost, __FUNCTION__);
		std::stringstream ss;
		ss << "BindSlot: " << name << " at 0x" << std::hex << slotHost
			<< " (idx=0x" << idx << ") wrote op=0x" << op
			<< ", readback=0x" << readback
			<< (readback == op ? " (VERIFIED MATCH)" : " (MISMATCH!)");
		Logging::LoggerService::LogInformation(ss.str());
	}
}

void BindHLEAddresses()
{
	if (Main::baseAddr == 0)
		return;

	HMODULE hCemu = GetModuleHandleA(NULL);
	if (!hCemu) hCemu = GetModuleHandleA("Cemu.exe");
	if (!hCemu) return;

	osLib_registerHLEFunctionType osLib_registerHLEFunction = (osLib_registerHLEFunctionType)GetProcAddress(hCemu, "osLib_registerHLEFunction");
	if (!osLib_registerHLEFunction) return;

	osLib_getHLEFunctionAddressType osLib_getHLEFunctionAddress = (osLib_getHLEFunctionAddressType)((char*)osLib_registerHLEFunction + 0x10);

	uint32_t addr_OnActorCreate = osLib_getHLEFunctionAddress("ukl_actorinterceptor", "OnActorCreate");
	uint32_t addr_OnActorDeleteLater = osLib_getHLEFunctionAddress("ukl_actorinterceptor", "OnActorDeleteLater");
	uint32_t addr_OnActorErase = osLib_getHLEFunctionAddress("ukl_actorinterceptor", "OnActorErase");
	uint32_t addr_WeatherSync = osLib_getHLEFunctionAddress("multiplayer", "WeatherSync");
	uint32_t addr_fnCallMain = osLib_getHLEFunctionAddress("spawnactors", "fnCallMain");
	uint32_t addr_RemoteBomb = osLib_getHLEFunctionAddress("ukl_remotebombaiinterceptor", "OnCalc");
	uint32_t addr_TimeMgr = osLib_getHLEFunctionAddress("ukl_timemgrinterceptor", "OnInit");

	// Safety check: ensure lookup returned a sane HLE table index
	if (addr_OnActorCreate == 0 || addr_OnActorCreate > 0xFFFF)
	{
		static bool s_loggedLookupError = false;
		if (!s_loggedLookupError)
		{
			s_loggedLookupError = true;
			std::stringstream ss;
			ss << "BindHLEAddresses: osLib_getHLEFunctionAddress lookup invalid (OnActorCreate=0x"
				<< std::hex << addr_OnActorCreate << "). Aborting slot binding.";
			Logging::LoggerService::LogError(ss.str());
		}
		return;
	}

	static bool s_loggedAddresses = false;
	if (!s_loggedAddresses)
	{
		s_loggedAddresses = true;
		std::stringstream ss;
		ss << "BindHLEAddresses: OnActorCreate idx=0x" << std::hex << addr_OnActorCreate
			<< ", fnCallMain idx=0x" << addr_fnCallMain
			<< ", WeatherSync idx=0x" << addr_WeatherSync
			<< ", RemoteBomb idx=0x" << addr_RemoteBomb
			<< ", TimeMgr idx=0x" << addr_TimeMgr;
		Logging::LoggerService::LogInformation(ss.str());
	}

	// Pure dynamic scan of graphic pack codecaves in range [0x01800000, 0x01820000]
	// NEVER write to hardcoded fallback offsets, which corrupt adjacent modules!
	uint8_t* pStart = (uint8_t*)(Main::baseAddr + 0x01800000);
	bool foundActor = false;
	bool foundSpawn = false;
	bool foundWeather = false;
	bool foundBomb = false;
	bool foundTime = false;

	const char targetStrActor[] = "ukl_actorinterceptor";
	const char targetStrSpawn[] = "spawnactors";
	const char targetStrWeather[] = "WeatherSync";
	const char targetStrBomb[] = "ukl_remotebombaiinterceptor";
	const char targetStrTime[] = "ukl_timemgrinterceptor";

	for (size_t offset = 0; offset < 0x20000; offset += 4)
	{
		if (!foundActor && memcmp(pStart + offset, targetStrActor, sizeof(targetStrActor) - 1) == 0)
		{
			uint64_t modBase = Main::baseAddr + 0x01800000 + offset;
			BindSlot(modBase + 0x2C, addr_OnActorCreate, "OnActorCreate (scan)");
			BindSlot(modBase + 0x44, addr_OnActorDeleteLater, "OnActorDeleteLater (scan)");
			BindSlot(modBase + 0x58, addr_OnActorErase, "OnActorErase (scan)");
			foundActor = true;
		}

		if (!foundSpawn && memcmp(pStart + offset, targetStrSpawn, sizeof(targetStrSpawn) - 1) == 0)
		{
			if (offset >= 0x14)
			{
				uint32_t spawnActorsGuestBase = 0x01800000 + (uint32_t)(offset - 0x14);
				uint64_t modBase = Main::baseAddr + spawnActorsGuestBase;
				BindSlot(modBase, addr_fnCallMain, "fnCallMain (scan)");
				s_storageStartGuestAddr = spawnActorsGuestBase + 0x20;
				s_storageEndGuestAddr = spawnActorsGuestBase + 0x8020;
				s_startTrnsDataGuestAddr = spawnActorsGuestBase + 0x8020;

				// Enable register interception immediately so createActor_ captures r3..r10 on the very first actor created!
				Memory::write_byte(Main::baseAddr + s_startTrnsDataGuestAddr + 1, 1, "BindHLEAddresses::InterceptRegisters");
				foundSpawn = true;
			}
		}

		if (!foundWeather && memcmp(pStart + offset, targetStrWeather, sizeof(targetStrWeather) - 1) == 0)
		{
			if (offset >= 4)
			{
				uint64_t modBase = Main::baseAddr + 0x01800000 + offset - 4;
				BindSlot(modBase, addr_WeatherSync, "WeatherSync (scan)");
				foundWeather = true;
			}
		}

		if (!foundBomb && memcmp(pStart + offset, targetStrBomb, sizeof(targetStrBomb) - 1) == 0)
		{
			uint64_t modBase = Main::baseAddr + 0x01800000 + offset;
			BindSlot(modBase + 0x28, addr_RemoteBomb, "RemoteBomb (scan)");
			foundBomb = true;
		}

		if (!foundTime && memcmp(pStart + offset, targetStrTime, sizeof(targetStrTime) - 1) == 0)
		{
			uint64_t modBase = Main::baseAddr + 0x01800000 + offset;
			BindSlot(modBase + 0x24, addr_TimeMgr, "TimeMgr (scan)");
			foundTime = true;
		}

		if (foundActor && foundSpawn && foundWeather && foundBomb && foundTime)
			break;
	}

	static bool s_loggedScanResult = false;
	if (!s_loggedScanResult)
	{
		s_loggedScanResult = true;
		std::stringstream ss;
		ss << "BindHLEAddresses Scan Result: Actor=" << (foundActor ? "OK" : "MISSING")
		   << ", Spawn=" << (foundSpawn ? "OK" : "MISSING")
		   << ", Weather=" << (foundWeather ? "OK" : "MISSING")
		   << ", Bomb=" << (foundBomb ? "OK" : "MISSING")
		   << ", Time=" << (foundTime ? "OK" : "MISSING");
		if (foundSpawn)
		{
			ss << " | storageStart=0x" << std::hex << s_storageStartGuestAddr
			   << ", trnsData=0x" << s_startTrnsDataGuestAddr;
		}
		Logging::LoggerService::LogInformation(ss.str());
	}
}

