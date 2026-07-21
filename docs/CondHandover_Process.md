# Conditional Handover (CHO)

## Process

### gNB

1.  When UE initially connects (RRC Setup), determine which Measurement Identities (MeasIds) should be used by the UE.  MeasIds can be basic events (e.g., eventA3) or conditional (condT1). GNB is free to assign these according to installed policy.
2.  The MeasIds need to be configured according to the event type, to determine when they are triggered.  For RSRP-based events, this is either an RSRP level or an offset from the serving cell.  For distance-based events, this is a reference point and a distance from the reference point.  For timer-based events, this is a timestamp for a triggering time.
    a.  Distance-based events will require a policy to determine what the reference point is.  In a terrestrial network, this could be the center of a neighboring cell.  In an NTN, this could be the expected location of the next satellite.
    b.  Time-based events will require a policy to calculate the target timestamp.  This would typically be tied to motion - how long will it take for motion to move the UE to require service by a different gNB.

3.  The gNB generates a MeasConfig IE to be included in an RRC Reconfiguration message.  The MeasConfig contains the MeasIds as configured for the UE.  In teh simulator, this RRC Reconfiguration message with the MeasIds is sent immediately after RRC Setup, and does not contain any CHO "candidates", due to the latency in calculating handover targets (see below).  This MeasConfig is marked as a "fullConfig", meaning the UE should remove all prior stored MeasIds, and replace with those in the current MeasConfig.
4.  If any of the MeasIds are CHO MeasIds, the gNB then determines which target gNBs should be provided to the UE, so that when the conditional event is triggered, the UE can immediately perform teh handover.  This process is similar to a basic handover procedure, but requires the serving gNB to make an "in-advance" policy assessment of what is teh best target gNB if the conditional event occurs.

    -  The gNB projects which of its neighbor gNBs will be best positioned to serve the UE at the time of teh trigger.  In NTN, this means propagating satellite positions forward in time to the time when the event is expected to be triggered, and determining the post-positioned target satellite at that time.  "Best positioned" is a policy question that can be determined using different criteria.  In the simulator, the policy is "maximum transit time" - longest time the target will be in service range of the UE.  Other policies could be highest current elevation angle, closest distance to UE, etc.
    -  For the selected best target, the serving gNB starts a handover messaging sequence.  The simulator uses the N2 interface, so the process is: (1) NGAP HandoverRequired to AMF, (2) NGAP HandoverRequest from AMF to target gNB, (3) NGAP HandoverRequestAck from target gNB to AMF, (4) NGAP HandoverCommand from AMF to serving gNB.  Note that the fact that this is CHO-related is indicated in the messaging, so that the target gNB and AMF know that the handover is not imminent (which allows them to set longer timers for handover completion).  The whole process may introduce several hundred ms of latency.
    -  Upon receiving the NGAP HandoverCommand from the AMF, it determines that it is associated with a pending CHO preparation.  It then creates an RRC Reconfiguration message containing another MeasConfig IE.  In this MeasConfig, no additional MeasIds are included (they could be, but not in the current simulator implementation), but rather a ConditionalReconfiguration IE is included that contains the RRCReconfiguration-ReconfigWithSync message received from the target gNB and a mapping to an existing MeasId that is the trigger for executing a handover to the target gNB.  Teh RRC Reconfiguration message with the ConditionalReconfiguration IE is sent to the UE.


#### Key gNB Data Structures in Simulator

| Structure | Role in CHO flow |
|---|---|
| `RrcUeContext` | Per-UE gNB context at the RRC layer. Stores the UE's active MeasIds, the sent MeasConfig history, and the state of pending CHO preparations so RRCReconfigs can be assembled from target gNB responses. |
| `GnbHandoverConfig` | Top-level gNB handover policy. Enables CHO, defines the default CHO profile, and carries the base `events` plus `candidateProfiles` used to build measurement and CHO messages. |
| `GnbChoCandidateProfileConfig` | Per-candidate gNB policy. Holds the `candidateProfileId`, optional `targetCellId`, and the `conditions` (MeasIds) that are copied into the UE measurement configuration. |
| `nr::rrc::common::ReportConfigEvent` | Shared event description for A2/A3/A5, `D1`, and `condT1`. This is the main configuration object used to define when a MeasId should fire. |
| `nr::rrc::common::DynamicEventTriggerParams` | Runtime trigger inputs used by the gNB to resolve distance- and time-based thresholds before sending MeasConfig / CHO prep. |
| `HandoverMeasurementIdentity` | Summary of a configured MeasId on the gNB side. Used when exporting the MeasConfig for handover preparation and for tracking the UE's configured measurement identities. |
| `HandoverPreparationInfo` | Per-UE NGAP-layer structure that maintains state of in-process CHO preparation.  Allows NGAP layer to distinguish between normal handover messaginge and CHO-related messaging. |

The gNB's CHO runtime is mostly stored directly on `RrcUeContext`: `choPreparationPending`, `choPreparationCandidatePcis`, `choPreparationCandidateScores`, `choPreparationMeasIds`, `choPreparationCandidateProfileId`, and `usedCondReconfigIds`.



### UE

1.  Per the normal measurement evaluation process, the UE periodically performs measurements according to its installed Measurement Identities (MeasIds). 
2.  If any of its MeasIds are triggered, it checks its Conditional Handover list to see if any of the MeasIds are associated with a stored CHO candidate.
3.  If one CHO candidate is triggered, the UE uses it for handover: it takes the stored Reconfiguration with Sync information and generates an RRC ReconfigurationComplete message that it sends to the target gNB.
4.  If multiple CHO candidates are triggered, the UE uses a tiebreaking procedure to select the best candidate to use for handover.  This is a UE policy decision, and in the simulation, the UE uses a mirror process to the gNB in selecting the best gNB: it propagates satellites to the handover time, and select the satellite with the longest transit time.
5.  If none of the triggered MeasIds are associated with a CHO candidate, the UE returns to the normal measurement process to determine if a Measurement Report should be sent to the gNB.


#### Key Data Structures in Simulator

| Structure | Role in CHO flow |
|---|---|
| `UeMeasConfig` | UE struct representing a Measurement Identity received from the gNB. Holds the received MeasIds, which contain MeasObjects, ReportConfigs, and MeasIds.  Also includes a `MeasIdState` that tracks the runtime state of the MeasId. |
| `MeasIdState` | Per-MeasId runtime trigger state. Tracks whether the condition is reportable, when it started being satisfied, whether it is currently satisfied, and whether it has already been reported. |
| `ChoCandidate` | UE-side CHO candidate record. Stores the candidate ID, target PCI, new C-RNTI, T304 value, triggering MeasIds, TransactionId, and whether the candidate was already executed. ChoCandidate simulates an RRCReconfigWithSync message by storing teh key components of that message for purposes of handover (cRNTI, T304 timer value, TransactionId)|
| `UeRrcTask::m_measConfig` | Active UE list of current `UeMeasConfig`s received from the network. This is teh set of MeasIds that the UE evaluates during each measurement evaluation cycle during RRC_CONNECTED.|
| `UeRrcTask::m_choCandidates` | Active UE list of `CHOCandidate`s. This is the set of conditional reconfiguration candidates evaluated when a MeasId has been triggered. |

On the UE, the CHO decision is driven by the measurement engine's runtime maps inside `UeMeasConfig` and the candidate list stored on `UeRrcTask`. The candidate's `measIds` are the link between the measurement trigger and the stored `ReconfigurationWithSync` payload.



## Supported capabilities

### Events

condA3, condD1 and condT1 are implemented

condA3 (`condEventA3`) - trigger based on RSRP of serving gNB below RSRP of target gNB by at least an offset (e.g., 3dBm)
    parameters: offset, hysteresis, time-to-trigger

condD1 (`condEventD1-r17`)  trigger based on meeting distance threshold requirements.  
    Two requirements: distance to reference location 1 must be greater than threshold1 AND distance to reference location 2 must be greater than threshold2.
    parameters: threshold1, referenceLocation1, threshold2, referenceLocation2, hysteresis, time-to-trigger

condT1 (`condEventT1-r17`) - trigger based on meeting timestamp threshold AND being within a duration (i.e. creates a time window of validity)
    parameters; threshold, duration

### IEs

MeasConfig: 
- CHO requires use of `condTriggerConfig-r16` IEs to specify the conditional event types for a MeasId
- CHO requires the use of `ConditionalReconfiguration-r16` IE to specify the CHO candidates.

See representation of ASN.1 structure below:

``` asn.1
-- Full Message Structure (for IEs used in this simulator), with sub-definitions inserted:
RRCReconfiguration { 
    rrc-TransactionIdentifier               RRC-TransactionIdentifier, 
    RRCReconfiguration-IEs {
        radioBearerConfig                   RadioBearerConfig,
        secondaryCellGroup                  OCTET STRING (CONTAINING CellGroupConfig),

        measConfig {
            measObjectToRemoveList              MeasObjectToRemoveList,
            measObjectToAddModList {
        
                { 
                    measObjectId                                MeasObjectId, 
                    measObject { 
                        measObjectNR {
                        } 
                    } 
                },
                ... 
            },

            reportConfigToRemoveList            ReportConfigToRemoveList,
            ReportConfigToAddModList {
                {
                    reportConfigId    ReportConfigId,
                    reportConfig {
                        ReportConfigNR {
                            reportType CHOICE {
                                eventTriggered  {
                                    eventId {
                                        eventA3 {
                                            a3-Offset              INT(-30..30),   -- 1 unit = 0.5db
                                            reportOnLeave          BOOLEAN,        -- true = trigger when leaving curr cell
                                            hysteresis             INT(0..30),    -- 1 unit = 0.5db
                                            timeToTrigger          TimeToTrigger,  -- ms 
                                            useAllowedCellList     BOOLEAN              
                                        },
                                        ...
                                    }
                                }
                                
                                condTriggerConfig-r16  {
                                    condEventId {
                                        condEventD1-r17  { 
                                            distanceThreshFromReference1-r17 INTEGER(0..65525), 
                                            distanceThreshFromReference2-r17 INTEGER(0..65525), 
                                            referenceLocation1-r17           ReferenceLocation-r17, 
                                            referenceLocation2-r17           ReferenceLocation-r17, 
                                            hysteresisLocation-r17           HysteresisLocation-r17, 
                                            timeToTrigger-r17                TimeToTrigger 
                                        }
                                    }
                                }
                            }
                        }
                    }
                },
                ...
            }
            measIdToRemoveList                  MeasIdToRemoveList,
            measIdToAddModList {
                {
                    measId              INT (1..64),
                    measObjectId        INT (1..64),
                    reportConfigId      INT (1..64)
                },
                ...
            }
        },

        lateNonCriticalExtension            OCTET STRING,
        nonCriticalExtension {

            RRCReconfiguration-v1530-IEs {
                masterCellGroup                     OCTET STRING (CONTAINING CellGroupConfig),
                fullConfig                          ENUMERATED {true}, -- true = replace current configs with configs in this message
                dedicatedNAS-MessageList            SEQUENCE (SIZE(1..maxDRB)) OF DedicatedNAS-Message,
                masterKeyUpdate                     MasterKeyUpdate,
                sk-Counter                          SK-Counter,
                nonCriticalExtension {

                    RRCReconfiguration-v1540-IEs { 
                        otherConfig-v1540                       OtherConfig-v1540                                                      OPTIONAL, -- Need M
                        nonCriticalExtension {

                            RRCReconfiguration-v1560-IEs { 
                                mrdc-SecondaryCellGroupConfig            SetupRelease { MRDC-SecondaryCellGroupConfig }                        OPTIONAL,   -- Need M
                                radioBearerConfig2                       OCTET STRING (CONTAINING RadioBearerConfig)                           OPTIONAL,   -- Need M
                                sk-Counter                               SK-Counter                                                            OPTIONAL,   -- Need N
                                nonCriticalExtension  {

                                    RRCReconfiguration-v1610-IEs {

                                        conditionalReconfiguration-r16 {

                                            attemptCondReconfig-r16             ENUMERATED {true},
                                            condReconfigToRemoveList-r16        CondReconfigToRemoveList-r16,
                                            condReconfigToAddModList-r16 {
                                                {
                                                    condReconfigId-r16               CondReconfigId-r16, 
                                                    condExecutionCond-r16            SEQUENCE (SIZE (1..2)) OF MeasId                      OPTIONAL,    -- Need M
                                                    condRRCReconfig-r16              OCTET STRING (CONTAINING RRCReconfiguration)          OPTIONAL,    -- Cond condReconfigAdd
                                                    ..., 
                                                    [[ 
                                                    condExecutionCondSCG-r17         OCTET STRING (CONTAINING CondReconfigExecCondSCG-r17) OPTIONAL  -- Need M
                                                    ]] 

                                                }

                                            }
                                        }

                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

```