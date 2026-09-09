#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

// НАШ УНІКАЛЬНИЙ GUID (Такий самий, як у нашому клієнті C++)
#define COMPILER_UEFI_GUID \
  { 0x12345678, 0x1234, 0x1234, { 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC } }

static EFI_GUID gMyDriverGuid = COMPILER_UEFI_GUID;

// Вказівник на оригінальну функцію BIOS, щоб комп'ютер не зависав
static EFI_SET_VARIABLE gOriginalSetVariable = NULL;

// Структура пакету даних, яка приходить від нашого C++ клієнта
typedef struct {
    UINT32     command_id;      // 1 - читання, 2 - модуль
    UINT32     target_pid;
    UINT64     source_address;
    UINT64     output_buffer;
    UINT64     read_size;
} UEFI_READ_PACKET;

// --- ГОЛОВНИЙ ХУК-ПЕРЕХОПЛЮВАЧ ---
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
    // 1. Перевіряємо, чи це запис від нашого читу (по GUID та імені змінної)
    if (CompareGuid(VendorGuid, &gMyDriverGuid) && StrCmp(VariableName, L"UefiRead") == 0) {
        if (Data != NULL && DataSize >= sizeof(UEFI_READ_PACKET)) {
            UEFI_READ_PACKET *packet = (UEFI_READ_PACKET*)Data;

            if (packet->command_id == 1) { // КОМАНДА НА ЧИТАННЯ ПАМ'ЯТІ ГРИ CS2
                // UEFI працює на рівні фізичної пам'яті (Ring -2).
                // Виконується пряме низькорівневе копіювання байтів із процесу гри в клієнт!
                CopyMem((VOID*)(packet->output_buffer), (VOID*)(packet->source_address), (UINTN)packet->read_size);
                return EFI_SUCCESS;
            }
        }
    }

    // 2. Якщо це звичайний запит Windows — передаємо його оригінальній функції BIOS
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
    Print(L"[+] UefiRuntimeBridge: Initializing custom BIOS Hook...\n");

    // Зберігаємо адресу оригінальної функції SetVariable у глобальну змінну
    gOriginalSetVariable = gRT->SetVariable;

    // ПРИМУСОВО робимо ХУК: записуємо адресу нашого коду замість оригінальної функції в таблицю!
    gRT->SetVariable = MyCustomSetVariable;

    Print(L"[+] UefiRuntimeBridge: Hook successfully installed! VBS will trust this memory.\n");
    return EFI_SUCCESS;
}
