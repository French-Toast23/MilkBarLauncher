#include "Memory.h"
#include "LoggerService.h"
#include <iostream>
#include <algorithm>

static bool SafeCopy(void* dest, const void* src, size_t count)
{
    __try
    {
        memcpy(dest, src, count);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

uint64_t Memory::getBaseAddress()
{
    if (base_addr == 0)
    {
        HMODULE hModule = GetModuleHandleA(NULL);
        if (!hModule)
            hModule = GetModuleHandleA("Cemu.exe");
        if (hModule)
        {
            Memory::memory_getBaseType memory_getBase = (Memory::memory_getBaseType)GetProcAddress(hModule, "memory_getBase");
            if (memory_getBase)
            {
                base_addr = (uint64_t)memory_getBase();
            }
        }
    }

    return base_addr;
}

DWORD Memory::read_memory(uint64_t Addr, const char* caller)
{
    DWORD val = 0;
    if (!SafeCopy(&val, (const void*)Addr, sizeof(val)))
    {
        Logging::LoggerService::LogWarning("Access violation in read_memory", caller);
        return 0;
    }
    return val;
}

void Memory::ValidateAddress(uint64_t address)
{
    if (base_addr != 0 && address < base_addr)
        throw std::invalid_argument("Address below base_addr");
}

int Memory::swap_Endian(int number)
{
    int byte0, byte1, byte2, byte3;
    byte0 = (number & 0x000000FF) >> 0;
    byte1 = (number & 0x0000FF00) >> 8;
    byte2 = (number & 0x00FF0000) >> 16;
    byte3 = (number & 0xFF000000) >> 24;
    return ((byte0 << 24) | (byte1 << 16) | (byte2 << 8) | (byte3 << 0));
}

std::vector<BYTE> Memory::read_bytes(uint64_t Addr, int bytes, const char* caller)
{
    if (bytes <= 0 || bytes > 0x1000000)
        return {};

    std::vector<BYTE> bytesRead(bytes, 0);
    if (!SafeCopy(bytesRead.data(), (const void*)Addr, bytes))
    {
        Logging::LoggerService::LogWarning("Access violation in read_bytes", caller);
        return {};
    }
    return bytesRead;
}

float Memory::read_bigEndianFloat(uint64_t Addr, const char* caller)
{
    DWORD result = 0;
    if (!SafeCopy(&result, (const void*)Addr, sizeof(result)))
    {
        Logging::LoggerService::LogWarning("Access violation in read_bigEndianFloat", caller);
        return 0.0f;
    }
    int swapped_value = Memory::swap_Endian(int(result));
    float bigf = 0.0f;
    memcpy(&bigf, &swapped_value, 4);
    return bigf;
}

int Memory::read_bigEndian4Bytes(uint64_t Addr, const char* caller)
{
    DWORD val = 0;
    if (!SafeCopy(&val, (const void*)Addr, sizeof(val)))
    {
        Logging::LoggerService::LogWarning("Access violation in read_bigEndian4Bytes", caller);
        return 0;
    }
    return Memory::swap_Endian(int(val));
}

int Memory::read_bigEndian4BytesOffset(uint64_t Offset, const char* caller)
{
    uint64_t base = getBaseAddress();
    if (base == 0)
        return 0;
    return Memory::read_bigEndian4Bytes(Offset + base, caller);
}

std::string Memory::read_string(uint64_t Addr, int bytes, const char* caller)
{
    if (bytes <= 0 || bytes > 4096)
        bytes = 50;

    std::vector<char> buf(bytes + 1, 0);
    if (!SafeCopy(buf.data(), (const void*)Addr, bytes))
    {
        Logging::LoggerService::LogWarning("Access violation in read_string", caller);
        return "";
    }
    buf[bytes] = '\0';
    return std::string(buf.data());
}

void Memory::write_bigEndianFloat(uint64_t Addr, float value, const char* caller)
{
    int val_int = 0;
    memcpy(&val_int, &value, 4);
    DWORD swapped = (DWORD)Memory::swap_Endian(val_int);
    if (!SafeCopy((void*)Addr, &swapped, sizeof(swapped)))
    {
        Logging::LoggerService::LogWarning("Access violation in write_bigEndianFloat", caller);
    }
}

void Memory::write_bigEndian4Bytes(uint64_t Addr, int value, const char* caller)
{
    DWORD swapped = (DWORD)Memory::swap_Endian(value);
    if (!SafeCopy((void*)Addr, &swapped, sizeof(swapped)))
    {
        Logging::LoggerService::LogWarning("Access violation in write_bigEndian4Bytes", caller);
    }
}

void Memory::write_byte(uint64_t Addr, BYTE byte, const char* caller)
{
    if (!SafeCopy((void*)Addr, &byte, sizeof(byte)))
    {
        Logging::LoggerService::LogWarning("Access violation in write_byte", caller);
    }
}

void Memory::write_bytes(uint64_t Addr, std::vector<BYTE> bytes, const char* caller)
{
    if (bytes.empty())
        return;

    if (!SafeCopy((void*)Addr, bytes.data(), bytes.size()))
    {
        Logging::LoggerService::LogWarning("Access violation in write_bytes", caller);
    }
}

void Memory::write_string(uint64_t Addr, std::string string, int bytes, const char* caller)
{
    if (bytes == 0)
        bytes = (int)string.size();

    if (bytes > 0)
    {
        std::vector<BYTE> buf(bytes, 0);
        size_t toCopy = (size_t)bytes < string.size() ? (size_t)bytes : string.size();
        memcpy(buf.data(), string.data(), toCopy);
        if (!SafeCopy((void*)Addr, buf.data(), bytes))
        {
            Logging::LoggerService::LogWarning("Access violation in write_string", caller);
        }
    }
}

std::vector<BYTE> Memory::getNop(int length)
{
    std::vector<BYTE> result;
    for (int i = 0; i < length; i++)
    {
        result.push_back(0x90);
    }
    return result;
}

std::string Memory::hexStr(std::vector<BYTE> data)
{
    std::stringstream ss;
    ss << std::hex;
    for (size_t i = 0; i < data.size(); i++)
    {
        ss << std::setw(2) << std::setfill('0') << (int)data[i];
    }
    return ss.str();
}

std::string Memory::extractLocName(uint64_t Addr, int bytes)
{
    std::string locString = Memory::read_string(Addr, bytes, __FUNCTION__);
    std::string result = "";
    for (size_t i = 0; i < locString.size(); i++)
    {
        if (locString[i] == ' ')
        {
            result = " ";
            break;
        }
        result += locString[i];
    }
    return result;
}

bool Memory::CompareSignatures(std::vector<BYTE> First, std::vector<BYTE> Second, std::vector<int> WildCards)
{
    for (size_t i = 0; i < First.size(); i++)
    {
        if (std::find(WildCards.begin(), WildCards.end(), (int)i) != WildCards.end()) continue;
        if (i >= Second.size() || First[i] != Second[i]) return false;
    }
    return true;
}

uint64_t Memory::ReadPointers(uint64_t InitialAddress, std::vector<int> readingOffsets, bool IncludeBaseAddress)
{
    uint64_t response = InitialAddress;

    for (size_t i = 0; i < readingOffsets.size(); i++)
    {
        response = read_bigEndian4BytesOffset(response + readingOffsets[i], __FUNCTION__);
        if (response == 0) return 0;
    }

    return response + (IncludeBaseAddress ? getBaseAddress() : 0);
}

uint64_t Memory::FindLocalLinkActor()
{
    uint64_t base = getBaseAddress();
    if (base == 0) return 0;

    std::vector<int> sig = { 'G', 'a', 'm', 'e', 'R', 'O', 'M', 'P', 'l', 'a', 'y', 'e', 'r', 0 };
    std::vector<uint64_t> matches = PatternScanMultiple(sig, base, 8, 0, false, 0, 10);
    for (uint64_t strAddr : matches)
    {
        if (strAddr < base + 0x10) continue;
        uint64_t candidateVirtualAddr = (strAddr - 0x10) - base;
        if (candidateVirtualAddr < 0x1000) continue;

        uint64_t posAddr = ReadPointers(candidateVirtualAddr, { 0x3A0, 0x50, 0x4, 0x80, 0x0, 0x5C, 0x18 }, true) + 0x50;
        if (posAddr >= 30000)
        {
            return candidateVirtualAddr;
        }
    }
    return 0;
}
