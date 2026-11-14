#include "Memory.h"
#include <iostream>

static inline bool SafeReadByte(const void* addr, byte* out)
{
    __try {
        *out = *(const byte*)addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint64_t Memory::PatternScan(std::vector<int> signature, uint64_t baseAddr, int region, uint64_t regionOffset, bool Multiple, bool multipleRegions, uint64_t regionMaxOffset)
{

    SYSTEM_INFO si;
    GetSystemInfo(&si);

    uint64_t startAddress = baseAddr;
    uint64_t maxGuest = baseAddr != 0 ? (baseAddr + 0x100000000ULL) : (uint64_t)(si.lpMaximumApplicationAddress);
    uint64_t endAddress = (uint64_t)(si.lpMaximumApplicationAddress);
    if (endAddress > maxGuest)
        endAddress = maxGuest;

    MEMORY_BASIC_INFORMATION Scanmbi{ 0 };
    DWORD protectflags = (PAGE_GUARD | PAGE_NOCACHE | PAGE_NOACCESS);
    DWORD readable = (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);

    int contador = 1;
    for (uint64_t i = startAddress; i < endAddress - signature.size();) {
        if (VirtualQuery((LPCVOID)i, &Scanmbi, sizeof(Scanmbi))) {
            if (Scanmbi.RegionSize == 0 || Scanmbi.RegionSize <= signature.size()) {
                i += (Scanmbi.RegionSize ? Scanmbi.RegionSize : 0x1000);
                contador++;
                continue;
            }

            if (Scanmbi.Protect & protectflags || !(Scanmbi.Protect & readable) || !(Scanmbi.State & MEM_COMMIT)) {
                i += Scanmbi.RegionSize;

                contador++;
                continue; // if bad adress then dont read from it
            }

            if (region != 0 && contador < region)
            {
                i += Scanmbi.RegionSize;
                contador++;
                continue;
            }

            if (contador > region && region != 0 && !multipleRegions)
            {
                break;
            }

            //get last '?' position in pattern and use it to calculate the max shift value.
            //the last position in the pattern should never be a '?' -> we do not bother checking it
            uint64_t maxShift = signature.size() - 1;
            uint64_t maxIndex = signature.size() - 2;
            uint64_t wildCardIndex = 0;
            for (uint64_t i = 0; i < maxIndex + 1; i++) {
                if (signature.at(i) == -1) {
                    maxShift = maxIndex - i;
                    wildCardIndex = i;
                }
            }

            //initialize the shift table
            uint64_t shiftTable[256];
            for (uint64_t i = 0; i <= 255; i++) {
                shiftTable[i] = maxShift;
            }

            //fill shiftTable
            //forgot this in the video: Because max shift should always be '?' we only update the shift table for bytes to the right of the last '?'
            for (uint64_t i = wildCardIndex + 1; i < maxIndex; i++) {
                shiftTable[signature.at(i)] = maxIndex - i;
            }

            uint64_t startingAddress = 0;
            uint64_t endAddress = Scanmbi.RegionSize - signature.size();

            if (region != 0 && regionOffset != 0)
            {
                startingAddress = regionOffset;
            }

            if (region != 0 && regionMaxOffset != 0)
            {
                if (regionMaxOffset < endAddress)
                    endAddress = regionMaxOffset;
            }

            for (uint64_t currentIndex = startingAddress; currentIndex < endAddress;) {

                for (uint64_t sigIndex = maxIndex; sigIndex >= 0; sigIndex--) {
                    byte reading = 0;
                    if (!SafeReadByte((const void*)((uint64_t)Scanmbi.BaseAddress + currentIndex + sigIndex), &reading)) {
                        currentIndex += 1;
                        break;
                    }

                    if (reading != signature.at(sigIndex) && signature.at(sigIndex) != -1) {
                        currentIndex += shiftTable[reading];
                        break;
                    }
                    else if (sigIndex == 0) {
                        byte lastByte = 0;
                        if (!SafeReadByte((const void*)((uint64_t)Scanmbi.BaseAddress + currentIndex + signature.size() - 1), &lastByte) ||
                            signature.at(signature.size() - 1) != lastByte)
                        {
                            currentIndex += 1;
                            break;
                        }

                        return (uint64_t)Scanmbi.BaseAddress + currentIndex;
                    }
                }
            }

            if (region != 0 && contador == region && !multipleRegions)
            {
                break;
            }

            i = (uint64_t)Scanmbi.BaseAddress + Scanmbi.RegionSize;
        }
        else {
            i += 0x1000;
        }
    }
    return NULL;

}

std::vector<uint64_t> Memory::PatternScanMultiple(std::vector<int> signature, uint64_t baseAddr, int region, uint64_t regionOffset, bool multipleRegions, uint64_t regionMaxOffset, int expectedValues)
{
    std::vector<uint64_t> result;

    SYSTEM_INFO si;
    GetSystemInfo(&si);

    uint64_t startAddress = baseAddr;
    uint64_t maxGuest = baseAddr != 0 ? (baseAddr + 0x100000000ULL) : (uint64_t)(si.lpMaximumApplicationAddress);
    uint64_t endAddress = (uint64_t)(si.lpMaximumApplicationAddress);
    if (endAddress > maxGuest)
        endAddress = maxGuest;

    MEMORY_BASIC_INFORMATION mbi{ 0 };
    DWORD protectflags = (PAGE_GUARD | PAGE_NOCACHE | PAGE_NOACCESS);
    DWORD readable = (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);

    int contador = 1;
    for (uint64_t i = startAddress; i < endAddress - signature.size();) {
        if (VirtualQuery((LPCVOID)i, &mbi, sizeof(mbi))) {
            if (mbi.RegionSize == 0 || mbi.RegionSize <= signature.size()) {
                i += (mbi.RegionSize ? mbi.RegionSize : 0x1000);
                contador++;
                continue;
            }

            if (mbi.Protect & protectflags || !(mbi.Protect & readable) || !(mbi.State & MEM_COMMIT)) {
                i += mbi.RegionSize;

                contador++;
                continue; // if bad adress then dont read from it
            }

            if (region != 0 && contador < region)
            {
                i += mbi.RegionSize;
                contador++;
                continue;
            }

            if (contador > region && region != 0 && !multipleRegions)
            {
                break;
            }


            //get last '?' position in pattern and use it to calculate the max shift value.
            //the last position in the pattern should never be a '?' -> we do not bother checking it
            uint64_t maxShift = signature.size() - 1;
            uint64_t maxIndex = signature.size() - 2;
            uint64_t wildCardIndex = 0;
            for (uint64_t i = 0; i < maxIndex + 1; i++) {
                if (signature.at(i) == -1) {
                    maxShift = maxIndex - i;
                    wildCardIndex = i;
                }
            }

            //initialize the shift table
            uint64_t shiftTable[256];
            for (uint64_t i = 0; i <= 255; i++) {
                shiftTable[i] = maxShift;
            }


            //fill shiftTable
            //forgot this in the video: Because max shift should always be '?' we only update the shift table for bytes to the right of the last '?'
            for (uint64_t i = wildCardIndex + 1; i < maxIndex; i++) {
                shiftTable[signature.at(i)] = maxIndex - i;
            }


            uint64_t startingAddress = 0;
            uint64_t endAddress = mbi.RegionSize - signature.size();

            if (region != 0 && regionOffset != 0)
            {
                startingAddress = regionOffset;
            }

            if (region != 0 && regionMaxOffset != 0)
            {
                if (regionMaxOffset < endAddress)
                    endAddress = regionMaxOffset;
            }

            for (uint64_t currentIndex = startingAddress; currentIndex < endAddress;) {

                for (uint64_t sigIndex = maxIndex; sigIndex >= 0; sigIndex--) {
                    byte reading = 0;
                    if (!SafeReadByte((const void*)((uint64_t)mbi.BaseAddress + currentIndex + sigIndex), &reading)) {
                        currentIndex += 1;
                        break;
                    }

                    if (reading != signature.at(sigIndex) && signature.at(sigIndex) != -1) {
                        currentIndex += shiftTable[reading];
                        break;
                    }
                    else if (sigIndex == 0) {

                        byte lastByte = 0;
                        if (!SafeReadByte((const void*)((uint64_t)mbi.BaseAddress + currentIndex + signature.size() - 1), &lastByte) ||
                            signature.at(signature.size() - 1) != lastByte)
                        {
                            currentIndex += 1;
                            break;
                        }

                        //return (uint64_t)mbi.BaseAddress + currentIndex;
                        result.push_back((uint64_t)mbi.BaseAddress + currentIndex);

                        if (result.size() == expectedValues) return result;

                        currentIndex++;
                        break;

                    }
                }
            }


            if (region != 0 && contador == region && !multipleRegions)
            {
                break;
            }

            i = (uint64_t)mbi.BaseAddress + mbi.RegionSize;
        }
        else {
            i += 0x1000;
        }
    }
    return result;

}

uint64_t Memory::TryPatternScan(std::vector<int> signature, uint64_t baseAddr, int region, uint64_t regionOffset, bool Multiple, bool multipleRegions, uint64_t regionMaxOffset, int retries, std::string flagName)
{
    uint64_t result = 0;
    int retryCounter = 0;
    bool infinite = false;

    if (retries == 0)
        infinite = true;

    while (result < 30000 && (infinite || retryCounter < retries))
    {
        result = PatternScan(signature, baseAddr, region, regionOffset, Multiple, multipleRegions, regionMaxOffset);
        retryCounter++;

        if (result < 30000)
        {
            Logging::LoggerService::LogDebug("Could not complete pattern scan." + flagName != "" ? "Flag: " + flagName : "", __FUNCTION__);

            Sleep(2000);
        }
    }

    return result;
}

uint64_t Memory::findRegionBaseAddress(uint64_t baseAddr, int region)
{

    SYSTEM_INFO si;
    GetSystemInfo(&si);

    uint64_t startAddress = baseAddr;
    uint64_t maxGuest = baseAddr != 0 ? (baseAddr + 0x100000000ULL) : (uint64_t)(si.lpMaximumApplicationAddress);
    uint64_t endAddress = (uint64_t)(si.lpMaximumApplicationAddress);
    if (endAddress > maxGuest)
        endAddress = maxGuest;

    MEMORY_BASIC_INFORMATION mbi{ 0 };
    DWORD protectflags = (PAGE_GUARD | PAGE_NOCACHE | PAGE_NOACCESS);

    std::vector<uint64_t> result;

    uint64_t i = startAddress;

    int contador = 1;
    while (i < endAddress) {
        if (VirtualQuery((LPCVOID)i, &mbi, sizeof(mbi))) {
            if (mbi.RegionSize == 0) break;
            if (mbi.Protect & protectflags || !(mbi.State & MEM_COMMIT)) {
                i += mbi.RegionSize;
                contador++;
                continue; // if bad address then dont read from it
            }

            if (region != 0 && contador != region)
            {
                i += mbi.RegionSize;
                contador++;
                continue;
            }

            if (contador == region)
            {
                return i;
            }
        }
        else
        {
            break;
        }
    }

    return 0;

}