// memory/memory_driver.h - Повна синхронізація зі структурами SingularityDxe.c
#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include <cstdint>
#include <cstdio>
#include <memory>
#include "imemory.h"
#include "shared.h"
#include "driver_manager.h"
#include "memory_utils.h"

// Еталонний GUID оригінального проекту Singularity
const wchar_t* SINGULARITY_GUID = L"{deadfade-0601-47C6-84E7-2EBC937D1B11}"; 

class MemoryDriver : public IMemory {
public:
    explicit MemoryDriver(bool use_kdmapper)
        : m_use_kdmapper(use_kdmapper) {}

    ~MemoryDriver() override {
        close();
    }

    bool attach(const wchar_t* process_name) override {
        close();

        if (m_use_kdmapper) {
            auto result = DriverManager::setup_kdmapper();
            if (result != DriverManager::READY) return false;

            h_driver = CreateFileW(KDMP_USER_PATH, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h_driver == INVALID_HANDLE_VALUE) return false;
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

        if (m_use_kdmapper) {
            if (h_driver == INVALID_HANDLE_VALUE) return false;
            READ_MEMORY_REQUEST request{};
            request.target_pid = pid;
            request.source_address = static_cast<ULONG64>(address);
            request.read_size = static_cast<ULONG>(size);
            DWORD returned = 0;
            return DeviceIoControl(h_driver, IOCTL_READ_MEMORY, &request, sizeof(request), buffer, static_cast<DWORD>(size), &returned, nullptr);
        } 
        else {
            // Еталонна структура MemoryCommand з файлу SingularityDxe.c (Рядок 48)
            struct SINGULARITY_MEMORY_COMMAND {
                int magic;                    // Має бути 0xDEADFADE
                int operation;                // 0 - копіювання пам'яті
                unsigned long long data[10];  // data[0] - dst, data[1] - src
                int size;                     // кількість байт
            };

            SINGULARITY_MEMORY_COMMAND cmd{};
            cmd.magic = 0xDEADFADE;           // Наш перепустковий квиток у коді Singularity!
            cmd.operation = 0;                // Операція 0: CopyMem
            cmd.data[0] = reinterpret_cast<unsigned long long>(buffer);  // Куди покласти дані в нашому читі
            cmd.data[1] = static_cast<unsigned long long>(address);       // Звідки зчитати дані з гри CS2
            cmd.size = static_cast<int>(size);

            // Викликаємо оригінальну системну назву "Singularity42" (Рядок 44)
            SetFirmwareEnvironmentVariableW(L"Singularity42", SINGULARITY_GUID, &cmd, sizeof(cmd));
            return true;
        }
    }

    uintptr_t get_client_base() const override { return m_modules.client; }
    DWORD get_pid() const override { return pid; }

private:
    HANDLE    h_driver = INVALID_HANDLE_VALUE;
    DWORD     pid = 0;
    bool      m_use_kdmapper = false;

    uintptr_t query_module_base(const wchar_t* module_name, size_t* out_size) const {
        if (!pid) return 0;

        if (m_use_kdmapper) {
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
        else {
            // Безпечний та швидкий пошук бази client.dll, який не тригерить VAC
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
    }
};
