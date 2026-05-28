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
      |                    |                                |--- 8. HANDOVER NOTIFY    ----->|                |
      |                    |                                |                                |                |
      |                    |<========== 10. End Marker (DL) ==================================================|
      |                    |                                |                                |                |
      |                    |                                |                                |                |
      |<========== User Data (DL/UL) ======================>|<=========== User Data (DL/UL) =================>|
      |                    |                                |                                |                | 
      |                    |                                |                                |                |
      |                    |<------------------- 9. UE CONTEXT RELEASE ----------------------|                |
      |                    |                                |                                |                |
```
Control Plane Signalling - Basic Handover




## N2/N3 Handover - Conditional Handover Procedure

Similar to Basic Handover, but:
- RRC can instruct NGAP to prepare multiple CHO candidates
- UE responds to source gNB RRC Reconfiguration with RRC Reconfig Complete
- UE triggers CHO according to trigger condition automatically.
- User Plane forwarding to target starts when UE Context Release is received from AMF



```
      UE                 Source gNB                       Target gNB            Candidate gNBs                            AMF            UPF(s)
      |                       |                                |                       |                                   |                |
      |===== User Data =======|============ User Data ======================================================================================|
      |                       |                                |                       |                                   |                |
      |__________________0. Mobility control information provided by AMF___________________________________________________|                |
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |                                   |                |
      |                /------+------\                         |                       |                                   |                |
      |                | Handover    |                         |                       |                                   |                |
      |                |    Decision |                         |                       |                                   |                |
      |                \------+------/                         |                       |                                   |                |
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |                                   |                |  
      |                       |------------------------------  HANDOVER REQUIRED(s)--------------------------------------->|                |
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |<------  HANDOVER REQUEST(s) ------|                |
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |                                   |                |
      |                       |                                |                /------+------\                            |                |
      |                       |                                |                | Admission   |                            |                |
      |                       |                                |                |    Control  |                            |                |
      |                       |                                |                \------+------/                            |                |  
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |-----  HANDOVER REQUEST ACK(s) --->|                |
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |-----  HANDOVER PREP FAILURE(s) -->|                |
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |                                   |                |
      |                       |<---------------------------------------- HANDOVER COMMAND(s)-------------------------------|                |
      |                       |                                |                       |                                   |                |
      |<---- RRCReconfig(s) --|                                |                       |                                   |                |
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |                                   |                |
 /----+----\                  |                                |                       |                                   |                |
 | Detach/ |                  |                                |                       |                                   |                |
 | Sync    |                  |                                |                       |                                   |                |  
 \----+----/                  |                                |                       |                                   |                |  
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |                                   |                |
      |                       |<=========================================== User Data (DL) =================================================| 
      |                /------+------\                         |                       |                                   |                |  
      |                | Buffer User |                         |                       |                                   |                |  
      |                | Data        |                         |                       |                                   |                |  
      |                \------+------/                         |                       |                                   |                |
      |                       |                                |                       |                                   |                |
      |------------- RRCReconfigComplete --------------------->|                       |                                   |                | 
      |                       |                                |                       |                                   |                |
      |============== User Data (UL) =========================>|============ User Data (UL) ===============================================>|
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |                                   |                |
      |                       |                                |------------------- HANDOVER NOTIFY    ------------------->|                |
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |                                   |                |
      |                       |<====================== 10. End Marker (DL) =================================================================|
      |                       |                                |                       |                                   |                |
      |                       |                                |<=========================== User Data (DL) ================================|
      |                       |                                |                       |                                   |                |
      |                       |                         /-------------\                |                                   |                |  
      |                       |                         | Buffer User |                |                                   |                |  
      |                       |                         | Data        |                |                                   |                |  
      |                       |                         \------+------/                |                                   |                |  
      |                       |                                |                       |                                   |                |
      |                       |<------------------------------------------------- UE CONTEXT RELEASE ----------------------|                |
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |<------  HANDOVER CANCEL(s) -------|                |
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |                                   |                |
      |                       |============================================ User Data (DL) ================================================>|
      |                       |                                |<=========================== User Data (DL) ================================|
      |<============= User Data (DL) ==========================|                       |                                   |                |
      |                       |                                |                       |                                   |                |
      |                       |=======================  End Marker (DL) ===================================================================>|
      |                       |                                |                       |                                   |                |
      |                       |                                |                       |                                   |                |
      |=========== User Data (DL/UL) ==========================|============ User Data (DL/UL) =============================================|
      |                       |                                |                       |                                   |                | 
      |                       |                                |                       |                                   |                | 
```
Control Plane Signalling - Basic Handover

      
```
Control plane Signaling - Conditional Handover


0. and 1.	Same as step 0, 1 in Basic Handover.

2.	The source gNB decides to use CHO.

3.	The source gNB requests CHO for one or more candidate cells belonging to one or more candidate gNBs. A Handover Required message is sent to AMF for each candidate cell.  Handover required includes IEs that indicate CHO and provide parameters: extended timer based on expected execution.

4. AMF receives Handover Required. AMF tracks handover pending for UE.  AMF notifies SMF of handover request.

5.  SMF sets up temporary GTP-U tunnel legs through UPF for candidate gNBs, sends tunnel information (e.g., tunnel IDs) to AMF.

6.  AMF sends Handover Request messages to each candidate gNB.  Handover request includes tunnel information.

7.	The candidate gNBs receive Handover Request, perform admission control (same as basic handover).

8.	Candidate gNB(s) sends response Handover Request Acknowledge for accepted UE, Handover Preparation Failure for rejected UE.  Ack response includes RRCReconfiguration for UE to execute handover.  Response includes CHO indicator IEs.

9.  AMF maintains pending handover for gNB's that accepted UE.  AMF notifies SMF of conditional handover to accepting gNBs.

10. SMF kills tunnels for rejecting gNBs, sets up temporary GTP-U tunnel leg through UPF for source gNB, returns tunning information to AMF.
 
11. AMF sends Handover Command to source gNB.  Includes RRCReconfig from candidate gNB, tunnel infomration for temporary GTP-U tunnel.

12.	The source gNB sends a RRCReconfiguration message to the UE, containing the CHO execution condition(s) and the RRCReconfig from the candidate gNB.

13.	The UE sends an RRCReconfigurationComplete message to the source gNB to acknowledge receipt.

14.	The UE maintains connection with the source gNB after receiving CHO configuration, and starts evaluating the CHO execution conditions for the candidate cell(s). If at least one CHO candidate cell satisfies the corresponding CHO execution condition, the UE detaches from the source gNB, applies the stored RRCReconfig for that selected candidate cell, synchronises to that candidate cell and completes the RRC handover procedure by sending RRCReconfigurationComplete message to the target gNB. The UE releases stored CHO configurations after successful completion of RRC handover procedure.

15. The source gNB starts buffering downlink user plane PDUs to UE on detecting radio link failure (or alternatively, UE can provide an End Marker on the upstream bearer).
    - late data forwarding: source gNB holds data until target gNB is identified, then sends to target gNB using its tunnel
    - early data forwarding: source gNB sends to all candidate gNBs once trigger condition occurs (e.g., RLF).  Candidates buffer until UE connection.

16.	The target gNB sends the Handover Notify message to the AMF to inform that the UE has successfully accessed the target cell. 

17. AMF receives the Handover Notify.  It clears all pending handovers for UE and sends Handover Cancel messages to all unselected candidate gNBs.  It notifies SMF to switch downlink data to the target gNB.

18. SMF causes UPF to send End Marker to source gNB.

19. UPF sends downlionk data to target Gnb.

20. Target gNB buffers downlink data until End Marker received on temporary tunnel (to preserve delivery order).

19. AMF sends UE Context Release message to source gNB to signal handover completion.  In some cases, message could include which tunnel to use for downlink forwarding.

20. Source gNB receives UE Context Release, starts downlink forwarding towards target gNB using temporary GTP-U tunnel.

21. Source gNB sends End Marker when buffer is empty.

22. UPF notifies SMF when End Marker sent to target gNB.

23. SMF tears down temporary tunnels.

24. Target gNB receives End Marker.  Starts delivery of buffered PDUs 

25.  Normal operation.

