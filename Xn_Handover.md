# Xn Interface

The Xn Layer is implemented using the XnTask class.  The XnTask uses the standard NTS message queue to handle messages between layers.

The Xn control plane interface (Xn-C) is implemented using SCTP as transport and XNAP as its application layer protocol. XNAP is standardized to use SCTP port 38422.  All non-UE-Associated messages are sent using StreamID 0.  Ue-associated messages are assigned an available StreamId for the initial message in a procedure sequence.  Since messages can be identified from their application layer content, strict StreamID checking can be disabled.

The Xn user plane interface (Xn-U) is implemented using UDP as transport and GTP-U as tunneling transport.

The XnTask class implements the following functionality:

- Periodic updates to Xn connections list based on current state of neighbors
- Mobility Management (handover)
    -   Handover Preparation
    -	Handover Cancel
    -	SN Status Transfer
    -	Retrieve UE Context
    -	RAN Paging
    -	Xn-U Address Indication
    -	UE Context Release
    -	Handover Success Indication
    -	Conditional Handover Cancel
    -	Retrieve UE Context Confirm

Relevant 3GPP documentation: 
- TS 38.420 - Xn General Principles
- TS 38.421 - XN Layer 1 (no requirements)
- TS 38.422 - Xn Signalling Transport  (SCTP)
- TS 38.423 - Xn Application Protocol (XNAP)
- TS 38.424 - Xn Data transport


## Xn Handover - Basic Handover Procedure (Intra-RAT)

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
      |                    |-- 3. HANDOVER REQUEST -------->|                                |                |  P
      |                    |                                |                                |                |  R
      |                    |                         /------+------\                         |                |  E
      |                    |                         | 4. Admission|                         |                |  P
      |                    |                         |    Control  |                         |                |  .
      |                    |                         \------+------/                         |                |  
      |                    |                                |                                |                |
      |                    |<-- 5. HANDOVER REQUEST ACK ----|                                |                |
      |                    |                                |                                |                |
      |<- 6. RRCReconfig --|                                |                                |                |
      |                    |                                |                                |                |
      |                    |<======================== User Data (DL) =========================================| 
      |             /------+------\                         |                                |                |  
      |             | Buffer User |                         |                                |                |  
      |             | Data        |                         |                                |                |  
      |             \------+------/                         |                                |                |  
 /----+----\               |                                |                                |                |  H
 | Detach/ |               |                                |                                |                |  O
 | Sync    |               |                                |                                |                |  
 \----+----/               |-- 7.  SN STATUS TRANSFER ----->|                                |                |  
      |                    |                                |                                |                |
      |                    |                                |                                |                |
      |                    |========= User Data (DL) ======>|                                |                |
      |                    |                        /-------------\                          |                |  
      |                    |                        | Buffer User |                          |                |  
      |                    |                        | Data        |                          |                |  
      |                    |                        \------+------/                          |                |  
      |                    |                                |                                |                |
      |- RRCReconfigComplete ------------------------------>|                                |                | 
      |                    |                                |                                |                |
      |                    |                                |                                |                |
      |=========== User Data (UL) =========================>|============ User Data (UL) ====================>|
      |<========== User Data (DL) ==========================|                                |                |
      |                    |                                |                                |                | 
      |                    |                                |                                |                |
      |                    |                                |-- 9. PATH SWITCH REQUEST ----->|                |
      |                    |                                |                                |                |
      |                    |                                |<-- 11. PATH SWITCH REQ. ACK ---|                |
      |                    |                                |                                |                |
      |                    |<========== 10. End Marker (DL) ==================================================|
      |                    |                                |                                |                |
      |                    |                                |                                |                |
      |=========== User Data (DL/UL) =======================|============ User Data (DL/UL) ==================|
      |                    |                                |                                |                | 
      |                    |                                |                                |                |
      |                    |<-- 12. UE CONTEXT RELEASE -----|                                |                |
      |                    |                                |                                |                |
```
Control Plane Signalling - Basic Handover


Pre-conditions:

0.	The UE context within the source gNB contains information regarding roaming and access restrictions which were provided either at connection establishment or at the last TA update.

1.	The source gNB configures the UE measurement procedures and the UE reports according to the measurement configuration.

Handover Preparation:

2.	The source gNB decides to handover the UE, based on MeasurementReport.

3.	The source gNB issues a Handover Request message to the target gNB passing a transparent RRC container with necessary information to prepare the handover at the target side. The information includes at least the target cell ID, KgNB*, the C-RNTI of the UE in the source gNB, RRM-configuration including UE inactive time, basic AS-configuration including antenna Info and DL Carrier Frequency, the current QoS flow to DRB mapping rules applied to the UE, the SIB1 information from source gNB, the UE capabilities for different RATs, PDU session related information, and can include the UE reported measurement information including beam-related information if available. The PDU session related information includes the slice information and QoS flow level QoS profile(s).

4.	Admission Control may be performed by the target gNB. Slice-aware admission control shall be performed if the slice information is sent to the target gNB. If the PDU sessions are associated with non-supported slices the target gNB shall reject such PDU Sessions.

5.	The target gNB prepares the handover with L1/L2 and sends the HANDOVER REQUEST ACKNOWLEDGE to the source gNB, which includes a transparent container to be sent to the UE as an RRC message to perform the handover.

NOTE 2:	As soon as the source gNB receives the HANDOVER REQUEST ACKNOWLEDGE, or as soon as the transmission of the handover command is initiated in the downlink, data forwarding may be initiated by source gNB.

Handover Execution:

6.	The source gNB triggers the Uu handover by sending an RRCReconfiguration message to the UE, containing the information required to access the target cell: at least the target cell ID, the new C-RNTI, the target gNB security algorithm identifiers for the selected security algorithms. It can also include a set of dedicated RACH resources, the association between RACH resources and SSB(s), the association between RACH resources and UE-specific CSI-RS configuration(s), common RACH resources, and system information of the target cell, etc.

7.	The source gNB sends the SN STATUS TRANSFER message to the target gNB to convey the uplink PDCP SN receiver status and the downlink PDCP SN transmitter status of DRBs for which PDCP status preservation applies (i.e. for RLC AM). The uplink PDCP SN receiver status includes at least the PDCP SN of the first missing UL PDCP SDU and may include a bit map of the receive status of the out of sequence UL PDCP SDUs that the UE needs to retransmit in the target cell, if any. The downlink PDCP SN transmitter status indicates the next PDCP SN that the target gNB shall assign to new PDCP SDUs, not having a PDCP SN yet.

8.	The UE synchronises to the target cell and completes the RRC handover procedure by sending RRCReconfigurationComplete message to target gNB. The UE releases the source resources and configurations and stops DL/UL reception/transmission with the source upon receiving an explicit release from the target node.

9.	The target gNB sends a PATH SWITCH REQUEST message to AMF to trigger 5GC to switch the DL data path towards the target gNB and to establish an NG-C interface instance towards the target gNB.

10.	5GC switches the DL data path towards the target gNB. The UPF sends one or more "end marker" packets on the old path to the source gNB per PDU session/tunnel and then can release any U-plane/TNL resources towards the source gNB.

11.	The AMF confirms the PATH SWITCH REQUEST message with the PATH SWITCH REQUEST ACKNOWLEDGE message.

12.	Upon reception of the PATH SWITCH REQUEST ACKNOWLEDGE message from the AMF, the target gNB sends the UE CONTEXT RELEASE to inform the source gNB about the success of the handover. The source gNB can then release radio and C-plane related resources associated to the UE context. Any ongoing data forwarding may continue.

User Plane:

The U-plane handling during the Intra-NR-Access mobility activity for UEs in RRC_CONNECTED takes the following principles into account to avoid data loss during HO:
-	During HO preparation, U-plane tunnels can be established between the source gNB and the target gNB;
-	During HO execution, user data can be forwarded from the source gNB to the target gNB;
-	Forwarding should take place in order as long as packets are received at the source gNB from the UPF or the source gNB buffer has not been emptied.
-	During HO completion, the target gNB sends a path switch request message to the AMF to inform that the UE has gained access and the AMF then triggers path switch related 5GC internal signalling and actual path switch of the source gNB to the target gNB in UPF;
-	The source gNB should continue forwarding data as long as packets are received at the source gNB from the UPF or the source gNB buffer has not been emptied.


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

