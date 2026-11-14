#pragma once

#include "Memory.h"
#include <array>

namespace DataTypes
{
	template <typename T>
	class BigEndian
	{
	public:
		T LastKnown;

		BigEndian();
		BigEndian(uint64_t Address, const char* caller);
		BigEndian(uint64_t baseAddr, std::vector<int> offsets, int finalOffset, const char* caller);
		BigEndian(std::vector<int> signature, uint64_t finalOffset, const char* caller, uint64_t scanOffset = -1, int maxRetries = 0);

		void setAddress(uint64_t Address, const char* caller, bool Validate = true);

		void set(T val, const char* caller);
		T get(const char* caller);
		bool isAddressSet() const { return AddressSet; }

	private:
		DWORD* Pointer;
		uint64_t address;
		bool AddressSet = false;
		const char* callerFunction = "";

		void SwapEndianness(T& val);

		bool ValidateAddress(uint64_t Address, const char* caller);
	};

	template <typename T>
	BigEndian<T>::BigEndian()
	{

	}

	template <typename T>
	BigEndian<T>::BigEndian(uint64_t Address, const char* caller)
	{
		setAddress(Address, caller);
	}

	template <typename T>
	BigEndian<T>::BigEndian(uint64_t baseAddr, std::vector<int> offsets, int finalOffset, const char* caller)
	{
		uint64_t addr = Memory::ReadPointers(baseAddr, offsets, true) + finalOffset;

		setAddress(addr, caller);
	}

	template <typename T>
	BigEndian<T>::BigEndian(std::vector<int> signature, uint64_t finalOffset, const char* caller, uint64_t scanOffset, int maxRetries)
	{
		bool saveOffset = false;
		if (scanOffset == (uint64_t)(-1))
		{
			scanOffset = Memory::ScanOffset;
			saveOffset = true;
		}

		uint64_t addr = Memory::PatternScan(signature, Memory::getBaseAddress(), 8, scanOffset) + finalOffset;
		if (addr - finalOffset < 30000 && scanOffset != 0)
		{
			addr = Memory::PatternScan(signature, Memory::getBaseAddress(), 8, 0) + finalOffset;
		}

		int retries = 0;
		while (addr - finalOffset < 30000 && retries < maxRetries)
		{
			Sleep(200);
			retries++;
			addr = Memory::PatternScan(signature, Memory::getBaseAddress(), 8, 0) + finalOffset;
		}

		if (addr - finalOffset < 30000)
		{
			Logging::LoggerService::LogDebug("Flag address not found in region 8 (optional/disabled).", caller);
			AddressSet = false;
			return;
		}

		if (saveOffset)
			Memory::ScanOffset = (Memory::RegionStart != 0 && addr > Memory::RegionStart) ? (addr - Memory::RegionStart) : 0;

		setAddress(addr, caller);
	}

	template <typename T>
	void BigEndian<T>::setAddress(uint64_t Address, const char* caller, bool Validate)
	{
		if (Address < 30000)
		{
			AddressSet = false;
			this->Pointer = nullptr;
			this->address = 0;
			return;
		}

		if (Validate)
			AddressSet = ValidateAddress(Address, caller);
		else
			AddressSet = true;

		if (AddressSet)
		{
			this->Pointer = (DWORD*)(Address);
			this->address = Address;
		}
	}

	template <typename T>
	void BigEndian<T>::set(T val, const char* caller)
	{
		LastKnown = val;

		if (!AddressSet || !Pointer || (uint64_t)Pointer < 30000)
		{
			return;
		}

		SwapEndianness(val);
		__try
		{
			memcpy(Pointer, &val, sizeof(T));
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			AddressSet = false;
		}
	}

	template <typename T>
	T BigEndian<T>::get(const char* caller)
	{
		if (!AddressSet || !Pointer || (uint64_t)Pointer < 30000)
		{
			return (T)0;
		}

		T val = (T)0;

		__try
		{
			memcpy(&val, Pointer, sizeof(T));
			SwapEndianness(val);
			LastKnown = val;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			AddressSet = false;
			return (T)0;
		}

		return val;
	}

	template <typename T>
	void BigEndian<T>::SwapEndianness(T& val)
	{
		union U {
			T val;
			std::array<std::uint8_t, sizeof(T)> raw;
		} src, dst;

		memcpy(&src.val, &val, sizeof(val));
		std::reverse_copy(src.raw.begin(), src.raw.end(), dst.raw.begin());
		memcpy(&val, &dst.val, sizeof(val));
	}

	template <typename T>
	bool BigEndian<T>::ValidateAddress(uint64_t Address, const char* caller)
	{
		if (Address < 30000)
		{
			return false;
		}

		MEMORY_BASIC_INFORMATION mbi{ 0 };
		DWORD protectflags = (PAGE_GUARD | PAGE_NOCACHE | PAGE_NOACCESS);

		if (VirtualQuery((LPCVOID)Address, &mbi, sizeof(mbi)))
		{
			if (mbi.Protect & protectflags || !(mbi.State & MEM_COMMIT)) {
				Logging::LoggerService::LogWarning("Address not committed/accessible: 0x" + std::to_string(Address), caller);
				return false;
			}
		}

		return true;
	}

}