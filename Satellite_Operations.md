# Satellite operations

## Periodic Location tracking

In NTN mode, the gNB 

- every 1s, the gNB computes its own location
- every 15s, the gNB computes the location of all Sats in the constellation, and determines the closest (50) sats as the "neighborhood".

## Handover Logic

### gNB

- On UE connection to a gNB, the gNB calculates the time of exit of gNB coverage (t_exit) based on elevation angle.
- gNB then calculates neighbor gNBs that will be in coverage range of UE at t_exit.
- gNB determines "best" candidates for next UE handover.  Currently, "best" means longest transit time in coverage range (elevation angle > threshold).
- gNB performs conditional handover procedure for selected best candidate(s).  (normal CHO)
- candidate gNBs do admission control, accept or reject UE and respond to source gNB.  (normal CHO)
- source gNB sends RRC Reconfiguration message(s) to UE with Conditional Handover instruction with condT1 event using t_exit time as the trigger time.  (normal CHO)

### UE

- UE stores conditional handover candidates from gNB received in RRC Reconfiguration messages. (normal CHO)
- UE periodically evaluates event trigger conditions. (normal CHO)
- when trigger condition is met, pick best gNB from UE's perspective
- how: use SIB19 information to select best sat and/or use experienced RSRP (avoids local occlusions)