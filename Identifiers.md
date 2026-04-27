5G Identifiers

gNB:

PCI = Physical Cell Id : 10-bits (number between 0-1007)
NCI = NR Cell Identity : 36-bit value
- gNB ID = high bits of the NCI (the split is operator selectable: 22-32 bit)
- CellId = low bits of the NCI

NCI is globally unique.
gNB ID is intended to identify equipment, while CellId is intended to identify sectors (cells) gNB provides service to.
PCI is only locally unique, used at radio layers to differentiate signals.
UE learns PCI-NCI mapping through SIB1

UE:

IMEI = International Mobile Equipment Identity : unique id for a hardware device
IMSI = International Mobile Subscriber Identity : unique id for a user account
cRNTI


SCTP

StreamId

NGAP

RAN-UE-NGAP-ID: assigned by gNB for UE
AMF-UE-NGAP-ID: assigned by AMF for UE (per GNB.  On handover, new id assigned)

UE