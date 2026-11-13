/**
  uefi.h - Minimal, self-contained UEFI definitions for UCTS.
  Hand written so the suite can be built without an EDK2 tree.
  Struct layouts follow the UEFI 2.10 specification.
**/
#ifndef UCTS_UEFI_H
#define UCTS_UEFI_H

/* ---------------------------------------------------------------- types */
typedef unsigned char       UINT8;
typedef signed char         INT8;
typedef unsigned short      UINT16;
typedef signed short        INT16;
typedef unsigned int        UINT32;
typedef signed int          INT32;
typedef unsigned long long  UINT64;
typedef signed long long    INT64;
typedef unsigned long long  UINTN;
typedef signed long long    INTN;
typedef char                CHAR8;
typedef unsigned short      CHAR16;
/* freestanding C runtime (src/runtime.c) */
void *memcpy(void *dst, const void *src, unsigned long long n);
void *memmove(void *dst, const void *src, unsigned long long n);
void *memset(void *dst, int c, unsigned long long n);
int   memcmp(const void *a, const void *b, unsigned long long n);

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

typedef unsigned char       BOOLEAN;
typedef void                VOID;

typedef UINTN               EFI_STATUS;
typedef VOID               *EFI_HANDLE;
typedef VOID               *EFI_EVENT;
typedef UINTN               EFI_TPL;
#define TPL_APPLICATION   4
#define TPL_CALLBACK      8
#define TPL_NOTIFY        16
#define TPL_HIGH_LEVEL    31
typedef UINT64              EFI_PHYSICAL_ADDRESS;
typedef UINT64              EFI_VIRTUAL_ADDRESS;
typedef UINT64              EFI_LBA;

#define EFIAPI
#define IN
#define OUT
#define OPTIONAL
#define CONST const

#define STATIC_ASSERT(cond, msg) typedef char __static_assert_##msg[(cond) ? 1 : -1]

/* ------------------------------------------------------------ constants */
#define EFI_SUCCESS               0
#define EFI_ERR(x)                (0x8000000000000000ULL | (x))
#define EFI_LOAD_ERROR            (EFI_ERR(1))
#define EFI_INVALID_PARAMETER     (EFI_ERR(2))
#define EFI_UNSUPPORTED           (EFI_ERR(3))
#define EFI_BAD_BUFFER_SIZE       (EFI_ERR(4))
#define EFI_BUFFER_TOO_SMALL      (EFI_ERR(5))
#define EFI_NOT_READY             (EFI_ERR(6))
#define EFI_DEVICE_ERROR          (EFI_ERR(7))
#define EFI_WRITE_PROTECTED       (EFI_ERR(8))
#define EFI_OUT_OF_RESOURCES      (EFI_ERR(9))
#define EFI_VOLUME_CORRUPTED      (EFI_ERR(10))
#define EFI_VOLUME_FULL           (EFI_ERR(11))
#define EFI_NO_MEDIA              (EFI_ERR(12))
#define EFI_MEDIA_CHANGED         (EFI_ERR(13))
#define EFI_NOT_FOUND             (EFI_ERR(14))
#define EFI_ACCESS_DENIED         (EFI_ERR(15))
#define EFI_NO_RESPONSE           (EFI_ERR(16))
#define EFI_NO_MAPPING            (EFI_ERR(17))
#define EFI_TIMEOUT               (EFI_ERR(18))
#define EFI_NOT_STARTED           (EFI_ERR(19))
#define EFI_ALREADY_STARTED       (EFI_ERR(20))
#define EFI_ABORTED               (EFI_ERR(21))
#define EFI_ICMP_ERROR            (EFI_ERR(22))
#define EFI_TFTP_ERROR            (EFI_ERR(23))
#define EFI_PROTOCOL_ERROR        (EFI_ERR(24))
#define EFI_INCOMPATIBLE_VERSION  (EFI_ERR(25))
#define EFI_SECURITY_VIOLATION    (EFI_ERR(26))
#define EFI_CRC_ERROR             (EFI_ERR(27))
#define EFI_END_OF_MEDIA          (EFI_ERR(28))
#define EFI_END_OF_FILE           (EFI_ERR(31))
#define EFI_INVALID_LANGUAGE      (EFI_ERR(32))
#define EFI_COMPROMISED_DATA      (EFI_ERR(33))

#define EFI_ERROR(s)              (((INTN)(s)) < 0)

#define EFI_PAGE_SIZE             4096ULL
#define EFI_SIZE_TO_PAGES(s)      (((s) + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE)

#define EFI_VARIABLE_NON_VOLATILE                    0x00000001
#define EFI_VARIABLE_BOOTSERVICE_ACCESS              0x00000002
#define EFI_VARIABLE_RUNTIME_ACCESS                  0x00000004

/* ---------------------------------------------------------------- GUIDs */
#pragma pack(1)
typedef struct {
  UINT32 Data1;
  UINT16 Data2;
  UINT16 Data3;
  UINT8  Data4[8];
} EFI_GUID;
#pragma pack()

#define EFI_GUID_DEF(name, a, b, c, d0, d1, d2, d3, d4, d5, d6, d7) \
  static const EFI_GUID name = { a, b, c, { d0, d1, d2, d3, d4, d5, d6, d7 } }

EFI_GUID_DEF(gEfiLoadedImageProtocolGuid, 0x5B1B31A1, 0x9562, 0x11D2, 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B);
EFI_GUID_DEF(gEfiGraphicsOutputProtocolGuid, 0x9042A9DE, 0x23DC, 0x4A38, 0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A);
EFI_GUID_DEF(gEfiSimpleTextInProtocolGuid, 0x387477C1, 0x69C7, 0x11D2, 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B);
EFI_GUID_DEF(gEfiSimpleTextOutProtocolGuid, 0x387477C2, 0x69C7, 0x11D2, 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B);
EFI_GUID_DEF(gEfiSimpleFileSystemProtocolGuid, 0x964E5B22, 0x6459, 0x11D2, 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B);
EFI_GUID_DEF(gEfiFileInfoGuid, 0x09576E92, 0x6D3F, 0x11D2, 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B);
EFI_GUID_DEF(gEfiBlockIoProtocolGuid, 0x964E5B21, 0x6459, 0x11D2, 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B);
EFI_GUID_DEF(gEfiDevicePathProtocolGuid, 0x09576E91, 0x6D3F, 0x11D2, 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B);
EFI_GUID_DEF(gEfiMpServiceProtocolGuid, 0x3FDDA605, 0xA76E, 0x4F46, 0xAD, 0x29, 0x12, 0xF4, 0x53, 0x1B, 0x3D, 0x08);
EFI_GUID_DEF(gEfiSmbiosProtocolGuid, 0x03583FF6, 0xCB36, 0x4940, 0x94, 0x7E, 0xB9, 0xB3, 0x9F, 0x4A, 0xFA, 0xF7);
EFI_GUID_DEF(gEfiAtaPassThruProtocolGuid, 0x1D3DE7F0, 0x0807, 0x424F, 0xAA, 0x69, 0x11, 0xA5, 0x4E, 0x19, 0xA4, 0x6F);
EFI_GUID_DEF(gEfiNvmExpressPassThruProtocolGuid, 0x52C78312, 0x8EDC, 0x4233, 0x98, 0xF2, 0x1A, 0x1A, 0xA5, 0xE3, 0x88, 0xA5);
EFI_GUID_DEF(gEfiDiskIoProtocolGuid, 0xCE345171, 0xBA0B, 0x11D2, 0x8E, 0x4F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B);
EFI_GUID_DEF(gEfiSimplePointerProtocolGuid, 0x31878C87, 0x0B75, 0x11D5, 0x9A, 0x4B, 0x00, 0x20, 0xAF, 0x7A, 0x3B, 0xDC);
EFI_GUID_DEF(gEfiAbsolutePointerProtocolGuid, 0x8D59D32B, 0xC655, 0x4AE9, 0x9B, 0x15, 0xF2, 0x59, 0x04, 0x99, 0x2A, 0x43);
EFI_GUID_DEF(gEfiPciIoProtocolGuid, 0x4CF5B200, 0x68B8, 0x4CA5, 0x9E, 0xEC, 0xB2, 0x3E, 0x3F, 0x50, 0x02, 0x9A);
EFI_GUID_DEF(gEfiPciRootBridgeIoProtocolGuid, 0x2F707EBB, 0x4A1A, 0x11D4, 0x9A, 0x38, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D);
EFI_GUID_DEF(gEfiSimpleNetworkProtocolGuid, 0xA19832B9, 0xAC25, 0x11D3, 0x9A, 0x2D, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D);

/* ------------------------------------------------------------- tables */
typedef struct {
  UINT64 Signature;
  UINT32 Revision;
  UINT32 HeaderSize;
  UINT32 CRC32;
  UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct {
  UINT16 Year;
  UINT8  Month;
  UINT8  Day;
  UINT8  Hour;
  UINT8  Minute;
  UINT8  Second;
  UINT8  Pad1;
  UINT32 Nanosecond;
  INT16  TimeZone;
  UINT8  Daylight;
  UINT8  Pad2;
} EFI_TIME;

typedef enum {
  AllocateAnyPages,
  AllocateMaxAddress,
  AllocateAddress,
  MaxAllocateType
} EFI_ALLOCATE_TYPE;

typedef enum {
  EfiReservedMemoryType,
  EfiLoaderCode,
  EfiLoaderData,
  EfiBootServicesCode,
  EfiBootServicesData,
  EfiRuntimeServicesCode,
  EfiRuntimeServicesData,
  EfiConventionalMemory,
  EfiUnusableMemory,
  EfiACPIReclaimMemory,
  EfiACPIMemoryNVS,
  EfiMemoryMappedIO,
  EfiMemoryMappedIOPortSpace,
  EfiPalCode,
  EfiPersistentMemory,
  EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
  UINT32               Type;
  EFI_PHYSICAL_ADDRESS PhysicalStart;
  EFI_VIRTUAL_ADDRESS  VirtualStart;
  UINT64               NumberOfPages;
  UINT64               Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef enum { TimerCancel, TimerPeriodic, TimerRelative } EFI_TIMER_DELAY;

/* --------------------------------------------------------------- input */
typedef struct {
  UINT16 ScanCode;
  CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
  EFI_STATUS (EFIAPI *Reset)(struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
  EFI_STATUS (EFIAPI *ReadKeyStroke)(struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key);
  EFI_EVENT  WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

#define SCAN_UP        0x0001
#define SCAN_DOWN      0x0002
#define SCAN_RIGHT     0x0003
#define SCAN_LEFT      0x0004
#define SCAN_HOME      0x0005
#define SCAN_END       0x0006
#define SCAN_INSERT    0x0007
#define SCAN_DELETE    0x0008
#define SCAN_PAGE_UP   0x0009
#define SCAN_PAGE_DOWN 0x000A
#define SCAN_F1        0x000B
#define SCAN_F2        0x000C
#define SCAN_F3        0x000D
#define SCAN_F4        0x000E
#define SCAN_F5        0x000F
#define SCAN_F6        0x0010
#define SCAN_F7        0x0011
#define SCAN_F8        0x0012
#define SCAN_F9        0x0013
#define SCAN_F10       0x0014
#define SCAN_F11       0x0015
#define SCAN_F12       0x0016
#define SCAN_ESC       0x0017

/* --------------------------------------------------------------- output */
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
  EFI_STATUS (EFIAPI *Reset)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
  EFI_STATUS (EFIAPI *OutputString)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);
  EFI_STATUS (EFIAPI *TestString)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);
  EFI_STATUS (EFIAPI *QueryMode)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber, UINTN *Columns, UINTN *Rows);
  EFI_STATUS (EFIAPI *SetMode)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber);
  EFI_STATUS (EFIAPI *SetAttribute)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Attribute);
  EFI_STATUS (EFIAPI *ClearScreen)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);
  EFI_STATUS (EFIAPI *SetCursorPosition)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Column, UINTN Row);
  EFI_STATUS (EFIAPI *EnableCursor)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN Visible);
  VOID *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

/* ----------------------------------------------------------------- GOP */
typedef enum {
  PixelRedGreenBlueReserved8BitPerColor,
  PixelBlueGreenRedReserved8BitPerColor,
  PixelBitMask,
  PixelBltOnly,
  PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
  UINT32 RedMask;
  UINT32 GreenMask;
  UINT32 BlueMask;
  UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
  UINT32 Version;
  UINT32 HorizontalResolution;
  UINT32 VerticalResolution;
  EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
  EFI_PIXEL_BITMASK PixelInformation;
  UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
  UINT32 MaxMode;
  UINT32 Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  UINTN SizeOfInfo;
  EFI_PHYSICAL_ADDRESS FrameBufferBase;
  UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef enum { EfiBltVideoFill, EfiBltVideoToBltBuffer, EfiBltBufferToVideo, EfiBltVideoToVideo, EfiGraphicsOutputBltOperationMax } EFI_GRAPHICS_OUTPUT_BLT_OPERATION;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
  EFI_STATUS (EFIAPI *QueryMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 ModeNumber, UINTN *SizeOfInfo, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);
  EFI_STATUS (EFIAPI *SetMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 ModeNumber);
  EFI_STATUS (EFIAPI *Blt)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *This, VOID *BltBuffer, EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation,
                           UINTN SourceX, UINTN SourceY, UINTN DestinationX, UINTN DestinationY,
                           UINTN Width, UINTN Height, UINTN Delta);
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* --------------------------------------------------------- device path */
#pragma pack(1)
typedef struct {
  UINT8 Type;
  UINT8 SubType;
  UINT8 Length[2];
} EFI_DEVICE_PATH_PROTOCOL;
#pragma pack()

/* -------------------------------------------------------- loaded image */
typedef struct {
  UINT32 Revision;
  EFI_HANDLE ParentHandle;
  VOID *SystemTable;
  EFI_HANDLE DeviceHandle;
  EFI_DEVICE_PATH_PROTOCOL *FilePath;
  VOID *Reserved;
  UINT32 LoadOptionsSize;
  VOID *LoadOptions;
  VOID *ImageBase;
  UINT64 ImageSize;
  EFI_MEMORY_TYPE ImageCodeType;
  EFI_MEMORY_TYPE ImageDataType;
  EFI_STATUS (EFIAPI *Unload)(EFI_HANDLE ImageHandle);
} EFI_LOADED_IMAGE_PROTOCOL;

/* ------------------------------------------------------- simple pointer */
typedef struct {
  INT32 RelativeMovementX;
  INT32 RelativeMovementY;
  INT32 RelativeMovementZ;
  BOOLEAN LeftButton;
  BOOLEAN RightButton;
} EFI_SIMPLE_POINTER_STATE;

typedef struct {
  UINT64 ResolutionX;
  UINT64 ResolutionY;
  UINT64 ResolutionZ;
  BOOLEAN LeftButton;
  BOOLEAN RightButton;
} EFI_SIMPLE_POINTER_MODE;

typedef struct EFI_SIMPLE_POINTER_PROTOCOL {
  EFI_STATUS (EFIAPI *Reset)(struct EFI_SIMPLE_POINTER_PROTOCOL *This, BOOLEAN ExtendedVerification);
  EFI_STATUS (EFIAPI *GetState)(struct EFI_SIMPLE_POINTER_PROTOCOL *This, EFI_SIMPLE_POINTER_STATE *State);
  EFI_EVENT WaitForInput;
  EFI_SIMPLE_POINTER_MODE *Mode;
} EFI_SIMPLE_POINTER_PROTOCOL;

/* --------------------------------------------------------- MP services */
typedef struct {
  UINT32 ProcessorId;
  UINT32 StatusFlag;
  UINT32 Location;
} EFI_PROCESSOR_INFORMATION;

typedef VOID (EFIAPI *EFI_AP_PROCEDURE)(IN VOID *Buffer);

typedef struct EFI_MP_SERVICES_PROTOCOL {
  EFI_STATUS (EFIAPI *GetNumberOfProcessors)(struct EFI_MP_SERVICES_PROTOCOL *This,
                                             UINTN *NumberOfProcessors, UINTN *NumberOfEnabledProcessors);
  EFI_STATUS (EFIAPI *GetProcessorInfo)(struct EFI_MP_SERVICES_PROTOCOL *This,
                                        UINTN ProcessorNumber, EFI_PROCESSOR_INFORMATION *ProcessorInfoBuffer);
  EFI_STATUS (EFIAPI *StartupAllAPs)(struct EFI_MP_SERVICES_PROTOCOL *This,
                                     EFI_AP_PROCEDURE Procedure, BOOLEAN SingleThread,
                                     EFI_EVENT WaitEvent, UINTN TimeoutInMicroseconds,
                                     VOID *ProcedureArgument, UINTN **FailedCpuList);
  EFI_STATUS (EFIAPI *StartupThisAP)(struct EFI_MP_SERVICES_PROTOCOL *This,
                                     EFI_AP_PROCEDURE Procedure, UINTN ProcessorNumber,
                                     EFI_EVENT WaitEvent, UINTN TimeoutInMicroseconds,
                                     VOID *ProcedureArgument, BOOLEAN *Finished);
  EFI_STATUS (EFIAPI *SwitchBSP)(VOID *a, VOID *b);
  EFI_STATUS (EFIAPI *EnableDisableAP)(VOID *a, VOID *b, VOID *c, VOID *d);
  EFI_STATUS (EFIAPI *WhoAmI)(struct EFI_MP_SERVICES_PROTOCOL *This, UINTN *ProcessorNumber);
} EFI_MP_SERVICES_PROTOCOL;

/* --------------------------------------------------------------- PCI IO */
typedef struct EFI_PCI_IO_PROTOCOL EFI_PCI_IO_PROTOCOL;

typedef struct {
  EFI_STATUS (EFIAPI *Read)(EFI_PCI_IO_PROTOCOL *This, UINT32 Width, UINT8 BarIndex, UINT64 Offset,
                            UINTN Count, VOID *Buffer);
  EFI_STATUS (EFIAPI *Write)(EFI_PCI_IO_PROTOCOL *This, UINT32 Width, UINT8 BarIndex, UINT64 Offset,
                             UINTN Count, VOID *Buffer);
} EFI_PCI_IO_ACCESS;

typedef struct {
  EFI_STATUS (EFIAPI *Read)(EFI_PCI_IO_PROTOCOL *This, UINT32 Width, UINT32 Offset, UINTN Count,
                            VOID *Buffer);
  EFI_STATUS (EFIAPI *Write)(EFI_PCI_IO_PROTOCOL *This, UINT32 Width, UINT32 Offset, UINTN Count,
                             VOID *Buffer);
} EFI_PCI_IO_CONFIG_ACCESS;

struct EFI_PCI_IO_PROTOCOL {
  EFI_PCI_IO_ACCESS PollMem;
  EFI_PCI_IO_ACCESS PollIo;
  EFI_PCI_IO_ACCESS Mem;
  EFI_PCI_IO_ACCESS Io;
  EFI_PCI_IO_CONFIG_ACCESS Pci;
  VOID *CopyMem;
  VOID *Map;
  VOID *Unmap;
  VOID *AllocateBuffer;
  VOID *FreeBuffer;
  VOID *Flush;
  EFI_STATUS (EFIAPI *GetLocation)(EFI_PCI_IO_PROTOCOL *This, UINTN *SegmentNumber,
                                   UINTN *BusNumber, UINTN *DeviceNumber, UINTN *FunctionNumber);
};

/* ----------------------------------------------------- PCI root bridge IO */
typedef enum { EfiPciWidthUint8, EfiPciWidthUint16, EfiPciWidthUint32, EfiPciWidthUint64,
               EfiPciWidthFifoUint8, EfiPciWidthFifoUint16, EfiPciWidthFifoUint32,
               EfiPciWidthFifoUint64, EfiPciWidthFillUint8, EfiPciWidthFillUint16,
               EfiPciWidthFillUint32, EfiPciWidthFillUint64 } EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH;

typedef struct EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL;

typedef struct {
  EFI_STATUS (EFIAPI *Read)(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *This,
                            EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH Width, UINT64 Address,
                            UINTN Count, VOID *Buffer);
  EFI_STATUS (EFIAPI *Write)(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *This,
                             EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH Width, UINT64 Address,
                             UINTN Count, VOID *Buffer);
} EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_PCI_ACCESS;

/* Only Pci.Read and SegmentNumber are used; the remaining members keep the
   layout identical to the firmware structure so the offsets stay valid. */
struct EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL {
  EFI_HANDLE                             ParentHandle;
  struct { VOID *Read; VOID *Write; } PollMem;
  struct { VOID *Read; VOID *Write; } PollIo;
  struct { VOID *Read; VOID *Write; } Mem;
  struct { VOID *Read; VOID *Write; } Io;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_PCI_ACCESS Pci;
  VOID *CopyMem;
  VOID *Map;
  VOID *Unmap;
  VOID *AllocateBuffer;
  VOID *FreeBuffer;
  VOID *Flush;
  VOID *GetAttributes;
  VOID *SetAttributes;
  VOID *Configuration;
  UINT32 SegmentNumber;
};

/* ------------------------------------------------------- simple network */
typedef struct {
  UINT8 Addr[32];
} EFI_MAC_ADDRESS;

typedef struct {
  UINT32  State;
  UINT32  HwAddressSize;
  UINT32  MediaHeaderSize;
  UINT32  MaxPacketSize;
  UINT32  NvRamSize;
  UINT32  NvRamAccessSize;
  UINT32  ReceiveFilterMask;
  UINT32  ReceiveFilterSetting;
  UINT32  MaxMCastFilterCount;
  UINT32  MCastFilterCount;
  EFI_MAC_ADDRESS MCastFilter[16];
  EFI_MAC_ADDRESS CurrentAddress;
  EFI_MAC_ADDRESS BroadcastAddress;
  EFI_MAC_ADDRESS PermanentAddress;
  UINT8   IfType;
  BOOLEAN MacAddressChangeable;
  BOOLEAN MultipleTxSupported;
  BOOLEAN MediaPresentSupported;
  BOOLEAN MediaPresent;
} EFI_SIMPLE_NETWORK_MODE;

typedef struct EFI_SIMPLE_NETWORK_PROTOCOL {
  UINT64 Revision;
  EFI_STATUS (EFIAPI *Start)(struct EFI_SIMPLE_NETWORK_PROTOCOL *This);
  EFI_STATUS (EFIAPI *Stop)(struct EFI_SIMPLE_NETWORK_PROTOCOL *This);
  EFI_STATUS (EFIAPI *Initialize)(struct EFI_SIMPLE_NETWORK_PROTOCOL *This, UINTN ExtraRxBuf, UINTN ExtraTxBuf);
  EFI_STATUS (EFIAPI *Reset)(struct EFI_SIMPLE_NETWORK_PROTOCOL *This, BOOLEAN ExtendedVerification);
  EFI_STATUS (EFIAPI *Shutdown)(struct EFI_SIMPLE_NETWORK_PROTOCOL *This);
  EFI_STATUS (EFIAPI *ReceiveFilters)(struct EFI_SIMPLE_NETWORK_PROTOCOL *This, UINT32 Enable, UINT32 Disable, BOOLEAN ResetMCast, UINTN MCastCount, EFI_MAC_ADDRESS *MCast);
  EFI_STATUS (EFIAPI *StationAddress)(struct EFI_SIMPLE_NETWORK_PROTOCOL *This, BOOLEAN Reset, EFI_MAC_ADDRESS *New);
  EFI_STATUS (EFIAPI *Statistics)(struct EFI_SIMPLE_NETWORK_PROTOCOL *This, BOOLEAN Reset, UINTN *Size, VOID *Table);
  EFI_STATUS (EFIAPI *MCastIpToMac)(struct EFI_SIMPLE_NETWORK_PROTOCOL *This, BOOLEAN IPv6, UINT32 *Ip, EFI_MAC_ADDRESS *Mac);
  EFI_STATUS (EFIAPI *NvData)(struct EFI_SIMPLE_NETWORK_PROTOCOL *This, BOOLEAN Read, UINTN Offset, UINTN Size, VOID *Buffer);
  EFI_STATUS (EFIAPI *GetStatus)(struct EFI_SIMPLE_NETWORK_PROTOCOL *This, UINT32 *IntStatus, VOID **TxBuf);
  EFI_STATUS (EFIAPI *Transmit)(struct EFI_SIMPLE_NETWORK_PROTOCOL *This, UINTN HdrSize, UINTN BufSize, VOID *Buf, EFI_MAC_ADDRESS *Src, EFI_MAC_ADDRESS *Dst, UINT16 *Proto);
  EFI_STATUS (EFIAPI *Receive)(struct EFI_SIMPLE_NETWORK_PROTOCOL *This, UINTN *HdrSize, UINTN *BufSize, VOID *Buf, EFI_MAC_ADDRESS *Src, EFI_MAC_ADDRESS *Dst, UINT16 *Proto);
  EFI_STATUS (EFIAPI *WaitForPacket)(struct EFI_SIMPLE_NETWORK_PROTOCOL *This, EFI_EVENT Event);
  EFI_SIMPLE_NETWORK_MODE *Mode;
} EFI_SIMPLE_NETWORK_PROTOCOL;

/* ---------------------------------------------------- pointer devices */
typedef struct {
  UINT64 AbsoluteMinX;
  UINT64 AbsoluteMinY;
  UINT64 AbsoluteMinZ;
  UINT64 AbsoluteMaxX;
  UINT64 AbsoluteMaxY;
  UINT64 AbsoluteMaxZ;
  UINT32 Attributes;
} EFI_ABSOLUTE_POINTER_MODE;

typedef struct {
  UINT64 CurrentX;
  UINT64 CurrentY;
  UINT64 CurrentZ;
  UINT32 ActiveButtons;
} EFI_ABSOLUTE_POINTER_STATE;

/* member order matters: edk2 puts Reset before GetState in both pointer
   protocols, which is exactly the kind of thing a hand written header gets
   wrong (see the PCI_IO offset probe) */
typedef struct EFI_ABSOLUTE_POINTER_PROTOCOL {
  EFI_STATUS (EFIAPI *Reset)(struct EFI_ABSOLUTE_POINTER_PROTOCOL *This,
                             BOOLEAN ExtendedVerification);
  EFI_STATUS (EFIAPI *GetState)(struct EFI_ABSOLUTE_POINTER_PROTOCOL *This,
                                EFI_ABSOLUTE_POINTER_STATE *State);
  EFI_EVENT  WaitForInput;
  EFI_ABSOLUTE_POINTER_MODE *Mode;
} EFI_ABSOLUTE_POINTER_PROTOCOL;

/* --------------------------------------------------------------- SMBIOS */
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT16  Handle;
} SMBIOS_STRUCTURE;

typedef struct {
  UINT8   AnchorString[4];
  UINT8   EntryPointStructureChecksum;
  UINT8   EntryPointLength;
  UINT8   MajorVersion;
  UINT8   MinorVersion;
  UINT16  MaxStructureSize;
  UINT8   EntryPointRevision;
  UINT8   FormattedArea[5];
  UINT8   IntermediateAnchorString[5];
  UINT8   IntermediateChecksum;
  UINT16  TableLength;
  UINT32  TableAddress;
  UINT16  NumberOfSmbiosStructures;
  UINT8   SmbiosBcdRevision;
} SMBIOS_ENTRY_POINT_32;

typedef struct {
  UINT8   AnchorString[5];
  UINT8   EntryPointStructureChecksum;
  UINT8   EntryPointLength;
  UINT8   MajorVersion;
  UINT8   MinorVersion;
  UINT8   Docrev;
  UINT8   EntryPointRevision;
  UINT8   Reserved;
  UINT32  StructureTableMaximumSize;
  UINT64  TableAddress;
} SMBIOS_ENTRY_POINT_64;

#pragma pack(1)
typedef struct {
  UINT8  Type;              /* 17 */
  UINT8  Length;
  UINT16 Handle;
  UINT16 PhysicalMemoryArrayHandle;
  UINT16 MemoryErrorInfoHandle;
  UINT16 TotalWidth;
  UINT16 DataWidth;
  UINT16 Size;
  UINT8  FormFactor;
  UINT8  DeviceSet;
  UINT8  DeviceLocator;
  UINT8  BankLocator;
  UINT8  MemoryType;
  UINT16 TypeDetail;
  UINT16 Speed;
  UINT8  Manufacturer;
  UINT8  SerialNumber;
  UINT8  AssetTag;
  UINT8  PartNumber;
  UINT8  Attributes;
  UINT32 ExtendedSize;
  UINT16 ConfiguredMemorySpeed;
  UINT16 MinimumVoltage;
  UINT16 MaximumVoltage;
  UINT16 ConfiguredVoltage;
  UINT8  MemoryTechnology;
  UINT16 MemoryOperatingModeCapability;
  UINT8  FirmwareVersion;
  UINT16 ModuleManufacturerId;
  UINT16 ModuleProductId;
  UINT16 MemorySubsystemControllerManufacturerId;
  UINT16 MemorySubsystemControllerProductId;
  UINT64 NonVolatileSize;
  UINT64 VolatileSize;
  UINT64 CacheSize;
  UINT64 LogicalSize;
} SMBIOS_TYPE17;

typedef struct {
  UINT8  Type;              /* 16 */
  UINT8  Length;
  UINT16 Handle;
  UINT8  Location;
  UINT8  Use;
  UINT8  MemoryErrorCorrection;
  UINT32 MaximumCapacity;
  UINT16 MemoryErrorInfoHandle;
  UINT16 NumberOfMemoryDevices;
  UINT64 ExtendedMaximumCapacity;
} SMBIOS_TYPE16;

typedef struct {
  UINT8  Type;              /* 4 */
  UINT8  Length;
  UINT16 Handle;
  UINT8  Socket;
  UINT8  ProcessorType;
  UINT8  ProcessorFamily;
  UINT8  ProcessorManufacturer;
  UINT64 ProcessorId;
  UINT8  ProcessorVersion;
  UINT8  Voltage;
  UINT16 ExternalClock;
  UINT16 MaxSpeed;
  UINT16 CurrentSpeed;
  UINT8  Status;
  UINT8  ProcessorUpgrade;
  UINT16 L1CacheHandle;
  UINT16 L2CacheHandle;
  UINT16 L3CacheHandle;
  UINT8  SerialNumber;
  UINT8  AssetTag;
  UINT8  PartNumber;
  UINT8  CoreCount;
  UINT8  CoreEnabled;
  UINT8  ThreadCount;
  UINT16 ProcessorCharacteristics;
  UINT16 ProcessorFamily2;
  UINT16 CoreCount2;
  UINT16 CoreEnabled2;
  UINT16 ThreadCount2;
} SMBIOS_TYPE4;

typedef struct {
  UINT8  Type;              /* 19 */
  UINT8  Length;
  UINT16 Handle;
  UINT32 StartingAddress;
  UINT32 EndingAddress;
  UINT16 MemoryArrayHandle;
  UINT8  PartitionWidth;
  UINT64 ExtendedStartingAddress;
  UINT64 ExtendedEndingAddress;
} SMBIOS_TYPE19;
#pragma pack()

typedef struct {
  UINT8  Type;
  UINT8  Length;
  UINT16 Handle;
  UINT8  Vendor;
  UINT8  BiosVersion;
  UINT16 BiosSegment;
  UINT8  BiosReleaseDate;
  UINT8  BiosSize;
  UINT64 BiosCharacteristics;
  UINT8  Extension[2];
  UINT8  MajorRelease;
  UINT8  MinorRelease;
  UINT8  EmbeddedControllerMajor;
  UINT8  EmbeddedControllerMinor;
  UINT16 ExtendedBiosSize;
} SMBIOS_TYPE0;

typedef struct {
  UINT8  Type;              /* 1 */
  UINT8  Length;
  UINT16 Handle;
  UINT8  Manufacturer;
  UINT8  ProductName;
  UINT8  Version;
  UINT8  SerialNumber;
  EFI_GUID Uuid;
  UINT8  WakeUpType;
  UINT8  SkuNumber;
  UINT8  Family;
} SMBIOS_TYPE1;

typedef struct {
  UINT8  Type;              /* 2 */
  UINT8  Length;
  UINT16 Handle;
  UINT8  Manufacturer;
  UINT8  ProductName;
  UINT8  Version;
  UINT8  SerialNumber;
  UINT8  AssetTag;
  UINT8  FeatureFlags;
  UINT8  LocationInChassis;
  UINT16 ChassisHandle;
  UINT8  BoardType;
  UINT8  NumberOfContainedObjectHandles;
} SMBIOS_TYPE2;

typedef enum {
  EfiSmbiosTypeBios = 0,
  EfiSmbiosTypeSystemInformation = 1,
  EfiSmbiosTypeSystemEnclosure = 3,
  EfiSmbiosTypeProcessor = 4,
  EfiSmbiosTypeCache = 7,
  EfiSmbiosTypePhysicalMemoryArray = 16,
  EfiSmbiosTypeMemoryDevice = 17,
  EfiSmbiosTypeMemoryArrayMappedAddress = 19,
  EfiSmbiosTypeMemoryDeviceMappedAddress = 20
} EFI_SMBIOS_TYPE;

typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT16  Handle;
} EFI_SMBIOS_TABLE_HEADER;

typedef struct EFI_SMBIOS_PROTOCOL {
  EFI_STATUS (EFIAPI *Add)(struct EFI_SMBIOS_PROTOCOL *This, EFI_HANDLE ProducerHandle, EFI_SMBIOS_TABLE_HEADER *Record, UINTN *RecordHandle);
  EFI_STATUS (EFIAPI *UpdateString)(struct EFI_SMBIOS_PROTOCOL *This, EFI_SMBIOS_TABLE_HEADER *Record, UINTN *StringNumber, CHAR8 *String);
  EFI_STATUS (EFIAPI *Remove)(struct EFI_SMBIOS_PROTOCOL *This, UINTN *RecordHandle);
  EFI_STATUS (EFIAPI *GetNext)(struct EFI_SMBIOS_PROTOCOL *This, UINT16 *RecordHandle,
                               UINT8 *Type, EFI_SMBIOS_TABLE_HEADER **Record,
                               EFI_HANDLE *ProducerHandle);
  UINT8 MajorVersion;
  UINT8 MinorVersion;
} EFI_SMBIOS_PROTOCOL;

/* --------------------------------------------------------------- files */
typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

struct EFI_FILE_PROTOCOL {
  UINT64 Revision;
  EFI_STATUS (EFIAPI *Open)(EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **NewHandle, CHAR16 *FileName, UINT64 OpenMode, UINT64 Attributes);
  EFI_STATUS (EFIAPI *Close)(EFI_FILE_PROTOCOL *This);
  EFI_STATUS (EFIAPI *Delete)(EFI_FILE_PROTOCOL *This);
  EFI_STATUS (EFIAPI *Read)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer);
  EFI_STATUS (EFIAPI *Write)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer);
  EFI_STATUS (EFIAPI *GetPosition)(EFI_FILE_PROTOCOL *This, UINT64 *Position);
  EFI_STATUS (EFIAPI *SetPosition)(EFI_FILE_PROTOCOL *This, UINT64 Position);
  EFI_STATUS (EFIAPI *GetInfo)(EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType, UINTN *BufferSize, VOID *Buffer);
  EFI_STATUS (EFIAPI *SetInfo)(EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType, UINTN BufferSize, VOID *Buffer);
  EFI_STATUS (EFIAPI *Flush)(EFI_FILE_PROTOCOL *This);
};

#define EFI_FILE_MODE_READ    0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE   0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE  0x8000000000000000ULL
#define EFI_FILE_DIRECTORY    0x0000000000000010ULL

typedef struct {
  UINT64 Size;
  UINT64 FileSize;
  UINT64 PhysicalSize;
  EFI_TIME CreateTime;
  EFI_TIME LastAccessTime;
  EFI_TIME ModificationTime;
  UINT64 Attribute;
  CHAR16 FileName[1];
} EFI_FILE_INFO;

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
  UINT64 Revision;
  EFI_STATUS (EFIAPI *OpenVolume)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root);
};

/* ------------------------------------------------------------ block io */
typedef struct {
  UINT32 MediaId;
  BOOLEAN RemovableMedia;
  BOOLEAN MediaPresent;
  BOOLEAN LogicalPartition;
  BOOLEAN ReadOnly;
  BOOLEAN WriteCaching;
  UINT32 BlockSize;
  UINT32 IoAlign;
  EFI_LBA LastBlock;
  EFI_LBA LowestAlignedLba;
  UINT32 LogicalBlocksPerPhysicalBlock;
  UINT32 OptimalTransferLengthGranularity;
} EFI_BLOCK_IO_MEDIA;

typedef struct EFI_BLOCK_IO_PROTOCOL {
  UINT64 Revision;
  EFI_BLOCK_IO_MEDIA *Media;
  EFI_STATUS (EFIAPI *Reset)(struct EFI_BLOCK_IO_PROTOCOL *This, BOOLEAN ExtendedVerification);
  EFI_STATUS (EFIAPI *ReadBlocks)(struct EFI_BLOCK_IO_PROTOCOL *This, UINT32 MediaId, EFI_LBA Lba, UINTN BufferSize, VOID *Buffer);
  EFI_STATUS (EFIAPI *WriteBlocks)(struct EFI_BLOCK_IO_PROTOCOL *This, UINT32 MediaId, EFI_LBA Lba, UINTN BufferSize, VOID *Buffer);
  EFI_STATUS (EFIAPI *FlushBlocks)(struct EFI_BLOCK_IO_PROTOCOL *This);
} EFI_BLOCK_IO_PROTOCOL;

/* ------------------------------------------------------ boot services */
typedef VOID (EFIAPI *EFI_EVENT_NOTIFY)(EFI_EVENT Event, VOID *Context);

typedef struct {
  EFI_TABLE_HEADER Hdr;
  EFI_TPL (EFIAPI *RaiseTPL)(EFI_TPL NewTpl);
  VOID (EFIAPI *RestoreTPL)(EFI_TPL OldTpl);
  EFI_STATUS (EFIAPI *AllocatePages)(EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType, UINTN Pages, EFI_PHYSICAL_ADDRESS *Memory);
  EFI_STATUS (EFIAPI *FreePages)(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);
  EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN *MemoryMapSize, EFI_MEMORY_DESCRIPTOR *MemoryMap, UINTN *MapKey, UINTN *DescriptorSize, UINT32 *DescriptorVersion);
  EFI_STATUS (EFIAPI *AllocatePool)(EFI_MEMORY_TYPE PoolType, UINTN Size, VOID **Buffer);
  EFI_STATUS (EFIAPI *FreePool)(VOID *Buffer);
  EFI_STATUS (EFIAPI *CreateEvent)(UINT32 Type, EFI_TPL NotifyTpl, EFI_EVENT_NOTIFY NotifyFunction, VOID *NotifyContext, EFI_EVENT *Event);
  EFI_STATUS (EFIAPI *SetTimer)(EFI_EVENT Event, EFI_TIMER_DELAY Type, UINT64 TriggerTime);
  EFI_STATUS (EFIAPI *WaitForEvent)(UINTN NumberOfEvents, EFI_EVENT *Event, UINTN *Index);
  EFI_STATUS (EFIAPI *SignalEvent)(EFI_EVENT Event);
  EFI_STATUS (EFIAPI *CloseEvent)(EFI_EVENT Event);
  EFI_STATUS (EFIAPI *CheckEvent)(EFI_EVENT Event);
  EFI_STATUS (EFIAPI *InstallProtocolInterface)(EFI_HANDLE *Handle, EFI_GUID *Protocol, UINTN InterfaceType, VOID *Interface);
  EFI_STATUS (EFIAPI *ReinstallProtocolInterface)(EFI_HANDLE Handle, EFI_GUID *Protocol, VOID *OldInterface, VOID *NewInterface);
  EFI_STATUS (EFIAPI *UninstallProtocolInterface)(EFI_HANDLE Handle, EFI_GUID *Protocol, VOID *Interface);
  EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **Interface);
  VOID *Reserved;
  EFI_STATUS (EFIAPI *RegisterProtocolNotify)(EFI_GUID *Protocol, EFI_EVENT Event, VOID **Registration);
  EFI_STATUS (EFIAPI *LocateHandle)(UINTN SearchType, EFI_GUID *Protocol, VOID *SearchKey, UINTN *BufferSize, EFI_HANDLE *Buffer);
  EFI_STATUS (EFIAPI *LocateDevicePath)(EFI_GUID *Protocol, EFI_DEVICE_PATH_PROTOCOL **DevicePath, EFI_HANDLE *Device);
  EFI_STATUS (EFIAPI *InstallConfigurationTable)(EFI_GUID *Guid, VOID *Table);
  EFI_STATUS (EFIAPI *LoadImage)(BOOLEAN BootPolicy, EFI_HANDLE ParentImageHandle, EFI_DEVICE_PATH_PROTOCOL *DevicePath, VOID *SourceBuffer, UINTN SourceSize, EFI_HANDLE *ImageHandle);
  EFI_STATUS (EFIAPI *StartImage)(EFI_HANDLE ImageHandle, UINTN *ExitDataSize, CHAR16 **ExitData);
  EFI_STATUS (EFIAPI *Exit)(EFI_HANDLE ImageHandle, EFI_STATUS ExitStatus, UINTN ExitDataSize, CHAR16 *ExitData);
  EFI_STATUS (EFIAPI *UnloadImage)(EFI_HANDLE ImageHandle);
  EFI_STATUS (EFIAPI *ExitBootServices)(EFI_HANDLE ImageHandle, UINTN MapKey);
  EFI_STATUS (EFIAPI *GetNextMonotonicCount)(UINT64 *Count);
  EFI_STATUS (EFIAPI *Stall)(UINTN Microseconds);
  EFI_STATUS (EFIAPI *SetWatchdogTimer)(UINTN Timeout, UINT64 WatchdogCode, UINTN DataSize, CHAR16 *WatchdogData);
  EFI_STATUS (EFIAPI *ConnectController)(EFI_HANDLE ControllerHandle, EFI_HANDLE *DriverImageHandle, EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath, BOOLEAN Recursive);
  EFI_STATUS (EFIAPI *DisconnectController)(EFI_HANDLE ControllerHandle, EFI_HANDLE DriverImageHandle, EFI_HANDLE ChildHandle);
  EFI_STATUS (EFIAPI *OpenProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **Interface, EFI_HANDLE AgentHandle, EFI_HANDLE ControllerHandle, UINT32 Attributes);
  EFI_STATUS (EFIAPI *CloseProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol, EFI_HANDLE AgentHandle, EFI_HANDLE ControllerHandle);
  EFI_STATUS (EFIAPI *OpenProtocolInformation)(EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **EntryBuffer, UINTN *EntryCount);
  EFI_STATUS (EFIAPI *ProtocolsPerHandle)(EFI_HANDLE Handle, EFI_GUID ***ProtocolBuffer, UINTN *ProtocolBufferCount);
  EFI_STATUS (EFIAPI *LocateHandleBuffer)(UINTN SearchType, EFI_GUID *Protocol, VOID *SearchKey, UINTN *NoHandles, EFI_HANDLE **Buffer);
  EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID *Protocol, VOID *Registration, VOID **Interface);
  EFI_STATUS (EFIAPI *InstallMultipleProtocolInterfaces)(EFI_HANDLE *Handle, ...);
  EFI_STATUS (EFIAPI *UninstallMultipleProtocolInterfaces)(EFI_HANDLE Handle, ...);
  EFI_STATUS (EFIAPI *CalculateCrc32)(VOID *Data, UINTN DataSize, UINT32 *Crc32);
  VOID (EFIAPI *CopyMem)(VOID *Destination, VOID *Source, UINTN Length);
  VOID (EFIAPI *SetMem)(VOID *Buffer, UINTN Size, UINT8 Value);
  EFI_STATUS (EFIAPI *CreateEventEx)(UINT32 Type, EFI_TPL NotifyTpl, EFI_EVENT_NOTIFY NotifyFunction, VOID *NotifyContext, EFI_GUID *EventGroup, EFI_EVENT *Event);
} EFI_BOOT_SERVICES;

typedef struct {
  EFI_TABLE_HEADER Hdr;
  EFI_STATUS (EFIAPI *GetTime)(EFI_TIME *Time, VOID *Capabilities);
  EFI_STATUS (EFIAPI *SetTime)(EFI_TIME *Time);
  EFI_STATUS (EFIAPI *GetWakeupTime)(BOOLEAN *Enabled, BOOLEAN *Pending, EFI_TIME *Time);
  EFI_STATUS (EFIAPI *SetWakeupTime)(BOOLEAN Enable, EFI_TIME *Time);
  EFI_STATUS (EFIAPI *SetVirtualAddressMap)(UINTN MemoryMapSize, UINTN DescriptorSize, UINT32 DescriptorVersion, EFI_MEMORY_DESCRIPTOR *VirtualMap);
  EFI_STATUS (EFIAPI *ConvertPointer)(UINTN DebugDisposition, VOID **Address);
  EFI_STATUS (EFIAPI *GetVariable)(CHAR16 *VariableName, EFI_GUID *VendorGuid, UINT32 *Attributes, UINTN *DataSize, VOID *Data);
  EFI_STATUS (EFIAPI *GetNextVariableName)(UINTN *VariableNameSize, CHAR16 *VariableName, EFI_GUID *VendorGuid);
  EFI_STATUS (EFIAPI *SetVariable)(CHAR16 *VariableName, EFI_GUID *VendorGuid, UINT32 Attributes, UINTN DataSize, VOID *Data);
  EFI_STATUS (EFIAPI *GetNextHighMonotonicCount)(UINT32 *HighCount);
  VOID (EFIAPI *ResetSystem)(UINT32 ResetType, EFI_STATUS ResetStatus, UINTN DataSize, VOID *ResetData);
} EFI_RUNTIME_SERVICES;

typedef struct {
  EFI_GUID VendorGuid;
  VOID *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
  EFI_TABLE_HEADER Hdr;
  CHAR16 *FirmwareVendor;
  UINT32 FirmwareRevision;
  EFI_HANDLE ConsoleInHandle;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
  EFI_HANDLE ConsoleOutHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
  EFI_HANDLE StandardErrorHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
  EFI_RUNTIME_SERVICES *RuntimeServices;
  EFI_BOOT_SERVICES *BootServices;
  UINTN NumberOfTableEntries;
  EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

#define EFI_SYSTEM_TABLE_SIGNATURE 0x5453595320494249ULL
#define EFI_REVISION(a, b) (((a) << 16) | (b))

/* EFI_RESET_TYPE */
#define EfiResetCold 0
#define EfiResetWarm 1
#define EfiResetShutdown 2

#endif /* UCTS_UEFI_H */
