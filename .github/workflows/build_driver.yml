#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseMemoryLib.h>

// НАШ УНІКАЛЬНИЙ GUID
#define COMPILER_UEFI_GUID \
  { 0x12345678, 0x1234, 0x1234, { 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC } }

static EFI_GUID gMyDriverGuid = COMPILER_UEFI_GUID;
static EFI_SET_VARIABLE gOriginalSetVariable = NULL;
static EFI_EVENT gVirtualAddressMapEvent = NULL;

typedef struct {
    UINT32     command_id;      
    UINT32     target_pid;
    UINT64     source_address;
    UINT64     output_buffer;
    UINT64     read_size;
} UEFI_READ_PACKET;

// --- БЕЗПЕЧНИЙ ХУК З СИНХРОНІЗАЦІЄЮ АДРЕС ---
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
    if (VariableName == NULL || VendorGuid == NULL) {
        return gOriginalSetVariable(VariableName, VendorGuid, Attributes, DataSize, Data);
    }

    if (CompareGuid(VendorGuid, &gMyDriverGuid) && StrCmp(VariableName, L"UefiRead") == 0) {
        if (Data != NULL && DataSize >= sizeof(UEFI_READ_PACKET)) {
            UEFI_READ_PACKET *packet = (UEFI_READ_PACKET*)Data;

            if (packet->command_id == 1) { 
                if (packet->output_buffer != 0 && packet->source_address != 0 && packet->read_size > 0) {
                    CopyMem((VOID*)(packet->output_buffer), (VOID*)(packet->source_address), (UINTN)packet->read_size);
                    return EFI_SUCCESS;
                }
            }
        }
    }

    return gOriginalSetVariable(VariableName, VendorGuid, Attributes, DataSize, Data);
}

// --- ФУНКЦІЯ ПЕРЕХОДУ НА ВІРТУАЛЬНІ АДРЕСИ ВІНДОВС ---
VOID
EFIAPI
OnSetVirtualAddressMap (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
    // Безпечна конвертація покажчиків під нову карту пам'яті Windows ядра
    if (gRT != NULL && gOriginalSetVariable != NULL) {
        gRT->ConvertPointer(0, (VOID**)&gOriginalSetVariable);
        gRT->ConvertPointer(0, (VOID**)&gRT);
    }
}

// --- ТОЧКА ВХОДУ UEFI ДРАЙВЕРА ---
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
    gOriginalSetVariable = gRT->SetVariable;
    gRT->SetVariable = MyCustomSetVariable;

    // ФІКС: Змінено ім'я макросу події на правильний стандарт Intel EDK2 - EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE
    gBS->CreateEvent (
           EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE,
           TPL_NOTIFY,
           OnSetVirtualAddressMap,
           NULL,
           &gVirtualAddressMapEvent
           );

    return EFI_SUCCESS;
}
