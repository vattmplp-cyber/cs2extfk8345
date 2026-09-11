#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <iostream>
#include "imemory.h"
#include "shared.h"
#include "driver_manager.h"
#include "memory_utils.h"

const wchar_t* SINGULARITY_GUID = L"{deadfade-0601-47C6-84E7-2EBC937D1B11}"; 

class MemoryDriver : public IMemory {
public:
    // Конструктор приймає тип бекенду (0 - WinAPI, 1 - Syscalls, 2 - kdmapper, 3 - UEFI Direct, 4 - UEFI + .sys)
    explicit MemoryDriver(int backend_mode)
        : m_backend_mode(backend_mode), h_driver(INVALID_HANDLE_VALUE), pid(0) {}

    ~MemoryDriver() override {
        close();
    }

    // Офіційне підвищення прав у токені за інструкцією твого ШІ (прибирає 1314)
    bool EnableSystemEnvironmentPrivilege() const {
        HANDLE hToken;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
            return false;
        }

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

        // РЕЖИМ 3 (kdmapper) або РЕЖИМ 5 (UEFI завантаження драйвера .sys)
        if (m_backend_mode == 3 || m_backend_mode == 5) {
            auto result = DriverManager::setup_kdmapper();
            if (result != DriverManager::READY) return false;

            h_driver = CreateFileW(KDMP_USER_PATH, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h_driver == INVALID_HANDLE_VALUE) return false;
        }
        // РЕЖИМ 4: Чистий UEFI BIOS Runtime (Singularity Op 0)
        else if (m_backend_mode == 4) {
            printf("[DEBUG] Activating SeSystemEnvironmentPrivilege for direct UEFI communication...\n");
            if (EnableSystemEnvironmentPrivilege()) {
                printf("[DEBUG] Token ACTIVATED successfully (ERROR_SUCCESS).\n");
            } else {
                printf("[DEBUG] Warning: Token activation ignored (VBS might still be active).\n");
            }
        }

        pid = find_process(process_name);
        if (!pid) return false;

        m_modules.client = query_module_base(L"client.dll", &m_modules.client_size);
        if (!m_modules.client) return false;
        
        m_modules.engine2 = query_module_base(L"engine2.dll", &m_modules.engine2_size);
        if (!m_modules.engine2) return false;
        
        m_modules.schemasystem = query_module_base(L"schemasystem.dll", &m_modules.schemasystem_size);
        m_modules.tier0 = query_module_base(L"tier0.dll", &m_modules.tier0_size);
        m_modules.vphysics2 = query_module_base(L"vphysics2.dll", &m_modules.vphysics2_size);

        return true;
    }

    void close() override {
        if (h_driver != INVALID_HANDLE_VALUE) {
            CloseHandle(h_driver); h_driver = INVALID_HANDLE_VALUE;
        }
        pid = 0; m_modules = {};
    }

    bool read_raw(uintptr_t address, void* buffer, size_t size) const override {
        if (!pid || !buffer || size == 0) return false;

        // РЕЖИМ 1: Юзермод WinAPI (OpenProcess / ReadProcessMemory)
        if (m_backend_mode == 1) {
            HANDLE hProc = OpenProcess(PROCESS_VM_READ, FALSE, pid);
            if (!hProc) return false;
            SIZE_T bytes_read = 0;
            BOOL status = ReadProcessMemory(hProc, reinterpret_cast<LPCVOID>(address), buffer, size, &bytes_read);
            CloseHandle(hProc);
            return status && (bytes_read == size);
        }
        // РЕЖИМ 2: Юзермод Indirect Syscalls (Безпечний обхід VAC через GetCurrentProcess)
        else if (m_backend_mode == 2) {
            SIZE_T bytes_read = 0;
            BOOL status = ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), buffer, size, &bytes_read);
            return status && (bytes_read == size);
        }
        // РЕЖИМ 3 або 5: Робота через завантажений Kernel-драйвер .sys (kdmapper / Singularity Op 5)
        else if (m_backend_mode == 3 || m_backend_mode == 5) {
            if (h_driver == INVALID_HANDLE_VALUE) return false;
            READ_MEMORY_REQUEST request{};
            request.target_pid = pid;
            request.source_address = static_cast<ULONG64>(address);
            request.read_size = static_cast<ULONG>(size);
            DWORD returned = 0;
            return DeviceIoControl(h_driver, IOCTL_READ_MEMORY, &request, sizeof(request), buffer, static_cast<DWORD>(size), &returned, nullptr);
        }
        // РЕЖИМ 4: Істинне апаратне чисте UEFI читання пам'яті (GlitchedPanda Op 0 memcpy)
        else if (m_backend_mode == 4) {
            struct SINGULARITY_MEMORY_COMMAND {
                int magic;                    
                int operation;                
                unsigned long long data[10];  // Повноцінний масив з 10 елементів
                int size;                     
            };

            SINGULARITY_MEMORY_COMMAND cmd{};
            cmd.magic = 0xDEADFADE;           
            cmd.operation = 0;                // Op 0: memcpy
            
            cmd.data[0] = reinterpret_cast<unsigned long long>(buffer);  // Destination
            cmd.data[1] = static_cast<unsigned long long>(address);       // Source
            cmd.size = static_cast<int>(size);

            // Викликаємо оригінальний Set, який перехоплює наш SingularityDxe.efi
            BOOL status = SetFirmwareEnvironmentVariableW(L"Singularity42", SINGULARITY_GUID, &cmd, sizeof(cmd));
            
            std::wcout << std::flush;
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
        uintptr_t client;
        size_t client_size;
        uintptr_t engine2;
        size_t engine2_size;
        uintptr_t schemasystem;
        size_t schemasystem_size;
        uintptr_t tier0;
        size_t tier0_size;
        uintptr_t vphysics2;
        size_t vphysics2_size;
    } m_modules;

    uintptr_t query_module_base(const wchar_t* module_name, size_t* out_size) const {
        if (!pid) return 0;
        uintptr_t base_addr = 0;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W me{}; me.dwSize = sizeof(me);
            if (Module32FirstW(snapshot, &me)) {
                do {
                    if (_wcsicmp(me.szModule, module_name) == 0) {
                        base_addr = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                        if (out_size) *out_size = static_cast<size_t>(me.modBaseSize);
                        break;
                    }
                } while (Module32NextW(snapshot, &me));
            }
            CloseHandle(snapshot);
        }
        return base_addr;
    }
};
