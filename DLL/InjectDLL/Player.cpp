#include "Player.h"

namespace Main {
	extern uint64_t s_LocalLinkAddr;
}

using namespace MemoryAccess;

void Player::PThread()
{
	float FunctionTime = 0; // In milliseconds
	DWORD LastSpawn = 0;
	const int SPAWN_LIMITER = 3;
	bool failedToDelete = false;
	DWORD PendingCreateSince = 0;          // a create was queued and the actor hasn't appeared yet
	const DWORD PENDING_CREATE_TIMEOUT = 10000;
	int equipmentRecreates = 0;            // respawns done to apply the current equipment
	const int MAX_EQUIPMENT_RECREATES = 2;

	std::stringstream startStream;
	startStream << "Player " << this->PlayerNumber << " thread starting...";
	Logging::LoggerService::LogInformation(startStream.str(), __FUNCTION__);

	while (RunThread)
	{
		try 
		{
			if (!connected || isFar) {
				Sleep(100);
				continue;
			}

			if (GameInstance == nullptr || GameInstance->baseAddr == 0 || Main::s_LocalLinkAddr == 0 || !Memory::IsStageReady())
			{
				Sleep(100);
				continue;
			}

			ValidateActor();

			DWORD TimerStart = GetTickCount();

			ActionEnum action = ActSkip;

			bool shouldExist = false;

			CharacterLocation LocalLocation = GameInstance->Location ? GameInstance->Location->LastKnown : CharacterLocation();
			std::string localMap = (!LocalLocation.Map.empty() && LocalLocation.Map != "Empty") ? LocalLocation.Map : "MainField";
			std::string remoteMap = (this->Location && !this->Location->Map.empty() && this->Location->Map != "Empty") ? this->Location->Map : "MainField";

			std::string reasonToSpawn = "";

			if (localMap == remoteMap && (localMap == "MainField" || (this->Location && LocalLocation.Section == this->Location->Section)) && !this->isFar)
			{
				shouldExist = true;
			}

			bool exists = this->baseAddr != 0;

			if (exists)
				PendingCreateSince = 0;

			if (this->EquipmentEdited)
			{
				this->EquipmentEdited = false;
				equipmentRecreates = 0;
			}

			// Model strings are read when the actor is created, so new armor needs a respawn
			// (same as the original mod). Capped so a moving resource address can't loop forever.
			if (this->Equipment->Changed && exists)
			{
				if (equipmentRecreates < MAX_EQUIPMENT_RECREATES)
				{
					equipmentRecreates++;
					action = ActCreate;
					reasonToSpawn = "Equipment changed. ";
				}
				else
				{
					this->Equipment->SetArmor(this->baseAddr);
					this->Equipment->SetWeapons(this->baseAddr);
					this->Equipment->Changed = false;
				}
			}

			if (failedToDelete && shouldExist)
			{
				action = ActCreate;
				reasonToSpawn = "Failed to delete. ";
				failedToDelete = false;
			}
			
			if (!exists && shouldExist)
			{
				if (PendingCreateSince != 0 && GetTickCount() - PendingCreateSince < PENDING_CREATE_TIMEOUT)
				{
					Sleep(100); // previous spawn still in flight, don't queue a duplicate
					continue;
				}
				action = ActCreate;
				reasonToSpawn += "Actor should exist.";
			}

			if (exists && !shouldExist)
				action = ActDelete;

			if (action == ActDelete)
			{
				Logging::LoggerService::LogDebug("Selected action: Delete", __FUNCTION__);
				//this->Delete->set(true, __FUNCTION__);
				if (this->Status && this->Status->isAddressSet())
					{ this->DeleteRequested = true; this->Status->set(1, __FUNCTION__); }

				while (this->baseAddr != 0)
					Sleep(50);

				//this->Delete->set(false, __FUNCTION__);
				if (this->Status && this->Status->isAddressSet())
					this->Status->set(0, __FUNCTION__);

				continue;
			}

			if (action == ActCreate)
			{
				if (LastSpawn != 0 && float(GetTickCount() - LastSpawn) / 1000 < SPAWN_LIMITER)
				{
					Sleep(300);
					continue;
				}

				if (GameInstance->baseAddr == 0 || Main::s_LocalLinkAddr == 0 || GameInstance->IsPaused() || !Memory::IsStageReady())
				{
					LastSpawn = GetTickCount();
					Logging::LoggerService::LogWarning("Create request failed: World is not loaded or game is paused.");
					continue;
				}

				if (Main::IsStageUnstable())
				{
					Sleep(200);
					continue;
				}

				Logging::LoggerService::LogDebug("Selected action: Create. Reason: " + reasonToSpawn, __FUNCTION__);

				if (this->baseAddr != 0 && this->Status && this->Status->isAddressSet())
					{ this->DeleteRequested = true; this->Status->set(1, __FUNCTION__); }
					//this->Delete->set(true, __FUNCTION__);

				DWORD deleteTime = GetTickCount();
				bool keepExisting = false;

				while (this->baseAddr != 0 || GameInstance->IsPaused())
				{
					if (GameInstance->baseAddr == 0 || Main::s_LocalLinkAddr == 0)
					{
						this->setAddress(0);
						break;
					}

					if (GameInstance->IsPaused())
						deleteTime = GetTickCount();

					// If it's been more than 5 seconds since you tried to delete, we can assume the actor is deleted
					// The game takes a little over 5 s to process Status = 1 (seen in logs), so wait 8 s.
					if (float(GetTickCount() - deleteTime) / 1000 > 8)
					{
						if (IsActorAlive())
						{
							// The actor ignored the delete request. Keep controlling it instead of
							// spawning a second one (the old one would be left sliding in T-pose).
							Logging::LoggerService::LogWarning("Player " + std::to_string(this->PlayerNumber) + " actor did not delete; keeping it.", __FUNCTION__);
							keepExisting = true;
							break;
						}
						this->setAddress(0);
						std::stringstream stream;
						stream << "Could not delete player " << this->PlayerNumber << ". Old actor could still be visible in the world.";
						Logging::LoggerService::LogWarning(stream.str(), __FUNCTION__);
						failedToDelete = true;
						if (this->Status && this->Status->isAddressSet())
							this->Status->set(0, __FUNCTION__);
						LastSpawn = GetTickCount();
						break;
					}

					Sleep(100);
				}

				//this->Delete->set(false, __FUNCTION__);
				if (this->Status && this->Status->isAddressSet())
					this->Status->set(0, __FUNCTION__);

				if (keepExisting)
				{
					equipmentRecreates = MAX_EQUIPMENT_RECREATES;
					this->Equipment->SetArmor(this->baseAddr);
					this->Equipment->SetWeapons(this->baseAddr);
					this->Equipment->Changed = false;
					LastSpawn = GetTickCount();
					continue;
				}

				if (GameInstance->baseAddr == 0 || Main::s_LocalLinkAddr == 0 || GameInstance->IsPaused() || !Memory::IsStageReady())
				{
					LastSpawn = GetTickCount();
					Logging::LoggerService::LogWarning("Create request failed: World is not loaded or game is paused.");
					continue;
				}

				if (this->Model.ModelType == 0)
				{
					Logging::LoggerService::LogDebug("Setting up " + std::to_string(this->PlayerNumber) + " armor...", __FUNCTION__);
					//this->Bumii->WriteMiiData(1033785125, 598924800);
					this->Equipment->SetArmor(this->baseAddr);
				}
				else if (this->Model.ModelType == 1)
				{
					Logging::LoggerService::LogDebug("Setting up " + std::to_string(this->PlayerNumber) + " model...", __FUNCTION__);
					//this->Bumii->WriteMiiData(1033785125, 598924800);
					this->Equipment->SetModel(this->Model.Model);
				}
				else
				{
					Logging::LoggerService::LogDebug("Setting up " + std::to_string(this->PlayerNumber) + " bumii...", __FUNCTION__);
					this->Bumii->WriteMiiData();
				}

				if (GameInstance->IsPaused())
				{
					LastSpawn = GetTickCount();
					Logging::LoggerService::LogWarning("Create request failed: Game is paused.");
					continue;
				}

				GameInstance->RequestCreate(this->PlayerNumber, this->LastServerPosition);
				if (this->Exists && this->Exists->isAddressSet())
					this->Exists->set(false, __FUNCTION__);

				LastSpawn = GetTickCount();
				PendingCreateSince = LastSpawn;

				continue;
			}

			if (this->baseAddr == 0)
			{
				Sleep(100);
				continue;
			}

			if (!this->Position || !this->Position->isAddressSet())
			{
				if (!this->resolvePosition())
				{
					Sleep(50);
					continue;
				}
			}

			//TODO: Bring targetFPS
			// Snapshot interpolation: draw the remote player slightly in the past, blended between real samples.
			{
				Vec3f drawPos;
				if (this->SamplePosition(drawPos))
					this->Teleport(drawPos);
			}
			this->Bomb->reset();
			this->Bomb2->reset();
			this->BombCube->reset();
			this->BombCube2->reset();

			FunctionTime = float(GetTickCount() - TimerStart);

			// ~120 updates/s so the blended position stays smooth with FPS++ at 60-120 FPS.
			if (8 - FunctionTime > 0)
				Sleep(DWORD(8 - FunctionTime));
		}
		catch (const std::exception& ex)
		{
			std::stringstream stream;
			stream << "Exception in Player::PThread: " << ex.what();
			Logging::LoggerService::LogWarning(stream.str(), __FUNCTION__);
			Sleep(100);
		}
		catch (...)
		{
			Logging::LoggerService::LogWarning("Caught unknown exception in Player::PThread.", __FUNCTION__);
			Sleep(100);
		}
	}

	std::stringstream endStream;
	endStream << "Player " << this->PlayerNumber << " thread stopping...";
	Logging::LoggerService::LogInformation(endStream.str(), __FUNCTION__);
}