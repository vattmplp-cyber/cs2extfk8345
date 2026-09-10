// memory/memory_driver.h - Повне розділення Режиму 3 (kdmapper) та Режиму 4 (UEFI)
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

const wchar_t* COMPILER_UEFI_GUID = L"{12345678-1234-1234-1234-123456789ABC}";

class MemoryDriver : public IMemory {
public:
    // Тепер конструктор чітко запам'ятовує, який режим ми обрали
    explicit MemoryDriver(bool use_kdmapper)
        : m_use_kdmapper(use_kdmapper) {}

    ~MemoryDriver() override {
        close();
    }

    bool attach(const wchar_t* process_name) override {
        close();

        // --- ЛОГІКА РЕЖИМУ 3 (KDMAPPER) ---
        if (m_use_kdmapper) {
            auto result = DriverManager::setup_kdmapper();
            if (result != DriverManager::READY) return false;

            h_driver = CreateFileW(
                KDMP_USER_PATH,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, 0, nullptr
            );

            if (h_driver == INVALID_HANDLE_VALUE) {
                printf("[-] Failed to open kdmapper driver device: %lu\n", GetLastError());
                return false;
            }
        } 
        // --- ЛОГІКА РЕЖИМУ 4 (UEFI BIOS RUNTIME) ---
        else {
            if (DriverManager::setup() != DriverManager::READY) {
                return false;
            }
            // Для UEFI нам не потрібен handle пристрою h_driver!
        }

        // Знаходимо ID процесу гри CS2
        pid = find_process(process_name);
        if (!pid) {
            printf("[-] Process '%ls' not found.\n", process_name);
            return false;
        }

        // Отримуємо бази модулів залежно від обраного режиму
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
            CloseHandle(h_driver);
            h_driver = INVALID_HANDLE_VALUE;
        }
        pid = 0;
        m_modules = {};
    }

    bool read_raw(uintptr_t address, void* buffer, size_t size) const override {
        if (!pid || !buffer || size == 0) return false;

        // ЯКЩО ОБРАНО РЕЖИМ 3: Працюємо через класичний драйвер ядра Windows
        if (m_use_kdmapper) {
            if (h_driver == INVALID_HANDLE_VALUE) return false;
            
            READ_MEMORY_REQUEST request{};
            request.target_pid = pid;
            request.source_address = static_cast<ULONG64>(address);
            request.read_size = static_cast<ULONG>(size);

            DWORD returned = 0;
            return DeviceIoControl(h_driver, IOCTL_READ_MEMORY, &request, sizeof(request), buffer, static_cast<DWORD>(size), &returned, nullptr);
        } 
        // ЯКЩО ОБРАНО РЕЖИМ 4: Працюємо через наш бездрайверний UEFI BIOS міст
        else {
            struct UEFI_READ_PACKET {
                DWORD     command_id;
                DWORD     target_pid;
                ULONG64   source_address;
                ULONG64   output_buffer;
                ULONG64   read_size;
            };

            UEFI_READ_PACKET packet{};
            packet.command_id = 1; // Команда читання
            packet.target_pid = pid;
            packet.source_address = static_cast<ULONG64>(address);
            packet.output_buffer = reinterpret_cast<ULONG64>(buffer);
            packet.read_size = static_cast<ULONG64>(size);

            SetFirmwareEnvironmentVariableW(L"UefiRead", COMPILER_UEFI_GUID, &packet, sizeof(packet));
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

        // РЕЖИМ 3: Запит модуля через драйвер
        if (m_use_kdmapper) {
            if (h_driver == INVALID_HANDLE_VALUE) return 0;
            MODULE_BASE_REQUEST request{};
            request.target_pid = pid;
            wcsncpy_s(request.module_name, 256, module_name, _TRUNCATE);

            MODULE_BASE_REQUEST response{};
            DWORD returned = 0;
            BOOL ok = DeviceIoControl(h_driver, IOCTL_GET_MODULE_BASE, &request, sizeof(request), &response, sizeof(response), &returned, nullptr);
            
            if (ok && returned == sizeof(MODULE_BASE_REQUEST)) {
                if (out_size) *out_size = static_cast<size_t>(response.module_size);
                return static_cast<uintptr_t>(response.base_address);
            }
            return 0;
        } 
        // РЕЖИМ 4: Запит модуля через UEFI
        else {
            struct UEFI_MODULE_PACKET {
                DWORD     command_id;
                DWORD     target_pid;
                ULONG64   base_address;
                ULONG64   module_size;
                wchar_t   module_name[256];
            };

            UEFI_MODULE_PACKET packet{};
            packet.command_id = 2; // Команда модуля
            packet.target_pid = pid;
            wcsncpy_s(packet.module_name, 256, module_name, _TRUNCATE);

            SetFirmwareEnvironmentVariableW(L"UefiModule", COMPILER_UEFI_GUID, &packet, sizeof(packet));

            if (packet.base_address != 0) {
                if (out_size) *out_size = static_cast<size_t>(packet.module_size);
                return static_cast<uintptr_t>(packet.base_address);
            }
            return 0;
        }
    }
};
