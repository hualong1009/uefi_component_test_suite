/* pci.h - PCI/PCIe device tree built from config space and device paths. */
#ifndef UCTS_PCI_H
#define UCTS_PCI_H

#include "uefi.h"

#define PCI_MAX_DEVS   192
#define PCI_MAX_NETS   8
#define PCI_NO_INDEX   0xFF

/* device kinds drive the icon and the label used in the topology view */
enum {
  PCI_KIND_ROOT = 0,
  PCI_KIND_BRIDGE,
  PCI_KIND_STORAGE,
  PCI_KIND_NET,
  PCI_KIND_DISPLAY,
  PCI_KIND_USB,
  PCI_KIND_AUDIO,
  PCI_KIND_SERIAL,
  PCI_KIND_OTHER,
  PCI_KIND_COUNT
};

typedef struct {
  UINT16  Segment;
  UINT8   Bus;
  UINT8   Dev;
  UINT8   Func;
  UINT16  VendorId;
  UINT16  DeviceId;
  UINT8   Class;
  UINT8   SubClass;
  UINT8   ProgIf;
  UINT8   Revision;
  UINT8   Kind;
  UINT8   IsBridge;
  UINT8   IsRoot;
  UINT8   SecondaryBus;
  UINT8   SubordinateBus;
  UINT8   LinkGen;          /* negotiated PCIe link generation, 0 = unknown */
  UINT8   LinkWidth;        /* negotiated lane count, 0 = unknown            */
  UINT32  Parent;           /* index of the parent node, PCI_NO_INDEX = root  */
  UINT8   DiskIndex;        /* attached block device index or PCI_NO_INDEX    */
  UINT8   NetIndex;         /* attached network interface index              */
  char    Name[44];         /* controller / class label shown in the tree     */
} PCI_DEV;

typedef struct {
  BOOLEAN Present;
  BOOLEAN MediaPresent;
  UINT8   Mac[6];
  UINT8   PciIndex;
  char    Name[32];
} NET_DEV;

VOID     PciEnumerate(VOID);
VOID     PciBindAssets(VOID);

UINT32   PciCount(VOID);
UINT32   PciSkipped(VOID);
PCI_DEV *PciAt(UINT32 index);
UINT32   PciRootCount(VOID);
UINT32   PciHostBridgeCount(VOID);
UINT32   PciEndpointCount(VOID);
UINT32   PciClassCount(UINT8 kind);
UINT32   NetCount(VOID);
NET_DEV *NetAt(UINT32 index);

const char *PciKindName(UINT8 kind);
const char *PciVendorName(UINT16 vid);
const char *PciClassText(const PCI_DEV *dev);
VOID     PciFormatAddress(const PCI_DEV *dev, char *buf, UINTN cap);
VOID     PciFormatMac(const UINT8 *mac, char *buf, UINTN cap);
BOOLEAN  PciHasLink(const PCI_DEV *dev);
UINT32   PciDepth(const PCI_DEV *dev);

#endif
