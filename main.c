#define GNU_EFI_USE_MS_ABI 1
#define MicrosoftCallingType __attribute__((ms_abi))

#include <efi.h>
#include <efilib.h>

#define baseOperation 0x6599
#define VARIABLE_NAME L"mgKsfCtkhfghBeEcVnRd"

#define COMMAND_MAGIC baseOperation*0x9173

typedef struct _DummyProtocalData {
	UINTN blank;
} DummyProtocalData;

typedef unsigned long long ptr64;

typedef struct _MemoryCommand
{
	int magic;
	int operation;
	ptr64 data[10];
} MemoryCommand;

typedef int (MicrosoftCallingType* PsLookupProcessByProcessId)(
	void* ProcessId,
	void* OutPEProcess
	);
typedef void* (MicrosoftCallingType* PsGetProcessSectionBaseAddress)(
	void* PEProcess
	);
typedef int (MicrosoftCallingType* MmCopyVirtualMemory)(
	void* SourceProcess,
	void* SourceAddress,
	void* TargetProcess,
	void* TargetAddress,
	ptr64 BufferSize,
	char PreviousMode,
	void* ReturnSize
	);
typedef void* (MicrosoftCallingType* PsGetProcessWow64Process)(
	void* PEProcess
	);

static const EFI_GUID ProtocolGuid
= { 0x2f84893e, 0xfd5e, 0x2038, {0x8d, 0x9e, 0x20, 0xa7, 0xaf, 0x9c, 0x32, 0xf1} };

static const EFI_GUID VirtualGuid
= { 0x13FA7698, 0xC831, 0x49C7, { 0x87, 0xEA, 0x8F, 0x43, 0xFC, 0xC2, 0x51, 0x96 } };

static const EFI_GUID ExitGuid
= { 0x27ABF055, 0xB1B8, 0x4C26, { 0x80, 0x48, 0x74, 0x8F, 0x37, 0xBA, 0xA2, 0xDF } };

static EFI_SET_VARIABLE oSetVariable = NULL;

static EFI_EVENT NotifyEvent = NULL;
static EFI_EVENT ExitEvent = NULL;
static BOOLEAN Virtual = FALSE;
static BOOLEAN Runtime = FALSE;

static PsLookupProcessByProcessId GetProcessByPid = (PsLookupProcessByProcessId)0;
static PsGetProcessSectionBaseAddress GetBaseAddress = (PsGetProcessSectionBaseAddress)0;
static MmCopyVirtualMemory MCopyVirtualMemory = (MmCopyVirtualMemory)0;
static PsGetProcessWow64Process GetProcessWow64Process = (PsGetProcessWow64Process)0;

EFI_STATUS
RunCommand(MemoryCommand* cmd)
{
	if (cmd->magic != COMMAND_MAGIC)
	{
		return EFI_ACCESS_DENIED;
	}

	if (cmd->operation == baseOperation * 0x724)
	{
		void* src_process_id = (void*)cmd->data[0];
		void* src_address = (void*)cmd->data[1];
		void* dest_process_id = (void*)cmd->data[2];
		void* dest_address = (void*)cmd->data[3];
		ptr64 size = cmd->data[4];
		void* resultAddr = (void*)cmd->data[5];

		if (src_process_id == (void*)4ULL) {
			CopyMem(dest_address, src_address, size);
		}
		else {
			void* SrcProc = 0;
			void* DstProc = 0;
			ptr64 size_out = 0;
			int status = 0;

			status = GetProcessByPid(src_process_id, &SrcProc);
			if (status < 0) {
				*(ptr64*)resultAddr = status;
				return EFI_SUCCESS;
			}

			status = GetProcessByPid(dest_process_id, &DstProc);
			if (status < 0) {
				*(ptr64*)resultAddr = status;
				return EFI_SUCCESS;
			}


			*(ptr64*)resultAddr = MCopyVirtualMemory(SrcProc, src_address, DstProc, dest_address, size, 1, &size_out);
		}
		return EFI_SUCCESS;
	}

	if (cmd->operation == baseOperation * 0x275)
	{
		GetProcessByPid = (PsLookupProcessByProcessId)cmd->data[0];
		GetBaseAddress = (PsGetProcessSectionBaseAddress)cmd->data[1];
		MCopyVirtualMemory = (MmCopyVirtualMemory)cmd->data[2];
		GetProcessWow64Process = (PsGetProcessWow64Process)cmd->data[3];
		ptr64 resultAddr = cmd->data[4];
		*(ptr64*)resultAddr = 1;
		return EFI_SUCCESS;
	}

	if (cmd->operation == baseOperation * 0x536)
	{
		void* pid = (void*)cmd->data[0];
		void* resultAddr = (void*)cmd->data[1];
		void* ProcessPtr = 0;

		if (GetProcessByPid(pid, &ProcessPtr) < 0 || ProcessPtr == 0) {
			resultAddr = 0;
			return EFI_SUCCESS;
		}

		*(ptr64*)resultAddr = (ptr64)GetProcessWow64Process(ProcessPtr); //Return peb32 Address
		//*(ptr64*)resultAddr = (ptr64)GetBaseAddress(ProcessPtr); //Return Base Address
		return EFI_SUCCESS;
	}

	return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
HookedSetVariable(
	IN CHAR16* VariableName,
	IN EFI_GUID* VendorGuid,
	IN UINT32 Attributes,
	IN UINTN DataSize,
	IN VOID* Data
)
{
	if (Virtual && Runtime)
	{
		if (VariableName != NULL && VariableName[0] != CHAR_NULL && VendorGuid != NULL)
		{
			if (StrnCmp(VariableName, VARIABLE_NAME,
				(sizeof(VARIABLE_NAME) / sizeof(CHAR16)) - 1) == 0)
			{
				if (DataSize == 0 && Data == NULL)
				{
					return EFI_SUCCESS;
				}

				if (DataSize == sizeof(MemoryCommand))
				{
					return RunCommand((MemoryCommand*)Data);
				}
			}
		}
	}

	return oSetVariable(VariableName, VendorGuid, Attributes, DataSize, Data);
}

VOID
EFIAPI
SetVirtualAddressMapEvent(
	IN EFI_EVENT Event,
	IN VOID* Context
)
{
	RT->ConvertPointer(0, &oSetVariable);

	RtLibEnableVirtualMappings();

	NotifyEvent = NULL;

	Virtual = TRUE;
}

VOID
EFIAPI
ExitBootServicesEvent(
	IN EFI_EVENT Event,
	IN VOID* Context
)
{
	BS->CloseEvent(ExitEvent);
	ExitEvent = NULL;

	BS = NULL;

	Runtime = TRUE;

	ST->ConOut->SetAttribute(ST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLUE);
	ST->ConOut->ClearScreen(ST->ConOut);
	Print(L"Driver seems to be working as expected! Windows is booting now...\n");
}

VOID*
SetServicePointer(
	IN OUT EFI_TABLE_HEADER* ServiceTableHeader,
	IN OUT VOID** ServiceTableFunction,
	IN VOID* NewFunction
)
{
	if (ServiceTableFunction == NULL || NewFunction == NULL)
		return NULL;

	ASSERT(BS != NULL);
	ASSERT(BS->CalculateCrc32 != NULL);

	CONST EFI_TPL Tpl = BS->RaiseTPL(TPL_HIGH_LEVEL);

	VOID* OriginalFunction = *ServiceTableFunction;
	*ServiceTableFunction = NewFunction;

	ServiceTableHeader->CRC32 = 0;
	BS->CalculateCrc32((UINT8*)ServiceTableHeader, ServiceTableHeader->HeaderSize, &ServiceTableHeader->CRC32);

	BS->RestoreTPL(Tpl);

	return OriginalFunction;
}

static
EFI_STATUS
EFI_FUNCTION
efi_unload(IN EFI_HANDLE ImageHandle)
{
	return EFI_ACCESS_DENIED;
}

EFI_STATUS
efi_main(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE* SystemTable)
{
	InitializeLib(ImageHandle, SystemTable);

	EFI_LOADED_IMAGE* LoadedImage = NULL;
	EFI_STATUS status = BS->OpenProtocol(ImageHandle, &LoadedImageProtocol,
		(void**)&LoadedImage, ImageHandle,
		NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);

	if (EFI_ERROR(status))
	{
		Print(L"Can't open protocol: %d\n", status);
		return status;
	}

	DummyProtocalData dummy = { 0 };
	status = LibInstallProtocolInterfaces(
		&ImageHandle, &ProtocolGuid,
		&dummy, NULL);

	if (EFI_ERROR(status))
	{
		Print(L"Can't register interface: %d\n", status);
		return status;
	}

	LoadedImage->Unload = (EFI_IMAGE_UNLOAD)efi_unload;

	status = BS->CreateEventEx(EVT_NOTIFY_SIGNAL,
		TPL_NOTIFY,
		SetVirtualAddressMapEvent,
		NULL,
		VirtualGuid,
		&NotifyEvent);

	if (EFI_ERROR(status))
	{
		Print(L"Can't create event (SetVirtualAddressMapEvent): %d\n", status);
		return status;
	}

	status = BS->CreateEventEx(EVT_NOTIFY_SIGNAL,
		TPL_NOTIFY,
		ExitBootServicesEvent,
		NULL,
		ExitGuid,
		&ExitEvent);

	if (EFI_ERROR(status))
	{
		Print(L"Can't create event (ExitBootServicesEvent): %d\n", status);
		return status;
	}

	oSetVariable = (EFI_SET_VARIABLE)SetServicePointer(&RT->Hdr, (VOID**)&RT->SetVariable, (VOID**)&HookedSetVariable);

	Print(L"\nF A X A\n");
	return EFI_SUCCESS;
}