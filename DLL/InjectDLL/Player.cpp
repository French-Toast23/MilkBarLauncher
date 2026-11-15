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

			if (this->Equipment->Changed && exists)
			{
				this->Equipment->SetArmor(this->baseAddr);
				this->Equipment->SetWeapons(this->baseAddr);
				this->Equipment->Changed = false;
			}

			if (failedToDelete && shouldExist)
			{
				action = ActCreate;
				reasonToSpawn = "Failed to delete. ";
				failedToDelete = false;
			}
			
			if (!exists && shouldExist)
			{
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
					this->Status->set(1, __FUNCTION__);

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

				Logging::LoggerService::LogDebug("Selected action: Create. Reason: " + reasonToSpawn, __FUNCTION__);

				if (this->baseAddr != 0 && this->Status && this->Status->isAddressSet())
					this->Status->set(1, __FUNCTION__);
					//this->Delete->set(true, __FUNCTION__);

				DWORD deleteTime = GetTickCount();

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
					if (float(GetTickCount() - deleteTime) / 1000 > 5)
					{
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
			this->Teleport(Helper::Extrapolation::Next(this->Position->LastKnown, this->Speed, (1000 / 60 - FunctionTime) / 1000)); //Substracting FunctionTime to be as close to 60 fps as posible.
			this->Bomb->reset();
			this->Bomb2->reset();
			this->BombCube->reset();
			this->BombCube2->reset();

			FunctionTime = float(GetTickCount() - TimerStart);

			if (1000 / 60 - FunctionTime > 0)
				Sleep(1000 / 60 - FunctionTime);
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