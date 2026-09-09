// memory/memory_driver.h - Версія для чистих UEFI викликів (Без .sys драйвера)
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

// Наш унікальний ідентифікатор UEFI-модуля (має збігатися з лоадером)
const wchar_t* COMPILER_UEFI_GUID = L"{12345678-1234-1234-1234-123456789ABC}";

class MemoryDriver : public IMemory {
public:
    explicit MemoryDriver(bool use_kdmapper = false) {
        // Параметр use_kdmapper нам більше не потрібен, ігноруємо його
    }

    ~MemoryDriver() override {
        close();
    }

    bool attach(const wchar_t* process_name) override {
        close();

        // Крок 1: Перевірка прав та зв'язку з UEFI
        if (DriverManager::setup() != DriverManager::READY) {
            return false;
        }

        // Крок 2: Знаходимо ID процесу гри CS2
        pid = find_process(process_name);
        if (!pid) {
            printf("[-] Process '%ls' not found.\n", process_name);
            return false;
        }

        // Крок 3: Отримуємо бази модулів прямо через наш UEFI міст
        m_modules.client = query_module_base(L"client.dll", &m_modules.client_size);
        if (!m_modules.client) {
            printf("[-] Failed to find client.dll via UEFI.\n");
            return false;
        }
        m_modules.engine2 = query_module_base(L"engine2.dll", &m_modules.engine2_size);
        if (!m_modules.engine2) {
            printf("[-] Failed to find engine2.dll via UEFI.\n");
            return false;
        }
        m_modules.schemasystem = query_module_base(L"schemasystem.dll", &m_modules.schemasystem_size);
        if (!m_modules.schemasystem) {
            return false;
        }
        m_modules.tier0 = query_module_base(L"tier0.dll", &m_modules.tier0_size);
        if (!m_modules.tier0) {
            return false;
        }
        m_modules.vphysics2 = query_module_base(L"vphysics2.dll", &m_modules.vphysics2_size);
        if (!m_modules.vphysics2) {
            return false;
        }

        return true;
    }

    void close() override {
        pid = 0;
        m_modules = {};
    }

    bool read_raw(uintptr_t address, void* buffer, size_t size) const override {
        if (!pid || !buffer || size == 0) return false;

        // Структура запиту, яку розпізнає наш UEFI-хук SetVariable
        // Вона повністю повторює логіку вашого старого драйвера!
        struct UEFI_READ_PACKET {
            DWORD     command_id; // Наприклад, 1 - читання, 2 - модуль
            DWORD     target_pid;
            ULONG64   source_address;
            ULONG64   output_buffer;
            ULONG64   read_size;
        };

        UEFI_READ_PACKET packet{};
        packet.command_id = 1; // Команда Читання
        packet.target_pid = pid;
        packet.source_address = static_cast<ULONG64>(address);
        packet.output_buffer = reinterpret_cast<ULONG64>(buffer);
        packet.read_size = static_cast<ULONG64>(size);

        // ВІДПРАВКА КОМАНДИ НАПРЯМУ В BIOS
        // Замість DeviceIoControl викликаємо стандартну Windows функцію роботи з UEFI
        SetFirmwareEnvironmentVariableW(L"UefiRead", COMPILER_UEFI_GUID, &packet, sizeof(packet));

        // Якщо UEFI успішно скопіював дані, повертаємо true
        return true;
    }

    uintptr_t get_client_base() const override { return m_modules.client; }
    DWORD get_pid() const override { return pid; }

private:
    DWORD     pid = 0;

    uintptr_t query_module_base(const wchar_t* module_name, size_t* out_size) const {
        if (!pid) return 0;

        struct UEFI_MODULE_PACKET {
            DWORD     command_id;
            DWORD     target_pid;
            ULONG64   base_address;
            ULONG64   module_size;
            wchar_t   module_name[256];
        };

        UEFI_MODULE_PACKET packet{};
        packet.command_id = 2; // Команда Отримання Бази Модуля
        packet.target_pid = pid;
        packet.base_address = 0;
        packet.module_size = 0;
        wcsncpy_s(packet.module_name, 256, module_name, _TRUNCATE);

        // Надсилаємо запит в BIOS, наш хук запише результат прямо всередину структури packet
        SetFirmwareEnvironmentVariableW(L"UefiModule", COMPILER_UEFI_GUID, &packet, sizeof(packet));

        if (packet.base_address != 0) {
            if (out_size) {
                *out_size = static_cast<size_t>(packet.module_size);
            }
            return static_cast<uintptr_t>(packet.base_address);
        }

        return 0;
    }
};
