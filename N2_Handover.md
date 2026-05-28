# N2/N3 Interface

The N2 interface is the control plane interface to AMF and is implemented using SCTP as transport and NGAP as its application layer protocol. NGAP is standardized to use SCTP port 38421.  All non-UE-Associated messages are sent using StreamID 0.  Ue-associated messages are assigned an available StreamId for the initial message in a procedure sequence.  Since messages can be identified from their application layer content, strict StreamID checking can be disabled.

The N3 interface is the user plane interface to UPF and is implemented using UDP as transport and GTP-U as tunneling transport.


The following UE Mobility management procedures are used to prepare, execute or cancel handovers:
-	Handover Preparation;
-	Handover Resource Allocation;
-	Handover Notification;
-	Path Switch Request;
-	Uplink RAN Status Transfer;
-	Downlink RAN Status Transfer;
-	Handover Cancellation ;
-	Handover Success;
-	Uplink RAN Early Status Transfer;
-	Downlink RAN Early Status Transfer.

Relevant 3GPP documentation: 
- TS 38.410 - NG General Principles
- TS 38.411 - NG Layer 1 (no requirements)
- TS 38.422 - NG Signalling Transport  (SCTP)
- TS 38.413 - NG Application Protocol (NGAP)
- TS 38.414 - NG Data transport
- TS 38.415 - NG PDU Session user plane protocol


## N2 Handover - Basic Handover Procedure (Intra-RAT)

```
      UE              Source gNB                       Target gNB                           AMF            UPF(s)
      |                    |                                |                                |                |
      |== User Data =======|============ User Data ===========================================================|
      |                    |                                |                                |                |
      |_______________0. Mobility control information provided by AMF________________________|                |
      |                    |                                |                                |                |  H
      |-- 1. MeasReport -->|                                |                                |                |  A
      |                    |                                |                                |                |  N
      |                    |                                |                                |                |  D
      |             /------+------\                         |                                |                |  O
      |             | 2. Handover |                         |                                |                |  V
      |             |    Decision |                         |                                |                |  E
      |             \------+------/                         |                                |                |  R
      |                    |                                |                                |                |
      |                    |                                |                                |                |  
      |                    |-------------------- 3. HANDOVER REQUIRED ---------------------->|                |  P
      |                    |                                |                                |                |  R
      |                    |                                |                                |                |  E
      |                    |                                |<---- 4. HANDOVER REQUEST ------|                |  P
      |                    |                                |                                |                |
      |                    |                                |                                |                |
      |                    |                         /------+------\                         |                |
      |                    |                         | 5. Admission|                         |                |
      |                    |                         |    Control  |                         |                |
      |                    |                         \------+------/                         |                |  
      |                    |                                |                                |                |
      |                    |                                |                                |                |
      |                    |                                |-- 6. HANDOVER REQUEST ACK ---->|                |
      |                    |                                |                                |                |
      |                    |                                |                                |                |
      |                    |<------------------ 5. HANDOVER COMMAND -------------------------|                |
      |                    |                                |                                |                |
      |<- 6. RRCReconfig --|                                |                                |                |
      |                    |                                |                                |                |
      |                    |<======================== User Data (DL) =========================================| 
      |             /------+------\                         |                                |                |  
      |             | Buffer User |                         |                                |                |  
      |             | Data        |                         |                                |                |  
      |             \------+------/                         |                                |                |
      |                    |                                |                                |                |
      |                    |========================= User Data (DL) ========================================>|
      |                    |                                |<================================================|
      |                    |                                |                                |                |
      |                    |                        /-------------\                          |                |  
      |                    |                        | Buffer User |                          |                |  
      |                    |                        | Data        |                          |                |  
      |                    |                        \------+------/                          |                |  
 /----+----\               |                                |                                |                |
 | Detach/ |               |                                |                                |                |
 | Sync    |               |                                |                                |                |  
 \----+----/               |                                |                                |                |  
      |                    |                                |                                |                |
      |------- 7. RRCReconfigComplete --------------------->|                                |                | 
      |                    |                                |                                |                |
      |=========== User Data (UL) =========================>|============ User Data (UL) ====================>|
      |<========== User Data (DL) ==========================|                                |                |
      |                    |                                |                                |                |
      |                    |                                |                                |                |
      |                    |                                |--- 8. HANDOVER COMPLETE  ----->|                |
      |                    |                                |                                |                |
      |                    |<========== 10. End Marker (DL) ==================================================|
      |                    |                                |                                |                |
      |                    |                                |                                |                |
      |=========== User Data (DL/UL) =======================|============ User Data (DL/UL) ==================|
      |                    |                                |                                |                | 
      |                    |                                |                                |                |
      |                    |<------------------- 9. UE CONTEXT RELEASE ----------------------|                |
      |                    |                                |                                |                |
```
Control Plane Signalling - Basic Handover




## Xn Handover - Conditional Handover Procedure

Similar to Basic Handover, but:
- RRC can instruct Xn to prepare multiple CHO candidates
- UE responds to source gNB RRC Reconfiguration with RRC Reconfig Complete
- UE triggers CHO according to trigger condition automatically.
- User Plane tunneling to target only starts when Handover Success is received from target gnb
- source gnb sends Handover Cancel to unused candidates



```
      UE                       Source gNB      Target gNB     Other Target(s)               AMF             UPF(s)
      |                             |               |                |                       |                |
      |===== User Data (DL/UL) =====|===================== User Data (DL/UL) =================================|
      |                             |               |                |                       |                |
      |_____________________________|__ 0. Mobility control information provided by AMF______|                |
      |                             |               |                |                       |                |
   /--+--\                          |               |                |                       |                |
   | 1.  |                          |               |                |                       |                |
   \--+--/                          |               |                |                       |                |
      |                      /------+------\        |                |                       |                |
      |                      | 2. CHO      |        |                |                       |                |
      |                      |    Decision |        |                |                       |                |
      |                      \------+------/        |                |                       |                |
      |                             |               |                |                       |                |
      |                             | /- 3. HANDOVER REQUEST(s)-\    |                       |                |  
      |                             |-------------->|                |                       |                |
      |                             |------------------------------->|                       |                |
      |                             |               |                |                       |                |
      |                             |        /------+------\         |                       |                |
      |                             |        | 4. Admission|         |                       |                |
      |                             |        |    Control  |         |                       |                |
      |                             |        \------+------/   /-----+-----\                 |                |
      |                             |               |          | 4. Adm.   |                 |                |
      |                             |               |          |    Control|                 |                |
      |                             |               |          \-----+-----/                 |                |
      |                             |               |                |                       |                |
      |                             |               |                |                       |                | 
      |                             | /- 5. HO REQUEST ACK(s) -\     |                       |                |  
      |                             |<--------------|                |                       |                |  
      |                             |<-------------------------------|                       |                |  
      |                             |               |                |                       |                |  
      |                             |               |                |                       |                |
      |<-- 6. RRCReconfiguration ---|               |                |                       |                |
      |                             |               |                |                       |                |
      |                             |               |                |                       |                |
      |--- 7. RRCReconfigComplete ->|               |                |                       |                |
      |                             |               |                |                       |                |
      |                             |               |                |                       |                |
      |                             |               |                |                       |                |
 /----+-------\                     |               |                |                       |                |
 | 8. Evaluate|                     |               |                |                       |                |
 | CHO        |                     |               |                |                       |                |
 \----+-------/                     |               |                |                       |                |
      |                             |               |                |                       |                |
 /----+----\                        |<===============User Data (DL) ==========================================|
 | Detach/ |                        |               |                |                       |                |
 | Sync    |                        |               |                |                       |                |
 \----+----/                        |               |                |                       |                |
      |                      /------+------\        |                |                       |                |  
      |                      | Buffer User |        |                |                       |                |  
      |                      | Data        |        |                |                       |                |  
      |                      \------+------/        |                |                       |                |  
      |---- 8.  RRCReconfigComplete -------------------------------->|                       |                | 
      |                             |               |                |                       |                |
      |=========== User Data (UL) =========================>|============ User Data (UL) ====================>|
      |                             |               |                |                       |                |  
      |                             |               |                |                       |                |
      |                             |<--  9. HANDOVER SUCCESS -------|                       |                |
      |                             |-- 10. SN STATUS TRANSFER ----->|                       |                |
      |                             |               |                |                       |                |
      |                             |========= User Data (DL) ======>|                       |                |
      |                             |               |                |                       |                |
      |<=================== User Data (DL) ==========================|                       |                |
      |                             |               |                |                                |                | 
      |                             |               |                |                                |                |
      |                             |               |                |-- 11. PATH SWITCH REQUEST ---->|                |
      |                             |               |                |                                |                |
      |                             |               |                |<-- 12. PATH SWITCH REQ. ACK ---|                |
      |                             |               |                |                                |                |
      |                             |<========== End Marker (DL) ======================================================|
      |                             |               |                |                                |                |
      |                             |               |                |                                |                |
      |=========== User Data (DL/UL) ================================|============ User Data (DL/UL) ==================|
      |                             |               |                |                                |                | 
      |                             |               |                |                                |                |
      |                             |<-- 13. UE CONTEXT RELEASE -----|                                |                |
      |                             |               |                |                                |                |
      |                             |      /* 14. HO CANCEL *\       |                                |                |
      |                             |-------------->|                |                                |                |
      |                             |------------------------------->|                                |                |
      |                             |               |                |                                |                |
      
```
Control plane Signaling - Conditional Handover


0. and 1.	Same as step 0, 1 in Xn Basic Handover.

2.	The source gNB decides to use CHO.

3.	The source gNB requests CHO for one or more candidate cells belonging to one or more candidate gNBs. A CHO request message is sent for each candidate cell.

4.	The candidate gNBs perform admission control (same as basic handover).

5.	Each candidate gNB(s) sends CHO response (HO REQUEST ACKNOWLEDGE) including configuration of CHO candidate cell(s) to the source gNB. 

6.	The source gNB sends an RRCReconfiguration message to the UE, containing the configuration of CHO candidate cell(s) and CHO execution condition(s).

-   CHO configuration of candidate cells can be followed by another reconfiguration from the source gNB.

7.	The UE sends an RRCReconfigurationComplete message to the source gNB to acknowledge receipt.

8.	The UE maintains connection with the source gNB after receiving CHO configuration, and starts evaluating the CHO execution conditions for the candidate cell(s). If at least one CHO candidate cell satisfies the corresponding CHO execution condition, the UE detaches from the source gNB, applies the stored corresponding configuration for that selected candidate cell, synchronises to that candidate cell and completes the RRC handover procedure by sending RRCReconfigurationComplete message to the target gNB. The UE releases stored CHO configurations after successful completion of RRC handover procedure.

9.	The target gNB sends the HANDOVER SUCCESS message to the source gNB to inform that the UE has successfully accessed the target cell. 

10. The source gNB sends the SN STATUS TRANSFER message following the principles described in step 7 of Intra-AMF/UPF Handover.

-	Late data forwarding may be initiated as soon as the source gNB receives the HANDOVER SUCCESS message.

11. Target gNB sends NGAP Path Switch Request to AMF (same as basic handover).

12. Target gNB receives NGAP Path Switch Request Ack from AMF (same as basic handover).

13. target gNB sends Xn UE Context Release to Source gNB (same as basic handover).

14.	The source gNB sends the HANDOVER CANCEL message toward the other (non-selected) candidate target gNBs, if any, to cancel pending CHO for the UE.


User Plane:

-   If late data forwarding is applied, the source NG-RAN node initiates data forwarding once it knows which target NG-RAN node the UE has successfully accessed (e.g., HANDOVER SUCCESS received). In that case the behavior of the Conditional Handover data forwarding follows the same behavior as defined for the intra-system handover data forwarding.
-   If early data forwarding is applied instead, the source NG-RAN node initiates data forwarding before the UE executes the handover, to a candidate target node of interest (e.g., HANDOVER REQUEST ACK received). The behavior of early data forwarding for the Conditional Handover follows the same principles for DRBs configured with DAPS handover in the intra-system handover.

