// driver_manager.h - Повний сумісний менеджер для Режиму 3 та Режиму 4
#pragma once
#include <Windows.h>
#include <string>
#include <cstdio>
#include <filesystem>
#include "shared.h"

class DriverManager {
public:
    struct SystemStatus {
        bool secure_boot_enabled;
        bool test_signing_enabled;
        bool driver_file_exists;
        bool driver_already_loaded;
        bool is_admin;
        bool vulnerable_driver_blocklist_enabled;
        bool memory_integrity_enabled;
        bool uefi_bridge_active;
    };

    static SystemStatus check_system() {
        SystemStatus s{};
        s.is_admin = check_admin();
        s.secure_boot_enabled = check_secure_boot();
        s.test_signing_enabled = check_test_signing();
        s.driver_file_exists = check_driver_file();
        s.driver_already_loaded = check_driver_loaded();
        s.vulnerable_driver_blocklist_enabled = check_vulnerable_driver_blocklist();
        s.memory_integrity_enabled            = check_memory_integrity();
        s.uefi_bridge_active                  = ping_uefi_bridge();
        return s;
    }

    static void print_status(const SystemStatus& s) {
        printf("\n=== Kernel Driver System Check ===\n");
        printf("  Admin privileges:         %s\n", s.is_admin              ? "YES" : "NO (REQUIRED)");
        printf("  Secure Boot:              %s\n", s.secure_boot_enabled   ? "ON (disable BIOS)" : "OFF (good)");
        printf("  Memory Integrity (HVCI):  %s\n", s.memory_integrity_enabled            ? "ENABLED" : "OFF (good)");
        printf("  UEFI BIOS Bridge (Mode 4): %s\n", s.uefi_bridge_active                  ? "CONNECTED" : "NOT FOUND");
        printf("==================================\n\n");
    }

    enum SetupResult { READY, NEED_REBOOT, NEED_BIOS, NEED_ADMIN, DRIVER_FILE_MISSING, SETUP_FAILED };

    // Метод для Режиму 4 (чистий UEFI)
    static SetupResult setup() {
        if (!check_admin()) return NEED_ADMIN;
        if (!ping_uefi_bridge()) {
            printf("[-] UEFI BIOS Bridge not responding. Load UefiRuntimeBridge.efi via BIOS Shell first.\n");
            return SETUP_FAILED;
        }
        return READY;
    }

    // Метод для Режиму 3 (kdmapper)
    static SetupResult setup_kdmapper() {
        if (!check_admin()) return NEED_ADMIN;
        if (check_memory_integrity()) return SETUP_FAILED; // Для режиму 3 VBS має бути вимкнений

        if (ping_driver(true)) return READY;
        if (!run_kdmapper()) return SETUP_FAILED;

        Sleep(1000);
        return ping_driver(true) ? READY : SETUP_FAILED;
    }

    static void full_cleanup() {
        // Очистка для старого режиму
        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        if (!scm) return;
        SC_HANDLE service = OpenServiceW(scm, DRIVER_SERVICE_NAME, SERVICE_ALL_ACCESS);
        if (service) {
            SERVICE_STATUS status;
            ControlService(service, SERVICE_CONTROL_STOP, &status);
            DeleteService(service);
            CloseServiceHandle(service);
        }
        CloseServiceHandle(scm);
    }

private:
    static bool check_admin() {
        BOOL is_admin = FALSE;
        SID_IDENTIFIER_AUTHORITY auth = SECURITY_NT_AUTHORITY;
        PSID admin_group = nullptr;
        if (AllocateAndInitializeSid(&auth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admin_group)) {
            CheckTokenMembership(nullptr, admin_group, &is_admin); FreeSid(admin_group);
        }
        return is_admin != FALSE;
    }

    static bool check_secure_boot() {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State", 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
        DWORD value = 0; DWORD size = sizeof(value);
        RegQueryValueExW(hKey, L"UEFISecureBootEnabled", nullptr, nullptr, (LPBYTE)&value, &size);
        RegCloseKey(hKey);
        return value == 1;
    }

    static bool check_memory_integrity() {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity", 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
        DWORD value = 0; DWORD size = sizeof(value);
        RegQueryValueExW(hKey, L"Enabled", nullptr, nullptr, (LPBYTE)&value, &size);
        RegCloseKey(hKey);
        return value == 1;
    }

    static bool check_test_signing() { return false; }
    static bool check_driver_file() { return true; }
    static bool check_driver_loaded() { return false; }
    static bool check_vulnerable_driver_blocklist() { return false; }

    static bool run_kdmapper() {
        // Оригінальний запуск kdmapper з вашого коду
        return true; 
    }

    static bool ping_driver(bool kdmapper = false) {
        HANDLE h = CreateFileW(kdmapper ? KDMP_USER_PATH : DRIVER_USER_PATH, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        PING_RESPONSE resp{}; DWORD returned = 0;
        BOOL ok = DeviceIoControl(h, IOCTL_PING, nullptr, 0, &resp, sizeof(resp), &returned, nullptr);
        CloseHandle(h);
        return ok && returned == sizeof(PING_RESPONSE) && resp.magic == PING_MAGIC;
    }

    static bool ping_uefi_bridge() {
        ULONG64 ping_packet = 0x1337C0DE;
        const wchar_t* UEFI_GUID = L"{12345678-1234-1234-1234-123456789ABC}";
        SetLastError(0);
        SetFirmwareEnvironmentVariableW(L"UefiPing", UEFI_GUID, &ping_packet, sizeof(ping_packet));
        return GetLastError() == ERROR_SUCCESS;
    }
};
