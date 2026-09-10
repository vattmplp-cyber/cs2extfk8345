#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

// НАШ УНІКАЛЬНИЙ GUID
#define COMPILER_UEFI_GUID \
  { 0x12345678, 0x1234, 0x1234, { 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC } }

static EFI_GUID gMyDriverGuid = COMPILER_UEFI_GUID;
static EFI_SET_VARIABLE gOriginalSetVariable = NULL;

typedef struct {
    UINT32     command_id;      
    UINT32     target_pid;
    UINT64     source_address;
    UINT64     output_buffer;
    UINT64     read_size;
} UEFI_READ_PACKET;

// --- ВИПРАВЛЕНИЙ БЕЗПЕЧНИЙ ХУК-ПЕРЕХОПЛЮВАЧ ---
EFI_STATUS
EFIAPI
MyCustomSetVariable (
  IN  CHAR16    *VariableName,
  IN  EFI_GUID  *VendorGuid,
  IN  UINT32    Attributes,
  IN  UINTN     DataSize,
  IN  VOID      *Data
  )
{
    // Золоте правило безпеки: якщо Windows прислала порожні системні параметри,
    // ми їх не чіпаємо і миттєво віддаємо оригінальному BIOS!
    if (VariableName == NULL || VendorGuid == NULL) {
        return gOriginalSetVariable(VariableName, VendorGuid, Attributes, DataSize, Data);
    }

    // Тепер порівняння GUID та імені змінної є повністю безпечним
    if (CompareGuid(VendorGuid, &gMyDriverGuid) && StrCmp(VariableName, L"UefiRead") == 0) {
        if (Data != NULL && DataSize >= sizeof(UEFI_READ_PACKET)) {
            UEFI_READ_PACKET *packet = (UEFI_READ_PACKET*)Data;

            if (packet->command_id == 1) { 
                // Безпечне копіювання: перевіряємо адреси перед виконанням транзиту
                if (packet->output_buffer != 0 && packet->source_address != 0 && packet->read_size > 0) {
                    CopyMem((VOID*)(packet->output_buffer), (VOID*)(packet->source_address), (UINTN)packet->read_size);
                    return EFI_SUCCESS;
                }
            }
        }
    }

    // Передаємо всі звичайні системні запити оригінальній функції плати Acer
    return gOriginalSetVariable(VariableName, VendorGuid, Attributes, DataSize, Data);
}

// --- ТОЧКА ВХОДУ UEFI ДРАЙВЕРА ---
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
    // Перевіряємо, щоб не зробити подвійний хук, якщо ми перезавантажуємо драйвер у Shell
    if (gRT->SetVariable == MyCustomSetVariable) {
        return EFI_SUCCESS;
    }

    gOriginalSetVariable = gRT->SetVariable;
    gRT->SetVariable = MyCustomSetVariable;

    return EFI_SUCCESS;
}
