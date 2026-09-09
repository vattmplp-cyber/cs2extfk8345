// driver_manager.h - Режим UEFI Runtime Services
#pragma once
#include <Windows.h>
#include <cstdio>

class DriverManager {
public:
    struct SystemStatus {
        bool is_admin;
        bool uefi_bridge_active;
    };

    static SystemStatus check_system() {
        SystemStatus s{};
        s.is_admin = check_admin();
        s.uefi_bridge_active = ping_uefi_bridge();
        return s;
    }

    static void print_status(const SystemStatus& s) {
        printf("\n=== UEFI Runtime Kernel Bridge Status ===\n");
        printf("  Admin privileges:    %s\n", s.is_admin           ? "YES (Good)" : "NO (REQUIRED)");
        printf("  UEFI BIOS Bridge:    %s\n", s.uefi_bridge_active ? "CONNECTED"  : "NOT FOUND (Load via BIOS Shell)");
        printf("=========================================\n\n");
    }

    enum SetupResult { READY, NEED_ADMIN, SETUP_FAILED };

    static SetupResult setup() {
        SystemStatus s = check_system();
        print_status(s);

        if (!s.is_admin) {
            printf("[-] Must run as Administrator to access Firmware Variables.\n");
            return NEED_ADMIN;
        }

        if (!s.uefi_bridge_active) {
            printf("[-] UEFI Hook not responding. Make sure you loaded the .efi driver before boot.\n");
            return SETUP_FAILED;
        }

        return READY;
    }

private:
    static bool check_admin() {
        BOOL is_admin = FALSE;
        SID_IDENTIFIER_AUTHORITY auth = SECURITY_NT_AUTHORITY;
        PSID admin_group = nullptr;

        if (AllocateAndInitializeSid(&auth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admin_group)) {
            CheckTokenMembership(nullptr, admin_group, &is_admin);
            FreeSid(admin_group);
        }
        return is_admin != FALSE;
    }

    static bool ping_uefi_bridge() {
        // Тестовий запит до нашого UEFI-хука SetVariable
        // Якщо він завантажений, він поверне успіх, якщо ні - Windows видасть помилку 1314 або 2
        ULONG64 ping_packet = 0x1337C0DE; // Наш секретний пароль-пінг
        // Специфічний GUID нашого UEFI-модуля (можна міняти на будь-який унікальний)
        const wchar_t* UEFI_GUID = L"{12345678-1234-1234-1234-123456789ABC}";
        
        SetLastError(0);
        SetFirmwareEnvironmentVariableW(L"UefiPing", UEFI_GUID, &ping_packet, sizeof(ping_packet));
        return GetLastError() == ERROR_SUCCESS;
    }
};
