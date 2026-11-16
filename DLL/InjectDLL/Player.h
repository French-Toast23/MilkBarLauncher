#pragma once

#include "Actor.h"
#include "EquipmentAccess.h"
#include "BumiiAccess.h"
#include "Extrapolation.h"
#include "LocalInstance.h"
#include "Vec3f_Operations.h"
#include "AnimTables.h"
#include "AnimNames.h"
#include "AnimDebug.h"
#include <set>
#include <mutex>
#include <deque>
#include <algorithm>
#include <cmath>

namespace MemoryAccess
{
	class Player : public Actor
	{
	public:
		int PlayerNumber;
		bool isFar = true;
		bool connected = false;
		std::string Name = "";
		ModelData Model = ModelData();
		int ping = 0;
		DWORD LastUpdated = 0;
		DWORD LastAttack = 0;
		int LastAnimation = 0;
		int ChargeAttackRetries = 0;
		bool EquipmentEdited = false; // remote player changed equipment since last check
		int animDiagWrites = 0;       // diagnostics: log the first few animation writes
		bool DeleteRequested = false; // we set Status=1 ourselves (expected deletion)
		bool LastIsEquipped = false;
		bool HoldWritten = false;
		int aliveMisses = 0;          // consecutive failed IsActorAlive checks
		uint32_t LastUpperAnimation = 0; // last clip written to the upper-body event step
		DWORD SpawnedAt = 0;          // tick of the last OnActorCreate for this player

		// The game deleted our new actor almost immediately: the placement data copied from the game's
		// last createActor_ call is unusable (seen 16 Nov 2026: memory reused by 'Kibako_Contain_0' at once).
		// Drop it so the next spawn copies another actor's data.
		void CheckInstantDeletion()
		{
			if (SpawnedAt != 0 && !DeleteRequested && GetTickCount() - SpawnedAt < 5000)
			{
				Logging::LoggerService::LogInformation("Player " + std::to_string(PlayerNumber) + " was deleted by the game right after spawning; discarding the spawn template.", __FUNCTION__);
				Main::RequestFreshSpawnCapture();
			}
			SpawnedAt = 0;
		}

		// Animations that break when played alone on the Jugador (animation test, 15 Nov 2026):
		// partial/additive layers (*_Upper, *_Add*, Charge_Move_*) freeze the rest of the body, and
		// horse/motorcycle rider poses float or sink without a mount. Keep the previous full-body one instead.
		static bool IsStandalonePlayable(uint32_t h)
		{
			std::string n = AnimNames::Lookup(h);
			return n.find("_Upper") == std::string::npos && n.find("_Add") == std::string::npos &&
				n.rfind("Charge_Move_", 0) != 0 && n.find("Horse") == std::string::npos &&
				n.find("Motorcycle") == std::string::npos;
		}

		// Animation hash to play: the received one if the Jugador ASList has it, otherwise the last good
		// one, otherwise idle (Nml_Wait). A hash missing from the ASList would leave the actor in T-pose.
		uint32_t ResolveAnim(int received) const
		{
			if (received != 0 && AnimTables::IsValidAnim(UINT32(received)) && IsStandalonePlayable(UINT32(received)))
				return UINT32(received);
			if (LastAnimation != 0)
				return UINT32(LastAnimation);
			return AnimTables::IdleAnim;
		}

		Vec3f Speed = Vec3f(0, 0, 0);
		Vec3f LastUpdatedPosition = Vec3f(0, 0, 0);
		Vec3f LastServerPosition = Vec3f(0, 0, 0);
		float AtkUp = 1;
		int Health = 0;

		CharacterLocation* Location = new CharacterLocation();
		EquipmentAccess* Equipment = new EquipmentAccess();
		BumiiAccess* Bumii = new BumiiAccess();
		LittleEndian<bool>* Exists = nullptr;
		LittleEndian<bool>* DispName = nullptr;
		BigEndian<int>* Status = nullptr;
		uint64_t NameAddr = 0;
		uint64_t AnimAddr = 0;
		uint64_t HoldAddr = 0;
		Vec3fBE* MapPin = nullptr;
		uint64_t AttackAddr = 0;
		BombAccess* Bomb = new BombAccess();
		BombAccess* Bomb2 = new BombAccess();
		BombAccess* BombCube = new BombAccess();
		BombAccess* BombCube2 = new BombAccess();

		bool HideFromMap = false;

		MemoryAccess::LocalInstance* GameInstance = nullptr;

		std::thread pThread;
		bool RunThread = false;

		enum ActionEnum
		{
			ActSkip,
			ActCreate,
			ActDelete
		};

		Player(byte playerNumber, MemoryAccess::LocalInstance* instance, MemoryAccess::LocalInstance::FlagAddresses actorFlags)
		{
			GameInstance = instance;
			PlayerNumber = playerNumber;
			Equipment->SetPlayerNumber(playerNumber);

			Exists = new LittleEndian<bool>(actorFlags.ExistsFlag, __FUNCTION__);
			DispName = new LittleEndian<bool>(actorFlags.DispNameFlag, __FUNCTION__);
			Status = new BigEndian<int>(actorFlags.StatusFlag, __FUNCTION__);
			if (Status && Status->isAddressSet())
				Status->set(0, __FUNCTION__);
			NameAddr = actorFlags.NameFlag;
			AnimAddr = actorFlags.AnimationFlag;
			HoldAddr = actorFlags.HoldFlag;
			MapPin = new Vec3fBE(actorFlags.MapPin, __FUNCTION__);
			AttackAddr = actorFlags.AttackAnimation;
		}

		void UpdateFlags(MemoryAccess::LocalInstance::FlagAddresses actorFlags)
		{
			Mutex.lock();
			if (Exists) Exists->setAddress(actorFlags.ExistsFlag, __FUNCTION__);
			if (DispName) DispName->setAddress(actorFlags.DispNameFlag, __FUNCTION__);
			if (Status) Status->setAddress(actorFlags.StatusFlag, __FUNCTION__);
			NameAddr = actorFlags.NameFlag;
			AnimAddr = actorFlags.AnimationFlag;
			HoldAddr = actorFlags.HoldFlag;
			if (MapPin) MapPin->setAddress(actorFlags.MapPin, __FUNCTION__);
			AttackAddr = actorFlags.AttackAnimation;
			LastAnimation = 0; // flag storage may have moved: force the animation string to be rewritten
			Mutex.unlock();
		}

		// True when baseAddr still points at our Jugador actor. Fast travel inside the open world
		// destroys remote actors without going through the erase hooks, and the freed memory is
		// reused by other actors (a fish ended up following Link), so the pointer must be re-checked.
		bool IsActorAlive()
		{
			uint64_t addr = this->baseAddr;
			if (addr == 0)
				return false;
			std::string name = Memory::read_string(Memory::getBaseAddress() + addr + 0x10, 16, __FUNCTION__);
			return name == "Jugador" + std::to_string(PlayerNumber);
		}

		// Drops a stale actor pointer. Returns true if the actor is still valid.
		bool ValidateActor()
		{
			if (this->baseAddr == 0)
				return false;
			if (IsActorAlive())
			{
				aliveMisses = 0;
				return true;
			}
			// A single failed read must not orphan a live actor: require several misses in a row.
			if (++aliveMisses < 3)
				return true;
			aliveMisses = 0;
			Logging::LoggerService::LogInformation("Player " + std::to_string(PlayerNumber) + " actor is gone (name read '" +
				Memory::read_string(Memory::getBaseAddress() + this->baseAddr + 0x10, 16, __FUNCTION__) + "'); dropping stale pointer.", __FUNCTION__);
			CheckInstantDeletion();
			if (!DeleteRequested)
				Main::MarkStageUnstable(8000, true); // vanished without our request (a teleport also trips the jump detector)
			DeleteRequested = false;
			setAddress(0);
			return false;
		}

		void setAddress(uint64_t addr)
		{
			Mutex.lock();
			Actor::setAddress(addr);
			LastAnimation = 0; // new (or no) actor: rewrite the animation flag on the next update
			LastUpperAnimation = 0;
			Mutex.unlock();
		}

		void set(DTO::CloseCharacterDTO* PlayerData, bool DispNames, bool isPaused)
		{
			if (!connected)
				Connect();

			isFar = false;
			Location->Map = PlayerData->Location.Map;
			Location->Section = PlayerData->Location.Section;

			if (!isPaused)
				UpdateName(DispNames && !HideFromMap);

			UpdateMapPin(PlayerData->Position, !HideFromMap);

			LastServerPosition = PlayerData->Position;

			if (this->baseAddr == 0 || !PlayerData->Updated || !ValidateActor())
				return;

			const std::string DEFAULT_ANIM = "Jugador" + std::to_string(PlayerNumber) + "_animationthing";
			const std::string DEFAULT_ATTACK = "Jugador32_AttackAnimation";

			/* Attack detection
			Animations stored in AttackAnimations are the animations we have identified as attack such as Sword_Attack_S1
			*/
			std::vector<int> AttackAnimations = { 928165310, 1294448978, 583341237, 897962260, -1903343953, -2023673433, 847655708, -1589496633, 2146177340, -1303343253, -123813806, 1543951008, 1128528738, -1248213324, 1874126347, 806287798, -742419528, -1630162536, 1600181768, 1039952503, -1888311262, -1480116640, -1927143427, -533414859, -36670094, -2108440740, -1884523108, -1615366629, -1246603057, -1470721656, -1996120276, -1797171605, -2076564023, -112340596, 728159080, -458680117, -1713443698, 1542065194, 647886959, -781586933, 997798184, 1175373165, -393452632, 554609132, -1130259111, 1021993131, -2109979814, -30085678, -18972657, 1185849402, -482719416, -1803236064, -1884724833, -1840404264, -1342573430, -1543868860, -1306706483, -1743252711, -2047026594, -426229114, -75723839, -2108575578, -14269213, -1827922336, -490566236, -1615496735, 1568774469, 553020672, 1761813251, 1024631879, 1080381442, -765641975, -207435949, -563725248, -297398764, -1192420357, -991578765, 745729712, 610861347, 831924215, -1370122879, 835249056, -176172835, 463333429, 259059134, -25556495, -1508973864, 74790034, -629870140, 1754272119, 1320358935, 789467239, 273082485, -1044574760, 780998171, 1752946335, -1169493048, 1414384225, 1199510679, 1767142188, 953715144, -1064130453, 964742086, -716436526, 1310776481, -447236380, -1742716255, -122454109, -646716027, -2145874292, 773977556, 710297995, 1201725498, -277486945, -813687283, 1234073523, 1553707875, -796449271, 1695977382, -1484777825, 1919785957, 971277116, 369948204, 1323545514, 24633967, 131976475, -1630233439, 747673358, -1029769694, 1559913345, 738022167, -1292631379, -973917637, -916295166, -996204138, -1025054081, -585303469, -576147724, -1259644422, -1747463897, -831621317, -432033607, 811876277, 1656633699, 791313214, 33565683, -1695879491, 554960384, 1531097105, 468014366, 353438393, 1046237356, -632640808, 131165289, -1693877687, -1553477000, -1206036550, 1657769172, -334607649, -1273182661, -820353236, 1974769815, -1432420688, -712724095, -2116836187, -866005140, 237519009, -1257045322, 22218317, -1492002104, -1798676531, 1578196338, 153657744, -1165622557, 140698192, 776070960 };

			bool IsAttack = false;

			// Only hashes that have an "Attack_<hash>" demo action can be played as attacks
			if (std::find(AttackAnimations.begin(), AttackAnimations.end(), PlayerData->Animation) != AttackAnimations.end()
				&& AnimTables::IsAttackAction(UINT32(PlayerData->Animation)))
			{
				LastAttack = GetTickCount();
				IsAttack = true;
			}

			PushSnapshot(PlayerData->Position);

			Rotation1->set(PlayerData->Rotation1, __FUNCTION__);
			Rotation2->set(PlayerData->Rotation2, __FUNCTION__);
			Rotation3->set(PlayerData->Rotation3, __FUNCTION__);
			Rotation4->set(PlayerData->Rotation4, __FUNCTION__);
			Rotation5->set(PlayerData->Rotation4, __FUNCTION__);

			if (isPaused)
				return;

			bool IsCharge = false;

			if (PlayerData->Animation == 806287798)
			{
				IsAttack = false;

				if (LastAnimation != 806287798)
				{
					ChargeAttackRetries = 0;
				}
				else
				{
					ChargeAttackRetries++;
					
					if (ChargeAttackRetries > 10)
					{
						IsAttack = true;
						IsCharge = true;
					}
				}
			}

			if (PlayerData->Animation == 835249056)
				IsAttack = false;

			// Diagnostics: log each received hash the Jugador ASList lacks once, so missing animations can be mapped.
			if (PlayerData->Animation != 0 && !AnimTables::IsValidAnim(UINT32(PlayerData->Animation)))
			{
				static std::mutex s_rejMutex;
				static std::set<uint32_t> s_rejected;
				std::lock_guard<std::mutex> lock(s_rejMutex);
				if (s_rejected.size() < 300 && s_rejected.insert(UINT32(PlayerData->Animation)).second)
				{
					std::stringstream ss;
					ss << "Player " << PlayerNumber << " sent animation " << UINT32(PlayerData->Animation)
				   << " " << AnimNames::Lookup(UINT32(PlayerData->Animation))
					   << " not in Jugador ASList (attack action: " << (AnimTables::IsAttackAction(UINT32(PlayerData->Animation)) ? "yes" : "no")
					   << "), keeping " << ResolveAnim(PlayerData->Animation);
					Logging::LoggerService::LogInformation(ss.str(), "AnimFlagDiag");
				}
			}

			// Animation test mode (anim_mode.txt = test): play the test animation and show its name as the tag.
			const bool animTest = MemoryAccess::LocalInstance::AnimMode() == 3;
			if (animTest)
			{
				AnimDebug::Tick();
				IsAttack = false;
				if (NameAddr >= 30000)
					Memory::write_string(NameAddr, AnimDebug::Label(), 32, __FUNCTION__);
				if (this->DispName && this->DispName->isAddressSet())
					this->DispName->set(true, __FUNCTION__);
			}
			const uint32_t wantAnim = animTest ? AnimDebug::Current() : ResolveAnim(PlayerData->Animation);

			if (this->Status && this->Status->isAddressSet() && this->Status->get(__FUNCTION__) != 1)
			{
				if (IsAttack && (IsCharge || PlayerData->Animation != LastAnimation))
				{
					this->Status->set(2, __FUNCTION__);
					std::string attackName = "Attack_" + std::to_string(UINT32(PlayerData->Animation));
					if (AttackAddr >= 30000)
						Memory::write_string(AttackAddr, attackName, DEFAULT_ATTACK.size(), __FUNCTION__);
					MemoryAccess::LocalInstance::WriteEvflAnimation(PlayerNumber, attackName, 1);
				}
				else
				{
					this->Status->set(0, __FUNCTION__);
					uint32_t animVal = wantAnim;
					if (animVal != UINT32(LastAnimation))
					{
						std::string animName = "Anim_" + std::to_string(animVal);
						// legacy target (event-flow file buffer) + the big-endian runtime copy the game runs
						if (AnimAddr >= 30000)
							Memory::write_string(AnimAddr, animName, DEFAULT_ANIM.size(), __FUNCTION__);
						MemoryAccess::LocalInstance::WriteEvflAnimation(PlayerNumber, animName, 0);
						if (animDiagWrites < 3)
						{
							animDiagWrites++;
							std::stringstream ss;
							ss << "Player " << PlayerNumber << " anim write '" << animName << "'";
							{
								std::lock_guard<std::mutex> lock(MemoryAccess::LocalInstance::EvflMutex);
								auto& t = MemoryAccess::LocalInstance::EvflAnimStrings;
								if (t.count(PlayerNumber))
									for (uint64_t a : t[PlayerNumber])
										ss << " runtime@0x" << std::hex << a << "='" << Memory::read_string(a, 40, __FUNCTION__) << "'";
								else
									ss << " (no runtime copy found yet)";
							}
							Logging::LoggerService::LogInformation(ss.str(), "AnimFlagDiag");
						}
					}
				}

				// Upper-body layer (event step "Jugador<N>_upperanimthing", TargetIndex 2): the sender's overlay
				// clip (shield, horse call, bow draw...). Without one, the upper body plays the same clip as the
				// body (or the attack clip during attacks) so it simply follows.
				uint32_t upperVal;
				if (!animTest && PlayerData->UpperAnimation != 0 && AnimTables::IsValidAnim(UINT32(PlayerData->UpperAnimation)))
					upperVal = UINT32(PlayerData->UpperAnimation);
				else if (IsAttack && AnimTables::IsValidAnim(UINT32(PlayerData->Animation)))
					upperVal = UINT32(PlayerData->Animation);
				else
					upperVal = wantAnim;
				if (upperVal != LastUpperAnimation)
				{
					LastUpperAnimation = upperVal;
					MemoryAccess::LocalInstance::WriteEvflAnimation(PlayerNumber, "Anim_" + std::to_string(upperVal), 3);
					static int s_upperLogs = 0;
					if (PlayerData->UpperAnimation != 0 && s_upperLogs < 200)
					{
						s_upperLogs++;
						Logging::LoggerService::LogInformation("Player " + std::to_string(PlayerNumber) + " upper body: " +
							AnimNames::Lookup(upperVal), "AnimFlagDiag");
					}
				}
			}

			uint32_t effectiveAnim = wantAnim;
			if (effectiveAnim != UINT32(LastAnimation))
			{
				static int s_loggedAnimCount = 0;
				if (s_loggedAnimCount < 400)
				{
					s_loggedAnimCount++;
					std::stringstream ss;
					ss << "Player " << (int)PlayerNumber << " animation changed: "
					   << LastAnimation << " -> " << effectiveAnim
					   << " (Anim_" << std::to_string(effectiveAnim) << " " << AnimNames::Lookup(effectiveAnim)
					   << ", received " << AnimNames::Lookup(UINT32(PlayerData->Animation)) << ")";
					Logging::LoggerService::LogInformation(ss.str());
				}
			}
			
			LastAnimation = effectiveAnim;

			if (HoldAddr >= 30000)
				Memory::write_string(HoldAddr, PlayerData->IsEquipped ? "Hold" : "Equip", 6, __FUNCTION__);
			if (PlayerData->IsEquipped != LastIsEquipped || !HoldWritten)
			{
				MemoryAccess::LocalInstance::WriteEvflAnimation(PlayerNumber, PlayerData->IsEquipped ? "Hold" : "Equip", 2);
				LastIsEquipped = PlayerData->IsEquipped;
				HoldWritten = true;
			}
			{
				CharacterEquipment before = *this->Equipment->LastKnown;
				this->Equipment->Changed = Equipment->Compare(PlayerData->Equipment);
				const CharacterEquipment& after = *this->Equipment->LastKnown;
				if (before.Head != after.Head || before.Upper != after.Upper || before.Lower != after.Lower ||
					before.Sword != after.Sword || before.Shield != after.Shield || before.WType != after.WType || before.Bow != after.Bow)
					EquipmentEdited = true;
			}
			AtkUp = PlayerData->AtkUp;
			Health = PlayerData->Health;

			ManageBomb(PlayerData->Bomb, 0);
			ManageBomb(PlayerData->Bomb2, 1);
			ManageBomb(PlayerData->BombCube, 2);
			ManageBomb(PlayerData->BombCube2, 3);
		}

		void set(DTO::FarCharacterDTO* PlayerData, bool DispNames, bool isPaused)
		{
			if (!connected)
				Connect();

			this->Health = 0;
			isFar = true;

			UpdateMapPin(PlayerData->Position, !HideFromMap);

			if (isPaused)
				return;

			UpdateName(DispNames);
			if (this->Status && this->Status->isAddressSet())
				{ DeleteRequested = true; this->Status->set(1, __FUNCTION__); }

		}

		void setName(std::string newName)
		{
			if (NameAddr >= 30000)
				Memory::write_string(NameAddr, newName, 32, __FUNCTION__);
		}

		// ---- Snapshot interpolation (replaces the old speed-guess extrapolation, which overshot and snapped back).
		// Every received position is stored with its arrival time. The remote player is drawn INTERP_DELAY_MS in the
		// past, blended between the two real positions around that moment. When packets are late it keeps moving
		// along the last direction for at most MAX_EXTRAPOLATE_MS, then waits. Jumps over SNAP_DISTANCE (fast travel,
		// respawn) are applied instantly.
		struct PosSnapshot { double Time; Vec3f Pos; };
		// Milliseconds from the high-resolution counter. GetTickCount only advances every ~15.6 ms, which made the
		// blend factor jump in steps (visible at 60-120 FPS with FPS++).
		static double NowMs()
		{
			static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
			LARGE_INTEGER c; QueryPerformanceCounter(&c);
			return double(c.QuadPart) * 1000.0 / double(freq.QuadPart);
		}
		std::deque<PosSnapshot> Snapshots;
		std::mutex SnapshotMutex;
		static constexpr double INTERP_DELAY_MS = 100.0;
		static constexpr double MAX_EXTRAPOLATE_MS = 150.0;
		static constexpr float SNAP_DISTANCE = 25.0f;

		static float Dist(Vec3f a, Vec3f b) // Vec3f's accessors aren't const, so take copies
		{
			float dx = a.x() - b.x(), dy = a.y() - b.y(), dz = a.z() - b.z();
			return sqrtf(dx * dx + dy * dy + dz * dz);
		}

		static Vec3f Lerp(Vec3f a, Vec3f b, float t)
		{
			return Vec3f(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t, a.z() + (b.z() - a.z()) * t);
		}

		void PushSnapshot(Vec3f pos)
		{
			std::lock_guard<std::mutex> lock(SnapshotMutex);
			double now = NowMs();
			if (!Snapshots.empty() && Snapshots.back().Pos.x() == pos.x() && Snapshots.back().Pos.y() == pos.y() && Snapshots.back().Pos.z() == pos.z())
			{
				Snapshots.back().Time = now; // standing still: keep one sample, refreshed
				if (Snapshots.size() >= 2 && Snapshots[Snapshots.size() - 2].Pos.x() == pos.x() && Snapshots[Snapshots.size() - 2].Pos.y() == pos.y() && Snapshots[Snapshots.size() - 2].Pos.z() == pos.z())
					return;
				Snapshots.push_back({ now, pos });
				return;
			}
			Snapshots.push_back({ now, pos });
			while (Snapshots.size() > 64)
				Snapshots.pop_front();
		}

		void ClearSnapshots()
		{
			std::lock_guard<std::mutex> lock(SnapshotMutex);
			Snapshots.clear();
		}

		PosSnapshot PrevSnapshot{ 0, Vec3f(0, 0, 0) }; // last sample dropped from the buffer (for extrapolation)

		// Position to draw now. False when nothing has been received yet.
		bool SamplePosition(Vec3f& out)
		{
			std::lock_guard<std::mutex> lock(SnapshotMutex);
			if (Snapshots.empty())
				return false;

			double renderTime = NowMs() - INTERP_DELAY_MS;

			// drop samples entirely behind the render time, keeping the one just before it
			while (Snapshots.size() >= 2 && Snapshots[1].Time <= renderTime)
			{
				PrevSnapshot = Snapshots.front();
				Snapshots.pop_front();
			}

			const PosSnapshot& a = Snapshots[0];
			if (renderTime <= a.Time)
			{
				out = a.Pos; // render time is before the oldest sample
				return true;
			}

			if (Snapshots.size() == 1)
			{
				// newest sample is already in the past: packets are late. Continue along the last movement
				// for a short while, then hold.
				double span = a.Time - PrevSnapshot.Time;
				if (PrevSnapshot.Time != 0 && span > 0 && Dist(PrevSnapshot.Pos, a.Pos) <= SNAP_DISTANCE)
				{
					double late = (std::min)(renderTime - a.Time, MAX_EXTRAPOLATE_MS);
					out = Lerp(PrevSnapshot.Pos, a.Pos, float(1.0 + late / span));
				}
				else
					out = a.Pos;
				return true;
			}

			const PosSnapshot& b = Snapshots[1];
			if (Dist(a.Pos, b.Pos) > SNAP_DISTANCE)
			{
				out = b.Pos; // a teleport, not movement
				return true;
			}
			double span = b.Time - a.Time;
			float t = span > 0 ? float((renderTime - a.Time) / span) : 1.0f;
			out = Lerp(a.Pos, b.Pos, (std::min)((std::max)(t, 0.0f), 1.0f));
			return true;
		}

		void Teleport(Vec3f newPosition, bool Save = false)
		{
			if (!Save)
			{
				Actor::Teleport(newPosition);
				return;
			}

			Vec3f OldPosition = this->Position->LastKnown;
			Vec3f PosOffsetPcnt = Vec3f();

			for (int i = 0; i < 3; i++)
			{
				PosOffsetPcnt[i] = (newPosition[i] - OldPosition[i]) / newPosition[i];
				if (PosOffsetPcnt[i] > 1)
				{
					PosOffsetPcnt[i] = 0;
				}
			}

			if (LastUpdated == 0)
			{
				Speed = Vec3f(0, 0, 0); // Set speed to 0 as we are just starting
			}
			else
			{
				Vec3f newSpeed = Helper::Extrapolation::CalcSpeed(LastUpdatedPosition, newPosition, float(GetTickCount() - LastUpdated) / 1000); // Returns Vec3f in distanceUnits/seconds
				if (Helper::Vec3f_Operations::Equals(newSpeed, Vec3f(0, 0, 0)))
				{
					Speed = newSpeed;
					Actor::Teleport(newPosition);

					LastUpdated = GetTickCount();
					LastUpdatedPosition = newPosition;
					return;
				}

				Speed = Vec3f((Speed.x() + newSpeed.x())/ 2, (Speed.y() + newSpeed.y()) / 2, (Speed.z() + newSpeed.z()) / 2);
			}

			Vec3f SpeedFromLastKnown = Helper::Extrapolation::CalcSpeed(OldPosition, newPosition, 1); // Value of 1 because we are not looking for the actual speed

			for (int i = 0; i < 3; i++)
			{
				Speed[i] += Speed[i] * PosOffsetPcnt[i]; //TODO: Test more deeply

				if (Helper::Vec3f_Operations::GetSigns(SpeedFromLastKnown[i]) != Helper::Vec3f_Operations::GetSigns(Speed[i]) && abs(OldPosition[i] - newPosition[i]) < 0.2)
				{
					Speed[i] = 0;
				}

				if (abs(OldPosition[i] - newPosition[i]) > 3)
				{
					Actor::Teleport(newPosition);
				}
			}

			LastUpdated = GetTickCount();
			LastUpdatedPosition = newPosition;
		}

		~Player()
		{
			Disconnect();
		}

		void Connect()
		{
			if (connected)
				return;

			//this->Delete->set(false, __FUNCTION__);
			if (this->Status && this->Status->isAddressSet())
				this->Status->set(0, __FUNCTION__);
			connected = true;
			if (PlayerNumber != 32)
			{
				Logging::LoggerService::LogInformation("Player " + Name + " joined.", __FUNCTION__);
				Memory::MessagerService::AddMessage("Player " + Name + " joined.");
			}
			this->RunThread = true;
			if (pThread.joinable())
				pThread.join();
			pThread = std::thread(&Player::PThread, this);
		}

		void Disconnect()
		{
			bool wasConnected = connected;
			connected = false;
			if (wasConnected && PlayerNumber != 32)
			{
				Logging::LoggerService::LogInformation("Player " + Name + " left.", __FUNCTION__);
				Memory::MessagerService::AddMessage("Player " + Name + " left.");
			}
			UpdateMapPin(Vec3f(0, 0, 0), false);
			Mutex.lock();
			this->baseAddr = 0;
			//this->Delete->set(true, __FUNCTION__);
			if (this->Status && this->Status->isAddressSet())
				{ DeleteRequested = true; this->Status->set(1, __FUNCTION__); }
			Mutex.unlock();
			this->RunThread = false;
			if (pThread.joinable())
				pThread.join();
		}

		void UpdateName(bool DispNames)
		{
			if (MemoryAccess::LocalInstance::AnimMode() == 3)
				return; // animation test mode owns the name tag
			std::string ActualName = this->Name;

			if (Name.size() > 19)
				ActualName = Name.substr(0, 19);

			if(this->Health > 0)
				ActualName += " HP: " + std::to_string(this->Health);

			if (NameAddr >= 30000)
				Memory::write_string(NameAddr, ActualName, 32, __FUNCTION__);
			if (this->DispName && this->DispName->isAddressSet())
				this->DispName->set(DispNames, __FUNCTION__);
		}

	private:
		std::shared_mutex Mutex;

		void PThread();

		void ManageBomb(Vec3f BombData, int bomb)
		{
			BombAccess* Bomb = nullptr;
			std::string BombName;

			if (bomb == 0)
			{
				Bomb = this->Bomb;
				BombName = "CustomRemoteBomb";
			}
			else if (bomb == 1)
			{
				Bomb = this->Bomb2;
				BombName = "CustomRemoteBomb2";
			}
			else if (bomb == 2)
			{
				Bomb = this->BombCube;
				BombName = "CustomRemoteBombCube";
			}
			else if (bomb == 3)
			{
				Bomb = this->BombCube2;
				BombName = "CustomRemoteBombCube2";
			}

			if (!Bomb)
				return;

			BombStatus newStatus = Deallocated;
			BombStatus playerStatus = Bomb->getStatus();

			if (BombData.x() == 0 && BombData.y() == 0 && BombData.z() == 0)
				newStatus = Exploded;
			else if (BombData.x() == -1 && BombData.y() == -1 && BombData.z() == -1)
				newStatus = Cancelled;
			else
				newStatus = Normal;

			if (playerStatus == Normal)
			{
				if (newStatus != Normal)
				{
					Bomb->changeState(newStatus);
					return;
				}

				Bomb->set(BombData, __FUNCTION__);
			}
			else if (playerStatus == Deallocated)
			{
				if (newStatus == Normal && GameInstance->RequestCreate(BombName, BombData))
				{
					Bomb->changeState(Processing);
				}
			}
		}

		void UpdateMapPin(Vec3f newPosition, bool show)
		{
			if (MapPin && MapPin->isAddressSet())
			{
				if (show)
					MapPin->set(newPosition, __FUNCTION__);
				else
					MapPin->set(Vec3f(12340.5, 2345.5, 0), __FUNCTION__);
			}
		}
	};
}