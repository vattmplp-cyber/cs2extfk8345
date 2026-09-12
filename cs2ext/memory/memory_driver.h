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

// ============================================================
//  Сумісна структура. Layout: 96 байт (як у EDK2 x64).
// ============================================================
#pragma pack(push, 8)
struct SINGULARITY_MEMORY_COMMAND {
    int                magic;
    int                operation;
    unsigned long long data[10];
    int                size;
};
#pragma pack(pop)

static_assert(sizeof(SINGULARITY_MEMORY_COMMAND) == 96,
              "SingularityDxe MemoryCommand size mismatch!");

// Операції (мають збігатися з .efi)
#define SING_OP_READ       0x00
#define SING_OP_INIT       0x01
#define SING_OP_WRITE_TEST 0x02
#define SING_OP_READ_TEST  0x03
#define SING_OP_CLEAR_TEST 0x04
#define SING_OP_FIND_PROC  0x10
#define SING_OP_CALL_ENTRY 0x05

class MemoryDriver : public IMemory {
public:
    explicit MemoryDriver(int backend_mode)
        : m_backend_mode(backend_mode), h_driver(INVALID_HANDLE_VALUE), pid(0) {}

    ~MemoryDriver() override { close(); }

    bool EnableSystemEnvironmentPrivilege() const {
        HANDLE hToken;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
            return false;

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

    // ------------------------------------------------------------
    //  Діагностика UEFI-каналу (не критично для роботи)
    // ------------------------------------------------------------
    bool TestUefiInit(int* out_driver_size) const {
        SINGULARITY_MEMORY_COMMAND cmd{};
        cmd.magic     = 0xDEADFADE;
        cmd.operation = SING_OP_INIT;

        DWORD cmd_size = sizeof(cmd);
        if (GetFirmwareEnvironmentVariableW(L"Singularity42", SINGULARITY_GUID, &cmd, cmd_size)) {
            if (out_driver_size) *out_driver_size = cmd.size;
            return true;
        }
        return false;
    }

    bool TestUefiWrite(const unsigned char* buffer_to_write, size_t size) const {
        if (!buffer_to_write || size == 0 || size > sizeof(SINGULARITY_MEMORY_COMMAND::data))
            return false;

        SINGULARITY_MEMORY_COMMAND cmd{};
        cmd.magic     = 0xDEADFADE;
        cmd.operation = SING_OP_WRITE_TEST;
        cmd.size      = static_cast<int>(size);
        memcpy(&cmd.data[0], buffer_to_write, size);

        DWORD cmd_size = sizeof(cmd);
        return GetFirmwareEnvironmentVariableW(L"Singularity42", SINGULARITY_GUID, &cmd, cmd_size) != 0;
    }

    bool TestUefiRead(unsigned char* output_buffer, size_t size_to_read) const {
        if (!output_buffer || size_to_read == 0 ||
            size_to_read > sizeof(SINGULARITY_MEMORY_COMMAND::data))
            return false;

        SINGULARITY_MEMORY_COMMAND cmd{};
        cmd.magic     = 0xDEADFADE;
        cmd.operation = SING_OP_READ_TEST;
        cmd.size      = static_cast<int>(size_to_read);

        DWORD cmd_size = sizeof(cmd);
        if (GetFirmwareEnvironmentVariableW(L"Singularity42", SINGULARITY_GUID, &cmd, cmd_size)) {
            memcpy(output_buffer, &cmd.data[0], size_to_read);
            return true;
        }
        return false;
    }

    bool TestUefiClear() const {
        SINGULARITY_MEMORY_COMMAND cmd{};
        cmd.magic     = 0xDEADFADE;
        cmd.operation = SING_OP_CLEAR_TEST;

        DWORD cmd_size = sizeof(cmd);
        return GetFirmwareEnvironmentVariableW(L"Singularity42", SINGULARITY_GUID, &cmd, cmd_size) != 0;
    }

    // ------------------------------------------------------------
    //  attach
    // ------------------------------------------------------------
    bool attach(const wchar_t* process_name) override {
        close();

        if (m_backend_mode == 3) {
            auto result = DriverManager::setup_kdmapper();
            if (result != DriverManager::READY) return false;

            h_driver = CreateFileW(KDMP_USER_PATH, GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
            if (h_driver == INVALID_HANDLE_VALUE) return false;
        }
        else if (m_backend_mode == 5) {
            printf("[DEBUG] UEFI Mode 5: Executing mapper.exe for MemReaderKdmp.sys...\n");

            STARTUPINFOA si = { sizeof(si) };
            PROCESS_INFORMATION pi;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;

            char cmd_line[] = "mapper.exe MemReaderKdmp.sys";

            if (CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 5000);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                printf("[DEBUG] mapper.exe finished.\n");
            } else {
                printf("[DEBUG ERROR] Failed to start mapper.exe!\n");
                return false;
            }

            h_driver = CreateFileW(L"\\\\.\\MemReaderKdmp", GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
            if (h_driver == INVALID_HANDLE_VALUE) {
                printf("[DEBUG ERROR] Failed to connect to MemReaderKdmp!\n");
                return false;
            }
            printf("[DEBUG SUCCESS] MemReaderKdmp active via mapper.exe!\n");
        }
        else if (m_backend_mode == 4) {
            printf("[DEBUG] UEFI Mode 4: enabling SeSystemEnvironmentPrivilege...\n");
            if (!EnableSystemEnvironmentPrivilege()) {
                printf("[DEBUG WARN] Failed to enable SeSystemEnvironmentPrivilege — "
                       "run as admin.\n");
            }
        }

        pid = find_process(process_name);
        if (!pid) return false;

        // ---- Mode 4: просимо UEFI знайти CR3 CS2 (сканування RAM) ----
        if (m_backend_mode == 4) {
            SINGULARITY_MEMORY_COMMAND cmd{};
            cmd.magic     = 0xDEADFADE;
            cmd.operation = SING_OP_FIND_PROC;
            cmd.data[0]   = static_cast<unsigned long long>(pid);

            DWORD cmd_size = sizeof(cmd);
            printf("[DEBUG] Mode 4: asking UEFI to locate PID=%u "
                   "(may take 1-3 seconds)...\n", pid);

            if (!GetFirmwareEnvironmentVariableW(L"Singularity42", SINGULARITY_GUID,
                                                 &cmd, cmd_size)) {
                printf("[DEBUG ERROR] Mode 4: UEFI could not locate PID %u in RAM!\n", pid);
                printf("        Check COM1 log for 'EPROCESS @ PA ...'\n");
                return false;
            }
            printf("[DEBUG SUCCESS] Mode 4: CS2 CR3 cached in UEFI. Ready.\n");
        }

        m_modules.client = query_module_base(L"client.dll", &m_modules.client_size);
        if (!m_modules.client) return false;

        m_modules.engine2      = query_module_base(L"engine2.dll", &m_modules.engine2_size);
        m_modules.schemasystem = query_module_base(L"schemasystem.dll", &m_modules.schemasystem_size);
        m_modules.tier0        = query_module_base(L"tier0.dll", &m_modules.tier0_size);
        m_modules.vphysics2    = query_module_base(L"vphysics2.dll", &m_modules.vphysics2_size);

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

    // ------------------------------------------------------------
    //  read_raw
    // ------------------------------------------------------------
    bool read_raw(uintptr_t address, void* buffer, size_t size) const override {
        if (!pid || !buffer || size == 0) return false;

        if (m_backend_mode == 1) {
            HANDLE hProc = OpenProcess(PROCESS_VM_READ, FALSE, pid);
            if (!hProc) return false;
            SIZE_T bytes_read = 0;
            BOOL status = ReadProcessMemory(hProc, reinterpret_cast<LPCVOID>(address),
                                            buffer, size, &bytes_read);
            CloseHandle(hProc);
            return status && (bytes_read == size);
        }
        else if (m_backend_mode == 2) {
            SIZE_T bytes_read = 0;
            BOOL status = ReadProcessMemory(GetCurrentProcess(),
                                            reinterpret_cast<LPCVOID>(address),
                                            buffer, size, &bytes_read);
            return status && (bytes_read == size);
        }
        else if (m_backend_mode == 3 || m_backend_mode == 5) {
            if (h_driver == INVALID_HANDLE_VALUE) return false;
            READ_MEMORY_REQUEST request{};
            request.target_pid     = pid;
            request.source_address = static_cast<ULONG64>(address);
            request.read_size      = static_cast<ULONG>(size);
            DWORD returned = 0;
            return DeviceIoControl(h_driver, IOCTL_READ_MEMORY,
                                   &request, sizeof(request),
                                   buffer, static_cast<DWORD>(size),
                                   &returned, nullptr);
        }
        // ---- Mode 4: чистий UEFI read через кешований CR3 ----
        else if (m_backend_mode == 4) {
            // Обмеження одного виклику — 1 МБ. Більше — ріжемо на частини.
            constexpr size_t MAX_CHUNK = 0x100000;  // 1 MB

            size_t done = 0;
            uint8_t* dst = reinterpret_cast<uint8_t*>(buffer);

            while (done < size) {
                size_t chunk = (size - done) > MAX_CHUNK ? MAX_CHUNK : (size - done);

                SINGULARITY_MEMORY_COMMAND cmd{};
                cmd.magic     = 0xDEADFADE;
                cmd.operation = SING_OP_READ;
                cmd.data[0]   = reinterpret_cast<unsigned long long>(dst + done);       // куди писати
                cmd.data[1]   = static_cast<unsigned long long>(address + done);        // звідки читати
                cmd.size      = static_cast<int>(chunk);

                DWORD cmd_size = sizeof(cmd);
                if (!GetFirmwareEnvironmentVariableW(L"Singularity42", SINGULARITY_GUID,
                                                     &cmd, cmd_size)) {
                    return false;
                }

                done += chunk;
            }
            return true;
        }
        return false;
    }

    uintptr_t get_client_base() const override { return m_modules.client; }
    DWORD     get_pid() const override { return pid; }

private:
    HANDLE    h_driver;
    DWORD     pid;
    int       m_backend_mode;

    struct {
        uintptr_t client;       size_t client_size;
        uintptr_t engine2;      size_t engine2_size;
        uintptr_t schemasystem; size_t schemasystem_size;
        uintptr_t tier0;        size_t tier0_size;
        uintptr_t vphysics2;    size_t vphysics2_size;
    } m_modules;

    uintptr_t query_module_base(const wchar_t* module_name, size_t* out_size) const {
        if (!pid) return 0;

        if (m_backend_mode == 3 || m_backend_mode == 5) {
            if (h_driver == INVALID_HANDLE_VALUE) return 0;
            MODULE_BASE_REQUEST request{};
            request.target_pid = pid;
            wcsncpy_s(request.module_name, 256, module_name, _TRUNCATE);

            MODULE_BASE_REQUEST response{};
            DWORD returned = 0;
            BOOL ok = DeviceIoControl(h_driver, IOCTL_GET_MODULE_BASE,
                                      &request, sizeof(request),
                                      &response, sizeof(response),
                                      &returned, nullptr);
            if (ok && returned == sizeof(MODULE_BASE_REQUEST)) {
                if (out_size) *out_size = static_cast<size_t>(response.module_size);
                return static_cast<uintptr_t>(response.base_address);
            }
            return 0;
        }

        uintptr_t base_addr = 0;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W me{};
            me.dwSize = sizeof(me);
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
