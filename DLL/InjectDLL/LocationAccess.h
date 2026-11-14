#pragma once
#include <Windows.h>
#include "Memory.h"
#include "CharacterLocation.h"
#include "LoggerService.h"

namespace DataTypes
{
	class LocationAccess
	{
	public:
		CharacterLocation LastKnown;

		LocationAccess()
		{
			LastKnown.Map = "MainField";
			LastKnown.Section = "None";

			std::vector<int> signature = { 0x03, -1, 0xB5, 0xCC, 0x00, 0x00, 0x00, 0x00, 0x10, 0x54, 0xF6, 0x44, 0x10, 0x2B, 0x7E, 0x78, 0x00, 0x00, 0x00 };

			uint64_t addr = 0;
			try
			{
				addr = Memory::PatternScan(signature, Memory::getBaseAddress(), 8, 0);
			}
			catch (...)
			{
				addr = 0;
			}

			if (addr >= 30000)
			{
				setAddress(addr, __FUNCTION__, false);
				Logging::LoggerService::LogInformation("Location address found at: 0x" + std::to_string(addr), __FUNCTION__);
			}
			else
			{
				AddressSet = false;
				Logging::LoggerService::LogWarning("Location address not found; defaulting to MainField (non-fatal).", __FUNCTION__);
			}
		}

		void setAddress(uint64_t Address, const char* caller, bool Validate = false)
		{
			if (Address < 30000)
			{
				AddressSet = false;
				LastKnown.Map = "MainField";
				LastKnown.Section = "None";
				return;
			}

			if (Validate)
				AddressSet = ValidateAddress(Address, caller);
			else
				AddressSet = true;

			if (AddressSet)
			{
				this->Address = Address;
				std::string m = Memory::extractLocName(Address + 0x14);
				std::string s = Memory::extractLocName(Address + 0x40);
				LastKnown.Map = (!m.empty() && m != " " && m.length() >= 3) ? m : "MainField";
				LastKnown.Section = (!s.empty() && s != " ") ? s : "None";
			}
			else
			{
				LastKnown.Map = "MainField";
				LastKnown.Section = "None";
			}
		}

		CharacterLocation get(const char* caller)
		{
			CharacterLocation result;
			result.Map = LastKnown.Map.empty() ? "MainField" : LastKnown.Map;
			result.Section = LastKnown.Section.empty() ? "None" : LastKnown.Section;

			if (AddressSet && Address >= 30000)
			{
				std::string m = Memory::extractLocName(Address + 0x14);
				std::string s = Memory::extractLocName(Address + 0x40);
				if (!m.empty() && m != " " && m.length() >= 3)
				{
					result.Map = m;
					LastKnown.Map = m;
				}
				if (!s.empty() && s != " ")
				{
					result.Section = s;
					LastKnown.Section = s;
				}
			}

			return result;
		}

		bool isAddressSet() const { return AddressSet; }

	private:
		uint64_t Address = 0;
		bool AddressSet = false;

		bool ValidateAddress(uint64_t Address, const char* caller)
		{
			if (Address < 30000)
			{
				Logging::LoggerService::LogWarning("Address below valid threshold.", caller);
				return false;
			}

			MEMORY_BASIC_INFORMATION mbi{ 0 };
			DWORD protectflags = (PAGE_GUARD | PAGE_NOCACHE | PAGE_NOACCESS);

			if (VirtualQuery((LPCVOID)Address, &mbi, sizeof(mbi)))
			{
				if (mbi.Protect & protectflags || !(mbi.State & MEM_COMMIT)) {
					Logging::LoggerService::LogWarning("Failed to validate address: memory not accessible.", caller);
					return false;
				}
			}

			return true;
		}
	};
}
