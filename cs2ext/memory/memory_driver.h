#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <iostream>
#include <fstream>
#include <vector>
#include "imemory.h"
#include "shared.h"
#include "driver_manager.h"
#include "memory_utils.h"

const wchar_t* SINGULARITY_GUID = L"{deadfade-0601-47C6-84E7-2EBC937D1B11}"; 

class MemoryDriver : public IMemory {
public:
    // Конструктор приймає тип бекенду:
    // 1 - WinAPI, 2 - Syscalls, 3 - kdmapper, 4 - Постійні виклики UEFI, 5 - UEFI Авто-.sys завантаження
    explicit MemoryDriver(int backend_mode)
        : m_backend_mode(backend_mode), h_driver(INVALID_HANDLE_VALUE), pid(0) {}

    ~MemoryDriver() override {
        close();
    }

    // Офіційне підвищення прав у токені за інструкцією твого ШІ (без 1314)
    bool EnableSystemEnvironmentPrivilege() const {
        HANDLE hToken;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;
        
        TOKEN_PRIVILEGES tp;
        LUID luid;
        if (!LookupPrivilegeValueW(NULL, L"SeSystemEnvironmentPrivilege", &luid)) { 
            CloseHandle(hToken); 
            return false; 
        }
        
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        
        if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) { 
            CloseHandle(hToken); 
            return false; 
        }
        
        bool success = (GetLastError() == ERROR_SUCCESS);
        CloseHandle(hToken);
        return success;
    }

    bool attach(const wchar_t* process_name) override {
        close();

        // РЕЖИМ 3: Класичний мапінг через kdmapper
        if (m_backend_mode == 3) {
            auto result = DriverManager::setup_kdmapper();
            if (result != DriverManager::READY) return false;

            h_driver = CreateFileW(KDMP_USER_PATH, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h_driver == INVALID_HANDLE_VALUE) return false;
        }
        // РЕЖИМ 5: АВТОНОМНЕ ЗАВАНТАЖЕННЯ ДРАЙВЕРА ЧЕРЕЗ UEFI BIOS (БЕЗ СТОРОННІХ ЕХЕ-МАППЕРІВ)
        else if (m_backend_mode == 5) {
            printf("[DEBUG] UEFI Mode 5: Preparing autonomous driver injection via Ring -2...\n");
            EnableSystemEnvironmentPrivilege();

            // 1. Зчитуємо файл нашого драйвера з папки читу в ОЗП
            std::ifstream file("MemReaderKdmp.sys", std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                printf("[DEBUG ERROR] MemReaderKdmp.sys not found next to EXE!\n");
                return false;
            }

            std::streamsize size = file.tellg();
            std::vector<char> buffer(size);
            file.seekg(0, std::ios::beg);
            if (!file.read(buffer.data(), size)) return false;
            file.close();

            struct SINGULARITY_MEMORY_COMMAND {
                int magic; int operation; unsigned long long data[10]; int size;
            };

            // 2. Виділяємо пул пам'яті в BIOS під драйвер через Op 1
            uintptr_t driver_buffer_uefi_addr = 0;
            SINGULARITY_MEMORY_COMMAND alloc_cmd{};
            alloc_cmd.magic = 0xDEADFADE;
            alloc_cmd.operation = 1; // Op 1: AllocatePool
            alloc_cmd.data[2] = static_cast<unsigned long long>(size); 
            alloc_cmd.data[3] = reinterpret_cast<unsigned long long>(&driver_buffer_uefi_addr);
            SetFirmwareEnvironmentVariableW(L"Singularity42", SINGULARITY_GUID, &alloc_cmd, sizeof(alloc_cmd));

            if (driver_buffer_uefi_addr == 0) {
                printf("[DEBUG ERROR] BIOS refused to allocate pool for the driver!\n");
                return false;
            }

            // 3. Копіюємо тіло нашого драйвера в біос-буфер через Op 0
            SINGULARITY_MEMORY_COMMAND copy_cmd{};
            copy_cmd.magic = 0xDEADFADE;
            copy_cmd.operation = 0; // Op 0: memcpy
            copy_cmd.data[0] = driver_buffer_uefi_addr; // Destination
            copy_cmd.data[1] = reinterpret_cast<unsigned long long>(buffer.data()); // Source
            copy_cmd.size = static_cast<int>(size);
            SetFirmwareEnvironmentVariableW(L"Singularity42", SINGULARITY_GUID, &copy_cmd, sizeof(copy_cmd));

            // 4. Тиснемо спусковий гачок — викликаємо Op 5 для ініціалізації водія в ядрі!
            unsigned long driver_start_status = 0;
            SINGULARITY_MEMORY_COMMAND run_cmd{};
            run_cmd.magic = 0xDEADFADE;
            run_cmd.operation = 5; // Op 5: CallDriverEntry
            run_cmd.data[0] = driver_buffer_uefi_addr; // Точка старту
            run_cmd.data[1] = reinterpret_cast<unsigned long long>(&driver_start_status);
            SetFirmwareEnvironmentVariableW(L"Singularity42", SINGULARITY_GUID, &run_cmd, sizeof(run_cmd));

            printf("[DEBUG] UEFI Driver Entry invoked. Kernel Status: 0x%lX\n", driver_start_status);

            // 5. Відкриваємо легітимний швидкий канал зв'язку з активованим драйвером
            h_driver = CreateFileW(L"\\\\.\\MemReaderKdmp", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h_driver == INVALID_HANDLE_VALUE) {
                printf("[DEBUG ERROR] Failed to connect to injected MemReaderKdmp driver handle!\n");
                return false;
            }
            printf("[DEBUG SUCCESS] MemReaderKdmp fully operational via Ring -2 UEFI injection!\n");
        }
        // РЕЖИМ 4: Чистий UEFI BIOS Runtime
        else if (m_backend_mode == 4) {
            printf("[DEBUG] UEFI Mode 4: Activating SeSystemEnvironmentPrivilege...\n");
            EnableSystemEnvironmentPrivilege();
        }

        pid = find_process(process_name);
        if (!pid) return false;

        m_modules.client = query_module_base(L"client.dll", &m_modules.client_size);
        if (!m_modules.client) return false;
        
        m_modules.engine2 = query_module_base(L"engine2.dll", &m_modules.engine2_size);
        m_modules.schemasystem = query_module_base(L"schemasystem.dll", &m_modules.schemasystem_size);
        m_modules.tier0 = query_module_base(L"tier0.dll", &m_modules.tier0_size);
        m_modules.vphysics2 = query_module_base(L"vphysics2.dll", &m_modules.vphysics2_size);

        return true;
    }

    void close() override {
        if (h_driver != INVALID_HANDLE_VALUE) { CloseHandle(h_driver); h_driver = INVALID_HANDLE_VALUE; }
        pid = 0; m_modules = {};
    }

    bool read_raw(uintptr_t address, void* buffer, size_t size) const override {
        if (!pid || !buffer || size == 0) return false;

        // РЕЖИМ 1: Юзермод WinAPI
        if (m_backend_mode == 1) {
            HANDLE hProc = OpenProcess(PROCESS_VM_READ, FALSE, pid); if (!hProc) return false;
            SIZE_T bytes_read = 0;
            BOOL status = ReadProcessMemory(hProc, reinterpret_cast<LPCVOID>(address), buffer, size, &bytes_read);
            CloseHandle(hProc); return status && (bytes_read == size);
        }
        // РЕЖИМ 2: Юзермод Indirect Syscalls
        else if (m_backend_mode == 2) {
            SIZE_T bytes_read = 0;
            BOOL status = ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), buffer, size, &bytes_read);
            return status && (bytes_read == size);
        }
        // РЕЖИМ 3 або 5: Швидке та безпечне зчитування через Kernel-драйвер MemReaderKdmp.sys
        else if (m_backend_mode == 3 || m_backend_mode == 5) {
            if (h_driver == INVALID_HANDLE_VALUE) return false;
            READ_MEMORY_REQUEST request{};
            request.target_pid = pid;
            request.source_address = static_cast<ULONG64>(address);
            request.read_size = static_cast<ULONG>(size);
            DWORD returned = 0;
            return DeviceIoControl(h_driver, IOCTL_READ_MEMORY, &request, sizeof(request), buffer, static_cast<DWORD>(size), &returned, nullptr);
        }
        // РЕЖИМ 4: Істинне апаратне чисте UEFI читання пам'яті (Постійні виклики Set)
        else if (m_backend_mode == 4) {
            struct SINGULARITY_MEMORY_COMMAND {
                int magic; int operation; unsigned long long data[10]; int size;
            };
            SINGULARITY_MEMORY_COMMAND cmd{};
            cmd.magic = 0xDEADFADE; cmd.operation = 0; // Op 0: memcpy
            
            // Записуємо змінні строго по масивах автора без зсувів
            cmd.data[0] = reinterpret_cast<unsigned long long>(buffer); // Destination
            cmd.data[1] = static_cast<unsigned long long>(address);      // Source
            cmd.size = static_cast<int>(size);

            BOOL status = SetFirmwareEnvironmentVariableW(L"Singularity42", SINGULARITY_GUID, &cmd, sizeof(cmd));
            _mm_pause();
            return status;
        }
        return false;
    }

    uintptr_t get_client_base() const override { return m_modules.client; }
    DWORD get_pid() const override { return pid; }

private:
    HANDLE    h_driver;
    DWORD     pid;
    int       m_backend_mode;
    struct {
        uintptr_t client; size_t client_size;
        uintptr_t engine2; uintptr_t schemasystem;
        uintptr_t tier0; uintptr_t vphysics2;
    } m_modules;

    uintptr_t query_module_base(const wchar_t* module_name, size_t* out_size) const {
        if (!pid) return 0;
        
        if (m_backend_mode == 3 || m_backend_mode == 5) {
            if (h_driver == INVALID_HANDLE_VALUE) return 0;
            MODULE_BASE_REQUEST request{}; request.target_pid = pid;
            wcsncpy_s(request.module_name, 256, module_name, _TRUNCATE);
            MODULE_BASE_REQUEST response{}; DWORD returned = 0;

            BOOL ok = DeviceIoControl(h_driver, IOCTL_GET_MODULE_BASE, &request, sizeof(request), &response, sizeof(response), &returned, nullptr);
            if (ok && returned == sizeof(MODULE_BASE_REQUEST)) {
                if (out_size) *out_size = static_cast<size_t>(response.module_size);
                return static_cast<uintptr_t>(response.base_address);
            }
            return 0;
        }
        
        uintptr_t base_addr = 0;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W me{}; me.dwSize = sizeof(me);
            if (Module32FirstW(snapshot, &me)) {
                do {
                    if (_wcsicmp(me.szModule, module_name) == 0) {
                        base_addr = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                        break;
                    }
                } while (Module32NextW(snapshot, &me));
            }
            CloseHandle(snapshot);
        }
        return base_addr;
    }
};
