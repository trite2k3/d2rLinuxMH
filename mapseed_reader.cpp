// mapseed_reader.cpp
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <vector>
#include <psapi.h>
#include <string>
#include <limits> // For UINT_MAX
#include <cstdint> // For uint64_t

// Function to get the process ID of the game
DWORD GetProcessID(const wchar_t* processName) {
    DWORD processID = 0;
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, processName) == 0) {
                processID = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return processID;
}

// Function to get the base address of the main module
DWORD_PTR GetModuleBaseAddress(DWORD processID, const wchar_t* moduleName) {
    DWORD_PTR moduleBaseAddress = 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processID);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W moduleEntry;
        moduleEntry.dwSize = sizeof(MODULEENTRY32W);

        if (Module32FirstW(hSnapshot, &moduleEntry)) {
            do {
                if (_wcsicmp(moduleEntry.szModule, moduleName) == 0) {
                    moduleBaseAddress = (DWORD_PTR)moduleEntry.modBaseAddr;
                    break;
                }
            } while (Module32NextW(hSnapshot, &moduleEntry));
        }
        CloseHandle(hSnapshot);
    }
    return moduleBaseAddress;
}

// Function to safely read process memory
bool ReadProcessMemorySafe(HANDLE hProcess, DWORD_PTR address, LPVOID buffer, SIZE_T size) {
    SIZE_T bytesRead;
    return ReadProcessMemory(hProcess, (LPCVOID)address, buffer, size, &bytesRead) && bytesRead == size;
}

// Function to find a pattern in the process's memory
DWORD_PTR FindPattern(HANDLE hProcess, DWORD_PTR startAddress, DWORD_PTR endAddress, const BYTE* pattern, const char* mask, SIZE_T patternSize) {
    BYTE* buffer = new BYTE[4096];
    SIZE_T bytesToRead = 4096;

    for (DWORD_PTR address = startAddress; address < endAddress; address += bytesToRead - patternSize) {
        SIZE_T bytesRead;
        if (!ReadProcessMemory(hProcess, (LPCVOID)address, buffer, bytesToRead, &bytesRead)) {
            continue;
        }

        for (SIZE_T i = 0; i < bytesRead - patternSize; ++i) {
            bool found = true;
            for (SIZE_T j = 0; j < patternSize; ++j) {
                if (mask[j] == 'x' && buffer[i + j] != pattern[j]) {
                    found = false;
                    break;
                }
            }
            if (found) {
                delete[] buffer;
                return address + i;
            }
        }
    }
    delete[] buffer;
    return 0;
}

// Reverse the map seed hash function to find the original seed
bool reverseMapSeedHash(DWORD hash, DWORD& seed) {
    const DWORD mapHashDivisor = 1 << 16;
    DWORD incrementalValue = 1;

    for (DWORD startValue = 0; startValue < UINT_MAX; startValue += incrementalValue) {
        DWORD seedResult = (startValue * 0x6AC690C5 + 666) & 0xFFFFFFFF;

        if (seedResult == hash) {
            seed = startValue;
            return true;
        }

        if (incrementalValue == 1 && (seedResult % mapHashDivisor) == (hash % mapHashDivisor)) {
            incrementalValue = mapHashDivisor;
        }
    }

    return false;
}

// Updated Function to get the map seed
bool GetMapSeed(HANDLE hProcess, DWORD_PTR moduleBaseAddress, DWORD_PTR& mapSeed) {
    // Step 1: Find the UnitTable offset by scanning for the pattern
    BYTE pattern[] = { 0x48, 0x03, 0xC7, 0x49, 0x8B, 0x8C, 0xC6 };
    const char* mask = "xxxxxxx";
    SIZE_T patternSize = sizeof(pattern);

    // Assuming the module size is reasonable (e.g., 0x1000000 bytes)
    DWORD_PTR startAddress = moduleBaseAddress;
    DWORD_PTR endAddress = moduleBaseAddress + 0x1000000;

    DWORD_PTR patternAddress = FindPattern(hProcess, startAddress, endAddress, pattern, mask, patternSize);
    if (patternAddress == 0) {
        std::cerr << "Pattern not found." << std::endl;
        return false;
    }
    // std::cout << "Pattern found at: 0x" << std::hex << patternAddress << std::dec << std::endl;

    // Step 2: Read the UnitTable offset
    DWORD unitTableOffset = 0;
    if (!ReadProcessMemorySafe(hProcess, patternAddress + 7, &unitTableOffset, sizeof(unitTableOffset))) {
        std::cerr << "Failed to read UnitTable offset." << std::endl;
        return false;
    }
    // std::cout << "UnitTable offset: 0x" << std::hex << unitTableOffset << std::dec << std::endl;

    // Step 3: Calculate the UnitTable address
    DWORD_PTR unitTableAddress = moduleBaseAddress + unitTableOffset;
    // std::cout << "UnitTable address: 0x" << std::hex << unitTableAddress << std::dec << std::endl;

    // Step 4: Read the player units from the UnitTable
    const int UNIT_TABLE_SIZE = 128; // Assuming 128 entries in the UnitTable
    std::vector<DWORD_PTR> playerUnits;
    for (int i = 0; i < UNIT_TABLE_SIZE; ++i) {
        DWORD_PTR unitAddress = 0;
        if (!ReadProcessMemorySafe(hProcess, unitTableAddress + i * sizeof(DWORD_PTR), &unitAddress, sizeof(unitAddress))) {
            continue;
        }
        if (unitAddress != 0) {
            // Read unitType from unitAddress + 0x0
            DWORD unitType = 0;
            if (!ReadProcessMemorySafe(hProcess, unitAddress + 0x0, &unitType, sizeof(unitType))) {
                continue;
            }
            if (unitType == 0) { // 0 indicates a player unit
                playerUnits.push_back(unitAddress);
            }
        }
    }
    if (playerUnits.empty()) {
        std::cerr << "No player units found." << std::endl;
        return false;
    }

    // Step 5: Iterate over the player units to find one with a valid actPtr
    DWORD_PTR playerUnitAddress = 0;
    DWORD_PTR actPtr = 0;
    for (DWORD_PTR unitAddr : playerUnits) {
        DWORD_PTR potentialActPtr = 0;
        if (ReadProcessMemorySafe(hProcess, unitAddr + 0x20, &potentialActPtr, sizeof(potentialActPtr))) {
            if (potentialActPtr != 0) {
                playerUnitAddress = unitAddr;
                actPtr = potentialActPtr;
                break;
            }
        }
    }
    if (playerUnitAddress == 0 || actPtr == 0) {
        std::cerr << "Failed to find a valid player unit with non-zero actPtr." << std::endl;
        return false;
    }
    // std::cout << "PlayerUnit address: 0x" << std::hex << playerUnitAddress << std::dec << std::endl;
    // std::cout << "ActPtr: 0x" << std::hex << actPtr << std::dec << std::endl;

    // Step 7: Read actMiscPtr from actPtr + 0x78
    DWORD_PTR actMiscPtr = 0;
    if (!ReadProcessMemorySafe(hProcess, actPtr + 0x78, &actMiscPtr, sizeof(actMiscPtr))) {
        std::cerr << "Failed to read actMiscPtr." << std::endl;
        return false;
    }
    // std::cout << "ActMiscPtr: 0x" << std::hex << actMiscPtr << std::dec << std::endl;

    // Step 8: Read dwInitSeedHash1 from actMiscPtr + 0x840
    DWORD dwInitSeedHash1 = 0;
    if (!ReadProcessMemorySafe(hProcess, actMiscPtr + 0x840, &dwInitSeedHash1, sizeof(dwInitSeedHash1))) {
        std::cerr << "Failed to read dwInitSeedHash1." << std::endl;
        return false;
    }
    // std::cout << "dwInitSeedHash1: " << dwInitSeedHash1 << std::endl;

    // Step 9: Read dwEndSeedHash1 from actMiscPtr + 0x868
    DWORD dwEndSeedHash1 = 0;
    if (!ReadProcessMemorySafe(hProcess, actMiscPtr + 0x868, &dwEndSeedHash1, sizeof(dwEndSeedHash1))) {
        // std::cerr << "Failed to read dwEndSeedHash1." << std::endl;
        return false;
    }
    // std::cout << "dwEndSeedHash1: " << dwEndSeedHash1 << std::endl;

    // Step 10: Use reverseMapSeedHash to get the seed
    DWORD seed = 0;
    bool found = reverseMapSeedHash(dwEndSeedHash1, seed);
    if (!found) {
        std::cerr << "Failed to reverse map seed hash." << std::endl;
        return false;
    }

    DWORD gameSeedXor = dwInitSeedHash1 ^ seed;

    if (gameSeedXor == 0) {
        std::cerr << "Game seed XOR is zero." << std::endl;
        return false;
    }

    mapSeed = seed;
    // std::cout << "Map Seed: " << mapSeed << std::endl;
    std::cout << mapSeed << std::endl;

    return true;
}


int wmain() {
    const wchar_t* gameProcessName = L"D2R.exe"; // Replace with the actual game executable name
    const wchar_t* moduleName = L"D2R.exe";      // Replace with the actual module name if different
    DWORD processID = GetProcessID(gameProcessName);

    if (processID == 0) {
        std::wcerr << L"Game process not found." << std::endl;
        return 1;
    }

    DWORD_PTR moduleBaseAddress = GetModuleBaseAddress(processID, moduleName);
    if (moduleBaseAddress == 0) {
        std::wcerr << L"Failed to get module base address." << std::endl;
        return 1;
    }
    // std::wcout << L"Module base address: 0x" << std::hex << moduleBaseAddress << std::dec << std::endl;

    HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, processID);
    if (hProcess == NULL) {
        std::wcerr << L"Failed to open process. Error: " << GetLastError() << std::endl;
        return 1;
    }

    DWORD_PTR mapSeed;
    if (GetMapSeed(hProcess, moduleBaseAddress, mapSeed)) {
        // std::wcout << L"Map Seed: " << mapSeed << std::endl;
        CloseHandle(hProcess);
        return 0;
    } else {
        std::wcerr << L"Failed to retrieve map seed." << std::endl;
        CloseHandle(hProcess);
        return 1;
    }
}
