@echo off
:: Enable Vulnerable Driver Blocklist & Core Isolation
:: Requires Administrator privileges

:: Check for admin rights
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [ERROR] This script must be run as Administrator.
    echo Right-click the script and select "Run as administrator".
    pause
    exit /b 1
)

echo ============================================
echo  Enable Driver Blocklist and Core Isolation
echo ============================================
echo.

echo [*] Enabling Vulnerable Driver Blocklist...

reg add "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\CI\Config" ^
    /v VulnerableDriverBlocklistEnable ^
    /t REG_DWORD ^
    /d 1 ^
    /f

if %errorLevel% equ 0 (
    echo [OK] VulnerableDriverBlocklistEnable set to 1 successfully.
) else (
    echo [ERROR] Failed to write registry key. Error code: %errorLevel%
    pause
    exit /b 1
)

echo.
echo [*] Enabling Core Isolation (Memory Integrity)...

reg add "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity" ^
    /v Enabled ^
    /t REG_DWORD ^
    /d 1 ^
    /f

if %errorLevel% equ 0 (
    echo [OK] Memory Integrity enabled successfully.
) else (
    echo [ERROR] Failed to enable Memory Integrity. Error code: %errorLevel%
    pause
    exit /b 1
)

echo.
echo ============================================
echo [DONE] All security features re-enabled.
echo A REBOOT is REQUIRED for changes to take effect.
echo ============================================
echo.

set /p reboot="Reboot now? (y/n): "
if /i "%reboot%"=="y" (
    shutdown /r /t 3 /c "Rebooting to apply security changes"
)
exit /b 0