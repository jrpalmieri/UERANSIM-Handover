-- ASN1START
-- **************************************************************
--
-- Elementary Procedure definitions
--
-- **************************************************************

XnAP-PDU-Descriptions {
itu-t (0) identified-organization (4) etsi (0) mobileDomain (0)
ngran-access (22) modules (3) xnap (2) version1 (1) xnap-PDU-Descriptions (0) }

DEFINITIONS AUTOMATIC TAGS ::=

BEGIN

-- **************************************************************
--
-- IE parameter types from other modules.
--
-- **************************************************************

IMPORTS
	Criticality,
	ProcedureCode

FROM XnAP-CommonDataTypes

	HandoverRequest,
	HandoverRequestAcknowledge,
	HandoverPreparationFailure,
	SNStatusTransfer,
	UEContextRelease,
	HandoverCancel,
	NotificationControlIndication,
	RANPaging,
	RetrieveUEContextRequest,
	RetrieveUEContextResponse,
	RetrieveUEContextConfirm,
	RetrieveUEContextFailure,
	XnUAddressIndication,
	SecondaryRATDataUsageReport,
	SNodeAdditionRequest,
	SNodeAdditionRequestAcknowledge,
	SNodeAdditionRequestReject,
	SNodeReconfigurationComplete,
	SNodeModificationRequest,
	SNodeModificationRequestAcknowledge,
	SNodeModificationRequestReject,
	SNodeModificationRequired,
	SNodeModificationConfirm,
	SNodeModificationRefuse,
	SNodeReleaseRequest,
	SNodeReleaseRequestAcknowledge,
	SNodeReleaseReject,
	SNodeReleaseRequired,
	SNodeReleaseConfirm,
	SNodeCounterCheckRequest,
	SNodeChangeRequired,
	SNodeChangeConfirm,
	SNodeChangeRefuse,
	RRCTransfer,
	XnRemovalRequest,
	XnRemovalResponse,
	XnRemovalFailure,
	XnSetupRequest,
	XnSetupResponse,
	XnSetupFailure,
	NGRANNodeConfigurationUpdate,
	NGRANNodeConfigurationUpdateAcknowledge,
	NGRANNodeConfigurationUpdateFailure,
	E-UTRA-NR-CellResourceCoordinationRequest,
	E-UTRA-NR-CellResourceCoordinationResponse,
	ActivityNotification,
	CellActivationRequest,
	CellActivationResponse,
	CellActivationFailure,
	ResetRequest,
	ResetResponse,
	ErrorIndication,
	PrivateMessage,
	DeactivateTrace,
	TraceStart,
	HandoverSuccess,
	ConditionalHandoverCancel,
	EarlyStatusTransfer,
	FailureIndication,
	HandoverReport,
	ResourceStatusRequest,
	ResourceStatusResponse,
	ResourceStatusFailure,
	ResourceStatusUpdate,
	MobilityChangeRequest,
	MobilityChangeAcknowledge,
	MobilityChangeFailure,
	AccessAndMobilityIndication,
	CellTrafficTrace,
	RANMulticastGroupPaging,
	ScgFailureInformationReport,
	ScgFailureTransfer,
	F1CTrafficTransfer,
	IABTransportMigrationManagementRequest,
	IABTransportMigrationManagementResponse,
	IABTransportMigrationManagementReject,
	IABTransportMigrationModificationRequest,
	IABTransportMigrationModificationResponse,
	IABResourceCoordinationRequest,
	IABResourceCoordinationResponse,
	CPCCancel,
	PartialUEContextTransfer,
	PartialUEContextTransferAcknowledge,
	PartialUEContextTransferFailure,
	RachIndication,
	DataCollectionRequest,
	DataCollectionResponse,
	DataCollectionFailure,
	DataCollectionUpdate



FROM XnAP-PDU-Contents

	id-handoverPreparation,
	id-sNStatusTransfer,
	id-handoverCancel,
	id-notificationControl,
	id-retrieveUEContext,
	id-rANPaging,
	id-xnUAddressIndication,
	id-uEContextRelease,
	id-secondaryRATDataUsageReport,
	id-sNGRANnodeAdditionPreparation,
	id-sNGRANnodeReconfigurationCompletion,
	id-mNGRANnodeinitiatedSNGRANnodeModificationPreparation,
	id-sNGRANnodeinitiatedSNGRANnodeModificationPreparation,
	id-mNGRANnodeinitiatedSNGRANnodeRelease,
	id-sNGRANnodeinitiatedSNGRANnodeRelease,
	id-sNGRANnodeCounterCheck,
	id-sNGRANnodeChange,
	id-activityNotification,
	id-rRCTransfer,
	id-xnRemoval,
	id-xnSetup,
	id-nGRANnodeConfigurationUpdate,
	id-e-UTRA-NR-CellResourceCoordination,
	id-cellActivation,
	id-reset,
	id-errorIndication,
	id-privateMessage,
	id-deactivateTrace,
	id-traceStart,
	id-handoverSuccess,
	id-conditionalHandoverCancel,
	id-earlyStatusTransfer,
	id-failureIndication,
	id-handoverReport,
	id-resourceStatusReportingInitiation,
	id-resourceStatusReporting,
	id-mobilitySettingsChange,
	id-accessAndMobilityIndication,
	id-cellTrafficTrace,
	id-RANMulticastGroupPaging,
	id-scgFailureInformationReport,
	id-scgFailureTransfer,
	id-f1CTrafficTransfer,
	id-iABTransportMigrationManagement,
	id-iABTransportMigrationModification,
	id-iABResourceCoordination,
	id-retrieveUEContextConfirm,
	id-cPCCancel,
	id-partialUEContextTransfer,
	id-rachIndication,
	id-dataCollectionReportingInitiation,
	id-dataCollectionReporting



FROM XnAP-Constants;

-- **************************************************************
--
-- Interface Elementary Procedure Class
--
-- **************************************************************

XNAP-ELEMENTARY-PROCEDURE ::= CLASS {
	&InitiatingMessage				,
	&SuccessfulOutcome				OPTIONAL,
	&UnsuccessfulOutcome				OPTIONAL,
	&procedureCode			ProcedureCode	UNIQUE,
	&criticality			Criticality		DEFAULT ignore
}
WITH SYNTAX {
	INITIATING MESSAGE		&InitiatingMessage
	[SUCCESSFUL OUTCOME		&SuccessfulOutcome]
	[UNSUCCESSFUL OUTCOME		&UnsuccessfulOutcome]
	PROCEDURE CODE			&procedureCode
	[CRITICALITY			&criticality]
}

-- **************************************************************
--
-- Interface PDU Definition
--
-- **************************************************************

XnAP-PDU ::= CHOICE {
	initiatingMessage	InitiatingMessage,
	successfulOutcome	SuccessfulOutcome,
	unsuccessfulOutcome	UnsuccessfulOutcome,
	...
}

InitiatingMessage ::= SEQUENCE {
	procedureCode	XNAP-ELEMENTARY-PROCEDURE.&procedureCode		({XNAP-ELEMENTARY-PROCEDURES}),
	criticality		XNAP-ELEMENTARY-PROCEDURE.&criticality			({XNAP-ELEMENTARY-PROCEDURES}{@procedureCode}),
	value			XNAP-ELEMENTARY-PROCEDURE.&InitiatingMessage	({XNAP-ELEMENTARY-PROCEDURES}{@procedureCode})
}

SuccessfulOutcome ::= SEQUENCE {
	procedureCode	XNAP-ELEMENTARY-PROCEDURE.&procedureCode		({XNAP-ELEMENTARY-PROCEDURES}),
	criticality		XNAP-ELEMENTARY-PROCEDURE.&criticality			({XNAP-ELEMENTARY-PROCEDURES}{@procedureCode}),
	value			XNAP-ELEMENTARY-PROCEDURE.&SuccessfulOutcome	({XNAP-ELEMENTARY-PROCEDURES}{@procedureCode})
}

UnsuccessfulOutcome ::= SEQUENCE {
	procedureCode	XNAP-ELEMENTARY-PROCEDURE.&procedureCode		({XNAP-ELEMENTARY-PROCEDURES}),
	criticality		XNAP-ELEMENTARY-PROCEDURE.&criticality			({XNAP-ELEMENTARY-PROCEDURES}{@procedureCode}),
	value			XNAP-ELEMENTARY-PROCEDURE.&UnsuccessfulOutcome	({XNAP-ELEMENTARY-PROCEDURES}{@procedureCode})
}

-- **************************************************************
--
-- Interface Elementary Procedure List
--
-- **************************************************************

XNAP-ELEMENTARY-PROCEDURES XNAP-ELEMENTARY-PROCEDURE ::= {
	XNAP-ELEMENTARY-PROCEDURES-CLASS-1			|
	XNAP-ELEMENTARY-PROCEDURES-CLASS-2			,
	...
}

XNAP-ELEMENTARY-PROCEDURES-CLASS-1 XNAP-ELEMENTARY-PROCEDURE ::= {
	handoverPreparation										|
	retrieveUEContext										|
	sNGRANnodeAdditionPreparation							|
	mNGRANnodeinitiatedSNGRANnodeModificationPreparation	|
	sNGRANnodeinitiatedSNGRANnodeModificationPreparation	|
	mNGRANnodeinitiatedSNGRANnodeRelease					|
	sNGRANnodeinitiatedSNGRANnodeRelease					|
	sNGRANnodeChange										|
	xnRemoval												|
	xnSetup													|
	nGRANnodeConfigurationUpdate							|
	e-UTRA-NR-CellResourceCoordination						|
	cellActivation											|
	reset													|
	resourceStatusReportingInitiation						|
	mobilitySettingsChange									|
	iABTransportMigrationManagement							|
	iABTransportMigrationModification						|
	iABResourceCoordination									|
	partialUEContextTransfer								|
	dataCollectionReportingInitiation,
	...
}

XNAP-ELEMENTARY-PROCEDURES-CLASS-2 XNAP-ELEMENTARY-PROCEDURE ::= {
	sNStatusTransfer						|
	handoverCancel							|
	rANPaging								|
	xnUAddressIndication					|
	uEContextRelease						|
	sNGRANnodeReconfigurationCompletion		|
	sNGRANnodeCounterCheck					|
	rRCTransfer								|
	errorIndication							|
	privateMessage							|
	notificationControl						|
	activityNotification					|
	secondaryRATDataUsageReport				|
	deactivateTrace							|
	traceStart								|
	handoverSuccess							|
	conditionalHandoverCancel				|
	earlyStatusTransfer						|
	failureIndication						|
	handoverReport							|
	resourceStatusReporting					|
	accessAndMobilityIndication				|
	cellTrafficTrace						|
	rANMulticastGroupPaging					|
	scgFailureInformationReport				|
	scgFailureTransfer						|
	f1CTrafficTransfer						|
	retrieveUEContextConfirm				|
	cPCCancel								|
	rachIndication							|
	dataCollectionReporting,
	...

}

-- **************************************************************
--
-- Interface Elementary Procedures
--
-- **************************************************************

handoverPreparation	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		HandoverRequest
	SUCCESSFUL OUTCOME		HandoverRequestAcknowledge
	UNSUCCESSFUL OUTCOME	HandoverPreparationFailure
	PROCEDURE CODE			id-handoverPreparation
	CRITICALITY				reject
}


sNStatusTransfer	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		SNStatusTransfer
	PROCEDURE CODE			id-sNStatusTransfer
	CRITICALITY				ignore
}


handoverCancel	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		HandoverCancel
	PROCEDURE CODE			id-handoverCancel
	CRITICALITY				ignore
}


retrieveUEContext	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		RetrieveUEContextRequest
	SUCCESSFUL OUTCOME		RetrieveUEContextResponse
	UNSUCCESSFUL OUTCOME	RetrieveUEContextFailure
	PROCEDURE CODE			id-retrieveUEContext
	CRITICALITY				reject
}


rANPaging	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		RANPaging
	PROCEDURE CODE			id-rANPaging
	CRITICALITY				reject
}


xnUAddressIndication	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		XnUAddressIndication
	PROCEDURE CODE			id-xnUAddressIndication
	CRITICALITY				reject
}


uEContextRelease	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		UEContextRelease
	PROCEDURE CODE			id-uEContextRelease
	CRITICALITY				reject
}


sNGRANnodeAdditionPreparation	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		SNodeAdditionRequest
	SUCCESSFUL OUTCOME		SNodeAdditionRequestAcknowledge
	UNSUCCESSFUL OUTCOME	SNodeAdditionRequestReject
	PROCEDURE CODE			id-sNGRANnodeAdditionPreparation
	CRITICALITY				reject
}


sNGRANnodeReconfigurationCompletion	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		SNodeReconfigurationComplete
	PROCEDURE CODE			id-sNGRANnodeReconfigurationCompletion
	CRITICALITY				reject
}


mNGRANnodeinitiatedSNGRANnodeModificationPreparation	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		SNodeModificationRequest
	SUCCESSFUL OUTCOME		SNodeModificationRequestAcknowledge
	UNSUCCESSFUL OUTCOME	SNodeModificationRequestReject
	PROCEDURE CODE			id-mNGRANnodeinitiatedSNGRANnodeModificationPreparation
	CRITICALITY				reject
}


sNGRANnodeinitiatedSNGRANnodeModificationPreparation	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		SNodeModificationRequired
	SUCCESSFUL OUTCOME		SNodeModificationConfirm
	UNSUCCESSFUL OUTCOME	SNodeModificationRefuse
	PROCEDURE CODE			id-sNGRANnodeinitiatedSNGRANnodeModificationPreparation
	CRITICALITY				reject
}


mNGRANnodeinitiatedSNGRANnodeRelease	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		SNodeReleaseRequest
	SUCCESSFUL OUTCOME		SNodeReleaseRequestAcknowledge
	UNSUCCESSFUL OUTCOME	SNodeReleaseReject
	PROCEDURE CODE			id-mNGRANnodeinitiatedSNGRANnodeRelease
	CRITICALITY				reject
}


sNGRANnodeinitiatedSNGRANnodeRelease	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		SNodeReleaseRequired
	SUCCESSFUL OUTCOME		SNodeReleaseConfirm
	PROCEDURE CODE			id-sNGRANnodeinitiatedSNGRANnodeRelease
	CRITICALITY				reject
}


sNGRANnodeCounterCheck	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		SNodeCounterCheckRequest
	PROCEDURE CODE			id-sNGRANnodeCounterCheck
	CRITICALITY				reject
}


sNGRANnodeChange		XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		SNodeChangeRequired
	SUCCESSFUL OUTCOME		SNodeChangeConfirm
	UNSUCCESSFUL OUTCOME	SNodeChangeRefuse
	PROCEDURE CODE			id-sNGRANnodeChange
	CRITICALITY				reject
}


rRCTransfer	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		RRCTransfer
	PROCEDURE CODE			id-rRCTransfer
	CRITICALITY				reject
}


xnRemoval	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		XnRemovalRequest
	SUCCESSFUL OUTCOME		XnRemovalResponse
	UNSUCCESSFUL OUTCOME		XnRemovalFailure
	PROCEDURE CODE			id-xnRemoval
	CRITICALITY				reject
}


xnSetup	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		XnSetupRequest
	SUCCESSFUL OUTCOME		XnSetupResponse
	UNSUCCESSFUL OUTCOME		XnSetupFailure
	PROCEDURE CODE			id-xnSetup
	CRITICALITY				reject
}


nGRANnodeConfigurationUpdate	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		NGRANNodeConfigurationUpdate
	SUCCESSFUL OUTCOME		NGRANNodeConfigurationUpdateAcknowledge
	UNSUCCESSFUL OUTCOME	NGRANNodeConfigurationUpdateFailure
	PROCEDURE CODE			id-nGRANnodeConfigurationUpdate
	CRITICALITY				reject
}

partialUEContextTransfer	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		PartialUEContextTransfer
	SUCCESSFUL OUTCOME		PartialUEContextTransferAcknowledge
	UNSUCCESSFUL OUTCOME	PartialUEContextTransferFailure
	PROCEDURE CODE			id-partialUEContextTransfer
	CRITICALITY				reject
}

e-UTRA-NR-CellResourceCoordination	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		E-UTRA-NR-CellResourceCoordinationRequest
	SUCCESSFUL OUTCOME		E-UTRA-NR-CellResourceCoordinationResponse
	PROCEDURE CODE			id-e-UTRA-NR-CellResourceCoordination
	CRITICALITY				reject
}


cellActivation	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		CellActivationRequest
	SUCCESSFUL OUTCOME		CellActivationResponse
	UNSUCCESSFUL OUTCOME	CellActivationFailure
	PROCEDURE CODE			id-cellActivation
	CRITICALITY				reject
}


reset	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		ResetRequest
	SUCCESSFUL OUTCOME		ResetResponse
	PROCEDURE CODE			id-reset
	CRITICALITY				reject
}


errorIndication	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		ErrorIndication
	PROCEDURE CODE			id-errorIndication
	CRITICALITY				ignore
}


notificationControl			XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		NotificationControlIndication
	PROCEDURE CODE			id-notificationControl
	CRITICALITY				ignore
}


activityNotification		XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		ActivityNotification
	PROCEDURE CODE			id-activityNotification
	CRITICALITY				ignore
}


privateMessage			XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		PrivateMessage
	PROCEDURE CODE			id-privateMessage
	CRITICALITY				ignore
}

secondaryRATDataUsageReport	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		SecondaryRATDataUsageReport
	PROCEDURE CODE			id-secondaryRATDataUsageReport
	CRITICALITY				reject
}

deactivateTrace XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		DeactivateTrace
	PROCEDURE CODE			id-deactivateTrace
	CRITICALITY				ignore
}

traceStart XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		TraceStart
	PROCEDURE CODE			id-traceStart
	CRITICALITY				ignore
}

handoverSuccess			XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		HandoverSuccess
	PROCEDURE CODE			id-handoverSuccess
	CRITICALITY				ignore
}

conditionalHandoverCancel	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		ConditionalHandoverCancel
	PROCEDURE CODE			id-conditionalHandoverCancel
	CRITICALITY				ignore
}

earlyStatusTransfer		XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		EarlyStatusTransfer
	PROCEDURE CODE			id-earlyStatusTransfer
	CRITICALITY				ignore
}

failureIndication XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		FailureIndication
	PROCEDURE CODE			id-failureIndication
	CRITICALITY				ignore
}

handoverReport XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		HandoverReport
	PROCEDURE CODE			id-handoverReport
	CRITICALITY				ignore
}

resourceStatusReportingInitiation	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE				ResourceStatusRequest
	SUCCESSFUL OUTCOME				ResourceStatusResponse
	UNSUCCESSFUL OUTCOME			ResourceStatusFailure
	PROCEDURE CODE					id-resourceStatusReportingInitiation
	CRITICALITY						reject
}

resourceStatusReporting XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		ResourceStatusUpdate
	PROCEDURE CODE			id-resourceStatusReporting
	CRITICALITY				ignore
}

mobilitySettingsChange	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE				MobilityChangeRequest
	SUCCESSFUL OUTCOME				MobilityChangeAcknowledge
	UNSUCCESSFUL OUTCOME			MobilityChangeFailure
	PROCEDURE CODE					id-mobilitySettingsChange
	CRITICALITY						reject
}


accessAndMobilityIndication XNAP-ELEMENTARY-PROCEDURE ::={
	INITIATING MESSAGE		AccessAndMobilityIndication
	PROCEDURE CODE			id-accessAndMobilityIndication
	CRITICALITY				ignore
}

cellTrafficTrace XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		CellTrafficTrace
	PROCEDURE CODE			id-cellTrafficTrace
	CRITICALITY				ignore
}

rANMulticastGroupPaging 	XNAP-ELEMENTARY-PROCEDURE ::={
	INITIATING MESSAGE 		RANMulticastGroupPaging
	PROCEDURE CODE			id-RANMulticastGroupPaging
	CRITICALITY				reject
}

scgFailureInformationReport XNAP-ELEMENTARY-PROCEDURE ::={
	INITIATING MESSAGE 		ScgFailureInformationReport
	PROCEDURE CODE			id-scgFailureInformationReport
	CRITICALITY				ignore
}

scgFailureTransfer XNAP-ELEMENTARY-PROCEDURE ::={
	INITIATING MESSAGE		ScgFailureTransfer
	PROCEDURE CODE			id-scgFailureTransfer
	CRITICALITY				ignore
}

f1CTrafficTransfer			XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		F1CTrafficTransfer
	PROCEDURE CODE			id-f1CTrafficTransfer
	CRITICALITY				reject
}

iABTransportMigrationManagement XNAP-ELEMENTARY-PROCEDURE ::={
	INITIATING MESSAGE		IABTransportMigrationManagementRequest
	SUCCESSFUL OUTCOME		IABTransportMigrationManagementResponse
	UNSUCCESSFUL OUTCOME	IABTransportMigrationManagementReject
	PROCEDURE CODE			id-iABTransportMigrationManagement
	CRITICALITY				reject
}

iABTransportMigrationModification XNAP-ELEMENTARY-PROCEDURE ::={
	INITIATING MESSAGE		IABTransportMigrationModificationRequest
	SUCCESSFUL OUTCOME		IABTransportMigrationModificationResponse
	PROCEDURE CODE			id-iABTransportMigrationModification
	CRITICALITY				reject
}

iABResourceCoordination XNAP-ELEMENTARY-PROCEDURE ::={
	INITIATING MESSAGE		IABResourceCoordinationRequest
	SUCCESSFUL OUTCOME		IABResourceCoordinationResponse
	PROCEDURE CODE			id-iABResourceCoordination
	CRITICALITY				reject
}

retrieveUEContextConfirm	XNAP-ELEMENTARY-PROCEDURE ::={
	INITIATING MESSAGE		RetrieveUEContextConfirm
	PROCEDURE CODE			id-retrieveUEContextConfirm
	CRITICALITY				reject
}

cPCCancel XNAP-ELEMENTARY-PROCEDURE ::={
	INITIATING MESSAGE		CPCCancel
	PROCEDURE CODE			id-cPCCancel
	CRITICALITY				ignore
}

rachIndication XNAP-ELEMENTARY-PROCEDURE ::={
	INITIATING MESSAGE		RachIndication
	PROCEDURE CODE			id-rachIndication
	CRITICALITY				ignore
}

dataCollectionReportingInitiation	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE				DataCollectionRequest
	SUCCESSFUL OUTCOME				DataCollectionResponse
	UNSUCCESSFUL OUTCOME			DataCollectionFailure
	PROCEDURE CODE					id-dataCollectionReportingInitiation
	CRITICALITY						reject
}

dataCollectionReporting	XNAP-ELEMENTARY-PROCEDURE ::= {
	INITIATING MESSAGE		DataCollectionUpdate
	PROCEDURE CODE			id-dataCollectionReporting
	CRITICALITY				ignore
}


END
-- ASN1STOP

9.3.4	PDU Definitions
-- ASN1START
-- **************************************************************
--
-- PDU definitions for XnAP.
--
-- **************************************************************

XnAP-PDU-Contents {
itu-t (0) identified-organization (4) etsi (0) mobileDomain (0)
ngran-access (22) modules (3) xnap (2) version1 (1) xnap-PDU-Contents (1) }

DEFINITIONS AUTOMATIC TAGS ::=

BEGIN	

-- **************************************************************
--
-- IE parameter types from other modules.
--
-- **************************************************************

IMPORTS

	ActivationIDforCellActivation,
	AMF-Region-Information,
	AMF-UE-NGAP-ID,
	AS-SecurityInformation,
	AssistanceDataForRANPaging,
	AerialUESubscriptionInformation,
	A2XPC5QoSParameters,
	BitRate,
	Cause,
	CellAndCapacityAssistanceInfo-EUTRA,
	CellAndCapacityAssistanceInfo-NR,
	CellAssistanceInfo-EUTRA,
	CellAssistanceInfo-NR,
	CHOinformation-Req,
	CHOinformation-Ack,
	CHOinformation-AddReq,
	CHOinformation-AddReqAck,
	CHOinformation-ModReq,
	CHO-MRDC-EarlyDataForwarding,
	CHO-MRDC-Indicator,
	CPTransportLayerInformation,
	TNLA-To-Add-List,
	TNLA-To-Update-List,
	TNLA-To-Remove-List,
	TNLA-Setup-List,
	TNLA-Failed-To-Setup-List,
	CriticalityDiagnostics,
	XnUAddressInfoperPDUSession-List,
	DAPSResponseInfo-List,
	DataTrafficResourceIndication,
	DeliveryStatus,
	DesiredActNotificationLevel,
	DRB-ID,
	DRB-List,
	DRB-Number,
	DRBsSubjectToDLDiscarding-List,
	DRBsSubjectToEarlyStatusTransfer-List,
	DRBsSubjectToStatusTransfer-List,
	DRBToQoSFlowMapping-List,
	E-UTRA-CGI,
	ExpectedUEActivityBehaviour,
	ExpectedUEBehaviour,
	ExtendedUEIdentityIndexValue,
	FiveGCMobilityRestrictionListContainer,
	GlobalCell-ID,
	GlobalNG-RANNode-ID,
	GlobalNG-RANCell-ID,
	GUAMI,
	InterfaceInstanceIndication,
	I-RNTI,
	Local-NG-RAN-Node-Identifier,
	LocationInformationSNReporting,
	LocationReportingInformation,
	LowerLayerPresenceStatusChange,
	LTEA2XServicesAuthorized,
	LTEUESidelinkAggregateMaximumBitRate,
	LTEV2XServicesAuthorized,
	MR-DC-ResourceCoordinationInfo,
	ServedCells-E-UTRA,
	ServedCells-NR,
	ServedCellsToUpdate-E-UTRA,
	ServedCellsToUpdate-NR,
	MAC-I,
	MaskedIMEISV,
	MDT-Configuration,
	MDTPLMNList,
	MobilityRestrictionList,
	Neighbour-NG-RAN-Node-List,
	NG-RAN-Cell-Identity,
	NG-RANnodeUEXnAPID,
	NR-CGI,
	NE-DC-TDM-Pattern,
	NRA2XServicesAuthorized,
	NRUESidelinkAggregateMaximumBitRate,
	NRV2XServicesAuthorized,
	PagingDRX,
	EUTRAPagingeDRXInformation,
	PagingPriority,
	PartialListIndicator,
	PLMN-Identity,
	PDCPChangeIndication,
	PDUSessionAggregateMaximumBitRate,
	PDUSession-ID,
	PDUSession-List,
	PDUSession-List-withCause,
	PDUSession-List-withDataForwardingFromTarget,
	PDUSession-List-withDataForwardingRequest,
	PDUSessionResourcesAdmitted-List,
	PDUSessionResourcesNotAdmitted-List,
	PDUSessionResourcesToBeSetup-List,
	PDUSessionResourceChangeRequiredInfo-SNterminated,
	PDUSessionResourceChangeRequiredInfo-MNterminated,
	PDUSessionResourceChangeConfirmInfo-SNterminated,
	PDUSessionResourceChangeConfirmInfo-MNterminated,
	PDUSessionResourceSecondaryRATUsageList,
	PDUSessionResourceSetupInfo-SNterminated,
	PDUSessionResourceSetupInfo-MNterminated,
	PDUSessionResourceSetupResponseInfo-SNterminated,
	PDUSessionResourceSetupResponseInfo-MNterminated,
	PDUSessionResourceModificationInfo-SNterminated,
	PDUSessionResourceModificationInfo-MNterminated,
	PDUSessionResourceModificationResponseInfo-SNterminated,
	PDUSessionResourceModificationResponseInfo-MNterminated,
	PDUSessionResourceModConfirmInfo-SNterminated,
	PDUSessionResourceModConfirmInfo-MNterminated,
	PDUSessionResourceModRqdInfo-SNterminated,
	PDUSessionResourceModRqdInfo-MNterminated,
	PDUSessionType,
	PC5QoSParameters,
	QoSFlowIdentifier,
	QoSFlowNotificationControlIndicationInfo,
	QoSFlows-List,
	RANPagingArea,
	ResetRequestTypeInfo,
	ResetResponseTypeInfo,
	RFSP-Index,
	RRCConfigIndication,
	RRCResumeCause,
	SCGConfigurationQuery,
	SCGreconfigNotification,
	SecurityIndication,
	S-NG-RANnode-SecurityKey,
	SpectrumSharingGroupID,
	SplitSRBsTypes,
	S-NG-RANnode-Addition-Trigger-Ind,
	S-NSSAI,
	TargetCellList,
	TAISupport-List,
	Target-CGI,
	TimeToWait,
	TraceActivation,
	UEAggregateMaximumBitRate,
	UEContextID,
	UEContextInfoRetrUECtxtResp,
	UEContextKeptIndicator,
	UEHistoryInformation,
	UEIdentityIndexValue,
	UERadioCapabilityForPaging,
	UERadioCapabilityID,
	UERANPagingIdentity,
	UESecurityCapabilities,
	UPTransportLayerInformation,
	UserPlaneTrafficActivityReport,
	XnBenefitValue,
	RANPagingFailure,
	TNLConfigurationInfo,
	MaximumCellListSize,
	MessageOversizeNotification,
	NG-RANTraceID,
	MobilityInformation,
	InitiatingCondition-FailureIndication,
	HandoverReportType,
	TargetCellinEUTRAN,
	C-RNTI,
	UERLFReportContainer,
	Measurement-ID,
	RegistrationRequest,
	ReportCharacteristics,
	CellToReport,
	ReportingPeriodicity,
	CellMeasurementResult,
	UEHistoryInformationFromTheUE,
	MobilityParametersInformation,
	MobilityParametersModificationRange,
	RAReport,
	IABNodeIndication,
	SNTriggered,
	SCGIndicator,
	UESpecificDRX,
	DirectForwardingPathAvailability,
	TransportLayerAddress,
	PrivacyIndicator,
	URIaddress,
	MBS-Session-ID,
	UEIdentityIndexList-MBSGroupPaging,
	MBS-SessionInformation-List,
	MBS-SessionInformationResponse-List,
	SuccessfulHOReportInformation,
	PSCellHistoryInformationRetrieve,
	SSBOffsets-List,
	NG-RANnode2SSBOffsetsModificationRange,
	Coverage-Modification-List,
	SCGFailureReportContainer,
	SNMobilityInformation,
	PSCellChangeHistory,
	CHOConfiguration,
	SCGUEHistoryInformation,
	F1CTrafficContainer,
	NoPDUSessionIndication,
	IAB-TNL-Address-Request,
	IAB-TNL-Address-Response,
	TrafficIndex,
	TrafficProfile,
	TrafficToBeReleaseInformation,
	F1-TerminatingTopologyBHInformation,
	Non-F1-TerminatingTopologyBHInformation,
	BHInfoList,
	IABTNLAddress,
	IABCellInformation,
	IABTNLAddressException,
	TimeSynchronizationAssistanceInformation,
	SCGActivationRequest,
	SCGActivationStatus,
	CPAInformationRequest,
	CPAInformationAck,
	CPCInformationRequired,
	CPCInformationConfirm,
	CPAInformationModReq,
	CPAInformationModReqAck,
	CPC-DataForwarding-Indicator,
	CPCInformationUpdate,
	CPACInformationModRequired,
	QMCConfigInfo,
	FiveGProSeAuthorized,
	FiveGProSePC5QoSParameters,
	ServedCellSpecificInfoReq-NR,
	NRPagingeDRXInformation,
	NRPagingeDRXInformationforRRCINACTIVE,
	SDTSupportRequest,
	SDT-Termination-Request,
	SDTPartialUEContextInfo,
	SDTDataForwardingDRBList,
	PEIPSassistanceInformation,
	UESliceMaximumBitRateList,
	PagingCause,
	MDTPLMNModificationList,
	F1-terminatingIAB-donorIndicator,
	SRB-ID,
	AdditionalListofPDUSessionResourceChangeConfirmInfo-SNterminated,
	HashedUEIdentityIndexValue,
	MBS-DataForwarding-Indicator,
	IABAuthorizationStatus,
	NID,
	MT-SDT-Information,
	PosPartialUEContextInfo,
	SRSConfiguration,
	RaReportIndicationList,
	SuccessfulPSCellChangeReportInformation,
	CPACConfiguration,
	TimeSinceFailure,
	SPRAvailability,
	DLLBTFailureInformationRequest,
	DLLBTFailureInformationList,
	CellBasedUETrajectoryPrediction,
	DataCollectionID,
	RequestedPredictionTime,
	NodeMeasurementInitiationResult-List,
	CellMeasurementInitiationResult-List,
	UEAssociatedInfoResult-List,
	UETrajectoryCollectionConfiguration,
	UEPerformanceCollectionConfiguration,
	CellMeasurementResultForDataCollection-List,
	CellToReportForDataCollection-List,
	CandidateRelayUEInfoList,
	NRPagingLongeDRXInformationforRRCINACTIVE,
	QMCCoordinationRequest,
	QMCCoordinationResponse,
	DirectForwardingPathAvailabilityWithSourceMN,
	Conditional-Reconfig-List,
	PDUSetbasedHandlingIndicator,
	MobileIAB-AuthorizationStatus,
	BAPAddress,
	S-CPAC-Request,
	SK-COUNTER,
	RegistrationRequestForDataCollection,
	ReportCharacteristicsForDataCollection,
	ReportingPeriodicityForDataCollection,
	NodeAssociatedInfoResult,
	SLPositioning-Ranging-Services-Info,
	PDUSessionsListToBeReleased-UPError,
	UserPlaneFailureIndication,
	SRSPositioningConfigOrActivationRequest,
	NRPPaPositioningInformation,
	SemipersistentPositioningInformation





FROM XnAP-IEs

	PrivateIE-Container{},
	ProtocolExtensionContainer{},
	ProtocolIE-Container{},
	ProtocolIE-ContainerList{},
	ProtocolIE-ContainerPair{},
	ProtocolIE-ContainerPairList{},
	ProtocolIE-Single-Container{},
	XNAP-PRIVATE-IES,
	XNAP-PROTOCOL-EXTENSION,
	XNAP-PROTOCOL-IES,
	XNAP-PROTOCOL-IES-PAIR
FROM XnAP-Containers


	id-A2XPC5QoSParameters,
	id-ActivatedServedCells,
	id-ActivationIDforCellActivation,
	id-AdditionalDRBIDs,
	id-AerialUESubscriptionInformation,
	id-AMF-Region-Information,
	id-AMF-Region-Information-To-Add,
	id-AMF-Region-Information-To-Delete,
	id-AssistanceDataForRANPaging,
	id-AvailableDRBIDs,
	id-Cause,
	id-cellAssistanceInfo-EUTRA,
	id-cellAssistanceInfo-NR,
	id-CellAndCapacityAssistanceInfo-EUTRA,
	id-CellAndCapacityAssistanceInfo-NR,
	id-ConfigurationUpdateInitiatingNodeChoice,
	id-UEContextID,
	id-CriticalityDiagnostics,
	id-XnUAddressInfoperPDUSession-List,
	id-DesiredActNotificationLevel,
	id-DRBsSubjectToStatusTransfer-List,
	id-ExpectedUEBehaviour,
	id-ExtendedUEIdentityIndexValue,
	id-FiveGCMobilityRestrictionListContainer,
	id-GlobalNG-RAN-node-ID,
	id-GUAMI,
	id-indexToRatFrequSelectionPriority,
	id-List-of-served-cells-E-UTRA,
	id-List-of-served-cells-NR,
	id-LocationInformationSN,
	id-LocationInformationSNReporting,
	id-LocationReportingInformation,
	id-LTEA2XServicesAuthorized,
	id-LTEA2XUEPC5AggregateMaximumBitRate,
	id-LTEUESidelinkAggregateMaximumBitRate,
	id-LTEV2XServicesAuthorized,
	id-MAC-I,
	id-MaskedIMEISV,
	id-MDT-Configuration,
	id-MDTPLMNList,
	id-MN-to-SN-Container,
	id-MobilityRestrictionList,
	id-M-NG-RANnodeUEXnAPID,
	id-new-NG-RAN-Cell-Identity,
	id-newNG-RANnodeUEXnAPID,
	id-NRA2XServicesAuthorized,
	id-NRA2XUEPC5AggregateMaximumBitRate,
	id-NRUESidelinkAggregateMaximumBitRate,
	id-NRV2XServicesAuthorized,
	id-oldNG-RANnodeUEXnAPID,
	id-OldtoNewNG-RANnodeResumeContainer,
	id-PagingCause,
	id-PagingDRX,
	id-EUTRAPagingeDRXInformation,
	id-PagingPriority,
	id-PartialListIndicator-EUTRA,
	id-PartialListIndicator-NR,
	id-PCellID,
	id-PDUSessionResourceSecondaryRATUsageList,
	id-PDUSessionResourcesActivityNotifyList,
	id-PDUSessionResourcesAdmitted-List,
	id-PDUSessionResourcesNotAdmitted-List,
	id-PDUSessionResourcesNotifyList,
	id-PDUSessionToBeAddedAddReq,
	id-PDUSessionToBeReleased-RelReqAck,
	id-procedureStage,
	id-RANPagingArea,
	id-requestedSplitSRB,
	id-RequiredNumberOfDRBIDs,
	id-ResetRequestTypeInfo,
	id-ResetResponseTypeInfo,
	id-RespondingNodeTypeConfigUpdateAck,
	id-RRCResumeCause,
	id-SCGreconfigNotification,
	id-selectedPLMN,
	id-ServedCellsToActivate,
	id-servedCellsToUpdate-E-UTRA,
	id-ServedCellsToUpdateInitiatingNodeChoice,
	id-servedCellsToUpdate-NR,
	id-sourceNG-RANnodeUEXnAPID,
	id-SpareDRBIDs,
	id-S-NG-RANnodeMaxIPDataRate-UL,
	id-S-NG-RANnodeMaxIPDataRate-DL,
	id-S-NG-RANnodeUEXnAPID,
	id-TAISupport-list,
	id-Target2SourceNG-RANnodeTranspContainer,
	id-targetCellGlobalID,
	id-targetNG-RANnodeUEXnAPID,
	id-TimeToWait,
	id-TNLA-To-Add-List,
	id-TNLA-To-Update-List,
	id-TNLA-To-Remove-List,
	id-TNLA-Setup-List,
	id-TNLA-Failed-To-Setup-List,
	id-TraceActivation,
	id-UEContextInfoHORequest,
	id-UEContextInfoRetrUECtxtResp,
	id-UEContextKeptIndicator,
	id-UEContextRefAtSN-HORequest,
	id-UEHistoryInformation,
	id-UEIdentityIndexValue,
	id-UERANPagingIdentity,
	id-UESecurityCapabilities,
	id-UserPlaneTrafficActivityReport,
	id-XnRemovalThreshold,
	id-PDUSessionAdmittedAddedAddReqAck,
	id-PDUSessionNotAdmittedAddReqAck,
	id-SN-to-MN-Container,
	id-RRCConfigIndication,
	id-SplitSRB-RRCTransfer,
	id-UEReportRRCTransfer,
	id-PDUSessionReleasedList-RelConf,
	id-BearersSubjectToCounterCheck,
	id-PDUSessionToBeReleasedList-RelRqd,
	id-ResponseInfo-ReconfCompl,
	id-initiatingNodeType-ResourceCoordRequest,
	id-respondingNodeType-ResourceCoordResponse,
	id-PDUSessionToBeReleased-RelReq,
	id-PDUSession-SNChangeRequired-List,
	id-PDUSession-SNChangeConfirm-List,
	id-PDCPChangeIndication,
	id-PC5QoSParameters,
	id-SCGConfigurationQuery,
	id-UEContextInfo-SNModRequest,
	id-requestedSplitSRBrelease,
	id-PDUSessionAdmitted-SNModResponse,
	id-PDUSessionNotAdmitted-SNModResponse,
	id-admittedSplitSRB,
	id-admittedSplitSRBrelease,
	id-PDUSessionAdmittedModSNModConfirm,
	id-PDUSessionReleasedSNModConfirm,
	id-s-ng-RANnode-SecurityKey,
	id-PDUSessionToBeModifiedSNModRequired,
	id-S-NG-RANnodeUE-AMBR,
	id-PDUSessionToBeReleasedSNModRequired,
	id-target-S-NG-RANnodeID,
	id-S-NSSAI,
	id-MR-DC-ResourceCoordinationInfo,
	id-RANPagingFailure,
	id-UERadioCapabilityForPaging,
	id-PDUSessionDataForwarding-SNModResponse,
	id-Secondary-MN-Xn-U-TNLInfoatM,
	id-NE-DC-TDM-Pattern,
	id-InterfaceInstanceIndication,
	id-S-NG-RANnode-Addition-Trigger-Ind,
	id-SNTriggered,
	id-DRBs-transferred-to-MN,
	id-TNLConfigurationInfo,
	id-MessageOversizeNotification,
	id-NG-RANTraceID,
	id-FastMCGRecoveryRRCTransfer-SN-to-MN,
	id-FastMCGRecoveryRRCTransfer-MN-to-SN,
	id-RequestedFastMCGRecoveryViaSRB3,
	id-AvailableFastMCGRecoveryViaSRB3,
	id-RequestedFastMCGRecoveryViaSRB3Release,
	id-ReleaseFastMCGRecoveryViaSRB3,
	id-CHOinformation-Req,
	id-CHOinformation-Ack,
	id-targetCellsToCancel,
	id-requestedTargetCellGlobalID,
	id-DAPSResponseInfo-List,
	id-CHO-MRDC-EarlyDataForwarding,
	id-CHO-MRDC-Indicator,
	id-MobilityInformation,
	id-InitiatingCondition-FailureIndication,
	id-UEHistoryInformationFromTheUE,
	id-HandoverReportType,
	id-HandoverCause,
	id-SourceCellCGI,
	id-TargetCellCGI,
	id-ReEstablishmentCellCGI,
	id-TargetCellinEUTRAN,
	id-SourceCellCRNTI,
	id-UERLFReportContainer,
	id-NGRAN-Node1-Measurement-ID,
	id-NGRAN-Node2-Measurement-ID,
	id-RegistrationRequest,
	id-ReportCharacteristics,
	id-CellToReport,
	id-ReportingPeriodicity,
	id-CellMeasurementResult,
	id-NG-RANnode1CellID,
	id-NG-RANnode2CellID,
	id-NG-RANnode1MobilityParameters,
	id-NG-RANnode2ProposedMobilityParameters,
	id-MobilityParametersModificationRange,
	id-RAReport,
	id-IABNodeIndication,
	id-UERadioCapabilityID,
	id-SCGIndicator,
	id-UESpecificDRX,
	id-PDUSessionExpectedUEActivityBehaviour,
	id-DirectForwardingPathAvailability,
	id-SourceNG-RAN-node-ID,
	id-TargetNodeID,
	id-ManagementBasedMDTPLMNList,
	id-PrivacyIndicator,
	id-TraceCollectionEntityIPAddress,
	id-TraceCollectionEntityURI,
	id-MBS-Session-ID,
	id-UEIdentityIndexList-MBSGroupPaging,
	id-MulticastRANPagingArea,
	id-MBS-SessionInformation-List,
	id-MBS-SessionInformationResponse-List,
	id-SuccessfulHOReportInformation,
	id-PSCellHistoryInformationRetrieve,
	id-SSBOffsets-List,
	id-NG-RANnode2SSBOffsetsModificationRange,
	id-Coverage-Modification-List,
	id-SourcePSCellCGI,
	id-FailedPSCellCGI,
	id-SCGFailureReportContainer,
	id-SNMobilityInformation,
	id-SourcePSCellID,
	id-SuitablePSCellCGI,
	id-PSCellChangeHistory,
	id-CHOConfiguration,
	id-SCGUEHistoryInformation,
	id-F1CTrafficContainer,
	id-NoPDUSessionIndication,
	id-F1-Terminating-IAB-DonorUEXnAPID,
	id-nonF1-Terminating-IAB-DonorUEXnAPID,
	id-IAB-TNL-Address-Request,
	id-IAB-TNL-Address-Response,
	id-TrafficToBeAddedList,
	id-TrafficToBeModifiedList,
	id-TrafficToBeReleaseInformation,
	id-TrafficAddedList,
	id-TrafficModifiedList,
	id-TrafficNotAddedList,
	id-TrafficNotModifiedList,
	id-TrafficRequiredToBeModifiedList,
	id-TrafficRequiredModifiedList,
	id-TrafficReleasedList,
	id-IABTNLAddressToBeAdded,
	id-IABTNLAddressToBeReleasedList,
	id-BoundaryNodeCellsList,
	id-ParentNodeCellsList,
	id-IABTNLAddressException,
	id-CHOinformation-AddReq,
	id-CHOinformation-AddReqAck,
	id-CHOinformation-ModReq,
	id-TimeSynchronizationAssistanceInformation,
	id-SCGActivationRequest,
	id-SCGActivationStatus,
	id-CPAInformationRequest,
	id-CPAInformationAck,
	id-CPCInformationRequired,
	id-CPCInformationConfirm,
	id-CPAInformationModReq,
	id-CPAInformationModReqAck,
	id-CPC-DataForwarding-Indicator,
	id-CPCInformationUpdate,
	id-CPACInformationModRequired,
	id-QMCConfigInfo,
	id-Local-NG-RAN-Node-Identifier,
	id-Neighbour-NG-RAN-Node-List,
	id-Local-NG-RAN-Node-Identifier-Removal,
	id-FiveGProSeAuthorized,
	id-FiveGProSePC5QoSParameters,
	id-FiveGProSeUEPC5AggregateMaximumBitRate,
	id-ServedCellSpecificInfoReq-NR,
	id-NRPagingeDRXInformation,
	id-NRPagingeDRXInformationforRRCINACTIVE,
	id-SDTSupportRequest,
	id-SDT-SRB-between-NewNode-OldNode,
	id-SDT-Termination-Request,
	id-SDTPartialUEContextInfo,
	id-SDTDataForwardingDRBList,
	id-PEIPSassistanceInformation,
	id-UESliceMaximumBitRateList,
	id-S-NG-RANnodeUE-Slice-MBR,
	id-ManagementBasedMDTPLMNModificationList,
	id-F1-terminatingIAB-donorIndicator,
	id-AdditionalListofPDUSessionResourceChangeConfirmInfo-SNterminated,
	id-HashedUEIdentityIndexValue,
	id-MBS-DataForwarding-Indicator,
	id-IABAuthorizationStatus,
	id-SelectedNID,
	id-MT-SDT-Information,
	id-PosPartialUEContextInfo,
	id-SRSConfiguration,
	id-RaReportIndicationList,
	id-SuccessfulPSCellChangeReportInformation,
	id-CPACConfiguration,
	id-TargetCellCRNTI,
	id-TimeSinceFailure,
	id-SPRAvailability,
	id-DLLBTFailureInformationRequest,
	id-DLLBTFailureInformationList,
	id-CellBasedUETrajectoryPrediction,
	id-DataCollectionID,
	id-RequestedPredictionTime,
	id-NodeMeasurementInitiationResult-List,
	id-CellMeasurementInitiationResult-List,
	id-UEAssociatedInfoResult-List,
	id-UETrajectoryCollectionConfiguration,
	id-UEPerformanceCollectionConfiguration,
	id-CellMeasurementResultForDataCollection-List,
	id-CellToReportForDataCollection-List,
	id-CandidateRelayUEInfoList,
	id-NRCellsAndSSBsList,
	id-ActivatedNRCellsAndSSBsList,
	id-NRPagingLongeDRXInformationforRRCINACTIVE,
	id-QMCCoordinationRequest,
	id-QMCCoordinationResponse,
	id-QoE-Measurement-Results,
	id-Src-SN-to-Tgt-SNQMCInfoInquiry,
	id-DirectForwardingPathAvailabilityWithSourceMN,
	id-accessed-PSCellID,
	id-conditional-Reconfig-ToCancel-List,
	id-PDUSetbasedHandlingIndicator,
	id-MobileIAB-AuthorizationStatus,
	id-MIAB-MT-BAP-Address,
	id-S-CPAC-Request,
	id-sk-Counter,
	id-Source-M-NG-RANnodeID,
	id-SourceSN-to-TargetSN-QMCInfo,
	id-RegistrationRequestForDataCollection,
	id-ReportCharacteristicsForDataCollection,
	id-ReportingPeriodicityForDataCollection,
	id-NodeAssociatedInfoResult,
	id-SLPositioning-Ranging-Services-Info,
	id-PDUSessionsListToBeReleased-UPError,
	id-UserPlaneFailureIndication,
	id-SRSPositioningConfigOrActivationRequest,
	id-NRPPaPositioningInformation,
	id-SemipersistentPositioningInformation,



	maxnoofCellsinNG-RANnode,
	maxnoofDRBs,
	maxnoofPDUSessions,
	maxnoofQoSFlows,
	maxnoofServedCellsIAB,
	maxnoofTrafficIndexEntries,
	maxnoofTLAsIAB,
	maxnoofBAPControlPDURLCCHs,
	maxnoofServingCells,
	maxnoofSSBAreas

FROM XnAP-Constants;

-- **************************************************************
--
-- HANDOVER REQUEST
--
-- **************************************************************

HandoverRequest ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{HandoverRequest-IEs}},
	...
}

HandoverRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-sourceNG-RANnodeUEXnAPID					CRITICALITY reject	TYPE NG-RANnodeUEXnAPID									PRESENCE mandatory}|
	{ ID id-Cause										CRITICALITY reject	TYPE Cause													PRESENCE mandatory}|
	{ ID id-targetCellGlobalID							CRITICALITY reject	TYPE Target-CGI												PRESENCE mandatory}|
	{ ID id-GUAMI										CRITICALITY reject	TYPE GUAMI													PRESENCE mandatory}|
	{ ID id-UEContextInfoHORequest						CRITICALITY reject	TYPE UEContextInfoHORequest								PRESENCE mandatory}|
	{ ID id-TraceActivation								CRITICALITY ignore	TYPE TraceActivation										PRESENCE optional }|
	{ ID id-MaskedIMEISV								CRITICALITY ignore	TYPE MaskedIMEISV											PRESENCE optional }|
	{ ID id-UEHistoryInformation						CRITICALITY ignore	TYPE UEHistoryInformation								PRESENCE mandatory}|
	{ ID id-UEContextRefAtSN-HORequest					CRITICALITY ignore	TYPE UEContextRefAtSN-HORequest						PRESENCE optional }|
	{ ID id-CHOinformation-Req							CRITICALITY reject	TYPE CHOinformation-Req									PRESENCE optional }|
	{ ID id-NRV2XServicesAuthorized						CRITICALITY ignore	TYPE NRV2XServicesAuthorized							PRESENCE optional }|
	{ ID id-LTEV2XServicesAuthorized					CRITICALITY ignore	TYPE LTEV2XServicesAuthorized							PRESENCE optional }|
	{ ID id-PC5QoSParameters							CRITICALITY ignore	TYPE PC5QoSParameters										PRESENCE optional }|
	{ ID id-MobilityInformation							CRITICALITY ignore	TYPE MobilityInformation									PRESENCE optional }|
	{ ID id-UEHistoryInformationFromTheUE				CRITICALITY ignore	TYPE UEHistoryInformationFromTheUE					PRESENCE optional }|
	{ ID id-IABNodeIndication							CRITICALITY reject	TYPE IABNodeIndication									PRESENCE optional }|
	{ ID id-NoPDUSessionIndication						CRITICALITY ignore	TYPE NoPDUSessionIndication								PRESENCE optional }|
	{ ID id-TimeSynchronizationAssistanceInformation	CRITICALITY ignore	TYPE TimeSynchronizationAssistanceInformation		PRESENCE optional }|
	{ ID id-QMCConfigInfo								CRITICALITY ignore	TYPE QMCConfigInfo											PRESENCE optional }|
	{ ID id-FiveGProSeAuthorized						CRITICALITY ignore	TYPE FiveGProSeAuthorized								PRESENCE optional }|
	{ ID id-FiveGProSePC5QoSParameters					CRITICALITY ignore	TYPE FiveGProSePC5QoSParameters						PRESENCE optional }|
	{ ID id-IABAuthorizationStatus						CRITICALITY ignore	TYPE IABAuthorizationStatus								PRESENCE optional }|
	{ ID id-DLLBTFailureInformationRequest				CRITICALITY ignore	TYPE DLLBTFailureInformationRequest					PRESENCE optional }|
	{ ID id-AerialUESubscriptionInformation				CRITICALITY ignore	TYPE AerialUESubscriptionInformation					PRESENCE optional }|
	{ ID id-NRA2XServicesAuthorized						CRITICALITY ignore	TYPE NRA2XServicesAuthorized							PRESENCE optional }|
	{ ID id-LTEA2XServicesAuthorized					CRITICALITY ignore	TYPE LTEA2XServicesAuthorized							PRESENCE optional }|
	{ ID id-A2XPC5QoSParameters							CRITICALITY ignore	TYPE A2XPC5QoSParameters									PRESENCE optional }|
	{ ID id-CellBasedUETrajectoryPrediction				CRITICALITY ignore	TYPE CellBasedUETrajectoryPrediction					PRESENCE optional }|
	{ ID id-DataCollectionID							CRITICALITY ignore	TYPE DataCollectionID										PRESENCE optional }|
	{ ID id-CandidateRelayUEInfoList					CRITICALITY reject	TYPE CandidateRelayUEInfoList							PRESENCE optional }|
	{ ID id-SourceSN-to-TargetSN-QMCInfo				CRITICALITY ignore	TYPE QMCConfigInfo											PRESENCE optional }|
	{ ID id-MobileIAB-AuthorizationStatus				CRITICALITY reject	TYPE MobileIAB-AuthorizationStatus					PRESENCE optional }|
	{ ID id-SLPositioning-Ranging-Services-Info			CRITICALITY ignore	TYPE SLPositioning-Ranging-Services-Info			PRESENCE optional },
	...
}

UEContextInfoHORequest ::= SEQUENCE {
	ng-c-UE-reference						AMF-UE-NGAP-ID,
	cp-TNL-info-source						CPTransportLayerInformation,
	ueSecurityCapabilities					UESecurityCapabilities,
	securityInformation						AS-SecurityInformation,
	indexToRatFrequencySelectionPriority	RFSP-Index															OPTIONAL,
	ue-AMBR									UEAggregateMaximumBitRate,
	pduSessionResourcesToBeSetup-List		PDUSessionResourcesToBeSetup-List,
	rrc-Context								OCTET STRING,
	locationReportingInformation			LocationReportingInformation										OPTIONAL,
	mrl										MobilityRestrictionList												OPTIONAL,
	iE-Extensions							ProtocolExtensionContainer { {UEContextInfoHORequest-ExtIEs} }	OPTIONAL,
	...
}

UEContextInfoHORequest-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	{ ID id-FiveGCMobilityRestrictionListContainer	CRITICALITY ignore	EXTENSION FiveGCMobilityRestrictionListContainer		PRESENCE optional }|
	{ ID id-NRUESidelinkAggregateMaximumBitRate		CRITICALITY ignore	EXTENSION NRUESidelinkAggregateMaximumBitRate			PRESENCE optional }|
	{ ID id-LTEUESidelinkAggregateMaximumBitRate	CRITICALITY ignore	EXTENSION LTEUESidelinkAggregateMaximumBitRate			PRESENCE optional }|
	{ ID id-MDTPLMNList								CRITICALITY ignore	EXTENSION MDTPLMNList											PRESENCE optional }|
	{ ID id-UERadioCapabilityID						CRITICALITY reject	EXTENSION UERadioCapabilityID								PRESENCE optional }|
	{ ID id-MBS-SessionInformation-List				CRITICALITY ignore	EXTENSION MBS-SessionInformation-List						PRESENCE optional }|
	{ ID id-FiveGProSeUEPC5AggregateMaximumBitRate	CRITICALITY ignore	EXTENSION NRUESidelinkAggregateMaximumBitRate			PRESENCE optional }|
	{ ID id-UESliceMaximumBitRateList				CRITICALITY ignore	EXTENSION UESliceMaximumBitRateList						PRESENCE optional }|
	{ ID id-NRA2XUEPC5AggregateMaximumBitRate		CRITICALITY ignore	EXTENSION NRUESidelinkAggregateMaximumBitRate			PRESENCE optional }|
	{ ID id-LTEA2XUEPC5AggregateMaximumBitRate		CRITICALITY ignore	EXTENSION LTEUESidelinkAggregateMaximumBitRate			PRESENCE optional },
	...
}

UEContextRefAtSN-HORequest ::= SEQUENCE {
	globalNG-RANNode-ID				GlobalNG-RANNode-ID,
	sN-NG-RANnodeUEXnAPID			NG-RANnodeUEXnAPID,
	iE-Extensions					ProtocolExtensionContainer { {UEContextRefAtSN-HORequest-ExtIEs} }	OPTIONAL,
	...
}

UEContextRefAtSN-HORequest-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	...
}

-- **************************************************************
--
-- HANDOVER REQUEST ACKNOWLEDGE
--
-- **************************************************************

HandoverRequestAcknowledge ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{HandoverRequestAcknowledge-IEs}},
	...
}

HandoverRequestAcknowledge-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-sourceNG-RANnodeUEXnAPID				CRITICALITY ignore	TYPE NG-RANnodeUEXnAPID								PRESENCE mandatory}|
	{ ID id-targetNG-RANnodeUEXnAPID				CRITICALITY ignore	TYPE NG-RANnodeUEXnAPID								PRESENCE mandatory}|
	{ ID id-PDUSessionResourcesAdmitted-List		CRITICALITY ignore	TYPE PDUSessionResourcesAdmitted-List			PRESENCE mandatory}|
	{ ID id-PDUSessionResourcesNotAdmitted-List		CRITICALITY ignore	TYPE PDUSessionResourcesNotAdmitted-List		PRESENCE optional }|
	{ ID id-Target2SourceNG-RANnodeTranspContainer	CRITICALITY ignore	TYPE OCTET STRING									PRESENCE mandatory}|
	{ ID id-UEContextKeptIndicator					CRITICALITY ignore	TYPE UEContextKeptIndicator						PRESENCE optional }|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore	TYPE CriticalityDiagnostics						PRESENCE optional }|
	{ ID id-DRBs-transferred-to-MN					CRITICALITY ignore	TYPE DRB-List										PRESENCE optional }|
	{ ID id-DAPSResponseInfo-List					CRITICALITY reject	TYPE DAPSResponseInfo-List						PRESENCE optional }|
	{ ID id-CHOinformation-Ack						CRITICALITY reject	TYPE CHOinformation-Ack								PRESENCE optional }|
	{ ID id-MBS-SessionInformationResponse-List		CRITICALITY ignore	TYPE MBS-SessionInformationResponse-List		PRESENCE optional }|
	{ ID id-RRCConfigIndication						CRITICALITY ignore	TYPE RRCConfigIndication							PRESENCE optional }|
	{ ID id-PDUSetbasedHandlingIndicator			CRITICALITY ignore	TYPE PDUSetbasedHandlingIndicator				PRESENCE optional },
	...
}

-- **************************************************************
--
-- HANDOVER PREPARATION FAILURE
--
-- **************************************************************

HandoverPreparationFailure ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{HandoverPreparationFailure-IEs}},
	...
}

HandoverPreparationFailure-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-sourceNG-RANnodeUEXnAPID				CRITICALITY ignore	TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-Cause									CRITICALITY ignore	TYPE Cause										PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore	TYPE CriticalityDiagnostics					PRESENCE optional }|
	{ ID id-requestedTargetCellGlobalID				CRITICALITY reject	TYPE Target-CGI									PRESENCE optional},
	...
}

-- **************************************************************
--
-- SN STATUS TRANSFER
--
-- **************************************************************

SNStatusTransfer ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{SNStatusTransfer-IEs}},
	...
}

SNStatusTransfer-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-sourceNG-RANnodeUEXnAPID				CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-targetNG-RANnodeUEXnAPID				CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-DRBsSubjectToStatusTransfer-List		CRITICALITY ignore		TYPE DRBsSubjectToStatusTransfer-List		PRESENCE mandatory}|
	{ ID id-CHOConfiguration							CRITICALITY ignore		TYPE CHOConfiguration								PRESENCE optional}|
	{ ID id-MobilityInformation						CRITICALITY ignore		TYPE MobilityInformation							PRESENCE optional},
	...
}

-- **************************************************************
--
-- UE CONTEXT RELEASE
--
-- **************************************************************

UEContextRelease ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{UEContextRelease-IEs}},
	...
}

UEContextRelease-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-sourceNG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-targetNG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory},
	...
}

-- **************************************************************
--
-- HANDOVER CANCEL
--
-- **************************************************************

HandoverCancel ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{HandoverCancel-IEs}},
	...
}

HandoverCancel-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-sourceNG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-targetNG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE optional }|
	{ ID id-Cause										CRITICALITY ignore		TYPE Cause											PRESENCE mandatory}|
	{ ID id-targetCellsToCancel							CRITICALITY reject		TYPE TargetCellList								PRESENCE optional},
	...
}

-- **************************************************************
--
-- HANDOVER SUCCESS
--
-- **************************************************************

HandoverSuccess ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{HandoverSuccess-IEs}},
	...
}

HandoverSuccess-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-sourceNG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-targetNG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-requestedTargetCellGlobalID					CRITICALITY reject		TYPE Target-CGI										PRESENCE mandatory}|
	{ ID id-accessed-PSCellID							CRITICALITY ignore		TYPE NR-CGI											PRESENCE optional},
	...
}

-- **************************************************************
--
-- CONDITIONAL HANDOVER CANCEL
--
-- **************************************************************

ConditionalHandoverCancel ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ ConditionalHandoverCancel-IEs}},
	...
}

ConditionalHandoverCancel-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-sourceNG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-targetNG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-Cause										CRITICALITY ignore		TYPE Cause											PRESENCE mandatory}|
	{ ID id-targetCellsToCancel							CRITICALITY reject		TYPE TargetCellList								PRESENCE optional}|
	{ ID id-conditional-Reconfig-ToCancel-List			CRITICALITY reject		TYPE Conditional-Reconfig-List				PRESENCE optional},
	...
}

-- **************************************************************
--
-- EARLY STATUS TRANSFER
--
-- **************************************************************

EarlyStatusTransfer ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ EarlyStatusTransfer-IEs}},
	...
}

EarlyStatusTransfer-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-sourceNG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-targetNG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-procedureStage								CRITICALITY reject		TYPE ProcedureStageChoice						PRESENCE mandatory},
	...
}

ProcedureStageChoice ::= CHOICE {
	first-dl-count						FirstDLCount,
	dl-discarding						DLDiscarding,
	choice-extension					ProtocolIE-Single-Container { {ProcedureStageChoice-ExtIEs} }
}

ProcedureStageChoice-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

FirstDLCount ::= SEQUENCE {
	dRBsSubjectToEarlyStatusTransfer			DRBsSubjectToEarlyStatusTransfer-List,
	iE-Extension								ProtocolExtensionContainer { {FirstDLCount-ExtIEs} }	OPTIONAL,
	...
}

FirstDLCount-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

DLDiscarding ::= SEQUENCE {
	dRBsSubjectToDLDiscarding					DRBsSubjectToDLDiscarding-List,
	iE-Extension								ProtocolExtensionContainer { {DLDiscarding-ExtIEs} }	OPTIONAL,
	...
}

DLDiscarding-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

-- **************************************************************
--
-- RAN PAGING
--
-- **************************************************************

RANPaging ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{RANPaging-IEs}},
	...
}

RANPaging-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-UEIdentityIndexValue						CRITICALITY reject	TYPE UEIdentityIndexValue								PRESENCE mandatory}|
	{ ID id-UERANPagingIdentity							CRITICALITY ignore	TYPE UERANPagingIdentity									PRESENCE mandatory}|
	{ ID id-PagingDRX									CRITICALITY ignore	TYPE PagingDRX												PRESENCE mandatory}|
	{ ID id-RANPagingArea								CRITICALITY reject	TYPE RANPagingArea											PRESENCE mandatory}|
	{ ID id-PagingPriority								CRITICALITY ignore	TYPE PagingPriority											PRESENCE optional }|
	{ ID id-AssistanceDataForRANPaging					CRITICALITY ignore	TYPE AssistanceDataForRANPaging						PRESENCE optional }|
	{ ID id-UERadioCapabilityForPaging					CRITICALITY ignore	TYPE UERadioCapabilityForPaging						PRESENCE optional }|
	{ ID id-ExtendedUEIdentityIndexValue				CRITICALITY ignore	TYPE ExtendedUEIdentityIndexValue						PRESENCE optional }|
	{ ID id-EUTRAPagingeDRXInformation					CRITICALITY ignore	TYPE EUTRAPagingeDRXInformation						PRESENCE optional }|
	{ ID id-UESpecificDRX								CRITICALITY ignore	TYPE UESpecificDRX											PRESENCE optional }|
	{ ID id-NRPagingeDRXInformation						CRITICALITY ignore	TYPE NRPagingeDRXInformation							PRESENCE optional }|
	{ ID id-NRPagingeDRXInformationforRRCINACTIVE		CRITICALITY ignore	TYPE NRPagingeDRXInformationforRRCINACTIVE			PRESENCE optional }|
	{ ID id-PagingCause									CRITICALITY ignore	TYPE PagingCause											PRESENCE optional }|
	{ ID id-PEIPSassistanceInformation					CRITICALITY ignore	TYPE PEIPSassistanceInformation						PRESENCE optional }|
	{ ID id-HashedUEIdentityIndexValue					CRITICALITY ignore	TYPE HashedUEIdentityIndexValue						PRESENCE optional }|
	{ ID id-MT-SDT-Information							CRITICALITY	ignore	TYPE MT-SDT-Information									PRESENCE optional }|
	{ ID id-NRPagingLongeDRXInformationforRRCINACTIVE	CRITICALITY ignore	TYPE NRPagingLongeDRXInformationforRRCINACTIVE	PRESENCE optional},
	...
}

-- **************************************************************
--
-- RETRIEVE UE CONTEXT REQUEST
--
-- **************************************************************

RetrieveUEContextRequest ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{RetrieveUEContextRequest-IEs}},
	...
}

RetrieveUEContextRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-newNG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-UEContextID								CRITICALITY reject		TYPE UEContextID									PRESENCE mandatory}|
	{ ID id-MAC-I									CRITICALITY reject		TYPE MAC-I											PRESENCE mandatory}|
	{ ID id-new-NG-RAN-Cell-Identity				CRITICALITY reject		TYPE NG-RAN-Cell-Identity						PRESENCE mandatory}|
	{ ID id-RRCResumeCause							CRITICALITY ignore		TYPE RRCResumeCause									PRESENCE optional }|
	{ ID id-SDTSupportRequest						CRITICALITY ignore		TYPE SDTSupportRequest							PRESENCE optional }|
	{ ID id-SRSPositioningConfigOrActivationRequest	CRITICALITY ignore	TYPE SRSPositioningConfigOrActivationRequest	PRESENCE optional },
	...
}

-- **************************************************************
--
-- RETRIEVE UE CONTEXT RESPONSE
--
-- **************************************************************

RetrieveUEContextResponse ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ RetrieveUEContextResponse-IEs}},
	...
}

RetrieveUEContextResponse-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-newNG-RANnodeUEXnAPID						CRITICALITY ignore	TYPE NG-RANnodeUEXnAPID										PRESENCE mandatory}|
	{ ID id-oldNG-RANnodeUEXnAPID						CRITICALITY ignore	TYPE NG-RANnodeUEXnAPID										PRESENCE mandatory}|
	{ ID id-GUAMI										CRITICALITY reject	TYPE GUAMI														PRESENCE mandatory}|
	{ ID id-UEContextInfoRetrUECtxtResp					CRITICALITY reject	TYPE UEContextInfoRetrUECtxtResp							PRESENCE mandatory}|
	{ ID id-TraceActivation								CRITICALITY ignore	TYPE TraceActivation											PRESENCE optional }|
	{ ID id-MaskedIMEISV								CRITICALITY ignore	TYPE MaskedIMEISV												PRESENCE optional }|
	{ ID id-LocationReportingInformation				CRITICALITY ignore	TYPE LocationReportingInformation							PRESENCE optional }|
	{ ID id-CriticalityDiagnostics						CRITICALITY ignore	TYPE CriticalityDiagnostics									PRESENCE optional }|
	{ ID id-NRV2XServicesAuthorized						CRITICALITY ignore	TYPE NRV2XServicesAuthorized								PRESENCE optional }|
	{ ID id-LTEV2XServicesAuthorized					CRITICALITY ignore	TYPE LTEV2XServicesAuthorized								PRESENCE optional }|
	{ ID id-PC5QoSParameters							CRITICALITY ignore	TYPE PC5QoSParameters											PRESENCE optional }|
	{ ID id-UEHistoryInformation						CRITICALITY ignore	TYPE UEHistoryInformation									PRESENCE optional }|
	{ ID id-UEHistoryInformationFromTheUE				CRITICALITY ignore	TYPE UEHistoryInformationFromTheUE						PRESENCE optional }|
	{ ID id-MDTPLMNList									CRITICALITY ignore	TYPE MDTPLMNList												PRESENCE optional }|
	{ ID id-IABNodeIndication							CRITICALITY reject	TYPE IABNodeIndication										PRESENCE optional }|
	{ ID id-UEContextRefAtSN-HORequest					CRITICALITY ignore	TYPE UEContextRefAtSN-HORequest							PRESENCE optional }|
	{ ID id-TimeSynchronizationAssistanceInformation	CRITICALITY ignore	TYPE TimeSynchronizationAssistanceInformation			PRESENCE optional }|
	{ ID id-QMCConfigInfo								CRITICALITY ignore	TYPE QMCConfigInfo												PRESENCE optional }|
	{ ID id-FiveGProSeAuthorized						CRITICALITY ignore	TYPE FiveGProSeAuthorized									PRESENCE optional }|
	{ ID id-FiveGProSePC5QoSParameters					CRITICALITY ignore	TYPE FiveGProSePC5QoSParameters							PRESENCE optional }|
	{ ID id-AerialUESubscriptionInformation				CRITICALITY ignore	TYPE AerialUESubscriptionInformation						PRESENCE optional }|
	{ ID id-NRA2XServicesAuthorized						CRITICALITY ignore	TYPE NRA2XServicesAuthorized								PRESENCE optional }|
	{ ID id-LTEA2XServicesAuthorized					CRITICALITY ignore	TYPE LTEA2XServicesAuthorized								PRESENCE optional }|
	{ ID id-A2XPC5QoSParameters							CRITICALITY ignore	TYPE A2XPC5QoSParameters										PRESENCE optional }|
	{ ID id-MobileIAB-AuthorizationStatus				CRITICALITY reject	TYPE MobileIAB-AuthorizationStatus						PRESENCE optional }|
	{ ID id-SLPositioning-Ranging-Services-Info			CRITICALITY ignore	TYPE SLPositioning-Ranging-Services-Info				PRESENCE optional },
	...
}

-- **************************************************************
--
-- RETRIEVE UE CONTEXT CONFIRM
--
-- **************************************************************

RetrieveUEContextConfirm ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{RetrieveUEContextConfirm-IEs}},
	...
}
RetrieveUEContextConfirm-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-oldNG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-newNG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-UEContextKeptIndicator					CRITICALITY ignore		TYPE UEContextKeptIndicator						PRESENCE optional }|
	{ ID id-SDT-Termination-Request					CRITICALITY ignore		TYPE SDT-Termination-Request					PRESENCE optional },
	...
}



-- **************************************************************
--
-- RETRIEVE UE CONTEXT FAILURE
--
-- **************************************************************

RetrieveUEContextFailure ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ RetrieveUEContextFailure-IEs}},
	...
}

RetrieveUEContextFailure-IEs XNAP-PROTOCOL-IES ::= {	
	{ ID id-newNG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-OldtoNewNG-RANnodeResumeContainer		CRITICALITY ignore		TYPE OCTET STRING									PRESENCE optional }|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause											PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics						PRESENCE optional }|
	{ ID id-SemipersistentPositioningInformation	CRITICALITY ignore		TYPE SemipersistentPositioningInformation		PRESENCE optional },
	...
}

-- **************************************************************
--
-- XN-U ADDRESS INDICATION
--
-- **************************************************************

XnUAddressIndication ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ XnUAddressIndication-IEs}},
	...
}

XnUAddressIndication-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-newNG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID								PRESENCE mandatory}|
	{ ID id-oldNG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID								PRESENCE mandatory}|
	{ ID id-XnUAddressInfoperPDUSession-List		CRITICALITY reject		TYPE XnUAddressInfoperPDUSession-List			PRESENCE mandatory}|
	{ ID id-CHO-MRDC-Indicator						CRITICALITY reject		TYPE CHO-MRDC-Indicator								PRESENCE optional }|
	{ ID id-CHO-MRDC-EarlyDataForwarding			CRITICALITY ignore		TYPE CHO-MRDC-EarlyDataForwarding					PRESENCE optional }|
	{ ID id-CPC-DataForwarding-Indicator			CRITICALITY reject		TYPE CPC-DataForwarding-Indicator					PRESENCE optional }|
	{ ID id-MBS-DataForwarding-Indicator			CRITICALITY ignore		TYPE MBS-DataForwarding-Indicator					PRESENCE optional }|
	{ ID id-MBS-SessionInformationResponse-List		CRITICALITY ignore		TYPE MBS-SessionInformationResponse-List		PRESENCE optional }|
	{ ID id-PDUSetbasedHandlingIndicator			CRITICALITY ignore		TYPE PDUSetbasedHandlingIndicator					PRESENCE optional },
	...
}

-- **************************************************************
--
-- S-NODE ADDITION REQUEST
--
-- **************************************************************

SNodeAdditionRequest ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeAdditionRequest-IEs}},
	...
}

SNodeAdditionRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID				CRITICALITY reject		TYPE NG-RANnodeUEXnAPID								PRESENCE mandatory}|
	{ ID id-UESecurityCapabilities				CRITICALITY reject		TYPE UESecurityCapabilities						PRESENCE mandatory}|
	{ ID id-s-ng-RANnode-SecurityKey			CRITICALITY reject		TYPE S-NG-RANnode-SecurityKey					PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUE-AMBR					CRITICALITY reject		TYPE UEAggregateMaximumBitRate					PRESENCE mandatory}|
	{ ID id-selectedPLMN						CRITICALITY ignore		TYPE PLMN-Identity									PRESENCE optional }|
	{ ID id-MobilityRestrictionList				CRITICALITY ignore		TYPE MobilityRestrictionList						PRESENCE optional }|
	{ ID id-indexToRatFrequSelectionPriority	CRITICALITY reject		TYPE RFSP-Index										PRESENCE optional }|
	{ ID id-PDUSessionToBeAddedAddReq			CRITICALITY reject		TYPE PDUSessionToBeAddedAddReq					PRESENCE mandatory}|
	{ ID id-MN-to-SN-Container					CRITICALITY reject		TYPE OCTET STRING									PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID				CRITICALITY reject		TYPE NG-RANnodeUEXnAPID								PRESENCE optional }|
	{ ID id-ExpectedUEBehaviour					CRITICALITY ignore		TYPE ExpectedUEBehaviour							PRESENCE optional }|
	{ ID id-requestedSplitSRB					CRITICALITY reject		TYPE SplitSRBsTypes									PRESENCE optional }|
	{ ID id-PCellID								CRITICALITY reject		TYPE GlobalNG-RANCell-ID							PRESENCE optional }|
	{ ID id-DesiredActNotificationLevel			CRITICALITY ignore		TYPE DesiredActNotificationLevel				PRESENCE optional }|
	{ ID id-AvailableDRBIDs						CRITICALITY reject		TYPE DRB-List										PRESENCE conditional}
-- This IE shall be present if there is at least one PDU Session Resource Setup Info – SN terminated in the PDU Session Resources To Be Added List IE. --|
	{ ID id-S-NG-RANnodeMaxIPDataRate-UL		CRITICALITY reject		TYPE BitRate										PRESENCE optional }|
	{ ID id-S-NG-RANnodeMaxIPDataRate-DL		CRITICALITY reject		TYPE BitRate										PRESENCE optional }|
	{ ID id-LocationInformationSNReporting		CRITICALITY ignore		TYPE LocationInformationSNReporting			PRESENCE optional }|
	{ ID id-MR-DC-ResourceCoordinationInfo		CRITICALITY ignore		TYPE MR-DC-ResourceCoordinationInfo			PRESENCE optional }|
	{ ID id-MaskedIMEISV						CRITICALITY ignore		TYPE MaskedIMEISV									PRESENCE optional }|
	{ ID id-NE-DC-TDM-Pattern					CRITICALITY ignore		TYPE NE-DC-TDM-Pattern								PRESENCE optional }|
	{ ID id-S-NG-RANnode-Addition-Trigger-Ind	CRITICALITY reject		TYPE S-NG-RANnode-Addition-Trigger-Ind		PRESENCE optional }|
	{ ID id-TraceActivation						CRITICALITY ignore		TYPE TraceActivation								PRESENCE optional }|
	{ ID id-RequestedFastMCGRecoveryViaSRB3		CRITICALITY ignore		TYPE RequestedFastMCGRecoveryViaSRB3			PRESENCE optional }|
	{ ID id-UERadioCapabilityID					CRITICALITY reject		TYPE UERadioCapabilityID							PRESENCE optional }|
	{ ID id-SourceNG-RAN-node-ID				CRITICALITY ignore		TYPE GlobalNG-RANNode-ID							PRESENCE optional }|
	{ ID id-ManagementBasedMDTPLMNList			CRITICALITY ignore		TYPE MDTPLMNList									PRESENCE optional }|
	{ ID id-UEHistoryInformation				CRITICALITY ignore		TYPE UEHistoryInformation							PRESENCE optional }|
	{ ID id-UEHistoryInformationFromTheUE		CRITICALITY ignore		TYPE UEHistoryInformationFromTheUE				PRESENCE optional }|
	{ ID id-PSCellChangeHistory					CRITICALITY ignore		TYPE PSCellChangeHistory							PRESENCE optional }|
	{ ID id-IABNodeIndication					CRITICALITY reject		TYPE IABNodeIndication								PRESENCE optional }|
	{ ID id-NoPDUSessionIndication				CRITICALITY ignore		TYPE NoPDUSessionIndication						PRESENCE optional }|
	{ ID id-CHOinformation-AddReq				CRITICALITY reject		TYPE CHOinformation-AddReq						PRESENCE optional }|
	{ ID id-SCGActivationRequest				CRITICALITY ignore		TYPE SCGActivationRequest							PRESENCE optional }|
	{ ID id-CPAInformationRequest				CRITICALITY reject		TYPE CPAInformationRequest						PRESENCE optional }|
	{ ID id-S-NG-RANnodeUE-Slice-MBR			CRITICALITY reject		TYPE UESliceMaximumBitRateList					PRESENCE optional }|
	{ ID id-F1-terminatingIAB-donorIndicator	CRITICALITY reject		TYPE F1-terminatingIAB-donorIndicator			PRESENCE optional }|
	{ ID id-SelectedNID							CRITICALITY	ignore		TYPE NID											PRESENCE optional }|
	{ ID id-QMCCoordinationRequest				CRITICALITY ignore		TYPE QMCCoordinationRequest						PRESENCE optional }|
	{ ID id-SourceSN-to-TargetSN-QMCInfo		CRITICALITY ignore		TYPE QMCConfigInfo									PRESENCE optional }|
	{ ID id-IABAuthorizationStatus				CRITICALITY ignore		TYPE IABAuthorizationStatus						PRESENCE optional }|
	{ ID id-Source-M-NG-RANnodeID				CRITICALITY ignore		TYPE GlobalNG-RANNode-ID							PRESENCE optional },
	...
}

PDUSessionToBeAddedAddReq ::= SEQUENCE (SIZE(1..maxnoofPDUSessions)) OF PDUSessionToBeAddedAddReq-Item

PDUSessionToBeAddedAddReq-Item ::= SEQUENCE {
	pduSessionId			PDUSession-ID,
	s-NSSAI					S-NSSAI,
	sN-PDUSessionAMBR		PDUSessionAggregateMaximumBitRate			OPTIONAL,
	sn-terminated			PDUSessionResourceSetupInfo-SNterminated	OPTIONAL,
	mn-terminated			PDUSessionResourceSetupInfo-MNterminated	OPTIONAL,
-- NOTE: If neither the PDU Session Resource Setup Info – SN terminated IE
-- nor the PDU Session Resource Setup Info – MN terminated IE is present,
-- abnormal conditions as specified in clause 8.3.1.4 apply.
	iE-Extension			ProtocolExtensionContainer { {PDUSessionToBeAddedAddReq-Item-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionToBeAddedAddReq-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

RequestedFastMCGRecoveryViaSRB3 ::= ENUMERATED {true, ...}

-- **************************************************************
--
-- S-NODE ADDITION REQUEST ACKNOWLEDGE
--
-- **************************************************************

SNodeAdditionRequestAcknowledge ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeAdditionRequestAcknowledge-IEs}},
	...
}

SNodeAdditionRequestAcknowledge-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID				CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID				CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-PDUSessionAdmittedAddedAddReqAck	CRITICALITY ignore		TYPE PDUSessionAdmittedAddedAddReqAck		PRESENCE mandatory}|
	{ ID id-PDUSessionNotAdmittedAddReqAck		CRITICALITY ignore		TYPE PDUSessionNotAdmittedAddReqAck		PRESENCE optional }|
	{ ID id-SN-to-MN-Container					CRITICALITY reject		TYPE OCTET STRING								PRESENCE mandatory}|
	{ ID id-admittedSplitSRB					CRITICALITY reject		TYPE SplitSRBsTypes								PRESENCE optional }|
	{ ID id-RRCConfigIndication					CRITICALITY reject		TYPE RRCConfigIndication						PRESENCE optional }|
	{ ID id-CriticalityDiagnostics				CRITICALITY ignore		TYPE CriticalityDiagnostics					PRESENCE optional }|
	{ ID id-LocationInformationSN				CRITICALITY ignore		TYPE Target-CGI									PRESENCE optional }|
	{ ID id-MR-DC-ResourceCoordinationInfo		CRITICALITY ignore		TYPE MR-DC-ResourceCoordinationInfo		PRESENCE optional }|
	{ ID id-AvailableFastMCGRecoveryViaSRB3		CRITICALITY ignore		TYPE AvailableFastMCGRecoveryViaSRB3		PRESENCE optional }|
	{ ID id-DirectForwardingPathAvailability	CRITICALITY ignore		TYPE DirectForwardingPathAvailability		PRESENCE optional }|
	{ ID id-SCGActivationStatus					CRITICALITY ignore		TYPE SCGActivationStatus						PRESENCE optional }|
	{ ID id-CPAInformationAck					CRITICALITY ignore		TYPE CPAInformationAck							PRESENCE optional }|
	{ ID id-SNMobilityInformation				CRITICALITY ignore		TYPE SNMobilityInformation					PRESENCE optional }|
	{ ID id-QMCCoordinationResponse				CRITICALITY ignore		TYPE QMCCoordinationResponse					PRESENCE optional }|
	{ ID id-CHOinformation-AddReqAck			CRITICALITY	reject		TYPE CHOinformation-AddReqAck				PRESENCE optional	}|
	{ ID id-DirectForwardingPathAvailabilityWithSourceMN CRITICALITY ignore	TYPE DirectForwardingPathAvailabilityWithSourceMN	PRESENCE optional },
	...
}

PDUSessionAdmittedAddedAddReqAck ::= SEQUENCE (SIZE(1..maxnoofPDUSessions)) OF PDUSessionAdmittedAddedAddReqAck-Item

PDUSessionAdmittedAddedAddReqAck-Item ::= SEQUENCE {
	pduSessionId			PDUSession-ID,
	sn-terminated			PDUSessionResourceSetupResponseInfo-SNterminated	OPTIONAL,
	mn-terminated			PDUSessionResourceSetupResponseInfo-MNterminated	OPTIONAL,
-- NOTE: If neither the PDU Session Resource Setup Response Info – SN terminated IE
-- nor the PDU Session Resource Setup Response Info – MN terminated IE is present,
-- abnormal conditions as specified in clause 8.3.1.4 apply.
	iE-Extension			ProtocolExtensionContainer { {PDUSessionAdmittedAddedAddReqAck-Item-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionAdmittedAddedAddReqAck-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PDUSessionNotAdmittedAddReqAck ::= SEQUENCE {
	pduSessionResourcesNotAdmitted-SNterminated		PDUSessionResourcesNotAdmitted-List OPTIONAL,
	pduSessionResourcesNotAdmitted-MNterminated		PDUSessionResourcesNotAdmitted-List OPTIONAL,
	iE-Extension			ProtocolExtensionContainer { {PDUSessionNotAdmittedAddReqAck-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionNotAdmittedAddReqAck-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

AvailableFastMCGRecoveryViaSRB3 ::= ENUMERATED {true, ...}

-- **************************************************************
--
-- S-NODE ADDITION REQUEST REJECT
--
-- **************************************************************

SNodeAdditionRequestReject ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeAdditionRequestReject-IEs}},
	...
}

SNodeAdditionRequestReject-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause											PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics						PRESENCE optional }|
	{ ID id-PCellID									CRITICALITY reject		TYPE GlobalNG-RANCell-ID							PRESENCE optional },
	...
}

-- **************************************************************
--
-- S-NODE RECONFIGURATION COMPLETE
--
-- **************************************************************

SNodeReconfigurationComplete ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeReconfigurationComplete-IEs}},
	...
}

SNodeReconfigurationComplete-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-ResponseInfo-ReconfCompl				CRITICALITY ignore		TYPE ResponseInfo-ReconfCompl					PRESENCE mandatory},
	...
}

ResponseInfo-ReconfCompl ::= SEQUENCE {
	responseType-ReconfComplete		ResponseType-ReconfComplete,
	iE-Extensions						ProtocolExtensionContainer { {ResponseInfo-ReconfCompl-ExtIEs} } OPTIONAL,
	...
}

ResponseInfo-ReconfCompl-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ResponseType-ReconfComplete ::= CHOICE {
	configuration-successfully-applied			Configuration-successfully-applied,
	configuration-rejected-by-M-NG-RANNode		Configuration-rejected-by-M-NG-RANNode,
	choice-extension					ProtocolIE-Single-Container { {ResponseType-ReconfComplete-ExtIEs} }
}

ResponseType-ReconfComplete-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

Configuration-successfully-applied ::= SEQUENCE {
	m-NG-RANNode-to-S-NG-RANNode-Container		OCTET STRING		OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { {Configuration-successfully-applied-ExtIEs} } OPTIONAL,
	...
}

Configuration-successfully-applied-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-sk-Counter	CRITICALITY ignore	EXTENSION SK-COUNTER		PRESENCE optional},
	...
}

Configuration-rejected-by-M-NG-RANNode ::= SEQUENCE {
	cause											Cause,
	m-NG-RANNode-to-S-NG-RANNode-Container		OCTET STRING		OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { {Configuration-rejected-by-M-NG-RANNode-ExtIEs} } OPTIONAL,
	...
}

Configuration-rejected-by-M-NG-RANNode-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- S-NODE MODIFICATION REQUEST
--
-- **************************************************************

SNodeModificationRequest ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeModificationRequest-IEs}},
	...
}

SNodeModificationRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID								PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID								PRESENCE mandatory}|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause												PRESENCE mandatory}|
	{ ID id-PDCPChangeIndication					CRITICALITY ignore		TYPE PDCPChangeIndication							PRESENCE optional }|
	{ ID id-selectedPLMN							CRITICALITY ignore		TYPE PLMN-Identity										PRESENCE optional }|
	{ ID id-MobilityRestrictionList					CRITICALITY ignore		TYPE MobilityRestrictionList						PRESENCE optional }|
	{ ID id-SCGConfigurationQuery					CRITICALITY ignore		TYPE SCGConfigurationQuery							PRESENCE optional }|
	{ ID id-UEContextInfo-SNModRequest				CRITICALITY reject		TYPE UEContextInfo-SNModRequest					PRESENCE optional }|
	{ ID id-MN-to-SN-Container						CRITICALITY ignore		TYPE OCTET STRING										PRESENCE optional }|
	{ ID id-requestedSplitSRB						CRITICALITY ignore		TYPE SplitSRBsTypes										PRESENCE optional }|
	{ ID id-requestedSplitSRBrelease				CRITICALITY ignore		TYPE SplitSRBsTypes										PRESENCE optional }|
	{ ID id-DesiredActNotificationLevel				CRITICALITY ignore		TYPE DesiredActNotificationLevel					PRESENCE optional }|
	{ ID id-AdditionalDRBIDs						CRITICALITY reject		TYPE DRB-List											PRESENCE optional }|
	{ ID id-S-NG-RANnodeMaxIPDataRate-UL			CRITICALITY reject		TYPE BitRate											PRESENCE optional }|
	{ ID id-S-NG-RANnodeMaxIPDataRate-DL			CRITICALITY reject		TYPE BitRate											PRESENCE optional }|
	{ ID id-LocationInformationSNReporting			CRITICALITY ignore		TYPE LocationInformationSNReporting				PRESENCE optional }|
	{ ID id-MR-DC-ResourceCoordinationInfo			CRITICALITY ignore		TYPE MR-DC-ResourceCoordinationInfo				PRESENCE optional }|
	{ ID id-PCellID									CRITICALITY reject		TYPE GlobalNG-RANCell-ID								PRESENCE optional }|
	{ ID id-NE-DC-TDM-Pattern						CRITICALITY ignore		TYPE NE-DC-TDM-Pattern								PRESENCE optional }|
	{ ID id-RequestedFastMCGRecoveryViaSRB3			CRITICALITY ignore		TYPE RequestedFastMCGRecoveryViaSRB3				PRESENCE optional }|
	{ ID id-RequestedFastMCGRecoveryViaSRB3Release	CRITICALITY ignore		TYPE RequestedFastMCGRecoveryViaSRB3Release	PRESENCE optional }|
	{ ID id-SNTriggered								CRITICALITY ignore		TYPE SNTriggered										PRESENCE optional }|
	{ ID id-TargetNodeID							CRITICALITY ignore		TYPE GlobalNG-RANNode-ID								PRESENCE optional }|
	{ ID id-PSCellHistoryInformationRetrieve		CRITICALITY ignore		TYPE PSCellHistoryInformationRetrieve			PRESENCE optional }|
	{ ID id-UEHistoryInformationFromTheUE			CRITICALITY ignore		TYPE UEHistoryInformationFromTheUE				PRESENCE optional }|
	{ ID id-CHOinformation-ModReq					CRITICALITY ignore		TYPE CHOinformation-ModReq							PRESENCE optional }|
	{ ID id-SCGActivationRequest					CRITICALITY ignore		TYPE SCGActivationRequest							PRESENCE optional }|
	{ ID id-CPAInformationModReq					CRITICALITY ignore		TYPE CPAInformationModReq							PRESENCE optional }|
	{ ID id-CPCInformationUpdate					CRITICALITY ignore		TYPE CPCInformationUpdate							PRESENCE optional }|
	{ ID id-S-NG-RANnodeUE-Slice-MBR				CRITICALITY ignore		TYPE UESliceMaximumBitRateList						PRESENCE optional }|
	{ ID id-ManagementBasedMDTPLMNModificationList	CRITICALITY	ignore		TYPE MDTPLMNModificationList						PRESENCE optional }|
	{ ID id-SelectedNID								CRITICALITY	ignore		TYPE NID												PRESENCE optional }|
	{ ID id-QMCCoordinationRequest					CRITICALITY ignore		TYPE QMCCoordinationRequest							PRESENCE optional }|
	{ ID id-Src-SN-to-Tgt-SNQMCInfoInquiry			CRITICALITY ignore		TYPE Src-SN-to-Tgt-SNQMCInfoInquiry				PRESENCE optional }|
	{ ID id-IABAuthorizationStatus					CRITICALITY ignore	TYPE IABAuthorizationStatus							PRESENCE optional },
	...
}

UEContextInfo-SNModRequest ::= SEQUENCE {
	ueSecurityCapabilities							UESecurityCapabilities								OPTIONAL,
	s-ng-RANnode-SecurityKey						S-NG-RANnode-SecurityKey							OPTIONAL,
	s-ng-RANnodeUE-AMBR								UEAggregateMaximumBitRate							OPTIONAL,
	indexToRatFrequencySelectionPriority			RFSP-Index											OPTIONAL,
	lowerLayerPresenceStatusChange					LowerLayerPresenceStatusChange						OPTIONAL,
	pduSessionResourceToBeAdded						PDUSessionsToBeAdded-SNModRequest-List				OPTIONAL,
	pduSessionResourceToBeModified					PDUSessionsToBeModified-SNModRequest-List			OPTIONAL,
	pduSessionResourceToBeReleased					PDUSessionsToBeReleased-SNModRequest-List			OPTIONAL,
	iE-Extension			ProtocolExtensionContainer { {UEContextInfo-SNModRequest-ExtIEs} }			OPTIONAL,
	...
}

UEContextInfo-SNModRequest-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PDUSessionsToBeAdded-SNModRequest-List ::= SEQUENCE (SIZE(1..maxnoofPDUSessions)) OF PDUSessionsToBeAdded-SNModRequest-Item

PDUSessionsToBeAdded-SNModRequest-Item ::= SEQUENCE {
	pduSessionId			PDUSession-ID,
	s-NSSAI					S-NSSAI,
	sN-PDUSessionAMBR		PDUSessionAggregateMaximumBitRate			OPTIONAL,
	sn-terminated			PDUSessionResourceSetupInfo-SNterminated	OPTIONAL,
	mn-terminated			PDUSessionResourceSetupInfo-MNterminated	OPTIONAL,
-- NOTE: If neither the PDU Session Resource Setup Info – SN terminated IE
-- nor the PDU Session Resource Setup Info – MN terminated IE is present,
-- abnormal conditions as specified in clause 8.3.3.4 apply.
	iE-Extension			ProtocolExtensionContainer { {PDUSessionsToBeAdded-SNModRequest-Item-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionsToBeAdded-SNModRequest-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ID id-PDUSessionExpectedUEActivityBehaviour		CRITICALITY ignore	EXTENSION ExpectedUEActivityBehaviour		PRESENCE optional},
	...
}

PDUSessionsToBeModified-SNModRequest-List ::= SEQUENCE (SIZE(1..maxnoofPDUSessions)) OF PDUSessionsToBeModified-SNModRequest-Item

PDUSessionsToBeModified-SNModRequest-Item ::= SEQUENCE {
	pduSessionId			PDUSession-ID,
	sN-PDUSessionAMBR		PDUSessionAggregateMaximumBitRate				OPTIONAL,
	sn-terminated			PDUSessionResourceModificationInfo-SNterminated	OPTIONAL,
	mn-terminated			PDUSessionResourceModificationInfo-MNterminated	OPTIONAL,
-- NOTE: If neither the PDU Session Resource Modification Info – SN terminated IE
-- nor the PDU Session Resource Modification Info – MN terminated IE is present,
-- abnormal conditions as specified in clause 8.3.3.4 apply.
	iE-Extension			ProtocolExtensionContainer { {PDUSessionsToBeModified-SNModRequest-Item-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionsToBeModified-SNModRequest-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-S-NSSAI										CRITICALITY reject	EXTENSION S-NSSAI									PRESENCE optional }|
	{ ID id-PDUSessionExpectedUEActivityBehaviour		CRITICALITY ignore	EXTENSION ExpectedUEActivityBehaviour		PRESENCE optional }|
	{ ID id-UserPlaneFailureIndication					CRITICALITY ignore	EXTENSION UserPlaneFailureIndication			PRESENCE optional },
	...
}

PDUSessionsToBeReleased-SNModRequest-List ::= SEQUENCE {
	pdu-session-list		PDUSession-List-withCause				OPTIONAL,
	iE-Extension			ProtocolExtensionContainer { {PDUSessionsToBeReleased-SNModRequest-List-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionsToBeReleased-SNModRequest-List-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

RequestedFastMCGRecoveryViaSRB3Release ::= ENUMERATED {true, ...}

Src-SN-to-Tgt-SNQMCInfoInquiry	::= ENUMERATED {true, ...}

-- **************************************************************
--
-- S-NODE MODIFICATION REQUEST ACKNOWLEDGE
--
-- **************************************************************

SNodeModificationRequestAcknowledge ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeModificationRequestAcknowledge-IEs}},
	...
}

SNodeModificationRequestAcknowledge-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID									PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID									PRESENCE mandatory}|
	{ ID id-PDUSessionAdmitted-SNModResponse		CRITICALITY ignore		TYPE PDUSessionAdmitted-SNModResponse				PRESENCE optional }|
	{ ID id-PDUSessionNotAdmitted-SNModResponse		CRITICALITY ignore		TYPE PDUSessionNotAdmitted-SNModResponse			PRESENCE optional }|
	{ ID id-SN-to-MN-Container						CRITICALITY ignore		TYPE OCTET STRING											PRESENCE optional }|
	{ ID id-admittedSplitSRB						CRITICALITY ignore		TYPE SplitSRBsTypes											PRESENCE optional }|
	{ ID id-admittedSplitSRBrelease					CRITICALITY ignore		TYPE SplitSRBsTypes											PRESENCE optional }|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics								PRESENCE optional }|
	{ ID id-LocationInformationSN					CRITICALITY ignore		TYPE Target-CGI												PRESENCE optional }|
	{ ID id-MR-DC-ResourceCoordinationInfo			CRITICALITY ignore		TYPE MR-DC-ResourceCoordinationInfo					PRESENCE optional }|
	{ ID id-PDUSessionDataForwarding-SNModResponse	CRITICALITY ignore		TYPE PDUSessionDataForwarding-SNModResponse		PRESENCE optional }|
	{ ID id-RRCConfigIndication						CRITICALITY reject		TYPE RRCConfigIndication									PRESENCE optional }|
	{ ID id-AvailableFastMCGRecoveryViaSRB3			CRITICALITY ignore		TYPE AvailableFastMCGRecoveryViaSRB3					PRESENCE optional }|
	{ ID id-ReleaseFastMCGRecoveryViaSRB3			CRITICALITY ignore		TYPE ReleaseFastMCGRecoveryViaSRB3					PRESENCE optional }|
	{ ID id-DirectForwardingPathAvailability		CRITICALITY ignore		TYPE DirectForwardingPathAvailability				PRESENCE optional }|
	{ ID id-SCGUEHistoryInformation					CRITICALITY ignore		TYPE SCGUEHistoryInformation							PRESENCE optional }|
	{ ID id-SCGActivationStatus						CRITICALITY ignore		TYPE SCGActivationStatus									PRESENCE optional }|
	{ ID id-CPAInformationModReqAck					CRITICALITY ignore		TYPE CPAInformationModReqAck							PRESENCE optional }|
	{ ID id-QMCCoordinationResponse					CRITICALITY ignore		TYPE QMCCoordinationResponse							PRESENCE optional }|
	{ ID id-SourceSN-to-TargetSN-QMCInfo			CRITICALITY ignore		TYPE QMCConfigInfo											PRESENCE optional },
	...
}
PDUSessionAdmitted-SNModResponse ::= SEQUENCE {
	pduSessionResourcesAdmittedToBeAdded			PDUSessionAdmittedToBeAddedSNModResponse			OPTIONAL,
	pduSessionResourcesAdmittedToBeModified			PDUSessionAdmittedToBeModifiedSNModResponse		OPTIONAL,
	pduSessionResourcesAdmittedToBeReleased			PDUSessionAdmittedToBeReleasedSNModResponse			OPTIONAL,
	iE-Extension			ProtocolExtensionContainer { {PDUSessionAdmitted-SNModResponse-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionAdmitted-SNModResponse-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PDUSessionAdmittedToBeAddedSNModResponse ::= SEQUENCE (SIZE(1..maxnoofPDUSessions)) OF PDUSessionAdmittedToBeAddedSNModResponse-Item
PDUSessionAdmittedToBeAddedSNModResponse-Item ::= SEQUENCE {
	pduSessionId			PDUSession-ID,
	sn-terminated			PDUSessionResourceSetupResponseInfo-SNterminated	OPTIONAL,
	mn-terminated			PDUSessionResourceSetupResponseInfo-MNterminated	OPTIONAL,
-- NOTE: If neither the PDU Session Resource Setup Response Info – SN terminated IE
-- nor the PDU Session Resource Setup Response Info – MN terminated IE is present,
-- abnormal conditions as specified in clause 8.3.3.4 apply.
	iE-Extension			ProtocolExtensionContainer { {PDUSessionAdmittedToBeAddedSNModResponse-Item-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionAdmittedToBeAddedSNModResponse-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PDUSessionAdmittedToBeModifiedSNModResponse::= SEQUENCE (SIZE(1..maxnoofPDUSessions)) OF PDUSessionAdmittedToBeModifiedSNModResponse-Item
PDUSessionAdmittedToBeModifiedSNModResponse-Item ::= SEQUENCE {
	pduSessionId			PDUSession-ID,
	sn-terminated			PDUSessionResourceModificationResponseInfo-SNterminated	OPTIONAL,
	mn-terminated			PDUSessionResourceModificationResponseInfo-MNterminated	OPTIONAL,
-- NOTE: If neither the PDU Session Resource Modification Response Info – SN terminated IE
-- nor the PDU Session Resource Modification Response Info – MN terminated IE is present,
-- abnormal conditions as specified in clause 8.3.3.4 apply.
	iE-Extension			ProtocolExtensionContainer { {PDUSessionAdmittedToBeModifiedSNModResponse-Item-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionAdmittedToBeModifiedSNModResponse-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PDUSessionAdmittedToBeReleasedSNModResponse ::= SEQUENCE {
	sn-terminated			PDUSession-List-withDataForwardingRequest		OPTIONAL,
	mn-terminated			PDUSession-List-withCause						OPTIONAL,
	iE-Extension			ProtocolExtensionContainer { {PDUSessionAdmittedToBeReleasedSNModResponse-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionAdmittedToBeReleasedSNModResponse-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PDUSessionNotAdmitted-SNModResponse ::= SEQUENCE {
	pdu-Session-List		PDUSession-List OPTIONAL,
	iE-Extension			ProtocolExtensionContainer { {PDUSessionNotAdmitted-SNModResponse-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionNotAdmitted-SNModResponse-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-PDUSessionResourcesNotAdmitted-List		CRITICALITY ignore		EXTENSION PDUSessionResourcesNotAdmitted-List	PRESENCE optional },
	...
}


PDUSessionDataForwarding-SNModResponse ::= SEQUENCE {
	sn-terminated		PDUSession-List-withDataForwardingRequest,
	iE-Extensions		ProtocolExtensionContainer { {PDUSessionDataForwarding-SNModResponse-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionDataForwarding-SNModResponse-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ReleaseFastMCGRecoveryViaSRB3 ::= ENUMERATED {true, ...}



-- **************************************************************
--
-- S-NODE MODIFICATION REQUEST REJECT
--
-- **************************************************************

SNodeModificationRequestReject ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeModificationRequestReject-IEs}},
	...
}

SNodeModificationRequestReject-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause											PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics						PRESENCE optional },
	...
}

-- **************************************************************
--
-- S-NODE MODIFICATION REQUIRED
--
-- **************************************************************

SNodeModificationRequired ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeModificationRequired-IEs}},
	...
}

SNodeModificationRequired-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause											PRESENCE mandatory}|
	{ ID id-PDCPChangeIndication					CRITICALITY ignore		TYPE PDCPChangeIndication						PRESENCE optional }|
	{ ID id-PDUSessionToBeModifiedSNModRequired		CRITICALITY ignore		TYPE PDUSessionToBeModifiedSNModRequired	PRESENCE optional }|
	{ ID id-PDUSessionToBeReleasedSNModRequired		CRITICALITY ignore		TYPE PDUSessionToBeReleasedSNModRequired	PRESENCE optional }|
	{ ID id-SN-to-MN-Container						CRITICALITY ignore		TYPE OCTET STRING									PRESENCE optional }|
	{ ID id-SpareDRBIDs								CRITICALITY ignore		TYPE DRB-List										PRESENCE optional }|
	{ ID id-RequiredNumberOfDRBIDs					CRITICALITY ignore		TYPE DRB-Number										PRESENCE optional }|
	{ ID id-LocationInformationSN					CRITICALITY ignore		TYPE Target-CGI										PRESENCE optional }|
	{ ID id-MR-DC-ResourceCoordinationInfo			CRITICALITY ignore		TYPE MR-DC-ResourceCoordinationInfo			PRESENCE optional }|
	{ ID id-RRCConfigIndication						CRITICALITY reject		TYPE RRCConfigIndication							PRESENCE optional }|
	{ ID id-AvailableFastMCGRecoveryViaSRB3			CRITICALITY ignore		TYPE AvailableFastMCGRecoveryViaSRB3			PRESENCE optional }|
	{ ID id-ReleaseFastMCGRecoveryViaSRB3			CRITICALITY ignore		TYPE ReleaseFastMCGRecoveryViaSRB3			PRESENCE optional }|
	{ ID id-SCGIndicator							CRITICALITY ignore		TYPE SCGIndicator									PRESENCE optional }|
	{ ID id-SCGUEHistoryInformation					CRITICALITY ignore		TYPE SCGUEHistoryInformation					PRESENCE optional }|
	{ ID id-SCGActivationRequest					CRITICALITY ignore		TYPE SCGActivationRequest						PRESENCE optional }|
	{ ID id-CPACInformationModRequired				CRITICALITY ignore		TYPE CPACInformationModRequired				PRESENCE optional }|
	{ ID id-SCGreconfigNotification					CRITICALITY ignore		TYPE SCGreconfigNotification					PRESENCE optional }|
	{ ID id-SPRAvailability							CRITICALITY ignore		TYPE SPRAvailability								PRESENCE optional }|
	{ ID id-QMCCoordinationRequest					CRITICALITY ignore		TYPE QMCCoordinationRequest						PRESENCE optional }|
	{ ID id-S-CPAC-Request							CRITICALITY reject		TYPE S-CPAC-Request									PRESENCE	optional }|
	{ ID id-PDUSessionsListToBeReleased-UPError		CRITICALITY ignore		TYPE PDUSessionsListToBeReleased-UPError	PRESENCE optional },
	...
}
PDUSessionToBeModifiedSNModRequired::= SEQUENCE (SIZE (1.. maxnoofPDUSessions)) OF	PDUSessionToBeModifiedSNModRequired-Item

PDUSessionToBeModifiedSNModRequired-Item ::= SEQUENCE {
	pduSessionId						PDUSession-ID,
	sn-terminated			PDUSessionResourceModRqdInfo-SNterminated	OPTIONAL,
	mn-terminated			PDUSessionResourceModRqdInfo-MNterminated	OPTIONAL,
-- NOTE: If neither the PDU Session Resource Modification Required Info – SN terminated IE
-- nor the PDU Session Resource Modification Required Info – MN terminated IE is present,
-- abnormal conditions as specified in clause 8.3.4.4 apply.
	iE-Extension		ProtocolExtensionContainer { {PDUSessionToBeModifiedSNModRequired-Item-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionToBeModifiedSNModRequired-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PDUSessionToBeReleasedSNModRequired ::= SEQUENCE {
	sn-terminated			PDUSession-List-withDataForwardingRequest		OPTIONAL,
	mn-terminated			PDUSession-List-withCause						OPTIONAL,
	iE-Extension			ProtocolExtensionContainer { {PDUSessionToBeReleasedSNModRequired-ExtIEs} } OPTIONAL,
	...
}

PDUSessionToBeReleasedSNModRequired-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

-- **************************************************************
--
-- S-NODE MODIFICATION CONFIRM
--
-- **************************************************************

SNodeModificationConfirm ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeModificationConfirm-IEs}},
	...
}

SNodeModificationConfirm-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-PDUSessionAdmittedModSNModConfirm		CRITICALITY ignore		TYPE PDUSessionAdmittedModSNModConfirm		PRESENCE optional }|
	{ ID id-PDUSessionReleasedSNModConfirm			CRITICALITY ignore		TYPE PDUSessionReleasedSNModConfirm			PRESENCE optional }|
	{ ID id-MN-to-SN-Container						CRITICALITY ignore		TYPE OCTET STRING									PRESENCE optional }|
	{ ID id-AdditionalDRBIDs						CRITICALITY reject		TYPE DRB-List										PRESENCE optional }|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics						PRESENCE optional }|
	{ ID id-MR-DC-ResourceCoordinationInfo			CRITICALITY ignore		TYPE MR-DC-ResourceCoordinationInfo			PRESENCE optional }|
	{ ID id-QMCCoordinationResponse					CRITICALITY ignore		TYPE QMCCoordinationResponse					PRESENCE optional },
	...
}

PDUSessionAdmittedModSNModConfirm ::= SEQUENCE (SIZE(1..maxnoofPDUSessions)) OF PDUSessionAdmittedModSNModConfirm-Item

PDUSessionAdmittedModSNModConfirm-Item ::= SEQUENCE {
	pduSessionId				PDUSession-ID,
	sn-terminated			PDUSessionResourceModConfirmInfo-SNterminated	OPTIONAL,
	mn-terminated			PDUSessionResourceModConfirmInfo-MNterminated	OPTIONAL,
-- NOTE: If neither the PDU Session Resource Modification Confirm Info – SN terminated IE
-- nor the PDU Session Resource Modification Confirm Info – MN terminated IE is present,
-- abnormal conditions as specified in clause 8.3.4.4 apply.
	iE-Extension			ProtocolExtensionContainer { {PDUSessionAdmittedModSNModConfirm-Item-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionAdmittedModSNModConfirm-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


PDUSessionReleasedSNModConfirm ::= SEQUENCE {
	sn-terminated			PDUSession-List-withDataForwardingFromTarget						OPTIONAL,
	mn-terminated			PDUSession-List														OPTIONAL,
	iE-Extension			ProtocolExtensionContainer { {PDUSessionAdmittedToBeReleasedSNModConfirm-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionAdmittedToBeReleasedSNModConfirm-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- S-NODE MODIFICATION REFUSE
--
-- **************************************************************

SNodeModificationRefuse ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeModificationRefuse-IEs}},
	...
}

SNodeModificationRefuse-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause											PRESENCE mandatory}|
	{ ID id-MN-to-SN-Container						CRITICALITY ignore		TYPE OCTET STRING									PRESENCE optional }|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics						PRESENCE optional },
	...
}

-- **************************************************************
--
-- S-NODE RELEASE REQUEST
--
-- **************************************************************

SNodeReleaseRequest ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeReleaseRequest-IEs}},
	...
}

SNodeReleaseRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE optional }|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause											PRESENCE mandatory}|
	{ ID id-PDUSessionToBeReleased-RelReq			CRITICALITY ignore		TYPE PDUSession-List-withCause					PRESENCE optional }|
	{ ID id-UEContextKeptIndicator					CRITICALITY ignore		TYPE UEContextKeptIndicator						PRESENCE optional }|
	{ ID id-MN-to-SN-Container						CRITICALITY ignore		TYPE OCTET STRING									PRESENCE optional }|
	{ ID id-DRBs-transferred-to-MN					CRITICALITY ignore		TYPE DRB-List										PRESENCE optional },
	...
}

-- **************************************************************
--
-- S-NODE RELEASE REQUEST ACKNOWLEDGE
--
-- **************************************************************

SNodeReleaseRequestAcknowledge ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeReleaseRequestAcknowledge-IEs}},
	...
}

SNodeReleaseRequestAcknowledge-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID						CRITICALITY reject		TYPE NG-RANnodeUEXnAPID									PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID						CRITICALITY reject		TYPE NG-RANnodeUEXnAPID									PRESENCE optional }|
	{ ID id-PDUSessionToBeReleased-RelReqAck			CRITICALITY ignore		TYPE PDUSessionToBeReleasedList-RelReqAck			PRESENCE optional }|
	{ ID id-CriticalityDiagnostics						CRITICALITY ignore		TYPE CriticalityDiagnostics							PRESENCE optional }|
	{ ID id-SCGUEHistoryInformation						CRITICALITY ignore		TYPE SCGUEHistoryInformation							PRESENCE optional }|
	{ ID id-SNMobilityInformation						CRITICALITY ignore		TYPE SNMobilityInformation								PRESENCE optional },
	...
}

PDUSessionToBeReleasedList-RelReqAck ::= SEQUENCE {
	pduSessionsToBeReleasedList-SNterminated		PDUSession-List-withDataForwardingRequest												OPTIONAL,
	iE-Extensions									ProtocolExtensionContainer { {PDUSessionToBeReleasedList-RelReqAck-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionToBeReleasedList-RelReqAck-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

-- **************************************************************
--
-- S-NODE RELEASE REJECT
--
-- **************************************************************

SNodeReleaseReject ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeReleaseReject-IEs}},
	...
}

SNodeReleaseReject-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE optional }|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause											PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics						PRESENCE optional },
	...
}

-- **************************************************************
--
-- S-NODE RELEASE REQUIRED
--
-- **************************************************************

SNodeReleaseRequired ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeReleaseRequired-IEs}},
	...
}

SNodeReleaseRequired-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-PDUSessionToBeReleasedList-RelRqd		CRITICALITY ignore		TYPE PDUSessionToBeReleasedList-RelRqd		PRESENCE optional }|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause											PRESENCE mandatory}|
	{ ID id-SN-to-MN-Container						CRITICALITY ignore		TYPE OCTET STRING									PRESENCE optional }|
	{ ID id-SCGUEHistoryInformation					CRITICALITY ignore		TYPE SCGUEHistoryInformation					PRESENCE optional }|
	{ ID id-PDUSessionsListToBeReleased-UPError		CRITICALITY ignore		TYPE PDUSessionsListToBeReleased-UPError	PRESENCE optional },
	...
}

PDUSessionToBeReleasedList-RelRqd ::= SEQUENCE {
	pduSessionsToBeReleasedList-SNterminated		PDUSession-List-withDataForwardingRequest						OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionToBeReleasedList-RelRqd-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionToBeReleasedList-RelRqd-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- S-NODE RELEASE CONFIRM
--
-- **************************************************************

SNodeReleaseConfirm ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeReleaseConfirm-IEs}},
	...
}

SNodeReleaseConfirm-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-PDUSessionReleasedList-RelConf			CRITICALITY ignore		TYPE PDUSessionReleasedList-RelConf			PRESENCE optional }|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics						PRESENCE optional },
	...
}

PDUSessionReleasedList-RelConf ::= SEQUENCE {
	pduSessionsReleasedList-SNterminated		PDUSession-List-withDataForwardingFromTarget					OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionReleasedList-RelConf-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionReleasedList-RelConf-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- S-NODE COUNTER CHECK REQUEST
--
-- **************************************************************

SNodeCounterCheckRequest ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeCounterCheckRequest-IEs}},
	...
}

SNodeCounterCheckRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-BearersSubjectToCounterCheck			CRITICALITY ignore		TYPE BearersSubjectToCounterCheck-List		PRESENCE mandatory},
	...
}

BearersSubjectToCounterCheck-List ::= SEQUENCE (SIZE(1..maxnoofDRBs)) OF BearersSubjectToCounterCheck-Item

BearersSubjectToCounterCheck-Item ::= SEQUENCE {
	drb-ID							DRB-ID,
	ul-count						INTEGER (0.. 4294967295),
	dl-count						INTEGER (0.. 4294967295),
	iE-Extensions					ProtocolExtensionContainer { {BearersSubjectToCounterCheck-Item-ExtIEs} }	OPTIONAL,
	...
}

BearersSubjectToCounterCheck-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- S-NODE CHANGE REQUIRED
--
-- **************************************************************

SNodeChangeRequired ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeChangeRequired-IEs}},
	...
}

SNodeChangeRequired-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-target-S-NG-RANnodeID					CRITICALITY reject		TYPE GlobalNG-RANNode-ID							PRESENCE mandatory}|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause											PRESENCE mandatory}|
	{ ID id-PDUSession-SNChangeRequired-List		CRITICALITY ignore		TYPE PDUSession-SNChangeRequired-List		PRESENCE optional }|
	{ ID id-SN-to-MN-Container						CRITICALITY reject		TYPE OCTET STRING									PRESENCE mandatory}|
	{ ID id-SCGUEHistoryInformation					CRITICALITY ignore		TYPE SCGUEHistoryInformation					PRESENCE optional }|
	{ ID id-SNMobilityInformation					CRITICALITY ignore		TYPE SNMobilityInformation						PRESENCE optional }|
	{ ID id-SourcePSCellID							CRITICALITY ignore		TYPE GlobalNG-RANCell-ID							PRESENCE optional }|
	{ ID id-CPCInformationRequired					CRITICALITY ignore		TYPE CPCInformationRequired						PRESENCE optional }|
	{ ID id-SourceSN-to-TargetSN-QMCInfo			CRITICALITY ignore		TYPE QMCConfigInfo									PRESENCE optional },
	...
}

PDUSession-SNChangeRequired-List ::= SEQUENCE (SIZE(1..maxnoofPDUSessions)) OF PDUSession-SNChangeRequired-Item

PDUSession-SNChangeRequired-Item ::= SEQUENCE {
	pduSessionId				PDUSession-ID,
	sn-terminated			PDUSessionResourceChangeRequiredInfo-SNterminated	OPTIONAL,
	mn-terminated			PDUSessionResourceChangeRequiredInfo-MNterminated	OPTIONAL,
-- NOTE: If the PDU Session Resource Change Required Info – SN terminated IE is not present,
-- abnormal conditions as specified in clause 8.3.5.4 apply.
	iE-Extension			ProtocolExtensionContainer { {PDUSession-SNChangeRequired-Item-ExtIEs} }	OPTIONAL,
	...
}

PDUSession-SNChangeRequired-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- S-NODE CHANGE CONFIRM
--
-- **************************************************************

SNodeChangeConfirm ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeChangeConfirm-IEs}},
	...
}

SNodeChangeConfirm-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-PDUSession-SNChangeConfirm-List			CRITICALITY ignore		TYPE PDUSession-SNChangeConfirm-List			PRESENCE optional }|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics						PRESENCE optional }|
	{ ID id-CPCInformationConfirm					CRITICALITY ignore		TYPE CPCInformationConfirm						PRESENCE optional }|
	{ ID id-MN-to-SN-Container						CRITICALITY ignore		TYPE OCTET STRING									PRESENCE optional },
	...
}
PDUSession-SNChangeConfirm-List ::= SEQUENCE (SIZE(1..maxnoofPDUSessions)) OF PDUSession-SNChangeConfirm-Item

PDUSession-SNChangeConfirm-Item ::= SEQUENCE {
	pduSessionId			PDUSession-ID,
	sn-terminated			PDUSessionResourceChangeConfirmInfo-SNterminated	OPTIONAL,
	mn-terminated			PDUSessionResourceChangeConfirmInfo-MNterminated	OPTIONAL,
-- NOTE: If the PDU Session Resource Change Confirm Info – SN terminated IE is not present,
-- abnormal conditions as specified in clause 8.3.5.4 apply.
	iE-Extension			ProtocolExtensionContainer { {PDUSession-SNChangeConfirm-Item-ExtIEs} }	OPTIONAL,
	...
}

PDUSession-SNChangeConfirm-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-AdditionalListofPDUSessionResourceChangeConfirmInfo-SNterminated		CRITICALITY ignore	EXTENSION			AdditionalListofPDUSessionResourceChangeConfirmInfo-SNterminated				PRESENCE optional},
	...
}


-- **************************************************************
--
-- S-NODE CHANGE REFUSE
--
-- **************************************************************

SNodeChangeRefuse ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ SNodeChangeRefuse-IEs}},
	...
}

SNodeChangeRefuse-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause											PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics						PRESENCE optional },
	...
}

-- **************************************************************
--
-- RRC TRANSFER
--
-- **************************************************************

RRCTransfer ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ RRCTransfer-IEs}},
	...
}

RRCTransfer-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-SplitSRB-RRCTransfer					CRITICALITY reject		TYPE SplitSRB-RRCTransfer						PRESENCE optional }|
	{ ID id-UEReportRRCTransfer						CRITICALITY reject		TYPE UEReportRRCTransfer							PRESENCE optional }|
	{ ID id-FastMCGRecoveryRRCTransfer-SN-to-MN		CRITICALITY ignore		TYPE FastMCGRecoveryRRCTransfer				PRESENCE optional }|
	{ ID id-FastMCGRecoveryRRCTransfer-MN-to-SN		CRITICALITY ignore		TYPE FastMCGRecoveryRRCTransfer				PRESENCE optional }|
	{ ID id-SDT-SRB-between-NewNode-OldNode			CRITICALITY ignore		TYPE SDT-SRB-between-NewNode-OldNode			PRESENCE optional }|
	{ ID id-QoE-Measurement-Results					CRITICALITY ignore		TYPE QoE-Measurement-Results					PRESENCE optional },
	...
}

SplitSRB-RRCTransfer ::= SEQUENCE {
	rrcContainer					OCTET STRING						OPTIONAL,
	srbType							ENUMERATED {srb1, srb2, ...},
	deliveryStatus					DeliveryStatus						OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {SplitSRB-RRCTransfer-ExtIEs} } OPTIONAL,
	...
}

SplitSRB-RRCTransfer-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

UEReportRRCTransfer::= SEQUENCE {
	rrcContainer					OCTET STRING,
	iE-Extensions					ProtocolExtensionContainer { {UEReportRRCTransfer-ExtIEs} } OPTIONAL,
	...
}

UEReportRRCTransfer-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

FastMCGRecoveryRRCTransfer::= SEQUENCE {
	rrcContainer					OCTET STRING,
	iE-Extensions					ProtocolExtensionContainer { { FastMCGRecoveryRRCTransfer-ExtIEs} } OPTIONAL,
	...
}

FastMCGRecoveryRRCTransfer-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SDT-SRB-between-NewNode-OldNode::= SEQUENCE {
	rrcContainer					OCTET STRING,
	srb-ID							SRB-ID,
	iE-Extensions					ProtocolExtensionContainer { { SDT-SRB-between-NewNode-OldNode-ExtIEs} } OPTIONAL,
	...
}

SDT-SRB-between-NewNode-OldNode-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

QoE-Measurement-Results ::= SEQUENCE {
	qOEReference								OCTET STRING (SIZE(6)),
	rrcContainerForRVQoEReport					OCTET STRING									OPTIONAL,
	rrcContainerForQoEReport					OCTET STRING									OPTIONAL,
	appLayerSessionStatus						ENUMERATED {started, stopped, ...}				OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { {QoE-Measurement-Results-ExtIEs} }	OPTIONAL,
	...
}

QoE-Measurement-Results-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- NOTIFICATION CONTROL INDICATION
--
-- **************************************************************

NotificationControlIndication ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{NotificationControlIndication-IEs}},
	...
}

NotificationControlIndication-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID				CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID				CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-PDUSessionResourcesNotifyList		CRITICALITY reject		TYPE PDUSessionResourcesNotifyList			PRESENCE optional },
	...
}

PDUSessionResourcesNotifyList ::= SEQUENCE (SIZE(1..maxnoofPDUSessions)) OF PDUSessionResourcesNotify-Item

PDUSessionResourcesNotify-Item ::= SEQUENCE {
	pduSessionId						PDUSession-ID,
	qosFlowsNotificationContrIndInfo	QoSFlowNotificationControlIndicationInfo,
	iE-Extensions						ProtocolExtensionContainer { {PDUSessionResourcesNotify-Item-ExtIEs} } OPTIONAL,
	...
}

PDUSessionResourcesNotify-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

-- **************************************************************
--
-- ACTIVITY NOTIFICATION
--
-- **************************************************************

ActivityNotification ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ActivityNotification-IEs}},
	...
}

ActivityNotification-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-UserPlaneTrafficActivityReport			CRITICALITY ignore		TYPE UserPlaneTrafficActivityReport			PRESENCE optional }|
	{ ID id-PDUSessionResourcesActivityNotifyList	CRITICALITY ignore		TYPE PDUSessionResourcesActivityNotifyList	PRESENCE optional }|
	{ ID id-RANPagingFailure						CRITICALITY ignore		TYPE RANPagingFailure								PRESENCE optional },
	...
}

PDUSessionResourcesActivityNotifyList ::= SEQUENCE (SIZE(1..maxnoofPDUSessions)) OF PDUSessionResourcesActivityNotify-Item

PDUSessionResourcesActivityNotify-Item ::= SEQUENCE {
	pduSessionId						PDUSession-ID,
	pduSessionLevelUPactivityreport		UserPlaneTrafficActivityReport											OPTIONAL,
	qosFlowsActivityNotifyList			QoSFlowsActivityNotifyList												OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { {PDUSessionResourcesActivityNotify-Item-ExtIEs} } OPTIONAL,
	...
}

PDUSessionResourcesActivityNotify-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

QoSFlowsActivityNotifyList ::= SEQUENCE (SIZE(1..maxnoofQoSFlows)) OF QoSFlowsActivityNotifyItem

QoSFlowsActivityNotifyItem ::= SEQUENCE {
	qosFlowIdentifier					QoSFlowIdentifier,
	pduSessionLevelUPactivityreport		UserPlaneTrafficActivityReport,
	iE-Extensions						ProtocolExtensionContainer { {QoSFlowsActivityNotifyItem-ExtIEs} } OPTIONAL,
	...
}

QoSFlowsActivityNotifyItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

-- **************************************************************
--
-- XN SETUP REQUEST
--
-- **************************************************************

XnSetupRequest ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ XnSetupRequest-IEs}},
	...
}

XnSetupRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-GlobalNG-RAN-node-ID				CRITICALITY reject	TYPE GlobalNG-RANNode-ID						PRESENCE mandatory}|
	{ ID id-TAISupport-list						CRITICALITY reject	TYPE TAISupport-List							PRESENCE mandatory}|
	{ ID id-AMF-Region-Information				CRITICALITY reject	TYPE AMF-Region-Information						PRESENCE mandatory}|
	{ ID id-List-of-served-cells-NR				CRITICALITY reject	TYPE ServedCells-NR								PRESENCE optional }|
	{ ID id-List-of-served-cells-E-UTRA			CRITICALITY reject	TYPE ServedCells-E-UTRA							PRESENCE optional }|
	{ ID id-InterfaceInstanceIndication			CRITICALITY reject	TYPE InterfaceInstanceIndication				PRESENCE optional }|
	{ ID id-TNLConfigurationInfo				CRITICALITY ignore	TYPE TNLConfigurationInfo						PRESENCE optional }|
	{ ID id-PartialListIndicator-NR				CRITICALITY ignore	TYPE PartialListIndicator						PRESENCE optional }|
	{ ID id-CellAndCapacityAssistanceInfo-NR 	CRITICALITY ignore 	TYPE CellAndCapacityAssistanceInfo-NR		PRESENCE optional }|
	{ ID id-PartialListIndicator-EUTRA			CRITICALITY ignore	TYPE PartialListIndicator						PRESENCE optional }|
	{ ID id-CellAndCapacityAssistanceInfo-EUTRA	CRITICALITY ignore 	TYPE CellAndCapacityAssistanceInfo-EUTRA	PRESENCE optional }|
	{ ID id-Local-NG-RAN-Node-Identifier		CRITICALITY ignore	TYPE Local-NG-RAN-Node-Identifier			PRESENCE optional }|
	{ ID id-Neighbour-NG-RAN-Node-List			CRITICALITY ignore	TYPE Neighbour-NG-RAN-Node-List				PRESENCE optional },
	...
}

-- **************************************************************
--
-- XN SETUP RESPONSE
--
-- **************************************************************

XnSetupResponse ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ XnSetupResponse-IEs}},
	...
}

XnSetupResponse-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-GlobalNG-RAN-node-ID				CRITICALITY reject	TYPE GlobalNG-RANNode-ID						PRESENCE mandatory}|
	{ ID id-TAISupport-list						CRITICALITY reject	TYPE TAISupport-List							PRESENCE mandatory}|
	{ ID id-List-of-served-cells-NR				CRITICALITY reject	TYPE ServedCells-NR								PRESENCE optional }|
	{ ID id-List-of-served-cells-E-UTRA			CRITICALITY reject	TYPE ServedCells-E-UTRA							PRESENCE optional }|
	{ ID id-CriticalityDiagnostics				CRITICALITY ignore	TYPE CriticalityDiagnostics						PRESENCE optional }|
	{ ID id-AMF-Region-Information				CRITICALITY reject	TYPE AMF-Region-Information						PRESENCE optional }|
	{ ID id-InterfaceInstanceIndication			CRITICALITY reject	TYPE InterfaceInstanceIndication				PRESENCE optional }|
	{ ID id-TNLConfigurationInfo				CRITICALITY ignore	TYPE TNLConfigurationInfo						PRESENCE optional }|
	{ ID id-PartialListIndicator-NR				CRITICALITY ignore	TYPE PartialListIndicator						PRESENCE optional }|
	{ ID id-CellAndCapacityAssistanceInfo-NR	CRITICALITY ignore 	TYPE CellAndCapacityAssistanceInfo-NR		PRESENCE optional }|
	{ ID id-PartialListIndicator-EUTRA			CRITICALITY ignore	TYPE PartialListIndicator						PRESENCE optional }|
	{ ID id-CellAndCapacityAssistanceInfo-EUTRA	CRITICALITY ignore 	TYPE CellAndCapacityAssistanceInfo-EUTRA	PRESENCE optional }|
	{ ID id-Local-NG-RAN-Node-Identifier		CRITICALITY ignore	TYPE Local-NG-RAN-Node-Identifier			PRESENCE optional }|
	{ ID id-Neighbour-NG-RAN-Node-List			CRITICALITY ignore	TYPE Neighbour-NG-RAN-Node-List				PRESENCE optional },
	...
}

-- **************************************************************
--
-- XN SETUP FAILURE
--
-- **************************************************************

XnSetupFailure ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ XnSetupFailure-IEs}},
	...
}

XnSetupFailure-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-Cause						CRITICALITY ignore	TYPE Cause							PRESENCE mandatory}|
	{ ID id-TimeToWait					CRITICALITY ignore	TYPE TimeToWait						PRESENCE optional }|
	{ ID id-CriticalityDiagnostics		CRITICALITY ignore	TYPE CriticalityDiagnostics			PRESENCE optional }|
	{ ID id-InterfaceInstanceIndication	CRITICALITY reject	TYPE InterfaceInstanceIndication	PRESENCE optional }|
	{ ID id-MessageOversizeNotification	CRITICALITY ignore	TYPE MessageOversizeNotification	PRESENCE optional },
	...
}

-- **************************************************************
--
-- NG-RAN NODE CONFIGURATION UPDATE
--
-- **************************************************************

NGRANNodeConfigurationUpdate ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ NGRANNodeConfigurationUpdate-IEs}},
	...
}

NGRANNodeConfigurationUpdate-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-TAISupport-list							CRITICALITY reject	TYPE TAISupport-List									PRESENCE optional }|
	{ ID id-ConfigurationUpdateInitiatingNodeChoice	CRITICALITY ignore	TYPE ConfigurationUpdateInitiatingNodeChoice	PRESENCE mandatory}|
	{ ID id-TNLA-To-Add-List						CRITICALITY ignore	TYPE TNLA-To-Add-List									PRESENCE optional }|
	{ ID id-TNLA-To-Remove-List						CRITICALITY ignore	TYPE TNLA-To-Remove-List								PRESENCE optional }|
	{ ID id-TNLA-To-Update-List						CRITICALITY ignore	TYPE TNLA-To-Update-List								PRESENCE optional }|
	{ ID id-GlobalNG-RAN-node-ID					CRITICALITY reject	TYPE GlobalNG-RANNode-ID								PRESENCE optional }|
	{ ID id-AMF-Region-Information-To-Add			CRITICALITY reject	TYPE AMF-Region-Information							PRESENCE optional }|
	{ ID id-AMF-Region-Information-To-Delete		CRITICALITY reject	TYPE AMF-Region-Information							PRESENCE optional }|
	{ ID id-InterfaceInstanceIndication				CRITICALITY reject	TYPE InterfaceInstanceIndication					PRESENCE optional }|
	{ ID id-TNLConfigurationInfo					CRITICALITY ignore	TYPE TNLConfigurationInfo								PRESENCE optional }|
	{ ID id-Coverage-Modification-List				CRITICALITY reject	TYPE Coverage-Modification-List						PRESENCE optional }|
	{ ID id-Local-NG-RAN-Node-Identifier			CRITICALITY ignore	TYPE Local-NG-RAN-Node-Identifier					PRESENCE optional }|
	{ ID id-Neighbour-NG-RAN-Node-List				CRITICALITY ignore	TYPE Neighbour-NG-RAN-Node-List						PRESENCE optional }|
	{ ID id-Local-NG-RAN-Node-Identifier-Removal	CRITICALITY ignore	TYPE Local-NG-RAN-Node-Identifier					PRESENCE optional },
	...
}

ConfigurationUpdateInitiatingNodeChoice ::= CHOICE {
	gNB									ProtocolIE-Container	{ {ConfigurationUpdate-gNB} },
	ng-eNB								ProtocolIE-Container	{ {ConfigurationUpdate-ng-eNB} },
	choice-extension					ProtocolIE-Single-Container { {ServedCellsToUpdateInitiatingNodeChoice-ExtIEs} }
}

ServedCellsToUpdateInitiatingNodeChoice-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

ConfigurationUpdate-gNB XNAP-PROTOCOL-IES ::= {
	{ ID id-servedCellsToUpdate-NR			CRITICALITY ignore TYPE	ServedCellsToUpdate-NR							PRESENCE optional }|
	{ ID id-cellAssistanceInfo-NR			CRITICALITY ignore TYPE	CellAssistanceInfo-NR							PRESENCE optional }|
	{ ID id-cellAssistanceInfo-EUTRA		CRITICALITY ignore TYPE	CellAssistanceInfo-EUTRA						PRESENCE optional }|
	{ ID id-ServedCellSpecificInfoReq-NR	CRITICALITY ignore TYPE	ServedCellSpecificInfoReq-NR					PRESENCE optional},
	...
}


ConfigurationUpdate-ng-eNB XNAP-PROTOCOL-IES ::= {
	{ ID id-servedCellsToUpdate-E-UTRA			CRITICALITY ignore TYPE	ServedCellsToUpdate-E-UTRA				PRESENCE optional }|
	{ ID id-cellAssistanceInfo-NR				CRITICALITY ignore TYPE	CellAssistanceInfo-NR						PRESENCE optional }|
	{ ID id-cellAssistanceInfo-EUTRA			CRITICALITY ignore TYPE	CellAssistanceInfo-EUTRA					PRESENCE optional },
	...
}



-- **************************************************************
--
-- NG-RAN NODE CONFIGURATION UPDATE ACKNOWLEDGE
--
-- **************************************************************

NGRANNodeConfigurationUpdateAcknowledge ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ NGRANNodeConfigurationUpdateAcknowledge-IEs}},
	...
}

NGRANNodeConfigurationUpdateAcknowledge-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-RespondingNodeTypeConfigUpdateAck			CRITICALITY ignore	TYPE RespondingNodeTypeConfigUpdateAck		PRESENCE mandatory}|
	{ ID id-TNLA-Setup-List								CRITICALITY ignore	TYPE TNLA-Setup-List								PRESENCE optional }|
	{ ID id-TNLA-Failed-To-Setup-List					CRITICALITY ignore	TYPE TNLA-Failed-To-Setup-List					PRESENCE optional }|
	{ ID id-CriticalityDiagnostics						CRITICALITY ignore	TYPE CriticalityDiagnostics						PRESENCE optional }|
	{ ID id-InterfaceInstanceIndication					CRITICALITY reject	TYPE InterfaceInstanceIndication				PRESENCE optional }|
	{ ID id-TNLConfigurationInfo						CRITICALITY ignore	TYPE TNLConfigurationInfo						PRESENCE optional	}|
	{ ID id-Local-NG-RAN-Node-Identifier				CRITICALITY ignore	TYPE Local-NG-RAN-Node-Identifier				PRESENCE optional	}|
	{ ID id-Neighbour-NG-RAN-Node-List					CRITICALITY ignore	TYPE Neighbour-NG-RAN-Node-List				PRESENCE optional	}|
	{ ID id-Local-NG-RAN-Node-Identifier-Removal		CRITICALITY ignore	TYPE Local-NG-RAN-Node-Identifier				PRESENCE optional	},
	...
}
RespondingNodeTypeConfigUpdateAck ::= CHOICE {
	ng-eNB					RespondingNodeTypeConfigUpdateAck-ng-eNB,
	gNB						RespondingNodeTypeConfigUpdateAck-gNB,
	choice-extension		ProtocolIE-Single-Container { {RespondingNodeTypeConfigUpdateAck-ExtIEs} }
}

RespondingNodeTypeConfigUpdateAck-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

RespondingNodeTypeConfigUpdateAck-ng-eNB ::= SEQUENCE {
	iE-Extension		ProtocolExtensionContainer { {RespondingNodeTypeConfigUpdateAck-ng-eNB-ExtIEs} }	OPTIONAL,
	...
}

RespondingNodeTypeConfigUpdateAck-ng-eNB-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-List-of-served-cells-E-UTRA				CRITICALITY ignore	EXTENSION ServedCells-E-UTRA							PRESENCE optional }|
	{ ID id-PartialListIndicator-EUTRA				CRITICALITY ignore	EXTENSION PartialListIndicator						PRESENCE optional }|
	{ ID id-CellAndCapacityAssistanceInfo-EUTRA		CRITICALITY ignore	EXTENSION CellAndCapacityAssistanceInfo-EUTRA	PRESENCE optional },
	...
}


RespondingNodeTypeConfigUpdateAck-gNB ::= SEQUENCE {
	served-NR-Cells		ServedCells-NR																OPTIONAL,
	iE-Extension		ProtocolExtensionContainer { {RespondingNodeTypeConfigUpdateAck-gNB-ExtIEs} }	OPTIONAL,
	...
}

RespondingNodeTypeConfigUpdateAck-gNB-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-PartialListIndicator-NR				CRITICALITY ignore	EXTENSION PartialListIndicator						PRESENCE optional }|
	{ ID id-CellAndCapacityAssistanceInfo-NR 	CRITICALITY ignore 	EXTENSION CellAndCapacityAssistanceInfo-NR		PRESENCE optional },
	...
}


-- **************************************************************
--
-- NG-RAN NODE CONFIGURATION UPDATE FAILURE
--
-- **************************************************************

NGRANNodeConfigurationUpdateFailure ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{NGRANNodeConfigurationUpdateFailure-IEs}},
	...
}

NGRANNodeConfigurationUpdateFailure-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-Cause						CRITICALITY ignore	TYPE Cause							PRESENCE mandatory}|
	{ ID id-TimeToWait					CRITICALITY ignore	TYPE TimeToWait						PRESENCE optional }|
	{ ID id-CriticalityDiagnostics		CRITICALITY ignore	TYPE CriticalityDiagnostics			PRESENCE optional }|
	{ ID id-InterfaceInstanceIndication	CRITICALITY reject	TYPE InterfaceInstanceIndication	PRESENCE optional },
	...
}


-- **************************************************************
--
-- E-UTRA - NR CELL RESOURCE COORDINATION REQUEST
--
-- **************************************************************

E-UTRA-NR-CellResourceCoordinationRequest ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{E-UTRA-NR-CellResourceCoordinationRequest-IEs}},
	...
}

E-UTRA-NR-CellResourceCoordinationRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-initiatingNodeType-ResourceCoordRequest	CRITICALITY reject	TYPE InitiatingNodeType-ResourceCoordRequest		PRESENCE mandatory}|
	{ ID id-InterfaceInstanceIndication				CRITICALITY reject	TYPE InterfaceInstanceIndication						PRESENCE optional },
	...
}

InitiatingNodeType-ResourceCoordRequest ::= CHOICE {
	ng-eNB								ResourceCoordRequest-ng-eNB-initiated,
	gNB									ResourceCoordRequest-gNB-initiated,
	choice-extension					ProtocolIE-Single-Container { {InitiatingNodeType-ResourceCoordRequest-ExtIEs} }
}

InitiatingNodeType-ResourceCoordRequest-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

ResourceCoordRequest-ng-eNB-initiated ::= SEQUENCE {
	dataTrafficResourceIndication		DataTrafficResourceIndication,
	spectrumSharingGroupID				SpectrumSharingGroupID,
	listofE-UTRACells					SEQUENCE (SIZE(1.. maxnoofCellsinNG-RANnode)) OF E-UTRA-CGI							OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { {ResourceCoordRequest-ng-eNB-initiated-ExtIEs} }	OPTIONAL,
	...
}

ResourceCoordRequest-ng-eNB-initiated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ResourceCoordRequest-gNB-initiated ::= SEQUENCE {
	dataTrafficResourceIndication		DataTrafficResourceIndication,
	listofE-UTRACells					SEQUENCE (SIZE(1.. maxnoofCellsinNG-RANnode)) OF E-UTRA-CGI						OPTIONAL,
	spectrumSharingGroupID				SpectrumSharingGroupID,
	listofNRCells						SEQUENCE (SIZE(1.. maxnoofCellsinNG-RANnode)) OF NR-CGI								OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { {ResourceCoordRequest-gNB-initiated-ExtIEs} }	OPTIONAL,
	...
}

ResourceCoordRequest-gNB-initiated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- E-UTRA - NR CELL RESOURCE COORDINATION RESPONSE
--
-- **************************************************************

E-UTRA-NR-CellResourceCoordinationResponse::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{E-UTRA-NR-CellResourceCoordinationResponse-IEs}},
	...
}

E-UTRA-NR-CellResourceCoordinationResponse-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-respondingNodeType-ResourceCoordResponse	CRITICALITY reject	TYPE RespondingNodeType-ResourceCoordResponse 	PRESENCE mandatory}|
	{ ID id-InterfaceInstanceIndication					CRITICALITY reject	TYPE InterfaceInstanceIndication						PRESENCE optional },
	...
}

RespondingNodeType-ResourceCoordResponse ::= CHOICE {
	ng-eNB								ResourceCoordResponse-ng-eNB-initiated,
	gNB									ResourceCoordResponse-gNB-initiated,
	choice-extension					ProtocolIE-Single-Container { {RespondingNodeType-ResourceCoordResponse-ExtIEs} }
}

RespondingNodeType-ResourceCoordResponse-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

ResourceCoordResponse-ng-eNB-initiated ::= SEQUENCE {
	dataTrafficResourceIndication		DataTrafficResourceIndication,
	spectrumSharingGroupID				SpectrumSharingGroupID,
	listofE-UTRACells					SEQUENCE (SIZE(1.. maxnoofCellsinNG-RANnode)) OF E-UTRA-CGI								OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { {ResourceCoordResponse-ng-eNB-initiated-ExtIEs} }		OPTIONAL,
	...
}

ResourceCoordResponse-ng-eNB-initiated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ResourceCoordResponse-gNB-initiated ::= SEQUENCE {
	dataTrafficResourceIndication		DataTrafficResourceIndication,
	spectrumSharingGroupID				SpectrumSharingGroupID,
	listofNRCells						SEQUENCE (SIZE(1.. maxnoofCellsinNG-RANnode)) OF NR-CGI									OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { {ResourceCoordResponse-gNB-initiated-ExtIEs} }		OPTIONAL,
	...
}

ResourceCoordResponse-gNB-initiated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

-- **************************************************************
--
-- SECONDARY RAT DATA USAGE REPORT
--
-- **************************************************************

SecondaryRATDataUsageReport ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container		{{SecondaryRATDataUsageReport-IEs}},
	...
}

SecondaryRATDataUsageReport-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID								PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID								PRESENCE mandatory}|
	{ ID id-PDUSessionResourceSecondaryRATUsageList	CRITICALITY reject		TYPE PDUSessionResourceSecondaryRATUsageList	PRESENCE mandatory},
	...
}


-- **************************************************************
--
-- XN REMOVAL REQUEST
--
-- **************************************************************

XnRemovalRequest ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ XnRemovalRequest-IEs}},
	...
}

XnRemovalRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-GlobalNG-RAN-node-ID		CRITICALITY reject	TYPE GlobalNG-RANNode-ID			PRESENCE mandatory}|
	{ ID id-XnRemovalThreshold			CRITICALITY reject	TYPE XnBenefitValue					PRESENCE optional }|
	{ ID id-InterfaceInstanceIndication	CRITICALITY reject	TYPE InterfaceInstanceIndication	PRESENCE optional },
	...
}

-- **************************************************************
--
-- XN REMOVAL RESPONSE
--
-- **************************************************************

XnRemovalResponse ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ XnRemovalResponse-IEs}},
	...
}

XnRemovalResponse-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-GlobalNG-RAN-node-ID		CRITICALITY reject	TYPE GlobalNG-RANNode-ID			PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics		CRITICALITY ignore	TYPE CriticalityDiagnostics			PRESENCE optional }|
	{ ID id-InterfaceInstanceIndication	CRITICALITY reject	TYPE InterfaceInstanceIndication	PRESENCE optional },
	...
}

-- **************************************************************
--
-- XN REMOVAL FAILURE
--
-- **************************************************************

XnRemovalFailure ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ XnRemovalFailure-IEs}},
	...
}

XnRemovalFailure-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-Cause						CRITICALITY ignore	TYPE Cause							PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics		CRITICALITY ignore	TYPE CriticalityDiagnostics			PRESENCE optional }|
	{ ID id-InterfaceInstanceIndication	CRITICALITY reject	TYPE InterfaceInstanceIndication	PRESENCE optional },
	...
}

-- **************************************************************
--
-- CELL ACTIVATION REQUEST
--
-- **************************************************************

CellActivationRequest ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ CellActivationRequest-IEs}},
	...
}

CellActivationRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-ServedCellsToActivate					CRITICALITY reject		TYPE ServedCellsToActivate						PRESENCE mandatory}|
	{ ID id-ActivationIDforCellActivation			CRITICALITY reject		TYPE ActivationIDforCellActivation			PRESENCE mandatory}|
	{ ID id-InterfaceInstanceIndication				CRITICALITY reject		TYPE InterfaceInstanceIndication				PRESENCE optional },
	...
}

ServedCellsToActivate ::= CHOICE {
	nr-cells							SEQUENCE (SIZE(1..maxnoofCellsinNG-RANnode)) OF NR-CGI,
	e-utra-cells						SEQUENCE (SIZE(1..maxnoofCellsinNG-RANnode)) OF E-UTRA-CGI,
	choice-extension					ProtocolIE-Single-Container { {ServedCellsToActivate-ExtIEs} }
}

ServedCellsToActivate-ExtIEs XNAP-PROTOCOL-IES ::= {
	{ ID id-NRCellsAndSSBsList			CRITICALITY ignore		TYPE ToBeActivatedNRCellsAndSSBsList				PRESENCE mandatory},
	...
}

ToBeActivatedNRCellsAndSSBsList ::= SEQUENCE (SIZE(1..maxnoofCellsinNG-RANnode)) OF ToBeActivatedNRCellsAndSSBs-Item

ToBeActivatedNRCellsAndSSBs-Item ::= SEQUENCE {
	nrCGI						NR-CGI,
	sSBstobeActivatedList		SEQUENCE (SIZE(1.. maxnoofSSBAreas)) OF SSBsToBeActivated-Item	OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { ToBeActivatedNRCellsAndSSBs-Item-ExtIEs} }	OPTIONAL,
	...
}

ToBeActivatedNRCellsAndSSBs-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SSBsToBeActivated-Item ::= SEQUENCE {
	ssbIndex			INTEGER(0..63),
	iE-Extensions		ProtocolExtensionContainer { { SSBsToBeActivated-Item-ExtIEs} }	OPTIONAL,
	...
}

SSBsToBeActivated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- CELL ACTIVATION RESPONSE
--
-- **************************************************************

CellActivationResponse ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{CellActivationResponse-IEs}},
	...
}

CellActivationResponse-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-ActivatedServedCells					CRITICALITY reject		TYPE ActivatedServedCells						PRESENCE mandatory}|
	{ ID id-ActivationIDforCellActivation			CRITICALITY reject		TYPE ActivationIDforCellActivation			PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics						PRESENCE optional }|
	{ ID id-InterfaceInstanceIndication				CRITICALITY reject		TYPE InterfaceInstanceIndication				PRESENCE optional },
	...
}

ActivatedServedCells ::= CHOICE {
	nr-cells							SEQUENCE (SIZE(1..maxnoofCellsinNG-RANnode)) OF NR-CGI,
	e-utra-cells						SEQUENCE (SIZE(1..maxnoofCellsinNG-RANnode)) OF E-UTRA-CGI,
	choice-extension					ProtocolIE-Single-Container { {ActivatedServedCells-ExtIEs} }
}

ActivatedServedCells-ExtIEs XNAP-PROTOCOL-IES ::= {
	{ ID id-ActivatedNRCellsAndSSBsList				CRITICALITY ignore		TYPE ActivatedNRCellsAndSSBsList			PRESENCE mandatory},
	...
}

ActivatedNRCellsAndSSBsList ::= SEQUENCE (SIZE(1..maxnoofCellsinNG-RANnode)) OF ActivatedNRCellsAndSSBs-Item

ActivatedNRCellsAndSSBs-Item ::= SEQUENCE {
	nrCGI					NR-CGI,
	sSBsActivatedList	SEQUENCE (SIZE(1..maxnoofSSBAreas)) OF SSBsActivated-Item		OPTIONAL,
	iE-Extensions	ProtocolExtensionContainer { {ActivatedNRCellsAndSSBs-Item-ExtIEs} }	OPTIONAL,
	...
}

ActivatedNRCellsAndSSBs-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SSBsActivated-Item ::= SEQUENCE {
	ssbIndex		INTEGER(0..63),
	iE-Extensions	ProtocolExtensionContainer { {SSBsActivated-Item-ExtIEs} }	OPTIONAL,
	...
}

SSBsActivated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- CELL ACTIVATION FAILURE
--
-- **************************************************************

CellActivationFailure ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{CellActivationFailure-IEs}},
	...
}

CellActivationFailure-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-ActivationIDforCellActivation			CRITICALITY reject		TYPE ActivationIDforCellActivation			PRESENCE mandatory}|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause											PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics						PRESENCE optional }|
	{ ID id-InterfaceInstanceIndication				CRITICALITY reject		TYPE InterfaceInstanceIndication				PRESENCE optional },
	...
}

-- **************************************************************
--
-- RESET REQUEST
--
-- **************************************************************

ResetRequest ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ResetRequest-IEs}},
	...
}

ResetRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-ResetRequestTypeInfo					CRITICALITY reject		TYPE ResetRequestTypeInfo						PRESENCE mandatory}|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause											PRESENCE mandatory}|
	{ ID id-InterfaceInstanceIndication				CRITICALITY reject		TYPE InterfaceInstanceIndication				PRESENCE optional },
	...
}

-- **************************************************************
--
-- RESET RESPONSE
--
-- **************************************************************

ResetResponse ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ResetResponse-IEs}},
	...
}

ResetResponse-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-ResetResponseTypeInfo					CRITICALITY reject		TYPE ResetResponseTypeInfo						PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics						PRESENCE optional }|
	{ ID id-InterfaceInstanceIndication				CRITICALITY reject		TYPE InterfaceInstanceIndication				PRESENCE optional },
	...
}

-- **************************************************************
--
-- ERROR INDICATION
--
-- **************************************************************

ErrorIndication ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ErrorIndication-IEs}},
	...
}

ErrorIndication-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-oldNG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE optional }|
	{ ID id-newNG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE optional }|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause											PRESENCE optional }|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics						PRESENCE optional }|
	{ ID id-InterfaceInstanceIndication				CRITICALITY reject		TYPE InterfaceInstanceIndication				PRESENCE optional },
	...
}

-- **************************************************************
--
-- PRIVATE MESSAGE
--
-- **************************************************************

PrivateMessage ::= SEQUENCE {
	privateIEs		PrivateIE-Container	{{PrivateMessage-IEs}},
	...
}

PrivateMessage-IEs XNAP-PRIVATE-IES ::= {
	...
}


-- **************************************************************
--
-- TRACE START
--
-- **************************************************************

TraceStart ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container		{ {TraceStartIEs} },
	...
}

TraceStartIEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY reject	TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY reject	TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-TraceActivation							CRITICALITY ignore	TYPE TraceActivation							PRESENCE optional },
	...
}

-- **************************************************************
--
-- DEACTIVATE TRACE
--
-- **************************************************************

DeactivateTrace ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container		{ {DeactivateTraceIEs} },
	...
}

DeactivateTraceIEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY reject	TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY reject	TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-NG-RANTraceID							CRITICALITY ignore	TYPE NG-RANTraceID								PRESENCE mandatory},
	...
}

-- **************************************************************
--
-- FAILURE INDICATION
--
-- **************************************************************

FailureIndication ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{FailureIndication-IEs}},
	...
}

FailureIndication-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-InitiatingCondition-FailureIndication				CRITICALITY reject		TYPE InitiatingCondition-FailureIndication							PRESENCE mandatory},
	...
}

-- **************************************************************
--
-- HANDOVER REPORT
--
-- **************************************************************

HandoverReport ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ HandoverReport-IEs}},
	...
}

HandoverReport-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-HandoverReportType			CRITICALITY ignore		TYPE HandoverReportType				PRESENCE mandatory}|
	{ ID id-HandoverCause				CRITICALITY ignore		TYPE Cause							PRESENCE mandatory}|
	{ ID id-SourceCellCGI				CRITICALITY ignore		TYPE GlobalNG-RANCell-ID			PRESENCE mandatory }|
	{ ID id-TargetCellCGI				CRITICALITY ignore		TYPE GlobalNG-RANCell-ID			PRESENCE mandatory }|
	{ ID id-ReEstablishmentCellCGI		CRITICALITY ignore		TYPE GlobalCell-ID					PRESENCE conditional }|
-- This IE shall be present if the Handover Report Type IE is set to the value "HO to wrong cell"
	{ ID id-TargetCellinEUTRAN			CRITICALITY ignore		TYPE TargetCellinEUTRAN				PRESENCE conditional }|
-- This IE shall be present if the Handover Report Type IE is set to the value "Inter-system ping-pong"
	{ ID id-SourceCellCRNTI				CRITICALITY ignore		TYPE C-RNTI							PRESENCE optional }|
	{ ID id-MobilityInformation			CRITICALITY ignore		TYPE MobilityInformation			PRESENCE optional }|
	{ ID id-UERLFReportContainer		CRITICALITY ignore		TYPE UERLFReportContainer			PRESENCE optional }|
	{ ID id-CHOConfiguration			CRITICALITY ignore		TYPE CHOConfiguration				PRESENCE optional}|
	{ ID id-TargetCellCRNTI				CRITICALITY ignore		TYPE C-RNTI							PRESENCE optional}|
	{ ID id-TimeSinceFailure			CRITICALITY ignore		TYPE TimeSinceFailure				PRESENCE optional},
	...
}

-- **************************************************************
--
-- RESOURCE STATUS REQUEST
--
-- **************************************************************

ResourceStatusRequest ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container	{{ResourceStatusRequest-IEs}},
	...
}

ResourceStatusRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-NGRAN-Node1-Measurement-ID			CRITICALITY reject	TYPE Measurement-ID					PRESENCE mandatory}|
	{ ID id-NGRAN-Node2-Measurement-ID			CRITICALITY ignore	TYPE Measurement-ID					PRESENCE conditional}|
-- This IE shall be present if the Registration Request IE is set to the value "stop" or "add".
	{ ID id-RegistrationRequest					CRITICALITY reject	TYPE RegistrationRequest			PRESENCE mandatory}|
	{ ID id-ReportCharacteristics				CRITICALITY reject	TYPE ReportCharacteristics			PRESENCE conditional}|
-- This IE shall be present if the Registration Request IE is set to the value "start".
	{ ID id-CellToReport						CRITICALITY ignore	TYPE CellToReport					PRESENCE optional}|
	{ ID id-ReportingPeriodicity				CRITICALITY ignore	TYPE ReportingPeriodicity			PRESENCE optional},
	...
}


-- **************************************************************
--
-- RESOURCE STATUS RESPONSE
--
-- **************************************************************

ResourceStatusResponse ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container	{{ResourceStatusResponse-IEs}},
	...
}

ResourceStatusResponse-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-NGRAN-Node1-Measurement-ID			CRITICALITY reject	TYPE Measurement-ID							PRESENCE mandatory}|
	{ ID id-NGRAN-Node2-Measurement-ID			CRITICALITY reject	TYPE Measurement-ID							PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics				CRITICALITY ignore	TYPE CriticalityDiagnostics					PRESENCE optional},
	...
}


-- **************************************************************
--
-- RESOURCE STATUS FAILURE
--
-- **************************************************************

ResourceStatusFailure ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container	{{ResourceStatusFailure-IEs}},
	...
}

ResourceStatusFailure-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-NGRAN-Node1-Measurement-ID				CRITICALITY reject	TYPE Measurement-ID									PRESENCE mandatory}|
	{ ID id-NGRAN-Node2-Measurement-ID				CRITICALITY reject	TYPE Measurement-ID									PRESENCE mandatory}|
	{ ID id-Cause									CRITICALITY ignore	TYPE Cause											PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore	TYPE CriticalityDiagnostics						PRESENCE optional},
	...
}


-- **************************************************************
--
-- RESOURCE STATUS UPDATE
--
-- **************************************************************

ResourceStatusUpdate ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container	{{ResourceStatusUpdate-IEs}},
	...
}

ResourceStatusUpdate-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-NGRAN-Node1-Measurement-ID		CRITICALITY reject	TYPE Measurement-ID					PRESENCE mandatory}|
	{ ID id-NGRAN-Node2-Measurement-ID		CRITICALITY reject	TYPE Measurement-ID					PRESENCE mandatory}|
	{ ID id-CellMeasurementResult			CRITICALITY ignore	TYPE CellMeasurementResult			PRESENCE mandatory},
	...
}

-- **************************************************************
--
-- MOBILITY CHANGE REQUEST
--
-- **************************************************************

MobilityChangeRequest ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container	{{MobilityChangeRequest-IEs}},
	...
}

MobilityChangeRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-NG-RANnode1CellID							CRITICALITY reject	TYPE GlobalNG-RANCell-ID							PRESENCE mandatory}|
	{ ID id-NG-RANnode2CellID							CRITICALITY reject	TYPE GlobalNG-RANCell-ID							PRESENCE mandatory}|
	{ ID id-NG-RANnode1MobilityParameters				CRITICALITY ignore	TYPE MobilityParametersInformation			PRESENCE optional}|
	{ ID id-NG-RANnode2ProposedMobilityParameters		CRITICALITY reject	TYPE MobilityParametersInformation			PRESENCE mandatory}|
	{ ID id-Cause										CRITICALITY ignore	TYPE Cause											PRESENCE mandatory}|
	{ ID id-SSBOffsets-List								CRITICALITY ignore	TYPE SSBOffsets-List								PRESENCE optional},
	...
}


-- **************************************************************
--
-- MOBILITY CHANGE ACKNOWLEDGE
--
-- **************************************************************

MobilityChangeAcknowledge ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container	{{MobilityChangeAcknowledge-IEs}},
	...
}

MobilityChangeAcknowledge-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-NG-RANnode1CellID					CRITICALITY reject	TYPE GlobalNG-RANCell-ID							PRESENCE mandatory}|
	{ ID id-NG-RANnode2CellID					CRITICALITY reject	TYPE GlobalNG-RANCell-ID							PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics				CRITICALITY ignore	TYPE CriticalityDiagnostics							PRESENCE optional},
	...
}


-- **************************************************************
--
-- MOBILITY CHANGE FAILURE
--
-- **************************************************************

MobilityChangeFailure ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container	{{MobilityChangeFailure-IEs}},
	...
}

MobilityChangeFailure-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-NG-RANnode1CellID						CRITICALITY reject	TYPE GlobalNG-RANCell-ID									PRESENCE mandatory}|
	{ ID id-NG-RANnode2CellID						CRITICALITY reject	TYPE GlobalNG-RANCell-ID									PRESENCE mandatory}|
	{ ID id-Cause									CRITICALITY ignore	TYPE Cause													PRESENCE mandatory}|
	{ ID id-MobilityParametersModificationRange		CRITICALITY ignore	TYPE MobilityParametersModificationRange				PRESENCE optional}|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore	TYPE CriticalityDiagnostics								PRESENCE optional}|
	{ ID id-NG-RANnode2SSBOffsetsModificationRange	CRITICALITY ignore	TYPE NG-RANnode2SSBOffsetsModificationRange			PRESENCE optional},
	...
}


-- **************************************************************
--
-- ACCESS AND MOBILITY INDICATION
--
-- **************************************************************

AccessAndMobilityIndication ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ AccessAndMobilityIndication-IEs}},
	...
}
AccessAndMobilityIndication-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-RAReport								CRITICALITY ignore	TYPE RAReport												PRESENCE optional}|
	{ ID id-SuccessfulHOReportInformation				CRITICALITY ignore	TYPE SuccessfulHOReportInformation					PRESENCE optional}|
	{ ID id-SuccessfulPSCellChangeReportInformation		CRITICALITY ignore	TYPE SuccessfulPSCellChangeReportInformation		PRESENCE optional}|
	{ ID id-DLLBTFailureInformationList					CRITICALITY ignore	TYPE DLLBTFailureInformationList						PRESENCE optional},
	...
}

-- **************************************************************
--
-- CELL TRAFFIC TRACE
--
-- **************************************************************

CellTrafficTrace ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container		{ {CellTrafficTraceIEs} },
	...
}

CellTrafficTraceIEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID			CRITICALITY reject	TYPE NG-RANnodeUEXnAPID				PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID			CRITICALITY reject	TYPE NG-RANnodeUEXnAPID				PRESENCE mandatory}|
	{ ID id-NG-RANTraceID					CRITICALITY ignore	TYPE NG-RANTraceID					PRESENCE mandatory}|
	{ ID id-TraceCollectionEntityIPAddress	CRITICALITY ignore	TYPE TransportLayerAddress			PRESENCE mandatory}|
	{ ID id-PrivacyIndicator				CRITICALITY ignore	TYPE PrivacyIndicator				PRESENCE optional }|
	{ ID id-TraceCollectionEntityURI		CRITICALITY ignore	TYPE URIaddress						PRESENCE optional },
	...
}

-- **************************************************************
--
-- RAN MULTICAST GROUP PAGING
--
-- **************************************************************

RANMulticastGroupPaging ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container	{{RANMulticastGroupPaging-IEs}},
	...
}

RANMulticastGroupPaging-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-MBS-Session-ID							CRITICALITY reject	TYPE MBS-Session-ID									PRESENCE mandatory}|
	{ ID id-UEIdentityIndexList-MBSGroupPaging		CRITICALITY reject	TYPE UEIdentityIndexList-MBSGroupPaging		PRESENCE mandatory}|
	{ ID id-MulticastRANPagingArea					CRITICALITY reject	TYPE RANPagingArea									PRESENCE mandatory},
	...
}

-- **************************************************************
--
-- SCG FAILURE INFORMATION REPORT
--
-- **************************************************************

ScgFailureInformationReport ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ ScgFailureInformationReport-IEs}},
	...
}

ScgFailureInformationReport-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-SourcePSCellCGI							CRITICALITY ignore		TYPE GlobalNG-RANCell-ID							PRESENCE optional }|
	{ ID id-FailedPSCellCGI							CRITICALITY ignore		TYPE GlobalNG-RANCell-ID							PRESENCE optional }|
	{ ID id-SCGFailureReportContainer				CRITICALITY ignore		TYPE SCGFailureReportContainer					PRESENCE mandatory}|
	{ ID id-SNMobilityInformation				CRITICALITY ignore		TYPE SNMobilityInformation						PRESENCE optional }|
	{ ID id-CPACConfiguration					CRITICALITY ignore		TYPE CPACConfiguration							PRESENCE optional },
	...
}

-- **************************************************************
--
-- SCG FAILURE TRANSFER
--
-- **************************************************************

ScgFailureTransfer ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ ScgFailureTransfer-IEs}},
	...
}
ScgFailureTransfer-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory},
	...
}


-- **************************************************************
--
-- F1-C TRAFFIC TRANSFER

--
-- **************************************************************

F1CTrafficTransfer ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container		{{ F1CTrafficTransfer-IEs}},
	...
}

F1CTrafficTransfer-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID						CRITICALITY reject	TYPE NG-RANnodeUEXnAPID			PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID						CRITICALITY reject	TYPE NG-RANnodeUEXnAPID			PRESENCE mandatory}|
	{ ID id-F1CTrafficContainer							CRITICALITY reject	TYPE F1CTrafficContainer			PRESENCE mandatory},
	...
}


-- **************************************************************
--
-- IAB TRANSPORT MIGRATION MANAGEMENT REQUEST
--
-- **************************************************************

IABTransportMigrationManagementRequest ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ IABTransportMigrationManagementRequest-IEs}},
	...
}

IABTransportMigrationManagementRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-F1-Terminating-IAB-DonorUEXnAPID		CRITICALITY reject		TYPE NG-RANnodeUEXnAPID						PRESENCE mandatory}|
	{ ID id-nonF1-Terminating-IAB-DonorUEXnAPID		CRITICALITY reject		TYPE NG-RANnodeUEXnAPID						PRESENCE mandatory}|
	{ ID id-TrafficToBeAddedList					CRITICALITY reject		TYPE TrafficToBeAddedList					PRESENCE optional }|
	{ ID id-TrafficToBeModifiedList					CRITICALITY reject		TYPE TrafficToBeModifiedList				PRESENCE optional }|
	{ ID id-TrafficToBeReleaseInformation			CRITICALITY reject		TYPE TrafficToBeReleaseInformation		PRESENCE optional }|
	{ ID id-IAB-TNL-Address-Request					CRITICALITY reject		TYPE IAB-TNL-Address-Request				PRESENCE optional }|
	{ ID id-IABTNLAddressException					CRITICALITY reject		TYPE IABTNLAddressException					PRESENCE optional }|
	{ ID id-MIAB-MT-BAP-Address						CRITICALITY reject		TYPE BAPAddress									PRESENCE optional },
	...
}

TrafficToBeAddedList ::= SEQUENCE (SIZE(1..maxnoofTrafficIndexEntries)) OF TrafficToBeAdded-Item

TrafficToBeAdded-Item ::= SEQUENCE {
	trafficIndex			TrafficIndex,
	trafficProfile			TrafficProfile,
	f1-TerminatingTopologyBHInformation		F1-TerminatingTopologyBHInformation		OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { {TrafficToBeAdded-Item-ExtIEs} }	OPTIONAL,
	...
}

TrafficToBeAdded-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

TrafficToBeModifiedList ::= SEQUENCE (SIZE(1..maxnoofTrafficIndexEntries)) OF TrafficToBeModified-Item

TrafficToBeModified-Item ::= SEQUENCE {
	trafficIndex			TrafficIndex,
	trafficProfile			TrafficProfile		OPTIONAL,
	f1-TerminatingTopologyBHInformation		F1-TerminatingTopologyBHInformation		OPTIONAL,
	iE-Extension			ProtocolExtensionContainer { {TrafficToBeModified-Item-ExtIEs} }	OPTIONAL,
	...
}

TrafficToBeModified-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

-- **************************************************************
--
-- IAB TRANSPORT MIGRATION MANAGEMENT RESPONSE
--
-- **************************************************************

IABTransportMigrationManagementResponse ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ IABTransportMigrationManagementResponse-IEs}},
	...
}

IABTransportMigrationManagementResponse-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-F1-Terminating-IAB-DonorUEXnAPID		CRITICALITY reject		TYPE NG-RANnodeUEXnAPID						PRESENCE mandatory}|
	{ ID id-nonF1-Terminating-IAB-DonorUEXnAPID		CRITICALITY reject		TYPE NG-RANnodeUEXnAPID						PRESENCE mandatory}|
	{ ID id-TrafficAddedList						CRITICALITY reject		TYPE TrafficAddedList							PRESENCE optional }|
	{ ID id-TrafficModifiedList						CRITICALITY reject		TYPE TrafficModifiedList						PRESENCE optional }|
	{ ID id-TrafficNotAddedList						CRITICALITY reject		TYPE TrafficNotAddedList						PRESENCE optional }|
	{ ID id-TrafficNotModifiedList					CRITICALITY reject		TYPE TrafficNotModifiedList					PRESENCE optional }|
	{ ID id-IAB-TNL-Address-Response				CRITICALITY reject		TYPE IAB-TNL-Address-Response				PRESENCE optional }|
	{ ID id-TrafficReleasedList						CRITICALITY reject		TYPE TrafficReleasedList						PRESENCE optional },
	...
}

TrafficAddedList ::= SEQUENCE (SIZE(1..maxnoofTrafficIndexEntries)) OF TrafficAdded-Item

TrafficAdded-Item ::= SEQUENCE {
	trafficIndex			TrafficIndex,
	non-F1-TerminatingTopologyBHInformation		Non-F1-TerminatingTopologyBHInformation,
	iE-Extensions			ProtocolExtensionContainer { {TrafficAdded-Item-ExtIEs} }	OPTIONAL,
	...
}

TrafficAdded-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

TrafficModifiedList ::= SEQUENCE (SIZE(1..maxnoofTrafficIndexEntries)) OF TrafficModified-Item

TrafficModified-Item ::= SEQUENCE {
	trafficIndex			TrafficIndex,
	non-F1-TerminatingTopologyBHInformation		Non-F1-TerminatingTopologyBHInformation,
	iE-Extensions			ProtocolExtensionContainer { {TrafficModified-Item-ExtIEs} }	OPTIONAL,
	...
}

TrafficModified-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

TrafficNotAddedList ::= SEQUENCE (SIZE(1..maxnoofTrafficIndexEntries)) OF TrafficNotAdded-Item

TrafficNotAdded-Item ::= SEQUENCE {
	trafficIndex			TrafficIndex,
	casue					Cause			OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { {TrafficNotAdded-Item-ExtIEs} }	OPTIONAL,
	...
}

TrafficNotAdded-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

TrafficNotModifiedList ::= SEQUENCE (SIZE(1..maxnoofTrafficIndexEntries)) OF TrafficNotModified-Item

TrafficNotModified-Item ::= SEQUENCE {
	trafficIndex			TrafficIndex,
	cause					Cause		OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { {TrafficNotModified-Item-ExtIEs} }	OPTIONAL,
	...
}

TrafficNotModified-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


TrafficReleasedList ::= SEQUENCE (SIZE(1..maxnoofTrafficIndexEntries)) OF TrafficReleased-Item

TrafficReleased-Item ::= SEQUENCE {
	trafficIndex			TrafficIndex,
	bHInfoList				BHInfoList		OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { { TrafficReleased-Item-ExtIEs} }	OPTIONAL,
	...
}

TrafficReleased-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- IAB TRANSPORT MIGRATION MANAGEMENT REJECT
--
-- **************************************************************

IABTransportMigrationManagementReject ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ IABTransportMigrationManagementReject-IEs}},
	...
}

IABTransportMigrationManagementReject-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-F1-Terminating-IAB-DonorUEXnAPID		CRITICALITY reject		TYPE NG-RANnodeUEXnAPID						PRESENCE mandatory}|
	{ ID id-nonF1-Terminating-IAB-DonorUEXnAPID		CRITICALITY reject		TYPE NG-RANnodeUEXnAPID						PRESENCE mandatory}|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause										PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics					PRESENCE optional },
	...
}


-- **************************************************************
--
-- IAB TRANSPORT MIGRATION MODIFICATION REQUEST
--
-- **************************************************************

IABTransportMigrationModificationRequest ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ IABTransportMigrationModificationRequest-IEs}},
	...
}

IABTransportMigrationModificationRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-F1-Terminating-IAB-DonorUEXnAPID			CRITICALITY reject	TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-nonF1-Terminating-IAB-DonorUEXnAPID			CRITICALITY reject	TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-TrafficRequiredToBeModifiedList				CRITICALITY reject	TYPE TrafficRequiredToBeModifiedList			PRESENCE optional }|
	{ ID id-TrafficToBeReleaseInformation				CRITICALITY reject	TYPE TrafficToBeReleaseInformation			PRESENCE optional }|
	{ ID id-IABTNLAddressToBeAdded						CRITICALITY reject	TYPE IAB-TNL-Address-Response					PRESENCE optional }|
	{ ID id-IABTNLAddressToBeReleasedList				CRITICALITY reject	TYPE IABTNLAddressToBeReleasedList			PRESENCE optional }|
	{ ID id-IABAuthorizationStatus						CRITICALITY ignore	TYPE IABAuthorizationStatus						PRESENCE optional }|
	{ ID id-MobileIAB-AuthorizationStatus				CRITICALITY ignore	TYPE MobileIAB-AuthorizationStatus			PRESENCE optional },
	...
}

TrafficRequiredToBeModifiedList ::= SEQUENCE (SIZE(1..maxnoofTrafficIndexEntries)) OF TrafficRequiredToBeModified-Item

TrafficRequiredToBeModified-Item ::= SEQUENCE {
	trafficIndex			TrafficIndex,
	non-f1-TerminatingTopologyBHInformation		Non-F1-TerminatingTopologyBHInformation,
	iE-Extensions			ProtocolExtensionContainer{ { TrafficRequiredToBeModified-Item-ExtIEs} }		OPTIONAL,
	...
}

TrafficRequiredToBeModified-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

IABTNLAddressToBeReleasedList ::= SEQUENCE (SIZE(1..maxnoofTLAsIAB)) OF IABTNLAddressToBeReleased-Item

IABTNLAddressToBeReleased-Item ::= SEQUENCE {
	iabTNLAddress			IABTNLAddress,
	iE-Extensions			ProtocolExtensionContainer{ { IABTNLAddressToBeReleased-Item-ExtIEs} }	OPTIONAL,
	...
}

IABTNLAddressToBeReleased-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}



-- **************************************************************
--
-- IAB TRANSPORT MIGRATION MODIFICATION RESPONSE
--
-- **************************************************************

IABTransportMigrationModificationResponse ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ IABTransportMigrationModificationResponse-IEs}},
	...
}

IABTransportMigrationModificationResponse-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-F1-Terminating-IAB-DonorUEXnAPID			CRITICALITY reject	TYPE NG-RANnodeUEXnAPID						PRESENCE mandatory}|
	{ ID id-nonF1-Terminating-IAB-DonorUEXnAPID			CRITICALITY reject	TYPE NG-RANnodeUEXnAPID						PRESENCE mandatory}|
	{ ID id-TrafficRequiredModifiedList					CRITICALITY reject	TYPE TrafficRequiredModifiedList			PRESENCE optional }|
	{ ID id-TrafficReleasedList							CRITICALITY reject	TYPE TrafficReleasedList						PRESENCE optional },
	...
}



TrafficRequiredModifiedList ::= SEQUENCE (SIZE(1..maxnoofTrafficIndexEntries)) OF TrafficRequiredModified-Item

TrafficRequiredModified-Item ::= SEQUENCE {
	trafficIndex			TrafficIndex,
	iE-Extensions			ProtocolExtensionContainer { { TrafficRequiredModified-Item-ExtIEs} }	OPTIONAL,
	...
}

TrafficRequiredModified-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- IAB RESOURCE COORDINATION REQUEST
--
-- **************************************************************

IABResourceCoordinationRequest ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ IABResourceCoordinationRequest-IEs}},
	...
}

IABResourceCoordinationRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-F1-Terminating-IAB-DonorUEXnAPID	CRITICALITY reject		TYPE NG-RANnodeUEXnAPID					PRESENCE mandatory}|
	{ ID id-nonF1-Terminating-IAB-DonorUEXnAPID	CRITICALITY reject		TYPE NG-RANnodeUEXnAPID					PRESENCE mandatory}|
	{ ID id-BoundaryNodeCellsList				CRITICALITY reject		TYPE BoundaryNodeCellsList			PRESENCE optional }|
	{ ID id-ParentNodeCellsList					CRITICALITY reject		TYPE ParentNodeCellsList				PRESENCE optional },
	...
}


BoundaryNodeCellsList ::= SEQUENCE (SIZE(1..maxnoofServedCellsIAB)) OF BoundaryNodeCellsList-Item

BoundaryNodeCellsList-Item ::= SEQUENCE {
	boundaryNodeCellInformation			IABCellInformation,
	iE-Extensions						ProtocolExtensionContainer { {BoundaryNodeCellsList-Item-ExtIEs} }		OPTIONAL,
	...
}

BoundaryNodeCellsList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ParentNodeCellsList ::= SEQUENCE (SIZE(1..maxnoofServingCells)) OF ParentNodeCellsList-Item

ParentNodeCellsList-Item ::= SEQUENCE {
	parentNodeCellInformation			IABCellInformation,
	iE-Extensions						ProtocolExtensionContainer { {ParentNodeCellsList-Item-ExtIEs} }	OPTIONAL,
	...
}

ParentNodeCellsList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- IAB RESOURCE COORDINATION RESPONSE
--
-- **************************************************************

IABResourceCoordinationResponse ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ IABResourceCoordinationResponse-IEs}},
	...
}

IABResourceCoordinationResponse-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-F1-Terminating-IAB-DonorUEXnAPID		CRITICALITY reject		TYPE NG-RANnodeUEXnAPID					PRESENCE mandatory}|
	{ ID id-nonF1-Terminating-IAB-DonorUEXnAPID		CRITICALITY reject		TYPE NG-RANnodeUEXnAPID					PRESENCE mandatory}|
	{ ID id-BoundaryNodeCellsList					CRITICALITY reject		TYPE BoundaryNodeCellsList				PRESENCE optional }|
	{ ID id-ParentNodeCellsList						CRITICALITY reject		TYPE ParentNodeCellsList					PRESENCE optional },
	...
}

-- **************************************************************
--
-- CONDITIONAL PSCELL CHANGE CANCEL
--
-- **************************************************************

CPCCancel ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ CPCCancel-IEs}},
	...
}
CPCCancel-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-M-NG-RANnodeUEXnAPID				CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-S-NG-RANnodeUEXnAPID				CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-Cause								CRITICALITY ignore		TYPE Cause										PRESENCE optional }|
	{ ID id-target-S-NG-RANnodeID				CRITICALITY reject		TYPE GlobalNG-RANNode-ID						PRESENCE mandatory},
	...
}

-- **************************************************************
--
-- PARTIAL UE CONTEXT TRANSFER
--
-- **************************************************************
PartialUEContextTransfer ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ PartialUEContextTransfer-IEs}},
	...
}

PartialUEContextTransfer-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-newNG-RANnodeUEXnAPID					CRITICALITY reject		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-oldNG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-SDTPartialUEContextInfo					CRITICALITY ignore		TYPE SDTPartialUEContextInfo					PRESENCE mandatory}|
	{ ID id-PosPartialUEContextInfo					CRITICALITY ignore		TYPE PosPartialUEContextInfo					PRESENCE optional },
	...
}

-- **************************************************************
--
-- PARTIAL UE CONTEXT TRANSFER ACKNOWLEDGE
--
-- **************************************************************
PartialUEContextTransferAcknowledge ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ PartialUEContextTransferAcknowledge-IEs}},
	...
}

PartialUEContextTransferAcknowledge-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-newNG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-oldNG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-SDTDataForwardingDRBList				CRITICALITY ignore		TYPE SDTDataForwardingDRBList					PRESENCE optional }|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics						PRESENCE optional }|
	{ ID id-SRSConfiguration						CRITICALITY ignore		TYPE SRSConfiguration								PRESENCE optional },
	...
}

-- **************************************************************
--
-- PARTIAL UE CONTEXT TRANSFER FAILURE
--
-- **************************************************************

PartialUEContextTransferFailure::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ PartialUEContextTransferFailure-IEs}},
	...
}

PartialUEContextTransferFailure-IEs XNAP-PROTOCOL-IES ::= {	
	{ ID id-newNG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-oldNG-RANnodeUEXnAPID					CRITICALITY ignore		TYPE NG-RANnodeUEXnAPID							PRESENCE mandatory}|
	{ ID id-Cause									CRITICALITY ignore		TYPE Cause											PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore		TYPE CriticalityDiagnostics						PRESENCE optional },
	...
}


-- **************************************************************
--
-- RACH INDICATION
--
-- **************************************************************

RachIndication ::= SEQUENCE {
	protocolIEs			ProtocolIE-Container	{{ RachIndication-IEs}},
	...
}

RachIndication-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-RaReportIndicationList					CRITICALITY reject		TYPE RaReportIndicationList						PRESENCE mandatory },
	...
}

-- **************************************************************
--
-- DATA COLLECTION REQUEST
--
-- **************************************************************

DataCollectionRequest ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container	{{DataCollectionRequest-IEs}},
	...
}

DataCollectionRequest-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-NGRAN-Node1-Measurement-ID					CRITICALITY reject	TYPE Measurement-ID										PRESENCE mandatory}|
	{ ID id-NGRAN-Node2-Measurement-ID					CRITICALITY ignore	TYPE Measurement-ID										PRESENCE conditional}|
-- This IE shall be present if the Registration Request for Data Collection IE is set to the value "stop".
	{ ID id-RegistrationRequestForDataCollection		CRITICALITY reject	TYPE RegistrationRequestForDataCollection		PRESENCE mandatory}|
	{ ID id-ReportCharacteristicsForDataCollection		CRITICALITY reject	TYPE ReportCharacteristicsForDataCollection	PRESENCE conditional}|
-- This IE shall be present if the Registration Request for Data Collection IE is set to the value "start".
	{ ID id-CellToReportForDataCollection-List			CRITICALITY ignore	TYPE CellToReportForDataCollection-List			PRESENCE optional}|
	{ ID id-ReportingPeriodicityForDataCollection		CRITICALITY ignore	TYPE ReportingPeriodicityForDataCollection		PRESENCE optional}|
	{ ID id-RequestedPredictionTime						CRITICALITY ignore	TYPE RequestedPredictionTime						PRESENCE optional}|
	{ ID id-UETrajectoryCollectionConfiguration			CRITICALITY ignore	TYPE UETrajectoryCollectionConfiguration		PRESENCE optional}|
	{ ID id-UEPerformanceCollectionConfiguration		CRITICALITY ignore	TYPE UEPerformanceCollectionConfiguration		PRESENCE optional},
	...
}


-- **************************************************************
--
-- DATA COLLECTION RESPONSE
--
-- **************************************************************

DataCollectionResponse ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container	{{DataCollectionResponse-IEs}},
	...
}

DataCollectionResponse-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-NGRAN-Node1-Measurement-ID					CRITICALITY reject	TYPE Measurement-ID										PRESENCE mandatory}|
	{ ID id-NGRAN-Node2-Measurement-ID					CRITICALITY reject	TYPE Measurement-ID										PRESENCE mandatory}|
	{ ID id-NodeMeasurementInitiationResult-List		CRITICALITY reject	TYPE NodeMeasurementInitiationResult-List		PRESENCE optional}|
	{ ID id-CellMeasurementInitiationResult-List		CRITICALITY reject	TYPE CellMeasurementInitiationResult-List		PRESENCE optional}|
	{ ID id-CriticalityDiagnostics						CRITICALITY ignore	TYPE CriticalityDiagnostics							PRESENCE optional},
	...
}


-- **************************************************************
--
-- DATA COLLECTION FAILURE
--
-- **************************************************************

DataCollectionFailure ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container	{{DataCollectionFailure-IEs}},
	...
}

DataCollectionFailure-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-NGRAN-Node1-Measurement-ID				CRITICALITY reject	TYPE Measurement-ID									PRESENCE mandatory}|
	{ ID id-NGRAN-Node2-Measurement-ID				CRITICALITY reject	TYPE Measurement-ID									PRESENCE mandatory}|
	{ ID id-Cause									CRITICALITY ignore	TYPE Cause											PRESENCE mandatory}|
	{ ID id-CriticalityDiagnostics					CRITICALITY ignore	TYPE CriticalityDiagnostics						PRESENCE optional},
	...
}


-- **************************************************************
--
-- DATA COLLECTION UPDATE
--
-- **************************************************************

DataCollectionUpdate ::= SEQUENCE {
	protocolIEs		ProtocolIE-Container	{{DataCollectionUpdate-IEs}},
	...
}

DataCollectionUpdate-IEs XNAP-PROTOCOL-IES ::= {
	{ ID id-NGRAN-Node1-Measurement-ID					CRITICALITY reject	TYPE Measurement-ID												PRESENCE mandatory}|
	{ ID id-NGRAN-Node2-Measurement-ID					CRITICALITY reject	TYPE Measurement-ID												PRESENCE mandatory}|
	{ ID id-CellMeasurementResultForDataCollection-List	CRITICALITY ignore	TYPE CellMeasurementResultForDataCollection-List		PRESENCE optional }|
	{ ID id-UEAssociatedInfoResult-List					CRITICALITY ignore	TYPE UEAssociatedInfoResult-List							PRESENCE optional }|
	{ ID id-NodeAssociatedInfoResult					CRITICALITY ignore	TYPE NodeAssociatedInfoResult								PRESENCE optional },
	...
}


END
-- ASN1STOP

9.3.5	Information Element definitions
-- ASN1START
-- **************************************************************
--
-- Information Element Definitions
--
-- **************************************************************

XnAP-IEs {
itu-t (0) identified-organization (4) etsi (0) mobileDomain (0)
ngran-access (22) modules (3) xnap (2) version1 (1) xnap-IEs (2) }

DEFINITIONS AUTOMATIC TAGS ::=

BEGIN

IMPORTS


	id-CNTypeRestrictionsForEquivalent,
	id-CNTypeRestrictionsForServing,
	id-Additional-UL-NG-U-TNLatUPF-List,
	id-ConfiguredTACIndication,
	id-AlternativeQoSParaSetList,
	id-CurrentQoSParaSetIndex,
	id-DefaultDRB-Allowed,
	id-DLCarrierList,
	id-EndpointIPAddressAndPort,
	id-ExtendedReportIntervalMDT,
	id-ExtendedTAISliceSupportList,
	id-FiveGCMobilityRestrictionListContainer,
	id-SecondarydataForwardingInfoFromTarget-List,
	id-LastE-UTRANPLMNIdentity,
	id-LTEA2XUEPC5AggregateMaximumBitRate,
	id-IntendedTDD-DL-ULConfiguration-NR,
	id-MaxIPrate-DL,
	id-SecurityResult,
	id-OldQoSFlowMap-ULendmarkerexpected,
	id-PDUSessionCommonNetworkInstance,
	id-PDUSession-PairID,
	id-BPLMN-ID-Info-EUTRA,
	id-BPLMN-ID-Info-NR,
	id-DRBsNotAdmittedSetupModifyList,
	id-Secondary-MN-Xn-U-TNLInfoatM,
	id-ULForwardingProposal,
	id-DRB-IDs-takenintouse,
	id-SplitSessionIndicator,
	id-NonGBRResources-Offered,
	id-MDT-Configuration,
	id-TraceCollectionEntityURI,
	id-NPN-Broadcast-Information,
	id-NPNPagingAssistanceInformation,
	id-NPNMobilityInformation,
	id-NPN-Support,
	id-LTEUESidelinkAggregateMaximumBitRate,
	id-NRA2XUEPC5AggregateMaximumBitRate,
	id-NRUESidelinkAggregateMaximumBitRate,
	id-ExtendedRATRestrictionInformation,
	id-QoSMonitoringRequest,
	id-QoSMonitoringDisabled,
	id-QosMonitoringReportingFrequency,
	id-DAPSRequestInfo,
	id-OffsetOfNbiotChannelNumberToDL-EARFCN,
	id-OffsetOfNbiotChannelNumberToUL-EARFCN,
	id-NBIoT-UL-DL-AlignmentOffset,
	id-TDDULDLConfigurationCommonNR,
	id-CarrierList,
	id-ULCarrierList,
	id-FrequencyShift7p5khz,
	id-SSB-PositionsInBurst,
	id-NRCellPRACHConfig,
	id-Redundant-UL-NG-U-TNLatUPF,
	id-Redundant-DL-NG-U-TNLatNG-RAN,
	id-CNPacketDelayBudgetDownlink,
	id-CNPacketDelayBudgetUplink,
	id-ExtendedPacketDelayBudget,
	id-Additional-Redundant-UL-NG-U-TNLatUPF-List,
	id-RedundantCommonNetworkInstance,
	id-TSCTrafficCharacteristics,
	id-RedundantQoSFlowIndicator,
	id-Additional-PDCP-Duplication-TNL-List,
	id-RedundantPDUSessionInformation,
	id-UsedRSNInformation,
	id-RLCDuplicationInformation,
	id-CSI-RSTransmissionIndication,
	id-UERadioCapabilityID,
	id-secondary-SN-UL-PDCP-UP-TNLInfo,
	id-pdcpDuplicationConfiguration,
	id-duplicationActivation,
	id-NPRACHConfiguration,
	id-QoSFlowsMappedtoDRB-SetupResponse-MNterminated,
	id-DL-scheduling-PDCCH-CCE-usage,
	id-UL-scheduling-PDCCH-CCE-usage,
	id-SFN-Offset,
	id-QoS-Mapping-Information,
	id-AdditionLocationInformation,
	id-dataForwardingInfoFromTargetE-UTRANnode,
	id-Cause,
	id-SecurityIndication,
	id-RRCConnReestab-Indicator,
	id-SourceDLForwardingIPAddress,
	id-SourceNodeDLForwardingIPAddress,
	id-M4ReportAmount,
	id-M5ReportAmount,
	id-M6ReportAmount,
	id-M7ReportAmount,
	id-BeamMeasurementIndicationM1,
	id-Supported-MBS-FSA-ID-List,
	id-MBS-AssistanceInformation,
	id-MBS-SessionAssociatedInformation,
	id-MBS-SessionInformation-List,
	id-SliceRadioResourceStatus-List,
	id-CompositeAvailableCapacitySupplementaryUplink,
	id-SSBOffsets-List,
	id-NG-RANnode2SSBOffsetsModificationRange,
	id-NR-U-Channel-List,
	id-NR-U-ChannelInfo-List,
	id-MIMOPRBusageInformation,
	id-UEAssistantIdentifier,
	id-IAB-MT-Cell-List,
	id-NoPDUSessionIndication,
	id-permutation,
	id-UL-GNB-DU-Cell-Resource-Configuration,
	id-DL-GNB-DU-Cell-Resource-Configuration,
	id-tdd-GNB-DU-Cell-Resource-Configuration,
	id-Additional-Measurement-Timing-Configuration-List,
	id-SurvivalTime,
	id-Local-NG-RAN-Node-Identifier,
	id-Neighbour-NG-RAN-Node-List,
	id-FiveGProSeUEPC5AggregateMaximumBitRate,
	id-Redcap-Bcast-Information,
	id-UESliceMaximumBitRateList,
	id-PositioningInformation,
	id-ServedCellSpecificInfoReq-NR,
	id-TAINSAGSupportList,
	id-earlyMeasurement,
	id-BeamMeasurementsReportConfiguration,
	id-CoverageModificationCause,
	id-UERLFReportContainerLTEExtension,
	id-ExcessPacketDelayThresholdConfiguration,
	id-Full-and-Short-I-RNTI-Profile-List,
	id-QosFlowMappingIndication,
	id-EquivalentSNPNs,
	id-CHOTimeBasedInformation,
	id-ChannelOccupancyTimePercentageUL,
	id-EnergyDetectionThresholdUL,
	id-PSCellListContainer,
	id-RadioResourceStatusNR-U,
	id-FiveGProSeLayer2Multipath,
	id-FiveGProSeLayer2UEtoUERelay,
	id-FiveGProSeLayer2UEtoUERemote,
	id-ClockQualityReportingControlInfo,
	id-CapabilityForBATAdaptation,
	id-PNI-NPNBasedMDT,
	id-PNI-NPN-AreaScopeofMDT,
	id-SNPN-CellBasedMDT,
	id-SNPN-TAIBasedMDT,
	id-SNPN-BasedMDT,
	id-S-CPAC-Request,
	id-S-CPAC-Request-Info,
	id-S-CPAC-ReferenceConfigRequest,
	id-S-CPAC-InterSN-ExecutionNotify,
	id-S-CPAC-dataforwardinginfofromSource,
	id-CPACcandidatePSCells-wotherInfo-list,
	id-eRedcap-Bcast-Information,
	id-NRPagingLongeDRXInformationforRRCINACTIVE,
	id-MBSCommServiceType,
	id-AssistanceInformationQoE-Meas,
	id-QoERVQoEReportingPaths,
	id-DirectForwardingPathAvailability,
	id-CHO-CPAC-Info,
	id-CHO-Maxnoof-CondReconfig,
	id-PDUSetQoSParameters,
	id-N6JitterInformation,
	id-ECNMarkingorCongestionInformationReportingRequest,
	id-TAISliceUnavailableCellList,
	id-MobileIABCell,
	id-XR-Bcast-Information,
	id-MaximumDataBurstVolume,
	id-CPAC-Preparation-Type,
	id-MN-only-MDT-collection,
	id-BarringExemptionforEmerCallInfo,
	id-Transmission-Bandwidth-asymmetric,
	id-NRPPaPositioningInformation,
	id-UEAveragePacketLossUL,
	id-SemipersistentPositioningInformation,
	maxEARFCN,
	maxnoofAllowedAreas,
	maxnoofAMFRegions,
	maxnoofAoIs,
	maxnoofBPLMNs,
	maxnoofCAGs,
	maxnoofCAGsperPLMN,
	maxnoofCellsinAoI,
	maxnoofCellsinNG-RANnode,
	maxnoofCellsinRNA,
	maxnoofCellsinUEHistoryInfo,
	maxnoofCellsUEMovingTrajectory,
	maxnoofDRBs,
	maxnoofEPLMNs,
	maxnoofEPLMNsplus1,
	maxnoofEUTRABands,
	maxnoofEUTRABPLMNs,
	maxnoofForbiddenTACs,
	maxnoofMBSFNEUTRA,
	maxnoofMultiConnectivityMinusOne,
	maxnoofNeighbours,
	maxnoofNIDs,
	maxnoofNRCellBands,
	maxnoofPDUSessions,
	maxnoofPLMNs,
	maxnoofProtectedResourcePatterns,
	maxnoofQoSFlows,
	maxnoofQoSParaSets,
	maxnoofRANAreaCodes,
	maxnoofRANAreasinRNA,
	maxnoofSCellGroups,
	maxnoofSCellGroupsplus1,
	maxnoofSliceItems,
	maxnoofExtSliceItems,
	maxnoofSNPNIDs,
	maxnoofsupportedTACs,
	maxnoofsupportedPLMNs,
	maxnoofTAI,
	maxnoofTAIsinAoI,
	maxnoofTNLAssociations,
	maxnoofUEContexts,
	maxNRARFCN,
	maxNrOfErrors,
	maxnoofRANNodesinAoI,
	maxnooftimeperiods,
	maxnoofslots,
	maxnoofExtTLAs,
	maxnoofGTPTLAs,
	maxnoofCHOcells,
	maxnoofPC5QoSFlows,
	maxnoofSSBAreas,
	maxnoofNRSCSs,
	maxnoofPhysicalResourceBlocks,
	maxnoofRAReports,
	maxnoofAdditionalPDCPDuplicationTNL,
	maxnoofRLCDuplicationstate,
	maxnoofBluetoothName,
	maxnoofCellIDforMDT,
	maxnoofMDTPLMNs,
	maxnoofTAforMDT,
	maxnoofWLANName,
	maxnoofSensorName,
	maxnoofNeighPCIforMDT,
	maxnoofFreqforMDT,
	maxnoofNonAnchorCarrierFreqConfig,
	maxnoofDataForwardingTunneltoE-UTRAN,
	maxnoofUEIDIndicesforMBSPaging,
	maxnoofMBSFSAs,
	maxnoofMBSQoSFlows,
	maxnoofMRBs,
	maxnoofCellsforMBS,
	maxnoofMBSServiceAreaInformation,
	maxnoofTAIforMBS,
	maxnoofAssociatedMBSSessions,
	maxnoofMBSSessions,
	maxnoofSuccessfulHOReports,
	maxnoofPSCellsPerSN,
	maxnoofNR-UChannelIDs,
	maxnoofCellsinCHO,
	maxnoofCHOexecutioncond,
	maxnoofServingCells,
	maxnoofBHInfo,
	maxnoofTLAsIAB,
	maxnoofTrafficIndexEntries,
	maxnoofBAPControlPDURLCCHs,
	maxnoofServedCellsIAB,
	maxnoofDUFSlots,
	maxnoofSymbols,
	maxnoofHSNASlots,
	maxnoofRBsetsPerCell,
	maxnoofChildIABNodes,
	maxnoofIABSTCInfo,
	maxnoofPSCellCandidates,
	maxnoofTargetSNs,
	maxnoofUEAppLayerMeas,
	maxnoofSNSSAIforQMC,
	maxnoofCellIDforQMC,
	maxnoofPLMNforQMC,
	maxnoofTAforQMC,
	maxnoofMTCItems,
	maxnoofCSIRSconfigurations,
	maxnoofCSIRSneighbourCells,
	maxnoofCSIRSneighbourCellsInMTC,
	maxnoofNeighbour-NG-RAN-Nodes,
	maxnoofSRBs,
	maxnoofSMBR,
	maxnoofNSAGs,
	maxnoofRBsetsPerCell1,
	maxnoofTargetSNsMinusOne,
	maxnoofThresholdsForExcessPacketDelay,
	maxnoofESNPNs,
	maxnoofSuccessfulPSCellChangeReports,
	maxnoofUEsforRAReportIndications,
	maxnoofPSCellsinCPAC,
	maxnoofCPACexecutioncond,
	maxnoofLBTFailureInformation,
	maxnoofCellsTrajectoryPredict,
	maxnoofCellsTrajectory,
	maxFailedCellMeasObjects,
	maxFailedMeasPerNode,
	maxnoofUEReports,
	maxnoofCandidateRelayUEs,
	maxnoofCAGforMDT,
	maxnoofMDTSNPNs,
	maxnoofSecurityConfigurations,
	maxnoofRSPPQoSFlows


FROM XnAP-Constants

	Criticality,
	ProcedureCode,
	ProtocolIE-ID,
	TriggeringMessage
FROM XnAP-CommonDataTypes

	ProtocolExtensionContainer{},
	ProtocolIE-Single-Container{},
	
	XNAP-PROTOCOL-EXTENSION,
	XNAP-PROTOCOL-IES
FROM XnAP-Containers;


-- A

A2XPC5QoSParameters ::= SEQUENCE {
	a2XPC5QoSFlowList					A2XPC5QoSFlowList,
	aA2XPC5LinkAggregateBitRates			BitRate											OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { A2XPC5QoSParameters-ExtIEs} }		OPTIONAL,
	...
}

A2XPC5QoSParameters-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

A2XPC5QoSFlowList ::= SEQUENCE (SIZE(1..maxnoofPC5QoSFlows)) OF A2XPC5QoSFlowItem

A2XPC5QoSFlowItem ::= SEQUENCE {
	a2XpQI				FiveQI,
	a2Xpc5FlowBitRates	A2XPC5FlowBitRates													OPTIONAL,
	a2Xrange			Range															OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { A2XPC5QoSFlowItem-ExtIEs} }		OPTIONAL,
	...
}

A2XPC5QoSFlowItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

A2XPC5FlowBitRates ::= SEQUENCE {
	a2XguaranteedFlowBitRate		BitRate,
	a2XmaximumFlowBitRate			BitRate,
	iE-Extensions		ProtocolExtensionContainer { { A2XPC5FlowBitRates-ExtIEs} }		OPTIONAL,
	...
}

A2XPC5FlowBitRates-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

AdditionalListofPDUSessionResourceChangeConfirmInfo-SNterminated ::= SEQUENCE (SIZE(1..maxnoofTargetSNsMinusOne)) OF AdditionalListofPDUSessionResourceChangeConfirmInfo-SNterminated-Item

AdditionalListofPDUSessionResourceChangeConfirmInfo-SNterminated-Item ::= SEQUENCE {
	pDUSessionResourceChangeConfirmInfo-SNterminated				PDUSessionResourceChangeConfirmInfo-SNterminated,
	iE-Extensions		ProtocolExtensionContainer { { AdditionalListofPDUSessionResourceChangeConfirmInfo-SNterminated-Item-ExtIEs} } OPTIONAL,
	...
}

AdditionalListofPDUSessionResourceChangeConfirmInfo-SNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

AveragePacketDelay ::= SEQUENCE {
	uL-AveragePacketDelay		AveragePacketDelayValue,
	dL-AveragePacketDelay		AveragePacketDelayValue,
	iE-Extensions		ProtocolExtensionContainer { {AveragePacketDelay-ExtIEs} }	OPTIONAL,
	...
}

AveragePacketDelay-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

AveragePacketDelayValue ::= INTEGER (0..10000)


AdditionLocationInformation ::= ENUMERATED {
	includePSCell,
	...
}

Additional-PDCP-Duplication-TNL-List ::= SEQUENCE (SIZE(1..maxnoofAdditionalPDCPDuplicationTNL)) OF Additional-PDCP-Duplication-TNL-Item
Additional-PDCP-Duplication-TNL-Item ::= SEQUENCE {
	additional-PDCP-Duplication-UP-TNL-Information	UPTransportLayerInformation,
	iE-Extensions		ProtocolExtensionContainer { { Additional-PDCP-Duplication-TNL-ExtIEs} }	OPTIONAL,
	...
}
Additional-PDCP-Duplication-TNL-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

Additional-UL-NG-U-TNLatUPF-Item ::= SEQUENCE {
	additional-UL-NG-U-TNLatUPF				UPTransportLayerInformation,
	iE-Extensions		ProtocolExtensionContainer { { Additional-UL-NG-U-TNLatUPF-Item-ExtIEs} }	OPTIONAL,
	...
}

Additional-UL-NG-U-TNLatUPF-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
{ ID id-PDUSessionCommonNetworkInstance		CRITICALITY ignore	EXTENSION PDUSessionCommonNetworkInstance		PRESENCE optional},
	...
}

Additional-UL-NG-U-TNLatUPF-List ::= SEQUENCE (SIZE(1..maxnoofMultiConnectivityMinusOne)) OF Additional-UL-NG-U-TNLatUPF-Item

Additional-Measurement-Timing-Configuration-List ::= SEQUENCE (SIZE(1.. maxnoofMTCItems)) OF Additional-Measurement-Timing-Configuration-Item

Additional-Measurement-Timing-Configuration-Item ::= SEQUENCE {
	additionalMeasurementTimingConfigurationIndex 		INTEGER (0..16),
	csi-RS-MTC-Configuration-List						CSI-RS-MTC-Configuration-List,
	iE-Extensions						ProtocolExtensionContainer { { Additional-Measurement-Timing-Configuration-Item-ExtIEs} }	OPTIONAL,
	...
}

Additional-Measurement-Timing-Configuration-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ActivationIDforCellActivation	::= INTEGER (0..255)

Active-MBS-SessionInformation ::= SEQUENCE {
	mBS-QoSFlowsToAdd-List								MBS-QoSFlowsToAdd-List,
	mBS-ServiceArea										MBS-ServiceArea												OPTIONAL,
	mBS-MappingandDataForwardingRequestInfofromSource	MBS-MappingandDataForwardingRequestInfofromSource		OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { Active-MBS-SessionInformation-ExtIEs} }		OPTIONAL,
	...
}

Active-MBS-SessionInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

DataCollectionID ::= SEQUENCE {
	nGRAN-Node1-Measurement-ID					Measurement-ID,
	nGRAN-Node2-Measurement-ID					Measurement-ID,
	iE-Extensions							ProtocolExtensionContainer { { DataCollectionID-ExtIEs} } OPTIONAL,
	...
}

DataCollectionID-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}



AerialControllerUE ::= ENUMERATED {
	authorized,
	not-authorized,
	...
}

AerialUE ::= ENUMERATED {
	authorized,
	not-authorized,
	...
}

AerialUESubscriptionInformation ::= ENUMERATED {
	allowed,
	not-allowed,
	...
}



AllocationandRetentionPriority ::= SEQUENCE {
	priorityLevel					INTEGER (0..15,...),
	pre-emption-capability			ENUMERATED {shall-not-trigger-preemption, may-trigger-preemption, ...},
	pre-emption-vulnerability		ENUMERATED {not-preemptable, preemptable, ...},
	iE-Extensions					ProtocolExtensionContainer { {AllocationandRetentionPriority-ExtIEs} } OPTIONAL,
	...
}

AllocationandRetentionPriority-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ActivationSFN ::= INTEGER (0..1023)

AllowedCAG-ID-List-perPLMN ::= SEQUENCE (SIZE(1..maxnoofCAGsperPLMN)) OF CAG-Identifier

AllowedPNI-NPN-ID-List ::= SEQUENCE (SIZE(1..maxnoofEPLMNsplus1)) OF AllowedPNI-NPN-ID-Item

AllowedPNI-NPN-ID-Item ::= SEQUENCE {
	plmn-id								PLMN-Identity,
	pni-npn-restricted-information		PNI-NPN-Restricted-Information,
	allowed-CAG-id-list-per-plmn		AllowedCAG-ID-List-perPLMN,
	iE-Extensions						ProtocolExtensionContainer { {AllowedPNI-NPN-ID-Item-ExtIEs} } OPTIONAL,
	...
}

AllowedPNI-NPN-ID-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

AllTrafficIndication ::= ENUMERATED {true,...}


AlternativeQoSParaSetList ::= SEQUENCE (SIZE(1..maxnoofQoSParaSets)) OF AlternativeQoSParaSetItem

AlternativeQoSParaSetItem ::= SEQUENCE {
	alternativeQoSParaSetIndex			QoSParaSetIndex,
	guaranteedFlowBitRateDL				BitRate					OPTIONAL,
	guaranteedFlowBitRateUL				BitRate					OPTIONAL,
	packetDelayBudget					PacketDelayBudget		OPTIONAL,
	packetErrorRate						PacketErrorRate			OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {AlternativeQoSParaSetItem-ExtIEs} }	OPTIONAL,
	...
}

AlternativeQoSParaSetItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
		{ ID id-MaximumDataBurstVolume	CRITICALITY ignore	EXTENSION MaximumDataBurstVolume		PRESENCE optional },
...
}


AMF-Region-Information ::= SEQUENCE (SIZE (1..maxnoofAMFRegions)) OF GlobalAMF-Region-Information

GlobalAMF-Region-Information ::= SEQUENCE {
	plmn-ID				PLMN-Identity,
	amf-region-id		BIT STRING (SIZE (8)),
	iE-Extensions					ProtocolExtensionContainer { {GlobalAMF-Region-Information-ExtIEs} } OPTIONAL,
	...
}

GlobalAMF-Region-Information-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


AMF-UE-NGAP-ID ::= INTEGER (0..1099511627775)


AreaOfInterestInformation ::= SEQUENCE (SIZE(1..maxnoofAoIs)) OF AreaOfInterest-Item

AreaOfInterest-Item ::= SEQUENCE {
	listOfTAIsinAoI					ListOfTAIsinAoI												OPTIONAL,
	listOfCellsinAoI				ListOfCells													OPTIONAL,
	listOfRANNodesinAoI				ListOfRANNodesinAoI											OPTIONAL,
	requestReferenceID	RequestReferenceID,
	iE-Extensions					ProtocolExtensionContainer { {AreaOfInterest-Item-ExtIEs} } OPTIONAL,
	...
}

AreaOfInterest-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


AreaScopeOfMDT-NR ::= CHOICE {	
	cellBased					CellBasedMDT-NR,
	tABased						TABasedMDT,
	tAIBased					TAIBasedMDT,
	...,
	choice-extension		ProtocolIE-Single-Container { {AreaScopeOfMDT-NR-ExtIEs} }
}

AreaScopeOfMDT-NR-ExtIEs XNAP-PROTOCOL-IES ::= {
	{ ID id-PNI-NPNBasedMDT			CRITICALITY ignore	TYPE PNI-NPNBasedMDT			PRESENCE mandatory}|
	{ ID id-SNPN-CellBasedMDT		CRITICALITY ignore	TYPE SNPN-CellBasedMDT			PRESENCE mandatory}|
	{ ID id-SNPN-TAIBasedMDT		CRITICALITY ignore	TYPE SNPN-TAIBasedMDT			PRESENCE mandatory}|
	{ ID id-SNPN-BasedMDT			CRITICALITY ignore	TYPE SNPN-BasedMDT			PRESENCE mandatory},
	...
}
AreaScopeOfMDT-EUTRA ::= CHOICE {	
	cellBased					CellBasedMDT-EUTRA,
	tABased						TABasedMDT,
	tAIBased					TAIBasedMDT,
	...,
	choice-extension		ProtocolIE-Single-Container { {AreaScopeOfMDT-EUTRA-ExtIEs} }

}

AreaScopeOfMDT-EUTRA-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}



AreaScopeOfNeighCellsList ::= SEQUENCE (SIZE(1..maxnoofFreqforMDT)) OF AreaScopeOfNeighCellsItem
AreaScopeOfNeighCellsItem ::= SEQUENCE {
	nrFrequencyInfo				NRFrequencyInfo,
	pciListForMDT				PCIListForMDT		OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { AreaScopeOfNeighCellsItem-ExtIEs} }	OPTIONAL,
	...
}

AreaScopeOfNeighCellsItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

AreaScopeOfQMC ::= CHOICE {	
	cellBased					CellBasedQMC,
	tABased						TABasedQMC,
	tAIBased					TAIBasedQMC,
	pLMNAreaBased				PLMNAreaBasedQMC,
	choice-extension			ProtocolIE-Single-Container { {AreaScopeOfQMC-ExtIEs} }
}

AreaScopeOfQMC-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

AS-SecurityInformation ::= SEQUENCE {
	key-NG-RAN-Star					BIT STRING (SIZE(256)),
	ncc								INTEGER (0..7),
	iE-Extensions					ProtocolExtensionContainer { {AS-SecurityInformation-ExtIEs} } OPTIONAL,
	...
}

AS-SecurityInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


AssistanceDataForRANPaging ::= SEQUENCE {
	ran-paging-attempt-info			RANPagingAttemptInfo	OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {AssistanceDataForRANPaging-ExtIEs} } OPTIONAL,
	...
}

AssistanceDataForRANPaging-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-NPNPagingAssistanceInformation	CRITICALITY ignore	EXTENSION NPNPagingAssistanceInformation	PRESENCE optional },
	...
}

AssistanceInformationQoE-Meas ::= INTEGER (1..16, ...)

Associated-QoSFlowInfo-List ::= SEQUENCE (SIZE(1..maxnoofMBSQoSFlows)) OF Associated-QoSFlowInfo-Item

Associated-QoSFlowInfo-Item ::= SEQUENCE {
	mBS-QoSFlowIdentifier				QoSFlowIdentifier,
	associatedUnicastQoSFlowIdentifier	QoSFlowIdentifier,
	iE-Extensions						ProtocolExtensionContainer { { Associated-QoSFlowInfo-Item-ExtIEs} }	OPTIONAL,
	...
}

Associated-QoSFlowInfo-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


AvailableCapacity ::= INTEGER (1.. 100,...)


AvailableRRCConnectionCapacityValue ::= INTEGER (0..100)


AvailableRVQoEMetrics ::= SEQUENCE {
	applicationLayerBufferLevelList						ENUMERATED {true, ...} 	OPTIONAL,
	playoutDelayForMediaStartup		ENUMERATED {true, ...} 		OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {AvailableRVQoEMetrics-ExtIEs} } OPTIONAL,
	...
}

AvailableRVQoEMetrics-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


AveragingWindow ::= INTEGER (0..4095, ...)


-- B

BAPAddress ::= BIT STRING (SIZE(10))

BAPPathID ::= BIT STRING (SIZE(10))

BAPRoutingID ::= SEQUENCE {
	bAPAddress		BAPAddress,
	bAPPathID		BAPPathID,
	iE-Extensions	ProtocolExtensionContainer { {BAPRoutingID-ExtIEs} }	OPTIONAL,
	...
}

BAPRoutingID-ExtIEs	XNAP-PROTOCOL-EXTENSION ::= {
	...
}


BeamMeasurementIndicationM1 ::= ENUMERATED {true, ...}

BeamMeasurementsReportConfiguration ::= SEQUENCE {
	beamMeasurementsReportQuantity			BeamMeasurementsReportQuantity			OPTIONAL,
	maxNrofRS-IndexesToReport				MaxNrofRS-IndexesToReport		OPTIONAL,
	iE-Extensions							ProtocolExtensionContainer { { BeamMeasurementsReportConfiguration-ExtIEs} } OPTIONAL,
	...
}

BeamMeasurementsReportConfiguration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


BeamMeasurementsReportQuantity ::= SEQUENCE {
	rSRP						ENUMERATED {true, ..., false},
	rSRQ						ENUMERATED {true, ..., false},
	sINR						ENUMERATED {true, ..., false},
	iE-Extensions			ProtocolExtensionContainer { { BeamMeasurementsReportQuantity-ExtIEs} } OPTIONAL,
	...
}

BeamMeasurementsReportQuantity-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


BHInfoIndex ::= INTEGER (1.. maxnoofBHInfo)

BHInfoList ::= SEQUENCE (SIZE(1.. maxnoofBHInfo)) OF BHInfo-Item

BHInfo-Item ::= SEQUENCE {
	bHInfoIndex			BHInfoIndex,
	iE-Extensions			ProtocolExtensionContainer { { BHInfo-Item-ExtIEs} }	OPTIONAL,
	...
}

BHInfo-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


BHRLCChannelID ::= BIT STRING (SIZE(16))

BAPControlPDURLCCH-List ::= SEQUENCE (SIZE(1.. maxnoofBAPControlPDURLCCHs)) OF BAPControlPDURLCCH-Item


BAPControlPDURLCCH-Item ::= SEQUENCE {
	bHRLCCHID			BHRLCChannelID,
	nexthopBAPAddress	BAPAddress,
	iE-Extensions			ProtocolExtensionContainer { { BAPControlPDURLCCH-Item-ExtIEs} }	OPTIONAL,
	...
}

BAPControlPDURLCCH-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}
BarringExemptionforEmerCallInfo::= ENUMERATED {true,...}


BluetoothMeasurementConfiguration ::= SEQUENCE {
	bluetoothMeasConfig				BluetoothMeasConfig,
	bluetoothMeasConfigNameList		BluetoothMeasConfigNameList		OPTIONAL,
	bt-rssi							ENUMERATED {true, ...}			OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { BluetoothMeasurementConfiguration-ExtIEs } } OPTIONAL,
	...
}

BluetoothMeasurementConfiguration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

BluetoothMeasConfigNameList ::= SEQUENCE (SIZE(1..maxnoofBluetoothName)) OF BluetoothName

BluetoothMeasConfig::= ENUMERATED {setup,...}

BluetoothName ::= OCTET STRING (SIZE (1..248))


BPLMN-ID-Info-EUTRA ::= SEQUENCE (SIZE(1..maxnoofEUTRABPLMNs)) OF BPLMN-ID-Info-EUTRA-Item

BPLMN-ID-Info-EUTRA-Item ::= SEQUENCE {
	broadcastPLMNs					BroadcastEUTRAPLMNs,
	tac								TAC,
	e-utraCI						E-UTRA-Cell-Identity,
	ranac							RANAC OPTIONAL,
	iE-Extension					ProtocolExtensionContainer { {BPLMN-ID-Info-EUTRA-Item-ExtIEs} } OPTIONAL,
	...
}

BPLMN-ID-Info-EUTRA-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

BPLMN-ID-Info-NR ::= SEQUENCE (SIZE(1..maxnoofBPLMNs)) OF BPLMN-ID-Info-NR-Item

BPLMN-ID-Info-NR-Item ::= SEQUENCE {
	broadcastPLMNs					BroadcastPLMNs,
	tac								TAC,
	nr-CI							NR-Cell-Identity,
	ranac							RANAC OPTIONAL,
	iE-Extension					ProtocolExtensionContainer { {BPLMN-ID-Info-NR-Item-ExtIEs} } OPTIONAL,
	...
}

BPLMN-ID-Info-NR-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-ConfiguredTACIndication		CRITICALITY ignore	EXTENSION ConfiguredTACIndication		PRESENCE optional }|
	{ ID id-NPN-Broadcast-Information	CRITICALITY reject	EXTENSION NPN-Broadcast-Information		PRESENCE optional },
	...
}

BitRate	::= INTEGER (0..4000000000000,...)



BroadcastCAG-Identifier-List ::= SEQUENCE (SIZE(1..maxnoofCAGs)) OF BroadcastCAG-Identifier-Item

BroadcastCAG-Identifier-Item ::= SEQUENCE {
	cag-Identifier					CAG-Identifier,
	iE-Extension					ProtocolExtensionContainer { {BroadcastCAG-Identifier-Item-ExtIEs} } OPTIONAL,
	...
}

BroadcastCAG-Identifier-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


BroadcastNID-List ::= SEQUENCE (SIZE(1..maxnoofNIDs)) OF BroadcastNID-Item

BroadcastNID-Item ::= SEQUENCE {
	nid							NID,
	iE-Extension				ProtocolExtensionContainer { {BroadcastNID-Item-ExtIEs} } OPTIONAL,
	...
}

BroadcastNID-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

BroadcastPLMNs ::= SEQUENCE (SIZE(1..maxnoofBPLMNs)) OF PLMN-Identity

BroadcastEUTRAPLMNs ::= SEQUENCE (SIZE(1..maxnoofEUTRABPLMNs)) OF PLMN-Identity


BroadcastPLMNinTAISupport-Item ::= SEQUENCE {
	plmn-id							PLMN-Identity,
	tAISliceSupport-List			SliceSupport-List,
	iE-Extension					ProtocolExtensionContainer { {BroadcastPLMNinTAISupport-Item-ExtIEs} } OPTIONAL,
	...
}

BroadcastPLMNinTAISupport-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-NPN-Support						CRITICALITY reject	EXTENSION NPN-Support						PRESENCE optional}|
	{ ID id-ExtendedTAISliceSupportList		CRITICALITY reject	EXTENSION ExtendedSliceSupportList		PRESENCE optional}|
	{ ID id-TAINSAGSupportList				CRITICALITY ignore	EXTENSION TAINSAGSupportList				PRESENCE optional}|
	{ ID id-TAISliceUnavailableCellList		CRITICALITY ignore	EXTENSION TAISliceUnavailableCellList	PRESENCE optional},
	...
}


BroadcastPNI-NPN-ID-Information ::= SEQUENCE (SIZE(1..maxnoofBPLMNs)) OF BroadcastPNI-NPN-ID-Information-Item

BroadcastPNI-NPN-ID-Information-Item ::= SEQUENCE {
	plmn-id							PLMN-Identity,
	broadcastCAG-Identifier-List	BroadcastCAG-Identifier-List,
	iE-Extension					ProtocolExtensionContainer { {BroadcastPNI-NPN-ID-Information-Item-ExtIEs} } OPTIONAL,
	...
}


BroadcastPNI-NPN-ID-Information-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


BroadcastSNPNID-List ::= SEQUENCE (SIZE(1..maxnoofSNPNIDs)) OF BroadcastSNPNID


BroadcastSNPNID ::= SEQUENCE {
	plmn-id							PLMN-Identity,
	broadcastNID-List								BroadcastNID-List,
	iE-Extension					ProtocolExtensionContainer { {BroadcastSNPNID-ExtIEs} } OPTIONAL,
	...
}

BroadcastSNPNID-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- C


CAG-Identifier	::= BIT STRING (SIZE (32))

CandidateRelayUEInfoList ::= SEQUENCE (SIZE(1..maxnoofCandidateRelayUEs)) OF CandidateRelayUEInfoItem

CandidateRelayUEInfoItem ::= SEQUENCE {
	candidateRelayUEID		BIT STRING(SIZE(24)),
	iE-Extensions			ProtocolExtensionContainer { { CandidateRelayUEInfoItem-ExtIEs } }		OPTIONAL,
	...
}

CandidateRelayUEInfoItem-ExtIEs	XNAP-PROTOCOL-EXTENSION ::= {
	...
}


CapacityValue ::= INTEGER (0..100)



CapacityValueInfo ::= SEQUENCE {
	capacityValue				CapacityValue,
	ssbAreaCapacityValueList 	SSBAreaCapacityValue-List 	OPTIONAL,
	iE-Extension 				ProtocolExtensionContainer { {CapacityValueInfo-ExtIEs} } OPTIONAL,
	...
}

CapacityValueInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


Cause ::= CHOICE {
	radioNetwork		CauseRadioNetworkLayer,
	transport			CauseTransportLayer,
	protocol			CauseProtocol,
	misc				CauseMisc,
	choice-extension	ProtocolIE-Single-Container { {Cause-ExtIEs} }
}

Cause-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

CauseRadioNetworkLayer ::= ENUMERATED {
	cell-not-available,
	handover-desirable-for-radio-reasons,
	handover-target-not-allowed,
	invalid-AMF-Set-ID,
	no-radio-resources-available-in-target-cell,
	partial-handover,
	reduce-load-in-serving-cell,
	resource-optimisation-handover,
	time-critical-handover,
	tXnRELOCoverall-expiry,
	tXnRELOCprep-expiry,
	unknown-GUAMI-ID,
	unknown-local-NG-RAN-node-UE-XnAP-ID,
	inconsistent-remote-NG-RAN-node-UE-XnAP-ID,
	encryption-and-or-integrity-protection-algorithms-not-supported,
	not-used-causes-value-1,
	multiple-PDU-session-ID-instances,
	unknown-PDU-session-ID,
	unknown-QoS-Flow-ID,
	multiple-QoS-Flow-ID-instances,
	switch-off-ongoing,
	not-supported-5QI-value,
	tXnDCoverall-expiry,
	tXnDCprep-expiry,
	action-desirable-for-radio-reasons,
	reduce-load,
	resource-optimisation,
	time-critical-action,
	target-not-allowed,
	no-radio-resources-available,
	invalid-QoS-combination,
	encryption-algorithms-not-supported,
	procedure-cancelled,
	rRM-purpose,
	improve-user-bit-rate,
	user-inactivity,
	radio-connection-with-UE-lost,
	failure-in-the-radio-interface-procedure,
	bearer-option-not-supported,
	up-integrity-protection-not-possible,
	up-confidentiality-protection-not-possible,
	resources-not-available-for-the-slice-s,
	ue-max-IP-data-rate-reason,
	cP-integrity-protection-failure,
	uP-integrity-protection-failure,
	slice-not-supported-by-NG-RAN,
	mN-Mobility,
	sN-Mobility,
	count-reaches-max-value,
	unknown-old-NG-RAN-node-UE-XnAP-ID,
	pDCP-Overload,
	drb-id-not-available,
	unspecified,
	...,
	ue-context-id-not-known,
	non-relocation-of-context,
	cho-cpc-resources-tobechanged,
	rSN-not-available-for-the-UP,
	npn-access-denied,
	report-characteristics-empty,
	existing-measurement-ID,
	measurement-temporarily-not-available,
	measurement-not-supported-for-the-object,
	ue-power-saving,
	not-existing-NG-RAN-node2-Measurement-ID,
	insufficient-ue-capabilities,
	normal-release,
	value-out-of-allowed-range,
	scg-activation-deactivation-failure,
	scg-deactivation-failure-due-to-data-transmission,
	ssb-not-available,
	lTM-triggered,
	no-Backhaul-Resource,
	mIAB-node-not-authorized,
	iAB-not-authorized
}

CauseTransportLayer ::= ENUMERATED {
	transport-resource-unavailable,
	unspecified,
	...
}

CauseProtocol ::= ENUMERATED {
	transfer-syntax-error,
	abstract-syntax-error-reject,
	abstract-syntax-error-ignore-and-notify,
	message-not-compatible-with-receiver-state,
	semantic-error,
	abstract-syntax-error-falsely-constructed-message,
	unspecified,
	...
}

CauseMisc ::= ENUMERATED {
	control-processing-overload,
	hardware-failure,
	o-and-M-intervention,
	not-enough-user-plane-processing-resources,
	unspecified,
	...
}

CellAssistanceInfo-NR	::= CHOICE {
	limitedNR-List				SEQUENCE (SIZE(1..maxnoofCellsinNG-RANnode)) OF NR-CGI,
	full-List					ENUMERATED {all-served-cells-NR, ...},
	choice-extension			ProtocolIE-Single-Container { {CellAssistanceInfo-NR-ExtIEs} }
}

CellAssistanceInfo-NR-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

CellAndCapacityAssistanceInfo-NR	::= SEQUENCE {
	maximumCellListSize					MaximumCellListSize								OPTIONAL,
	cellAssistanceInfo-NR		CellAssistanceInfo-NR 				OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { { CellAndCapacityAssistanceInfo-NR-ExtIEs} }	OPTIONAL,
	...
}


CellAndCapacityAssistanceInfo-NR-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

CellAndCapacityAssistanceInfo-EUTRA	::= SEQUENCE {
	maximumCellListSize					MaximumCellListSize								OPTIONAL,
	cellAssistanceInfo-EUTRA			CellAssistanceInfo-EUTRA 				OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { { CellAndCapacityAssistanceInfo-EUTRA-ExtIEs} }	OPTIONAL,
	...
}


CellAndCapacityAssistanceInfo-EUTRA-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


CellAssistanceInfo-EUTRA	::= CHOICE {
	limitedEUTRA-List			SEQUENCE (SIZE(1..maxnoofCellsinNG-RANnode)) OF E-UTRA-CGI,
	full-List					ENUMERATED {all-served-cells-E-UTRA, ...},
	choice-extension			ProtocolIE-Single-Container { {CellAssistanceInfo-EUTRA-ExtIEs} }
}

CellAssistanceInfo-EUTRA-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

CellBasedMDT-NR::= SEQUENCE {
	cellIdListforMDT-NR	CellIdListforMDT-NR,
	iE-Extensions		ProtocolExtensionContainer { {CellBasedMDT-NR-ExtIEs} } OPTIONAL,
	...
}

CellBasedMDT-NR-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

CellIdListforMDT-NR ::= SEQUENCE (SIZE(1..maxnoofCellIDforMDT)) OF NR-CGI

CellBasedQMC::= SEQUENCE {
	cellIdListforQMC		CellIdListforQMC,
	iE-Extensions		ProtocolExtensionContainer { {CellBasedQMC-ExtIEs} } OPTIONAL,
	...
}

CellBasedQMC-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

CellIdListforQMC ::= SEQUENCE (SIZE(1..maxnoofCellIDforQMC)) OF GlobalNG-RANCell-ID


CellBasedMDT-EUTRA::= SEQUENCE {
	cellIdListforMDT-EUTRA	CellIdListforMDT-EUTRA,
	iE-Extensions		ProtocolExtensionContainer { {CellBasedMDT-EUTRA-ExtIEs} } OPTIONAL,
	...
}

CellBasedMDT-EUTRA-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}
CellIdListforMDT-EUTRA ::= SEQUENCE (SIZE(1..maxnoofCellIDforMDT)) OF E-UTRA-CGI


CellCapacityClassValue ::= INTEGER (1..100,...)

CellDeploymentStatusIndicator ::= ENUMERATED {pre-change-notification, ...}

CellGroupID ::= INTEGER (0..maxnoofSCellGroups)


CellMeasurementResult ::= SEQUENCE (SIZE(1..maxnoofCellsinNG-RANnode)) OF CellMeasurementResult-Item

CellMeasurementResult-Item	::= SEQUENCE {
	cell-ID								GlobalNG-RANCell-ID,
	radioResourceStatus					RadioResourceStatus				OPTIONAL,
	tNLCapacityIndicator				TNLCapacityIndicator			OPTIONAL,
	compositeAvailableCapacityGroup		CompositeAvailableCapacityGroup	OPTIONAL,
	sliceAvailableCapacity				SliceAvailableCapacity			OPTIONAL,
	numberofActiveUEs					NumberofActiveUEs				OPTIONAL,
	rRCConnections						RRCConnections					OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { { CellMeasurementResult-Item-ExtIEs} }	OPTIONAL,
	...
}


CellMeasurementResult-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-NR-U-Channel-List	CRITICALITY ignore	EXTENSION NR-U-Channel-List PRESENCE optional },
	...
}


CellReplacingInfo ::= SEQUENCE {
	replacingCells					ReplacingCells,
	iE-Extensions					ProtocolExtensionContainer { {CellReplacingInfo-ExtIEs}}	OPTIONAL,
	...
}

CellReplacingInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


CellToReport ::= SEQUENCE (SIZE(1..maxnoofCellsinNG-RANnode)) OF CellToReport-Item

CellToReport-Item	::= SEQUENCE {
	cell-ID									GlobalNG-RANCell-ID,
	sSBToReport-List						SSBToReport-List			OPTIONAL,
	sliceToReport-List						SliceToReport-List			OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { { CellToReport-Item-ExtIEs} }	OPTIONAL,
	...
}


CellToReport-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

CellToReportForDataCollection-List ::= SEQUENCE (SIZE(1..maxnoofCellsinNG-RANnode)) OF CellToReportForDataCollection-Item

CellToReportForDataCollection-Item	::= SEQUENCE {
	cell-ID								GlobalNG-RANCell-ID,
	iE-Extensions						ProtocolExtensionContainer { { CellToReportForDataCollection-Item-ExtIEs} }	OPTIONAL,
	...
}

CellToReportForDataCollection-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}
CellBasedUETrajectoryPrediction ::= SEQUENCE (SIZE(1..maxnoofCellsTrajectoryPredict)) OF PredictedUETrajectory-Item



CellMeasurementInitiationResult-List ::= SEQUENCE (SIZE(1..maxnoofCellsinNG-RANnode)) OF CellMeasurementInitiationResult-Item

CellMeasurementInitiationResult-Item ::= SEQUENCE {
	cellID											GlobalNG-RANCell-ID,
	cellMeasurementFailureCause-List				CellMeasurementFailureCause-List OPTIONAL,
	iE-Extensions									ProtocolExtensionContainer { { CellMeasurementInitiationResult-Item-ExtIEs} }	OPTIONAL,
	...
}
CellMeasurementInitiationResult-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


CellMeasurementResultForDataCollection-List ::= SEQUENCE (SIZE(1..maxnoofCellsinNG-RANnode)) OF CellInfoResultForDataCollection-Item

CellInfoResultForDataCollection-Item ::= SEQUENCE {
	cellID											GlobalNG-RANCell-ID,
	predictedRadioResourceStatus					RadioResourceStatus					OPTIONAL,
	predictedNumberofActiveUEs						NumberofActiveUEs					OPTIONAL,
	predictedRRCConnections							RRCConnections						OPTIONAL,
	iE-Extensions									ProtocolExtensionContainer { {CellInfoResultForDataCollection-Item-ExtIEs} }	OPTIONAL,
	...
}
CellInfoResultForDataCollection-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


Cell-Type-Choice ::= CHOICE {
	ng-ran-e-utra			E-UTRA-Cell-Identity,
	ng-ran-nr				NR-Cell-Identity,
	e-utran					E-UTRA-Cell-Identity,
	choice-extension		ProtocolIE-Single-Container { { Cell-Type-Choice-ExtIEs} }
}

Cell-Type-Choice-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

CellMeasurementFailureCause-List ::= SEQUENCE (SIZE(1..maxFailedCellMeasObjects)) OF CellMeasurementFailureCause-Item

CellMeasurementFailureCause-Item ::= SEQUENCE {
	cellmeasurementFailedReportCharacteristics		BIT STRING(SIZE(32)),
	cause										Cause,
	iE-Extensions								ProtocolExtensionContainer { { CellMeasurementFailureCause-Item-ExtIEs} } OPTIONAL,
	...
}

CellMeasurementFailureCause-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

CHOConfiguration ::= SEQUENCE {
	choCandidateCell-List				CHOCandidateCell-List,
	iE-Extensions						ProtocolExtensionContainer { { CHOConfiguration-ExtIEs} }	OPTIONAL,
	...
}

CHOConfiguration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


CHOCandidateCell-List ::= SEQUENCE (SIZE(1..maxnoofCellsinCHO)) OF CHOCandidateCell-Item

CHOCandidateCell-Item ::= SEQUENCE {
	choCandidateCellID					GlobalNG-RANCell-ID,
	choExecutionCondition-List			CHOExecutionCondition-List,
	iE-Extensions						ProtocolExtensionContainer { { CHOCandidateCell-Item-ExtIEs} }	OPTIONAL,
	...
}

CHOCandidateCell-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


CHOExecutionCondition-List ::= SEQUENCE (SIZE(1..maxnoofCHOexecutioncond)) OF CHOExecutionCondition-Item

CHOExecutionCondition-Item ::= SEQUENCE {	measObjectContainer					MeasObjectContainer,
	reportConfigContainer				ReportConfigContainer,
	iE-Extensions						ProtocolExtensionContainer { { CHOExecutionCondition-Item-ExtIEs} }	OPTIONAL,
	...
}

CHOExecutionCondition-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ClockQualityAcceptanceCriteria ::= SEQUENCE {
	synchronisationState			BIT STRING (SIZE(8, ...))						OPTIONAL,
	traceabletoUTC					ENUMERATED {true, ...}						OPTIONAL,
	traceabletoGNSS					ENUMERATED {true, ...}						OPTIONAL,
	clockFrequencyStability			BIT STRING (SIZE(16))						OPTIONAL,
	clockAccuracy					INTEGER (1..40000000, ...)					OPTIONAL,
	parentTimeSource				BIT STRING (SIZE(16, ...))						OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { ClockQualityAcceptanceCriteria-ExtIEs} }	OPTIONAL,
	...
}

ClockQualityAcceptanceCriteria-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ClockQualityReportingControlInfo ::= SEQUENCE {
	clockQualityDetailLevel		ClockQualityDetailLevel,
	iE-Extensions				ProtocolExtensionContainer { {ClockQualityReportingControlInfo-ExtIEs} } OPTIONAL,
	...
}

ClockQualityReportingControlInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ClockQualityDetailLevel ::= CHOICE {
	clockQualityMetrics			NULL,
	acceptanceIndication		ClockQualityAcceptanceCriteria,
	choice-extension			ProtocolIE-Single-Container { {ClockQualityDetailLevel-ExtIEs} }
}

ClockQualityDetailLevel-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


CapabilityForBATAdaptation ::= ENUMERATED {true, ...}

CompositeAvailableCapacityGroup ::= SEQUENCE {
	compositeAvailableCapacityDownlink		CompositeAvailableCapacity,
	compositeAvailableCapacityUplink		CompositeAvailableCapacity,
	iE-Extensions				ProtocolExtensionContainer { { CompositeAvailableCapacityGroup-ExtIEs} }	OPTIONAL,
	...
}

CompositeAvailableCapacityGroup-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-CompositeAvailableCapacitySupplementaryUplink	CRITICALITY ignore	EXTENSION CompositeAvailableCapacity	PRESENCE optional	},
	...
}

CompositeAvailableCapacity ::= SEQUENCE {
	cellCapacityClassValue		CellCapacityClassValue				OPTIONAL,
	capacityValueInfo			CapacityValueInfo, -- this IE represents the IE "CapacityValue" in 9.2.2.52, it’s used to distinguish the "CapacityValue" in 9.2.2.54
	iE-Extensions				ProtocolExtensionContainer { { CompositeAvailableCapacity-ExtIEs} }OPTIONAL,
	...
}

CompositeAvailableCapacity-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ControlPlaneTrafficType ::= INTEGER (1..3, ...)

CHO-MRDC-EarlyDataForwarding ::= ENUMERATED {stop, ...}

CHO-MRDC-Indicator ::= ENUMERATED {true, ..., coordination-only }



CHOtrigger ::= ENUMERATED {
	cho-initiation,
	cho-replace,
	...
}

CHOinformation-Req ::= SEQUENCE {
	cho-trigger						CHOtrigger,
	targetNG-RANnodeUEXnAPID		NG-RANnodeUEXnAPID										OPTIONAL
		-- This IE shall be present if the CHO Trigger IE is present and set to "CHO-replace" --,
	cHO-EstimatedArrivalProbability	CHO-Probability											OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { CHOinformation-Req-ExtIEs} }	OPTIONAL,
	...
}

CHOinformation-Req-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	{ID id-CHOTimeBasedInformation	CRITICALITY reject	EXTENSION CHOTimeBasedInformation		PRESENCE optional}|
	{ ID id-CHO-Maxnoof-CondReconfig	CRITICALITY reject	EXTENSION CHO-Maxnoof-CondReconfig		PRESENCE optional },
	...
}

CHOTimeBasedInformation ::= SEQUENCE {
	cHO-HOWindowStart			CHO-HandoverWindowStart,
	cHO-HOWindowDuration		CHO-HandoverWindowDuration,
	iE-Extensions				ProtocolExtensionContainer { {CHOTimeBasedInformation-ExtIEs} }	OPTIONAL,
	...
}

CHOTimeBasedInformation-ExtIEs	XNAP-PROTOCOL-EXTENSION ::= {
	...
}

CHOinformation-Ack ::= SEQUENCE {
	requestedTargetCellGlobalID		Target-CGI,
	maxCHOoperations				MaxCHOpreparations											OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { CHOinformation-Ack-ExtIEs} }	OPTIONAL,
	...
}

CHOinformation-Ack-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	{ ID id-CHO-CPAC-Info				CRITICALITY reject		EXTENSION CHO-CPAC-Information	PRESENCE optional },
	...
}


CHOinformation-AddReq ::= SEQUENCE {
	source-M-NGRAN-node-ID				GlobalNG-RANNode-ID,
	source-M-NGRAN-node-UE-XnAP-ID		NG-RANnodeUEXnAPID,
	cHO-EstimatedArrivalProbability		CHO-Probability													OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { CHOinformation-AddReq-ExtIEs} }	OPTIONAL,
	...
}

CHOinformation-AddReq-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	...
}

CHOinformation-AddReqAck ::= SEQUENCE {
	pCell-ID						GlobalNG-RANCell-ID			OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { CHOinformation-AddReqAck-ExtIEs} }	OPTIONAL,
	...
}

CHOinformation-AddReqAck-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	...
}

CHOinformation-ModReq ::= SEQUENCE {
	conditionalReconfig					ENUMERATED {intra-mn-cho, ...},
	cHO-EstimatedArrivalProbability		CHO-Probability													OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { CHOinformation-ModReq-ExtIEs} }	OPTIONAL,
	...
}

CHOinformation-ModReq-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	...
}

CHO-Maxnoof-CondReconfig ::= INTEGER (1..8,...)

CHO-CPAC-Information ::= SEQUENCE {
	cHO-CPAC-config-indicator		CHO-CPAC-Config-Indicator		OPTIONAL,
	cHO-target-SN-node-list			CHO-target-SN-node-list,
	iE-Extensions					ProtocolExtensionContainer { {CHO-CPAC-Information-ExtIEs}}	OPTIONAL,
	...
}

CHO-CPAC-Information-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	...
}

CHO-CPAC-Config-Indicator ::= ENUMERATED {
	cho-only-not-prepared,
	...
}


CHO-Probability ::= INTEGER (1..100)

CHO-HandoverWindowStart ::= INTEGER (0.. 549755813887)

CHO-HandoverWindowDuration ::= INTEGER (1..6000)

CHO-target-SN-node-list ::= SEQUENCE (SIZE(1.. maxnoofTargetSNs)) OF CHO-target-SN-node-Item

CHO-target-SN-node-Item ::= SEQUENCE {
	target-S-NG-RANnodeID				GlobalNG-RANNode-ID,
	pduSessionResourcesAdmittedList		PDUSessionResourcesAdmitted-List,
	cho-Candidate-PSCells-list			CHO-Candidate-PSCells-list,
	iE-Extensions					ProtocolExtensionContainer { {CHO-target-SN-node-Item-ExtIEs}}	OPTIONAL,
	...
}

CHO-target-SN-node-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	...
}


CHO-Candidate-PSCells-list ::= SEQUENCE (SIZE(1..maxnoofPSCellCandidates)) OF CHO-Candidate-PSCells-Item

CHO-Candidate-PSCells-Item ::= SEQUENCE {
	pscell-id								NR-CGI,
	target2source-NG-RANNode-Container		OCTET STRING,
	iE-Extensions					ProtocolExtensionContainer { {CHO-Candidate-PSCells-Item-ExtIEs}}	OPTIONAL,
	...
}

CHO-Candidate-PSCells-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	...
}

CNsubgroupID ::= INTEGER (0..7, ...)

CompleteCandidateConfig-Indicator ::= ENUMERATED {complete-candidate-config, ...}

Conditional-Reconfig-List ::= SEQUENCE (SIZE(1..maxnoofPSCellCandidates)) OF Conditional-Reconfig-Item

Conditional-Reconfig-Item ::= SEQUENCE {
	pCell-ID			Target-CGI,
	pSCell-ID			NR-CGI				OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {Conditional-Reconfig-Item-ExtIEs} }	OPTIONAL,
	...

}

Conditional-Reconfig-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ConfiguredTACIndication ::= ENUMERATED {
	true,
	...
}


Connectivity-Support		::= SEQUENCE {
	eNDC-Support			ENUMERATED {supported, not-supported, ...},
	iE-Extensions			ProtocolExtensionContainer { {Connectivity-Support-ExtIEs} }	OPTIONAL,
	...
}

Connectivity-Support-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ContainerAppLayerMeasConfig ::= OCTET STRING (SIZE (1..8000))

COUNT-PDCP-SN12 ::= SEQUENCE {
	pdcp-SN12						INTEGER (0..4095),
	hfn-PDCP-SN12					INTEGER (0..1048575),
	iE-Extensions					ProtocolExtensionContainer { {COUNT-PDCP-SN12-ExtIEs} }	OPTIONAL,
	...
}

COUNT-PDCP-SN12-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


COUNT-PDCP-SN18 ::= SEQUENCE {
	pdcp-SN18						INTEGER (0..262143),
	hfn-PDCP-SN18					INTEGER (0..16383),
	iE-Extensions					ProtocolExtensionContainer { {COUNT-PDCP-SN18-ExtIEs} }	OPTIONAL,
	...
}

COUNT-PDCP-SN18-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

CoverageModificationCause ::=	ENUMERATED {
	coverage,
	cell-edge-capacity,
	...,
	network-energy-saving}

Coverage-Modification-List ::= SEQUENCE (SIZE (0..maxnoofCellsinNG-RANnode)) OF Coverage-Modification-List-Item

Coverage-Modification-List-Item ::= SEQUENCE {
	globalNG-RANCell-ID				GlobalCell-ID,
	cellCoverageState				INTEGER (0..63, ...),
	cellDeploymentStatusIndicator	CellDeploymentStatusIndicator			OPTIONAL,
	cellReplacingInfo				CellReplacingInfo						OPTIONAL,
-- This IE shall be present if the Cell Deployment Status Indicator IE is present.
	sSB-Coverage-Modification-List	SSB-Coverage-Modification-List,
	iE-Extension			ProtocolExtensionContainer { { Coverage-Modification-List-Item-ExtIEs} } OPTIONAL,
	...
}
Coverage-Modification-List-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
{ ID id-CoverageModificationCause		CRITICALITY ignore	EXTENSION CoverageModificationCause 		PRESENCE optional},
	...
}


CPTransportLayerInformation ::= CHOICE {
	endpointIPAddress			TransportLayerAddress,
	choice-extension			ProtocolIE-Single-Container { {CPTransportLayerInformation-ExtIEs} }
}

CPTransportLayerInformation-ExtIEs XNAP-PROTOCOL-IES ::= {
	{ ID id-EndpointIPAddressAndPort		CRITICALITY reject	TYPE EndpointIPAddressAndPort		PRESENCE mandatory},
	...
}

CPACcandidatePSCells-list ::= SEQUENCE (SIZE(1..maxnoofPSCellCandidates)) OF CPACcandidatePSCells-item

CPACcandidatePSCells-item ::= SEQUENCE {
	pscell-id						NR-CGI,
	iE-Extensions					ProtocolExtensionContainer { {CPACcandidatePSCells-item-ExtIEs}}	OPTIONAL,
	...
}

CPACcandidatePSCells-item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

CPACcandidatePSCells-wotherInfo-list ::= SEQUENCE (SIZE(1..maxnoofPSCellCandidates)) OF CPACcandidatePSCells-wotherInfo-item

CPACcandidatePSCells-wotherInfo-item ::= SEQUENCE {
	pscell-id							NR-CGI,
	s-CPAC-CompleteCandidateConfig-Indicator		CompleteCandidateConfig-Indicator		OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {CPACcandidatePSCells-wotherInfo-item-ExtIEs}}	OPTIONAL,
	...
}

CPACcandidatePSCells-wotherInfo-item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


CPACConfiguration ::= SEQUENCE {
	cpacCandidateCell-List				CPACCandidateCell-List,
	iE-Extensions						ProtocolExtensionContainer { { CPACConfiguration-ExtIEs} }	OPTIONAL,
	...
}

CPACConfiguration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


CPACCandidateCell-List ::= SEQUENCE (SIZE(1..maxnoofPSCellsinCPAC)) OF CPACCandidateCell-Item

CPACCandidateCell-Item ::= SEQUENCE {
	cpacCandidateCellID					GlobalNG-RANCell-ID,
	cpacExecutionCondition-List			CPACExecutionCondition-List,
	iE-Extensions						ProtocolExtensionContainer { { CPACCandidateCell-Item-ExtIEs} }	OPTIONAL,
	...
}

CPACCandidateCell-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


CPACExecutionCondition-List ::= SEQUENCE (SIZE(1..maxnoofCPACexecutioncond)) OF CPACExecutionCondition-Item

CPACExecutionCondition-Item ::= SEQUENCE {	
	measObjectContainer					MeasObjectContainer,
	reportConfigContainer				ReportConfigContainer,
	iE-Extensions						ProtocolExtensionContainer { { CPACExecutionCondition-Item-ExtIEs} }	OPTIONAL,
	...
}

CPACExecutionCondition-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

CPCindicator ::= ENUMERATED {cpc-initiation, cpc-modification, cpc-cancellation, ...}

CPAInformationRequest ::= SEQUENCE {
	max-no-of-pscells					INTEGER (1..maxnoofPSCellCandidates, ...),
	cpac-EstimatedArrivalProbability	CHO-Probability 	OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { { CPAInformationRequest-ExtIEs} } OPTIONAL,
	...
}

CPAInformationRequest-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-S-CPAC-Request-Info				CRITICALITY reject		EXTENSION	S-CPAC-Request-Info						PRESENCE		optional}|
	{ ID id-S-CPAC-ReferenceConfigRequest	CRITICALITY ignore		EXTENSION	S-CPAC-ReferenceConfig-Request		PRESENCE		optional},
	...
}

CPAInformationAck ::= SEQUENCE {
	candidate-pscells					CPACcandidatePSCells-list,
	iE-Extensions		ProtocolExtensionContainer { { CPAInformationAck-ExtIEs} } OPTIONAL,
	...
}

CPAInformationAck-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-CPACcandidatePSCells-wotherInfo-list	CRITICALITY reject		EXTENSION CPACcandidatePSCells-wotherInfo-list		PRESENCE optional},
	...
}

CPCInformationRequired::= SEQUENCE {
	cpc-target-sn-required-list		CPC-target-SN-required-list,
	iE-Extensions					ProtocolExtensionContainer { {CPCInformationRequired-ExtIEs} } OPTIONAL,
	...
}

CPCInformationRequired-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-S-CPAC-Request				CRITICALITY reject		EXTENSION	S-CPAC-Request					PRESENCE		optional},
	...
}

CPC-target-SN-required-list ::= SEQUENCE (SIZE(1..maxnoofTargetSNs)) OF CPC-target-SN-required-list-Item

CPC-target-SN-required-list-Item	::= SEQUENCE {
	target-S-NG-RANnodeID				GlobalNG-RANNode-ID,
	cpc-indicator						CPCindicator,
	max-no-of-pscells					INTEGER (1..maxnoofPSCellCandidates, ...),
	cpac-EstimatedArrivalProbability	CHO-Probability 																	OPTIONAL,
	sN-to-MN-Container					OCTET STRING,
	iE-Extensions						ProtocolExtensionContainer { { CPC-target-SN-required-list-Item-ExtIEs} }	OPTIONAL,
	...
}

CPC-target-SN-required-list-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}



CPCInformationConfirm ::= SEQUENCE {
	cpc-target-sn-confirm-list CPC-target-SN-confirm-list,
	iE-Extensions		ProtocolExtensionContainer { { CPCInformationConfirm-ExtIEs} } OPTIONAL,
	...
}

CPCInformationConfirm-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

CPC-target-SN-confirm-list ::= SEQUENCE (SIZE(1..maxnoofTargetSNs)) OF CPC-target-SN-confirm-list-Item

CPC-target-SN-confirm-list-Item ::= SEQUENCE {
	target-S-NG-RANnodeID			GlobalNG-RANNode-ID,
	candidate-pscells				CPACcandidatePSCells-list,
	iE-Extensions					ProtocolExtensionContainer { { CPC-target-SN-confirm-list-Item-ExtIEs} } OPTIONAL,
	...
}

CPC-target-SN-confirm-list-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-CPAC-Preparation-Type		CRITICALITY ignore		EXTENSION CPAC-Preparation-Type		PRESENCE optional},
	...
}

CPAInformationModReq ::= SEQUENCE {
	max-no-of-pscells					INTEGER (1..8, ...)	OPTIONAL,
	cpac-EstimatedArrivalProbability	CHO-Probability 	OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { { CPAInformationModReq-ExtIEs} } OPTIONAL,
	...
}

CPAInformationModReq-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-S-CPAC-Request-Info					CRITICALITY reject		EXTENSION	S-CPAC-Request-Info					PRESENCE		optional}|
	{ ID id-S-CPAC-ReferenceConfigRequest		CRITICALITY ignore		EXTENSION	S-CPAC-ReferenceConfig-Request	PRESENCE		optional}|
	{ ID id-S-CPAC-InterSN-ExecutionNotify		CRITICALITY reject		EXTENSION	S-CPAC-InterSN-ExecutionNotify	PRESENCE		optional},
	...
}

CPAInformationModReqAck ::= SEQUENCE {
	candidate-pscells					CPACcandidatePSCells-list,
	iE-Extensions						ProtocolExtensionContainer { { CPAInformationModReqAck-ExtIEs} } OPTIONAL,
	...
}

CPAInformationModReqAck-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-CPACcandidatePSCells-wotherInfo-list	CRITICALITY reject		EXTENSION CPACcandidatePSCells-wotherInfo-list	PRESENCE optional},
	...
}

CPC-DataForwarding-Indicator ::= ENUMERATED {triggered, early-data-transmission-stop, ..., coordination-only}

CPAC-Preparation-Type ::= ENUMERATED {s-cpac, ...}

CPACInformationModRequired ::= SEQUENCE {
	candidate-pscells	CPACcandidatePSCells-list,
	iE-Extensions		ProtocolExtensionContainer { { CPACInformationModRequired-ExtIEs} } OPTIONAL,
	...
}

CPACInformationModRequired-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-CPACcandidatePSCells-wotherInfo-list	CRITICALITY reject		EXTENSION CPACcandidatePSCells-wotherInfo-list		PRESENCE optional},
	...
}

CPCInformationUpdate::= SEQUENCE {
	cpc-target-sn-list					CPC-target-SN-mod-list,
	iE-Extensions		ProtocolExtensionContainer { { CPCInformationUpdate-ExtIEs} } OPTIONAL,
	...
}

CPCInformationUpdate-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


CPC-target-SN-mod-list ::= SEQUENCE (SIZE(1..maxnoofTargetSNs)) OF CPC-target-SN-mod-item

CPC-target-SN-mod-item ::= SEQUENCE {
	target-S-NG-RANnodeID				GlobalNG-RANNode-ID,
	candidate-pscells					CPCInformationUpdatePSCells-list,
	iE-Extensions		ProtocolExtensionContainer { {CPC-target-SN-mod-item-ExtIEs} } OPTIONAL,
	...
}

CPC-target-SN-mod-item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

CPCInformationUpdatePSCells-list ::= SEQUENCE (SIZE(1..maxnoofPSCellCandidates)) OF CPCInformationUpdatePSCells-item

CPCInformationUpdatePSCells-item ::= SEQUENCE {
	pscell-id						NR-CGI,
	iE-Extensions					ProtocolExtensionContainer { {CPCInformationUpdatePSCells-item-ExtIEs}}	OPTIONAL,
	...
}

CPCInformationUpdatePSCells-item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


CriticalityDiagnostics ::= SEQUENCE {
	procedureCode					ProcedureCode					OPTIONAL,
	triggeringMessage				TriggeringMessage				OPTIONAL,
	procedureCriticality			Criticality						OPTIONAL,
	iEsCriticalityDiagnostics		CriticalityDiagnostics-IE-List	OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {CriticalityDiagnostics-ExtIEs} }	OPTIONAL,
	...
}

CriticalityDiagnostics-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

CriticalityDiagnostics-IE-List ::= SEQUENCE (SIZE (1..maxNrOfErrors)) OF
	SEQUENCE {
		iECriticality			Criticality,
		iE-ID					ProtocolIE-ID,
		typeOfError				TypeOfError,
		iE-Extensions			ProtocolExtensionContainer { {CriticalityDiagnostics-IE-List-ExtIEs} } OPTIONAL,
		...
}

CriticalityDiagnostics-IE-List-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


C-RNTI ::= BIT STRING (SIZE(16))

CSI-RS-MTC-Configuration-List ::= SEQUENCE (SIZE(1.. maxnoofCSIRSconfigurations)) OF CSI-RS-MTC-Configuration-Item

CSI-RS-MTC-Configuration-Item ::= SEQUENCE {
	csi-RS-Index			INTEGER(0..95),
	csi-RS-Status			ENUMERATED {activated, deactivated, ...},
	csi-RS-Neighbour-List	CSI-RS-Neighbour-List OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { { CSI-RS-MTC-Configuration-Item-ExtIEs} }	OPTIONAL,
	...
}

CSI-RS-MTC-Configuration-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

CSI-RS-Neighbour-List ::= SEQUENCE (SIZE(1.. maxnoofCSIRSneighbourCells)) OF CSI-RS-Neighbour-Item

CSI-RS-Neighbour-Item ::= SEQUENCE {
	nr-cgi							NR-CGI,
	csi-RS-MTC-Neighbour-List 	CSI-RS-MTC-Neighbour-List OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { { CSI-RS-Neighbour-Item-ExtIEs} }	OPTIONAL,
	...
}

CSI-RS-Neighbour-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


CSI-RS-MTC-Neighbour-List ::= SEQUENCE (SIZE(1.. maxnoofCSIRSneighbourCellsInMTC)) OF CSI-RS-MTC-Neighbour-Item

CSI-RS-MTC-Neighbour-Item ::= SEQUENCE {
	csi-RS-Index		INTEGER(0..95),
	iE-Extensions		ProtocolExtensionContainer { { CSI-RS-MTC-Neighbour-Item-ExtIEs} }	OPTIONAL,
	...
}


CSI-RS-MTC-Neighbour-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}



CyclicPrefix-E-UTRA-DL ::= ENUMERATED {
	normal,
	extended,
	...
}


CyclicPrefix-E-UTRA-UL ::= ENUMERATED {
	normal,
	extended,
	...
}

CSI-RSTransmissionIndication ::= ENUMERATED {
	activated,
	deactivated,
	...
}

CAGListforMDT ::= SEQUENCE (SIZE(1.. maxnoofCAGforMDT))OF CAGListforMDTItem 

CAGListforMDTItem ::= SEQUENCE {
	plmnID				PLMN-Identity,
	cAGID				CAG-Identifier,
	iE-Extensions		ProtocolExtensionContainer { {CAGListforMDTItem-ExtIEs} }	OPTIONAL,
	...
}

CAGListforMDTItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	...
}


-- D


XnUAddressInfoperPDUSession-List ::= SEQUENCE (SIZE(1..maxnoofPDUSessions)) OF XnUAddressInfoperPDUSession-Item

XnUAddressInfoperPDUSession-Item ::= SEQUENCE {
	pduSession-ID			PDUSession-ID,
	dataForwardingInfoFromTargetNGRANnode		DataForwardingInfoFromTargetNGRANnode										OPTIONAL,
	pduSessionResourceSetupCompleteInfo-SNterm			PDUSessionResourceBearerSetupCompleteInfo-SNterminated		OPTIONAL,
	iE-Extension			ProtocolExtensionContainer { { XnUAddressInfoperPDUSession-Item-ExtIEs} }					OPTIONAL,
	...
}

XnUAddressInfoperPDUSession-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
{ ID id-SecondarydataForwardingInfoFromTarget-List	CRITICALITY ignore	EXTENSION SecondarydataForwardingInfoFromTarget-List	PRESENCE optional}|
{ ID id-DRB-IDs-takenintouse						CRITICALITY reject	EXTENSION DRB-List												PRESENCE optional}|
{ ID id-dataForwardingInfoFromTargetE-UTRANnode		CRITICALITY ignore	EXTENSION DataForwardingInfoFromTargetE-UTRANnode		PRESENCE optional},
	...
}

DataForwardingInfoFromTargetE-UTRANnode ::= SEQUENCE {
	dataForwardingInfoFromTargetE-UTRANnode-List			DataForwardingInfoFromTargetE-UTRANnode-List,
	iE-Extension		ProtocolExtensionContainer { { DataForwardingInfoFromTargetE-UTRANnode-ExtIEs} }	OPTIONAL,
	...
}

DataForwardingInfoFromTargetE-UTRANnode-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

DataForwardingInfoFromTargetE-UTRANnode-List ::= SEQUENCE (SIZE(1.. maxnoofDataForwardingTunneltoE-UTRAN)) OF DataForwardingInfoFromTargetE-UTRANnode-Item

DataForwardingInfoFromTargetE-UTRANnode-Item ::= SEQUENCE {
	dlForwardingUPTNLInformation	UPTransportLayerInformation,
	qosFlowsToBeForwarded-List	QoSFlowsToBeForwarded-List,
	iE-Extension		ProtocolExtensionContainer { { DataForwardingInfoFromTargetE-UTRANnode-Item-ExtIEs} }	OPTIONAL,
	...
}

DataForwardingInfoFromTargetE-UTRANnode-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

QoSFlowsToBeForwarded-List ::= SEQUENCE (SIZE(1..maxnoofQoSFlows)) OF QoSFlowsToBeForwarded-Item
QoSFlowsToBeForwarded-Item ::= SEQUENCE {
	qosFlowIdentifier			QoSFlowIdentifier,
	iE-Extension		ProtocolExtensionContainer { { QoSFlowsToBeForwarded-Item-ExtIEs} }	OPTIONAL,
	...
}

QoSFlowsToBeForwarded-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


DataForwardingInfoFromTargetNGRANnode ::= SEQUENCE {
	qosFlowsAcceptedForDataForwarding-List			QoSFLowsAcceptedToBeForwarded-List,
	pduSessionLevelDLDataForwardingInfo				UPTransportLayerInformation								OPTIONAL,
	pduSessionLevelULDataForwardingInfo				UPTransportLayerInformation								OPTIONAL,
	dataForwardingResponseDRBItemList				DataForwardingResponseDRBItemList						OPTIONAL,
	iE-Extension		ProtocolExtensionContainer { {DataForwardingInfoFromTargetNGRANnode-ExtIEs} }	OPTIONAL,
	...
}

DataForwardingInfoFromTargetNGRANnode-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-DirectForwardingPathAvailability	CRITICALITY ignore		EXTENSION DirectForwardingPathAvailability		PRESENCE optional },
	...
}


QoSFLowsAcceptedToBeForwarded-List ::= SEQUENCE (SIZE(1.. maxnoofQoSFlows)) OF QoSFLowsAcceptedToBeForwarded-Item

QoSFLowsAcceptedToBeForwarded-Item ::= SEQUENCE {
	qosFlowIdentifier			QoSFlowIdentifier,
	iE-Extension				ProtocolExtensionContainer { {QoSFLowsAcceptedToBeForwarded-Item-ExtIEs} }	OPTIONAL,
	...
}

QoSFLowsAcceptedToBeForwarded-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


DataforwardingandOffloadingInfofromSource ::= SEQUENCE {
	qosFlowsToBeForwarded			QoSFLowsToBeForwarded-List,
	sourceDRBtoQoSFlowMapping		DRBToQoSFlowMapping-List													OPTIONAL,
	iE-Extension		ProtocolExtensionContainer { {DataforwardingandOffloadingInfofromSource-ExtIEs} }	OPTIONAL,
	...
}

DataforwardingandOffloadingInfofromSource-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

QoSFLowsToBeForwarded-List ::= SEQUENCE (SIZE(1.. maxnoofQoSFlows)) OF QoSFLowsToBeForwarded-Item

QoSFLowsToBeForwarded-Item ::= SEQUENCE {
	qosFlowIdentifier			QoSFlowIdentifier,
	dl-dataforwarding			DLForwarding,
	ul-dataforwarding			ULForwarding,
	iE-Extension		ProtocolExtensionContainer { {QoSFLowsToBeForwarded-Item-ExtIEs} }	OPTIONAL,
	...
}

QoSFLowsToBeForwarded-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
{ ID id-ULForwardingProposal					CRITICALITY ignore	EXTENSION ULForwardingProposal		PRESENCE optional }|
{ ID id-SourceDLForwardingIPAddress				CRITICALITY ignore	EXTENSION TransportLayerAddress		PRESENCE optional}|
{ ID id-SourceNodeDLForwardingIPAddress			CRITICALITY ignore	EXTENSION TransportLayerAddress		PRESENCE optional},
	...
}



DataForwardingResponseDRBItemList ::= SEQUENCE (SIZE(1..maxnoofDRBs)) OF DataForwardingResponseDRBItem

DataForwardingResponseDRBItem ::= SEQUENCE {
	drb-ID				DRB-ID,
	dlForwardingUPTNL	UPTransportLayerInformation												OPTIONAL,
	ulForwardingUPTNL	UPTransportLayerInformation												OPTIONAL,
	iE-Extension		ProtocolExtensionContainer { {DataForwardingResponseDRBItem-ExtIEs} }	OPTIONAL,
	...
}

DataForwardingResponseDRBItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


DataTrafficResources ::= BIT STRING (SIZE(6..17600))


DataTrafficResourceIndication ::= SEQUENCE {
	activationSFN					ActivationSFN,
	sharedResourceType				SharedResourceType,
	reservedSubframePattern			ReservedSubframePattern										OPTIONAL,
	iE-Extension		ProtocolExtensionContainer { {DataTrafficResourceIndication-ExtIEs} }	OPTIONAL,
	...
}

DataTrafficResourceIndication-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


DAPSRequestInfo ::= SEQUENCE {
	dapsIndicator				ENUMERATED {daps-HO-required, ...},
	iE-Extensions				ProtocolExtensionContainer { {DAPSRequestInfo-ExtIEs} } OPTIONAL,
	...
}

DAPSRequestInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


DAPSResponseInfo-List ::= SEQUENCE (SIZE (1..maxnoofDRBs)) OF DAPSResponseInfo-Item

DAPSResponseInfo-Item ::= SEQUENCE {
	drbID						DRB-ID,
	dapsResponseIndicator		ENUMERATED {daps-HO-accepted, daps-HO-not-accepted, ...},
	iE-Extensions				ProtocolExtensionContainer { {DAPSResponseInfo-Item-ExtIEs} } OPTIONAL,
	...
}

DAPSResponseInfo-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


DeliveryStatus	::= INTEGER (0..4095, ...)


DesiredActNotificationLevel	::= ENUMERATED {none, qos-flow, pdu-session, ue-level, ...}

DefaultDRB-Allowed ::= ENUMERATED {true, false, ...}

DirectForwardingPathAvailability ::= ENUMERATED {direct-path-available, ...}

DirectForwardingPathAvailabilityWithSourceMN ::= ENUMERATED {direct-path-available, ...}

DLCountChoice ::= CHOICE {
	count12bits				COUNT-PDCP-SN12,
	count18bits				COUNT-PDCP-SN18,
	choice-extension		ProtocolIE-Single-Container { {DLCountChoice-ExtIEs} }
}

DLCountChoice-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


DLForwarding	::= ENUMERATED {dl-forwarding-proposed, ...}


DL-GBR-PRB-usage::= INTEGER (0..100)

DL-GBR-PRB-usage-for-MIMO::= INTEGER (0..100)

DL-non-GBR-PRB-usage::= INTEGER (0..100)

DL-non-GBR-PRB-usage-for-MIMO::= INTEGER (0..100)

DLF1Terminating-BHInfo ::= SEQUENCE {
	egressBAPRoutingID		BAPRoutingID,
	egressBHRLCCHID			BHRLCChannelID,
	iE-Extensions			ProtocolExtensionContainer { { DLF1Terminating-BHInfo-ExtIEs} } OPTIONAL,
	...
}

DLF1Terminating-BHInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

DLLBTFailureInformationRequest ::= ENUMERATED {inquiry, ...}
DLLBTFailureInformationList	::= SEQUENCE (SIZE(1.. maxnoofLBTFailureInformation)) OF DLLBTFailureInformationList-Item

DLLBTFailureInformationList-Item::= SEQUENCE {
	uEAssistantIdentifier		NG-RANnodeUEXnAPID,
	numberOfDLLBTFailures		INTEGER (1..1000,...)							OPTIONAL,
	iE-Extensions				ProtocolExtensionContainer { { DLLBTFailureInformationList-Item-ExtIEs} }	OPTIONAL,
	...
}

DLLBTFailureInformationList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


DLNonF1Terminating-BHInfo ::= SEQUENCE {
	ingressBAPRoutingID			BAPRoutingID,
	ingressBHRLCCHID			BHRLCChannelID,
	priorhopBAPAddress			BAPAddress,
	iabqosMappingInformation	IAB-QoS-Mapping-Information,
	iE-Extensions		ProtocolExtensionContainer { { DLNonF1Terminating-BHInfo-ExtIEs} } OPTIONAL,
	...
}

DLNonF1Terminating-BHInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


DL-Total-PRB-usage::= INTEGER (0..100)

DL-Total-PRB-usage-for-MIMO::= INTEGER (0..100)

DRB-ID	::= INTEGER (1..32, ...)


DRB-List ::= SEQUENCE (SIZE (1..maxnoofDRBs)) OF DRB-ID


DRB-List-withCause ::= SEQUENCE (SIZE (1..maxnoofDRBs)) OF DRB-List-withCause-Item

DRB-List-withCause-Item ::= SEQUENCE {
	drb-id		DRB-ID,
	cause		Cause,
	rLC-Mode	RLCMode								OPTIONAL,
	iE-Extension		ProtocolExtensionContainer { {DRB-List-withCause-Item-ExtIEs} }	OPTIONAL,
	...
}

DRB-List-withCause-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


DRB-Number ::= INTEGER (1..32, ...)


DRBsSubjectToDLDiscarding-List ::= SEQUENCE (SIZE (1..maxnoofDRBs)) OF DRBsSubjectToDLDiscarding-Item

DRBsSubjectToDLDiscarding-Item ::= SEQUENCE {
	drbID				DRB-ID,
	dlCount				DLCountChoice,
	iE-Extension		ProtocolExtensionContainer { { DRBsSubjectToDLDiscarding-Item-ExtIEs} }	OPTIONAL,
	...
}

DRBsSubjectToDLDiscarding-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


DRBsSubjectToEarlyStatusTransfer-List ::= SEQUENCE (SIZE (1..maxnoofDRBs)) OF DRBsSubjectToEarlyStatusTransfer-Item

DRBsSubjectToEarlyStatusTransfer-Item ::= SEQUENCE {
	drbID				DRB-ID,
	dlCount				DLCountChoice,
	iE-Extension		ProtocolExtensionContainer { { DRBsSubjectToEarlyStatusTransfer-Item-ExtIEs} }	OPTIONAL,
	...
}

DRBsSubjectToEarlyStatusTransfer-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


DRBsSubjectToStatusTransfer-List ::= SEQUENCE (SIZE (1..maxnoofDRBs)) OF DRBsSubjectToStatusTransfer-Item

DRBsSubjectToStatusTransfer-Item ::= SEQUENCE {
	drbID				DRB-ID,
	pdcpStatusTransfer-UL	DRBBStatusTransferChoice,
	pdcpStatusTransfer-DL	DRBBStatusTransferChoice,
	iE-Extension		ProtocolExtensionContainer { {DRBsSubjectToStatusTransfer-Item-ExtIEs} }	OPTIONAL,
	...
}

DRBsSubjectToStatusTransfer-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-OldQoSFlowMap-ULendmarkerexpected	CRITICALITY reject		EXTENSION QoSFlows-List					PRESENCE optional },
	...
}


DRBBStatusTransferChoice ::= CHOICE {
	pdcp-sn-12bits		DRBBStatusTransfer12bitsSN,
	pdcp-sn-18bits		DRBBStatusTransfer18bitsSN,
	choice-extension		ProtocolIE-Single-Container { {DRBBStatusTransferChoice-ExtIEs} }
}

DRBBStatusTransferChoice-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


DRBBStatusTransfer12bitsSN ::= SEQUENCE {
	receiveStatusofPDCPSDU	BIT STRING (SIZE(1..2048))											OPTIONAL,
	cOUNTValue				COUNT-PDCP-SN12,
	iE-Extension			ProtocolExtensionContainer { {DRBBStatusTransfer12bitsSN-ExtIEs} }	OPTIONAL,
	...
}

DRBBStatusTransfer12bitsSN-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


DRBBStatusTransfer18bitsSN ::= SEQUENCE {
	receiveStatusofPDCPSDU	BIT STRING (SIZE(1..131072))										OPTIONAL,
	cOUNTValue				COUNT-PDCP-SN18,
	iE-Extension			ProtocolExtensionContainer { {DRBBStatusTransfer18bitsSN-ExtIEs} }	OPTIONAL,
	...
}

DRBBStatusTransfer18bitsSN-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


DRBToQoSFlowMapping-List ::= SEQUENCE (SIZE (1..maxnoofDRBs)) OF DRBToQoSFlowMapping-Item

DRBToQoSFlowMapping-Item ::= SEQUENCE {
	drb-ID							DRB-ID,
	qosFlows-List					QoSFlows-List,
	rLC-Mode						RLCMode								OPTIONAL,
	iE-Extension		ProtocolExtensionContainer { {DRBToQoSFlowMapping-Item-ExtIEs} }	OPTIONAL,
	...
}

DRBToQoSFlowMapping-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-DAPSRequestInfo		CRITICALITY ignore		EXTENSION DAPSRequestInfo		PRESENCE optional },
	...
}


DUF-Slot-Config-List	::= SEQUENCE (SIZE(1..maxnoofDUFSlots)) OF DUF-Slot-Config-Item


DUF-Slot-Config-Item 	::=	CHOICE {
	explicitFormat				ExplicitFormat,
	implicitFormat				ImplicitFormat,
	choice-extension				ProtocolIE-Single-Container { { DUF-Slot-Config-Item-ExtIEs} }
}

DUF-Slot-Config-Item-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


DUFSlotformatIndex ::= INTEGER(0..254)

DUFTransmissionPeriodicity ::= ENUMERATED { ms0p5, ms0p625, ms1, ms1p25, ms2, ms2p5, ms5, ms10, ...}


DU-RX-MT-RX ::= ENUMERATED {supported, not-supported, supported-FDM-required, ...}

DU-TX-MT-TX ::= ENUMERATED {supported, not-supported, supported-FDM-required, ...}

DU-RX-MT-TX ::= ENUMERATED {supported, not-supported, supported-FDM-required, ...}

DU-TX-MT-RX ::= ENUMERATED {supported, not-supported, supported-FDM-required, ...}



DuplicationActivation ::= ENUMERATED {active, inactive, ...}


Dynamic5QIDescriptor ::= SEQUENCE {
	priorityLevelQoS			PriorityLevelQoS,
	packetDelayBudget			PacketDelayBudget,
	packetErrorRate				PacketErrorRate,
	fiveQI						FiveQI													OPTIONAL,
	delayCritical				ENUMERATED {delay-critical, non-delay-critical, ...}	OPTIONAL,
-- This IE shall be present if the GBR QoS Flow Information IE is present in the QoS Flow Level QoS Parameters IE.
	averagingWindow				AveragingWindow											OPTIONAL,
-- This IE shall be present if the GBR QoS Flow Information IE is present in the QoS Flow Level QoS Parameters IE.
	maximumDataBurstVolume		MaximumDataBurstVolume									OPTIONAL,
	iE-Extension		ProtocolExtensionContainer { {Dynamic5QIDescriptor-ExtIEs } }	OPTIONAL,
	...
}

Dynamic5QIDescriptor-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-ExtendedPacketDelayBudget		CRITICALITY ignore	EXTENSION ExtendedPacketDelayBudget		PRESENCE optional}|
	{ ID id-CNPacketDelayBudgetDownlink		CRITICALITY ignore	EXTENSION ExtendedPacketDelayBudget		PRESENCE optional}|
	{ ID id-CNPacketDelayBudgetUplink		CRITICALITY ignore	EXTENSION ExtendedPacketDelayBudget		PRESENCE optional},
	...
}


-- E

EarlyMeasurement ::= ENUMERATED {true, ...}

ECNMarkingorCongestionInformationReportingRequest ::= CHOICE {
	eCNMarkingAtRANRequest				ECNMarkingAtRANRequest,
	eCNMarkingAtUPFRequest				ECNMarkingAtUPFRequest,
	congestionInformationRequest		CongestionInformationRequest,
	choice-Extensions				ProtocolIE-Single-Container { {ECNMarkingorCongestionInformationReportingRequest-ExtIEs} }
}

ECNMarkingorCongestionInformationReportingRequest-ExtIEs XNAP-PROTOCOL-IES ::= {
	...

}

ECNMarkingAtRANRequest ::= ENUMERATED {ul, dl, both, stop,...}

ECNMarkingAtUPFRequest ::= ENUMERATED {ul, dl, both, stop,...}


CongestionInformationRequest	::= ENUMERATED {ul, dl, both, stop, ...}


EnergyCost ::= INTEGER (0..10000, ...)


EquivalentSNPNs ::= SEQUENCE (SIZE(1..maxnoofESNPNs)) OF SNPNIdentity


E-RAB-ID		::= INTEGER (0..15, ...)


E-UTRAARFCN ::= INTEGER (0..maxEARFCN)


E-UTRA-Cell-Identity			::= BIT STRING (SIZE(28))

ERedcap-Bcast-Information ::= BIT STRING(SIZE(8))

E-UTRA-CGI ::= SEQUENCE {
	plmn-id				PLMN-Identity,
	e-utra-CI			E-UTRA-Cell-Identity,
	iE-Extension		ProtocolExtensionContainer { {E-UTRA-CGI-ExtIEs} } 	OPTIONAL,
	...
}

E-UTRA-CGI-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


E-UTRAFrequencyBandIndicator ::= INTEGER (1..256, ...)


E-UTRAMultibandInfoList ::= SEQUENCE (SIZE(1..maxnoofEUTRABands)) OF E-UTRAFrequencyBandIndicator


EUTRAPagingeDRXInformation ::= SEQUENCE {
	eutrapaging-eDRX-Cycle		EUTRAPaging-eDRX-Cycle,
	eutrapaging-Time-Window		EUTRAPaging-Time-Window											OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { {EUTRAPagingeDRXInformation-ExtIEs} }	OPTIONAL,
	...
}

EUTRAPagingeDRXInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

EUTRAPaging-eDRX-Cycle ::= ENUMERATED {
	hfhalf, hf1, hf2, hf4, hf6,
	hf8, hf10, hf12, hf14, hf16,
	hf32, hf64, hf128, hf256,
	...
}


EUTRAPaging-Time-Window ::= ENUMERATED {
	s1, s2, s3, s4, s5,
	s6, s7, s8, s9, s10,
	s11, s12, s13, s14, s15, s16,
	...
}

E-UTRAPCI ::= INTEGER (0..503, ...)


E-UTRAPRACHConfiguration ::= SEQUENCE {
	rootSequenceIndex						INTEGER (0..837),
	zeroCorrelationIndex					INTEGER (0..15),
	highSpeedFlag							ENUMERATED {true, false, ...},
	prach-FreqOffset						INTEGER (0..94),
	prach-ConfigIndex						INTEGER (0..63)								OPTIONAL,
-- C-ifTDD: This IE shall be present if the EUTRA-Mode-Info IE in the Served Cell Information E-UTRA IE is set to the value "TDD" --
	iE-Extensions							ProtocolExtensionContainer { {E-UTRAPRACHConfiguration-ExtIEs} }	OPTIONAL,
	...
}

E-UTRAPRACHConfiguration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


E-UTRATransmissionBandwidth ::= ENUMERATED {bw6, bw15, bw25, bw50, bw75, bw100, ..., bw1}

EndpointIPAddressAndPort ::=SEQUENCE {
	endpointIPAddress 				TransportLayerAddress,
	portNumber						PortNumber,
	iE-Extensions					ProtocolExtensionContainer { { EndpointIPAddressAndPort-ExtIEs} } OPTIONAL
}

EndpointIPAddressAndPort-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


EventTriggered ::= SEQUENCE {
	loggedEventTriggeredConfig			LoggedEventTriggeredConfig,
	iE-Extensions		ProtocolExtensionContainer { { EventTriggered-ExtIEs} } OPTIONAL,
	...
}

EventTriggered-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

EventType ::= ENUMERATED {
	report-upon-change-of-serving-cell,
	report-UE-moving-presence-into-or-out-of-the-Area-of-Interest,
	...,
	report-upon-change-of-serving-cell-and-Area-of-Interest
}


EventTypeTrigger ::= CHOICE {
	outOfCoverage					ENUMERATED {true, ...},
	eventL1		EventL1,
	choice-Extensions		ProtocolIE-Single-Container { {EventTypeTrigger-ExtIEs} }
}

EventTypeTrigger-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

EventL1 ::= SEQUENCE {
	l1Threshold					MeasurementThresholdL1LoggedMDT,
	hysteresis					Hysteresis,
	timeToTrigger				TimeToTrigger,
	iE-Extensions		ProtocolExtensionContainer { { EventL1-ExtIEs} } OPTIONAL,
	...
}

EventL1-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

MeasurementThresholdL1LoggedMDT ::= CHOICE {
	threshold-RSRP				Threshold-RSRP,
	threshold-RSRQ				Threshold-RSRQ,
	...,
	choice-extension		ProtocolIE-Single-Container { {MeasurementThresholdL1LoggedMDT-ExtIEs} }
}

MeasurementThresholdL1LoggedMDT-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

ExcessPacketDelayThresholdConfiguration ::= SEQUENCE (SIZE(1..maxnoofThresholdsForExcessPacketDelay)) OF ExcessPacketDelayThresholdItem

ExcessPacketDelayThresholdItem ::= SEQUENCE {
	fiveQI									FiveQI,
	excessPacketDelayThresholdValue			ExcessPacketDelayThresholdValue,
	iE-Extensions		ProtocolExtensionContainer { { ExcessPacketDelayThresholdItem-ExtIEs } }	OPTIONAL,
	...
}

ExcessPacketDelayThresholdItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ExcessPacketDelayThresholdValue ::= ENUMERATED {
	ms0dot25, ms0dot5, ms1, ms2, ms4, ms5, ms10, ms20, ms30, ms40, ms50, ms60, ms70, ms80, ms90, ms100, ms150, ms300, ms500,
	...
}

ExpectedActivityPeriod ::= INTEGER (1..30|40|50|60|80|100|120|150|180|181, ...)

ExpectedHOInterval ::= ENUMERATED {
	sec15, sec30, sec60, sec90, sec120, sec180, long-time,
	...
}

ExpectedIdlePeriod ::= INTEGER (1..30|40|50|60|80|100|120|150|180|181, ...)

ExpectedUEActivityBehaviour ::= SEQUENCE {
	expectedActivityPeriod						ExpectedActivityPeriod						OPTIONAL,
	expectedIdlePeriod							ExpectedIdlePeriod							OPTIONAL,
	sourceOfUEActivityBehaviourInformation		SourceOfUEActivityBehaviourInformation		OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {ExpectedUEActivityBehaviour-ExtIEs} }	OPTIONAL,
	...
}

ExpectedUEActivityBehaviour-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ExpectedUEBehaviour	::= SEQUENCE {
	expectedUEActivityBehaviour		ExpectedUEActivityBehaviour 						OPTIONAL,
	expectedHOInterval				ExpectedHOInterval								OPTIONAL,
	expectedUEMobility				ExpectedUEMobility								OPTIONAL,
	expectedUEMovingTrajectory		ExpectedUEMovingTrajectory						OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {ExpectedUEBehaviour-ExtIEs} }	OPTIONAL,
	...
}

ExpectedUEBehaviour-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ExpectedUEMobility ::= ENUMERATED {
	stationary,
	mobile,
	...
}

ExpectedUEMovingTrajectory ::= SEQUENCE (SIZE(1..maxnoofCellsUEMovingTrajectory)) OF ExpectedUEMovingTrajectoryItem

ExpectedUEMovingTrajectoryItem ::= SEQUENCE {
	nGRAN-CGI				GlobalNG-RANCell-ID,
	timeStayedInCell		INTEGER (0..4095)													OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {ExpectedUEMovingTrajectoryItem-ExtIEs} }	OPTIONAL,
	...
}

ExpectedUEMovingTrajectoryItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SourceOfUEActivityBehaviourInformation ::= ENUMERATED {
	subscription-information,
	statistics,
	...
}

ExplicitFormat ::=	SEQUENCE {
	permutation			Permutation,
	noofDownlinkSymbols	INTEGER(0..14)		OPTIONAL,
	noofUplinkSymbols	INTEGER(0..14)		OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { ExplicitFormat-ExtIEs} } OPTIONAL,
	...
}

ExplicitFormat-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ExtendedRATRestrictionInformation ::= SEQUENCE {
	primaryRATRestriction		BIT STRING (SIZE(8, ..., 16)),
	secondaryRATRestriction		BIT STRING (SIZE(8, ...)),
	iE-Extensions		ProtocolExtensionContainer { {ExtendedRATRestrictionInformation-ExtIEs} }	OPTIONAL,
	...
}

ExtendedRATRestrictionInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ExtendedPacketDelayBudget ::= INTEGER (0..65535, ..., 65536..109999)

ExtendedSliceSupportList	::= SEQUENCE (SIZE(1..maxnoofExtSliceItems)) OF S-NSSAI

ExtendedUEIdentityIndexValue ::= BIT STRING (SIZE(16))

ExtTLAs ::= SEQUENCE (SIZE(1..maxnoofExtTLAs)) OF ExtTLA-Item

ExtTLA-Item ::= SEQUENCE {
	iPsecTLA							TransportLayerAddress				OPTIONAL,
	gTPTransportLayerAddresses			GTPTLAs								OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {ExtTLA-Item-ExtIEs} } OPTIONAL,
	...
}

ExtTLA-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


GTPTLAs	::= SEQUENCE (SIZE(1.. maxnoofGTPTLAs)) OF	GTPTLA-Item


GTPTLA-Item	::= SEQUENCE {
	gTPTransportLayerAddresses				TransportLayerAddress,
	iE-Extensions	ProtocolExtensionContainer { { GTPTLA-Item-ExtIEs } }		OPTIONAL,
	...
}

GTPTLA-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

-- F

F1CTrafficContainer ::= OCTET STRING

F1-terminatingIAB-donorIndicator ::= ENUMERATED {true, ...}

F1-TerminatingTopologyBHInformation	::= SEQUENCE {
	f1TerminatingBHInformation-List				F1TerminatingBHInformation-List,
	iE-Extensions							ProtocolExtensionContainer { {F1-TerminatingTopologyBHInformation-ExtIEs} }	OPTIONAL,
	...
}

F1-TerminatingTopologyBHInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

F1TerminatingBHInformation-List ::= SEQUENCE (SIZE(1..maxnoofBHInfo)) OF F1TerminatingBHInformation-Item

F1TerminatingBHInformation-Item ::= SEQUENCE {
	bHInfoIndex					BHInfoIndex,
	dLTNLAddress				IABTNLAddress,
	dlF1TerminatingBHInfo	DLF1Terminating-BHInfo		OPTIONAL,
	ulF1TerminatingBHInfo	ULF1Terminating-BHInfo		OPTIONAL,
	iE-Extension		ProtocolExtensionContainer { { F1TerminatingBHInformation-Item-ExtIEs} }	OPTIONAL,
	...
}

F1TerminatingBHInformation-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


FiveGCMobilityRestrictionListContainer ::= OCTET STRING
-- This octets of the OCTET STRING contain the Mobility Restriction List IE as specified in TS 38.413 [5]. --

FiveGProSeAuthorized ::= SEQUENCE {
	fiveGproSeDirectDiscovery					FiveGProSeDirectDiscovery									OPTIONAL,
	fiveGproSeDirectCommunication				FiveGProSeDirectCommunication								OPTIONAL,
	fiveGnrProSeLayer2UEtoNetworkRelay		FiveGProSeLayer2UEtoNetworkRelay								OPTIONAL,
	fiveGnrProSeLayer3UEtoNetworkRelay			FiveGProSeLayer3UEtoNetworkRelay							OPTIONAL,
	fiveGnrProSeLayer2RemoteUE					FiveGProSeLayer2RemoteUE									OPTIONAL,
	iE-Extensions								ProtocolExtensionContainer { {FiveGProSeAuthorized-ExtIEs} }	OPTIONAL,
	...
}

FiveGProSeAuthorized-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-FiveGProSeLayer2Multipath		CRITICALITY ignore	EXTENSION FiveGProSeLayer2Multipath		PRESENCE optional}|
	{ ID id-FiveGProSeLayer2UEtoUERelay		CRITICALITY ignore	EXTENSION FiveGProSeLayer2UEtoUERelay	PRESENCE optional}|
	{ ID id-FiveGProSeLayer2UEtoUERemote	CRITICALITY ignore	EXTENSION FiveGProSeLayer2UEtoUERemote	PRESENCE optional},
	...
}

FiveGProSeDirectDiscovery ::= ENUMERATED {
	authorized,
	not-authorized,
	...
}

FiveGProSeDirectCommunication ::= ENUMERATED {
	authorized,
	not-authorized,
	...
}

FiveGProSeLayer2UEtoNetworkRelay ::= ENUMERATED {
	authorized,
	not-authorized,
	...
}

FiveGProSeLayer3UEtoNetworkRelay ::= ENUMERATED {
	authorized,
	not-authorized,
	...
}

FiveGProSeLayer2RemoteUE ::= ENUMERATED {
	authorized,
	not-authorized,
	...
}

FiveGProSeLayer2Multipath ::= ENUMERATED {
	authorized,
	not-authorized,
	...
}

FiveGProSeLayer2UEtoUERelay ::= ENUMERATED {
	authorized,
	not-authorized,
	...
}

FiveGProSeLayer2UEtoUERemote ::= ENUMERATED {
	authorized,
	not-authorized,
	...
}

FiveGProSePC5QoSParameters ::= SEQUENCE {
	fiveGProSepc5QoSFlowList						FiveGProSePC5QoSFlowList,
	fiveGproSepc5LinkAggregateBitRates				BitRate				OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { { FiveGProSePC5QoSParameters-ExtIEs} }	OPTIONAL,
	...
}

FiveGProSePC5QoSParameters-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

FiveGProSePC5QoSFlowList::= SEQUENCE (SIZE(1..maxnoofPC5QoSFlows)) OF FiveGProSePC5QoSFlowItem

FiveGProSePC5QoSFlowItem::= SEQUENCE {
	fiveGproSepQI				FiveQI,
	fiveGproSepc5FlowBitRates	FiveGProSePC5FlowBitRates				OPTIONAL,
	fiveGproSerange				Range						OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { FiveGProSePC5QoSFlowItem-ExtIEs} }	OPTIONAL,
	...
}

FiveGProSePC5QoSFlowItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

FiveGProSePC5FlowBitRates ::= SEQUENCE {
	fiveGproSeguaranteedFlowBitRate		BitRate,
	fiveGproSemaximumFlowBitRate		BitRate,
	iE-Extensions		ProtocolExtensionContainer { { FiveGProSePC5FlowBitRates-ExtIEs} }	OPTIONAL,
	...
}

FiveGProSePC5FlowBitRates-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

FiveQI ::= INTEGER (0..255, ...)

Flows-Mapped-To-DRB-List	::=	SEQUENCE (SIZE(1.. maxnoofQoSFlows)) OF Flows-Mapped-To-DRB-Item

Flows-Mapped-To-DRB-Item 	::= SEQUENCE {
	qoSFlowIdentifier						QoSFlowIdentifier,
	qoSFlowLevelQoSParameters				QoSFlowLevelQoSParameters,
	qoSFlowMappingIndication				QoSFlowMappingIndication													OPTIONAL,
	iE-Extensions							ProtocolExtensionContainer { { Flows-Mapped-To-DRB-Item-ExtIEs} } 	OPTIONAL
}

Flows-Mapped-To-DRB-Item-ExtIEs 	XNAP-PROTOCOL-EXTENSION ::= {
	...
}

FreqDomainHSNAconfiguration-List ::= SEQUENCE (SIZE(1.. maxnoofHSNASlots)) OF FreqDomainHSNAconfiguration-List-Item

FreqDomainHSNAconfiguration-List-Item ::= SEQUENCE {
	rBsetIndex 									INTEGER(0.. maxnoofRBsetsPerCell1, ...),
	freqDomainSlotHSNAconfiguration-List 		FreqDomainSlotHSNAconfiguration-List, 	
	iE-Extensions		ProtocolExtensionContainer { { FreqDomainHSNAconfiguration-List-Item-ExtIEs} }	OPTIONAL,
	...
}

FreqDomainHSNAconfiguration-List-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


FreqDomainSlotHSNAconfiguration-List ::= SEQUENCE (SIZE(1.. maxnoofHSNASlots)) OF FreqDomainSlotHSNAconfiguration-List-Item

FreqDomainSlotHSNAconfiguration-List-Item ::=	SEQUENCE {
	slotIndex		INTEGER(1..maxnoofHSNASlots),	
	hSNADownlink	HSNADownlink		OPTIONAL,
	hSNAUplink		HSNAUplink			OPTIONAL,
	hSNAFlexible	HSNAFlexible		OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { FreqDomainSlotHSNAconfiguration-List-Item-ExtIEs} }	OPTIONAL,
	...
}

FreqDomainSlotHSNAconfiguration-List-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

FrequencyShift7p5khz ::= ENUMERATED {false, true, ...}

-- G


GBRQoSFlowInfo ::= SEQUENCE {
	maxFlowBitRateDL			BitRate,
	maxFlowBitRateUL			BitRate,
	guaranteedFlowBitRateDL		BitRate,
	guaranteedFlowBitRateUL		BitRate,
	notificationControl			ENUMERATED {notification-requested, ...}				OPTIONAL,
	maxPacketLossRateDL			PacketLossRate											OPTIONAL,
	maxPacketLossRateUL			PacketLossRate											OPTIONAL,
	iE-Extensions				ProtocolExtensionContainer { {GBRQoSFlowInfo-ExtIEs} }	OPTIONAL,
	...
}

GBRQoSFlowInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
{ ID id-AlternativeQoSParaSetList	CRITICALITY ignore	EXTENSION AlternativeQoSParaSetList	PRESENCE optional	},
	...
}

GlobalgNB-ID	::= SEQUENCE {
	plmn-id			PLMN-Identity,
	gnb-id			GNB-ID-Choice,
	iE-Extensions		ProtocolExtensionContainer { {GlobalgNB-ID-ExtIEs} } OPTIONAL,
	...
}

GlobalgNB-ID-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

GNB-DU-Cell-Resource-Configuration	::= SEQUENCE {
	subcarrierSpacing				SubcarrierSpacing,
	dUFTransmissionPeriodicity		DUFTransmissionPeriodicity		OPTIONAL,
	dUF-Slot-Config-List			DUF-Slot-Config-List			OPTIONAL,
	hSNATransmissionPeriodicity		HSNATransmissionPeriodicity,
	hNSASlotConfigList				HSNASlotConfigList				OPTIONAL,
	rBsetConfiguration						RBsetConfiguration		OPTIONAL,
	freqDomainHSNAconfiguration-List		FreqDomainHSNAconfiguration-List 	OPTIONAL,
	nACellResourceConfigurationList			NACellResourceConfigurationList 		OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { GNB-DU-Cell-Resource-Configuration-ExtIEs } } OPTIONAL,
	...
}

GNB-DU-Cell-Resource-Configuration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}



GNB-ID-Choice ::= CHOICE {
	gnb-ID					BIT STRING (SIZE(22..32)),
	choice-extension		ProtocolIE-Single-Container { {GNB-ID-Choice-ExtIEs} }
}

GNB-ID-Choice-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


GNB-RadioResourceStatus	::= SEQUENCE {
	ssbAreaRadioResourceStatus-List				SSBAreaRadioResourceStatus-List,
	iE-Extensions							ProtocolExtensionContainer { { GNB-RadioResourceStatus-ExtIEs} }	OPTIONAL,
	...
}

GNB-RadioResourceStatus-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-SliceRadioResourceStatus-List	CRITICALITY ignore	EXTENSION SliceRadioResourceStatus-List	PRESENCE optional	}|
	{ ID id-MIMOPRBusageInformation			CRITICALITY ignore	EXTENSION MIMOPRBusageInformation			PRESENCE optional	},
	...
}


GlobalCell-ID	::= SEQUENCE {
	plmn-id				PLMN-Identity,
	cell-type			Cell-Type-Choice,
	iE-Extensions		ProtocolExtensionContainer { { GlobalCell-ID-ExtIEs} } OPTIONAL,
	...
}

GlobalCell-ID-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


GlobalngeNB-ID	::= SEQUENCE {
	plmn-id			PLMN-Identity,
	enb-id			ENB-ID-Choice,
	iE-Extensions		ProtocolExtensionContainer { {GlobaleNB-ID-ExtIEs} } OPTIONAL,
	...
}

GlobaleNB-ID-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ENB-ID-Choice ::= CHOICE {
	enb-ID-macro			BIT STRING (SIZE(20)),
	enb-ID-shortmacro		BIT STRING (SIZE(18)),
	enb-ID-longmacro		BIT STRING (SIZE(21)),
	choice-extension		ProtocolIE-Single-Container { {ENB-ID-Choice-ExtIEs} }
}

ENB-ID-Choice-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


GlobalNG-RANCell-ID	::= SEQUENCE {
	plmn-id					PLMN-Identity,
	ng-RAN-Cell-id			NG-RAN-Cell-Identity,
	iE-Extensions		ProtocolExtensionContainer { {GlobalNG-RANCell-ID-ExtIEs} } OPTIONAL,
	...
}

GlobalNG-RANCell-ID-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


GlobalNG-RANNode-ID ::= CHOICE {
	gNB						GlobalgNB-ID,
	ng-eNB					GlobalngeNB-ID,
	choice-extension		ProtocolIE-Single-Container { {GlobalNG-RANNode-ID-ExtIEs} }
}

GlobalNG-RANNode-ID-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


GTP-TEID	::= OCTET STRING (SIZE(4))


GTPtunnelTransportLayerInformation ::= SEQUENCE {
	tnl-address			TransportLayerAddress,
	gtp-teid			GTP-TEID,
	iE-Extensions		ProtocolExtensionContainer { {GTPtunnelTransportLayerInformation-ExtIEs} } OPTIONAL,
	...
}

GTPtunnelTransportLayerInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ID id-QoS-Mapping-Information	CRITICALITY reject	EXTENSION QoS-Mapping-Information	PRESENCE optional },
	...
}


GUAMI ::= SEQUENCE {
	plmn-ID				PLMN-Identity,
	amf-region-id		BIT STRING (SIZE (8)),
	amf-set-id			BIT STRING (SIZE (10)),
	amf-pointer			BIT STRING (SIZE (6)),
	iE-Extensions		ProtocolExtensionContainer { {GUAMI-ExtIEs} } OPTIONAL,
	...
}

GUAMI-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

-- H


HandoverReportType ::= ENUMERATED {
	hoTooEarly,
	hoToWrongCell,
	intersystempingpong,
	...
}

HashedUEIdentityIndexValue ::= BIT STRING (SIZE(13, ...))

HSNASlotConfigList ::= SEQUENCE (SIZE(1..maxnoofHSNASlots)) OF HSNASlotConfigItem

HSNASlotConfigItem 	::=	SEQUENCE {
	hSNADownlink			HSNADownlink 		OPTIONAL,
	hSNAUplink				HSNAUplink 			OPTIONAL,
	hSNAFlexible			HSNAFlexible 		OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { { HSNASlotConfigItem-ExtIEs } } OPTIONAL,
	...
}

HSNASlotConfigItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

HSNADownlink ::= ENUMERATED { hard, soft, notavailable }

HSNAFlexible ::= ENUMERATED { hard, soft, notavailable }

HSNAUplink ::= ENUMERATED { hard, soft, notavailable }

HSNATransmissionPeriodicity ::=	ENUMERATED { ms0p5, ms0p625, ms1, ms1p25, ms2, ms2p5, ms5, ms10, ms20, ms40, ms80, ms160, ...}


Hysteresis ::=		INTEGER (0..30)


-- I

IABCellInformation::=	SEQUENCE{
	nRCGI 								NR-CGI,
	iAB-DU-Cell-Resource-Configuration-Mode-Info 	IAB-DU-Cell-Resource-Configuration-Mode-Info	OPTIONAL,
	iAB-STC-Info						IAB-STC-Info				OPTIONAL,
	rACH-Config-Common					RACH-Config-Common			OPTIONAL,
	rACH-Config-Common-IAB				RACH-Config-Common-IAB		OPTIONAL,
	cSI-RS-Configuration				OCTET STRING	OPTIONAL,
	sR-Configuration					OCTET STRING	OPTIONAL,
	pDCCH-ConfigSIB1					OCTET STRING	OPTIONAL,
	sCS-Common							OCTET STRING	OPTIONAL,
	multiplexingInfo					MultiplexingInfo	OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { { IABCellInformation-ExtIEs} } OPTIONAL,
	...
}


IABCellInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}



IAB-DU-Cell-Resource-Configuration-Mode-Info	::=	CHOICE {
	tDD		IAB-DU-Cell-Resource-Configuration-TDD-Info,
	fDD		IAB-DU-Cell-Resource-Configuration-FDD-Info,
	choice-extension			ProtocolIE-Single-Container { { IAB-DU-Cell-Resource-Configuration-Mode-Info-ExtIEs} }
}

IAB-DU-Cell-Resource-Configuration-Mode-Info-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

IAB-DU-Cell-Resource-Configuration-FDD-Info ::= SEQUENCE {
	gNB-DU-Cell-Resource-Configuration-FDD-UL				GNB-DU-Cell-Resource-Configuration,
	gNB-DU-Cell-Resource-Configuration-FDD-DL				GNB-DU-Cell-Resource-Configuration,
	uLFrequencyInfo 					NRFrequencyInfo			OPTIONAL,
	dLFrequencyInfo 					NRFrequencyInfo			OPTIONAL,
	uLTransmissionBandwidth 			NRTransmissionBandwidth	OPTIONAL,
	dlTransmissionBandwidth 			NRTransmissionBandwidth	OPTIONAL,
	uLCarrierList					NRCarrierList			OPTIONAL,
	dlCarrierList					NRCarrierList			OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {IAB-DU-Cell-Resource-Configuration-FDD-Info-ExtIEs} } OPTIONAL,
	...
}

IAB-DU-Cell-Resource-Configuration-FDD-Info-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

IAB-DU-Cell-Resource-Configuration-TDD-Info ::= SEQUENCE {
	gNB-DU-Cell-Resource-Configuration-TDD				GNB-DU-Cell-Resource-Configuration,
	frequencyInfo 					NRFrequencyInfo			OPTIONAL,
	transmissionBandwidth 			NRTransmissionBandwidth	OPTIONAL,
	carrierList						NRCarrierList			OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {IAB-DU-Cell-Resource-Configuration-TDD-Info-ExtIEs} } OPTIONAL,
	...
}

IAB-DU-Cell-Resource-Configuration-TDD-Info-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

IAB-MT-Cell-List ::= SEQUENCE (SIZE(1..maxnoofServingCells)) OF IAB-MT-Cell-List-Item

IAB-MT-Cell-List-Item ::= 	SEQUENCE {
	nRCellIdentity				NR-Cell-Identity,
	dU-RX-MT-RX					DU-RX-MT-RX,
	dU-TX-MT-TX					DU-TX-MT-TX,
	dU-RX-MT-TX					DU-RX-MT-TX,
	dU-TX-MT-RX					DU-TX-MT-RX,
	iE-Extensions				ProtocolExtensionContainer { { IAB-MT-Cell-List-Item-ExtIEs } } OPTIONAL,
	...
}

IAB-MT-Cell-List-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

IABNodeIndication ::= ENUMERATED {true,...}

IAB-QoS-Mapping-Information ::= SEQUENCE {
	dscp							BIT STRING (SIZE(6))			OPTIONAL,
	flow-label						BIT STRING (SIZE(20))		OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {IAB-QoS-Mapping-Information-ExtIEs} }	OPTIONAL,
	...
}

IAB-QoS-Mapping-Information-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

IAB-STC-Info	::=	SEQUENCE{
	iAB-STC-Info-List	IAB-STC-Info-List,
	iE-Extensions		ProtocolExtensionContainer { { IAB-STC-Info-ExtIEs } } OPTIONAL,
	...
}

IAB-STC-Info-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

IAB-STC-Info-List ::= 	SEQUENCE (SIZE(1..maxnoofIABSTCInfo)) OF IAB-STC-Info-Item

IAB-STC-Info-Item::=	SEQUENCE {
	sSB-freqInfo						SSB-freqInfo,
	sSB-subcarrierSpacing				SSB-subcarrierSpacing,
	sSB-transmissionPeriodicity			SSB-transmissionPeriodicity,
	sSB-transmissionTimingOffset		SSB-transmissionTimingOffset,
	sSB-transmissionBitmap				SSB-transmissionBitmap,
	iE-Extensions		ProtocolExtensionContainer { { IAB-STC-Info-Item-ExtIEs } } OPTIONAL,
	...
}

IAB-STC-Info-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

IAB-TNL-Address-Request ::= SEQUENCE {
	iABIPv4AddressesRequested				IABTNLAddressesRequested,
	iABIPv6RequestType						IABIPv6RequestType,
	iABTNLAddressToRemove-List				IABTNLAddressToRemove-List,
	iE-Extensions							ProtocolExtensionContainer { {IAB-TNL-Address-Request-ExtIEs} }	OPTIONAL,
	...
}

IAB-TNL-Address-Request-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

IABIPv6RequestType	::= CHOICE {
	iPv6Address						IABTNLAddressesRequested,
	iPv6Prefix						IABTNLAddressesRequested,
	choice-extension				ProtocolIE-Single-Container { {IABIPv6RequestType-ExtIEs} }
}

IABIPv6RequestType-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


IAB-TNL-Address-Response ::= SEQUENCE {
	iABAllocatedTNLAddress-List		IABAllocatedTNLAddress-List,
	iE-Extensions					ProtocolExtensionContainer { {IAB-TNL-Address-Response-ExtIEs} }	OPTIONAL,
	...
}

IAB-TNL-Address-Response-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

IABAllocatedTNLAddress-List	::= SEQUENCE (SIZE(1..maxnoofTLAsIAB))	OF IABAllocatedTNLAddress-Item

IABAllocatedTNLAddress-Item ::=	SEQUENCE {
	iABTNLAddress				IABTNLAddress,
	iABTNLAddressUsage			IABTNLAddressUsage		OPTIONAL,
	associatedDonorDUAddress	BAPAddress				OPTIONAL,
	iE-Extensions				ProtocolExtensionContainer { {IABAllocatedTNLAddress-Item-ExtIEs} }	OPTIONAL,
	...
}

IABAllocatedTNLAddress-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

IABTNLAddress ::= CHOICE {
	iPv4Address						BIT STRING (SIZE(32)),
	iPv6Address						BIT STRING (SIZE(128)),
	iPv6Prefix						BIT STRING (SIZE(64)),
	choice-extension				ProtocolIE-Single-Container { {IABTNLAddress-ExtIEs} }
}

IABTNLAddress-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

IABTNLAddressesRequested ::= SEQUENCE {
	tNLAddressesOrPrefixesRequestedAllTraffic	INTEGER (1..256) 	OPTIONAL,
	tNLAddressesOrPrefixesRequestedF1-C			INTEGER (1..256) 	OPTIONAL,
	tNLAddressesOrPrefixesRequestedF1-U			INTEGER (1..256) 	OPTIONAL,
	tNLAddressesOrPrefixesRequestedNoNF1		INTEGER (1..256) 	OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {IABTNLAddressesRequested-ExtIEs} } OPTIONAL
}

IABTNLAddressesRequested-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


IABTNLAddressToRemove-List	::= SEQUENCE (SIZE(1..maxnoofTLAsIAB))	OF IABTNLAddressToRemove-Item

IABTNLAddressToRemove-Item ::=	SEQUENCE {
	iABTNLAddress			IABTNLAddress,
	iE-Extension			ProtocolExtensionContainer { {IABTNLAddressToRemove-Item-ExtIEs} }	OPTIONAL,
	...
}

IABTNLAddressToRemove-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

IABTNLAddressUsage ::= ENUMERATED {
	f1-c,
	f1-u,
	non-f1,
	...,
	all
}

IABTNLAddressException ::= SEQUENCE (SIZE(1..maxnoofTLAsIAB)) OF IABTNLAddress-Item


IABTNLAddress-Item ::= SEQUENCE {
	iABTNLAddress					IABTNLAddress,
	iE-Extensions					ProtocolExtensionContainer { { IABTNLAddress-ItemExtIEs } }	OPTIONAL,
	...}

IABTNLAddress-ItemExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ImmediateMDT-NR ::= SEQUENCE {
	measurementsToActivate		MeasurementsToActivate,
	m1Configuration				M1Configuration				OPTIONAL,
-- This IE shall be present if the Measurements to Activate IE has the first bit set to "1".--
	m4Configuration				M4Configuration				OPTIONAL,
-- This IE shall be present if the Measurements to Activate IE has the fourth bit set to "1".--
	m5Configuration				M5Configuration				OPTIONAL,
-- This IE shall be present if the Measurements to Activate IE has the fifth bit set to "1".--
	mDT-Location-Info			MDT-Location-Info			OPTIONAL,
	m6Configuration				M6Configuration				OPTIONAL,
-- This IE shall be present if the Measurements to Activate IE has the seventh bit set to "1".--
	m7Configuration				M7Configuration				OPTIONAL,
-- This IE shall be present if the Measurements to Activate IE has the eighth bit set to "1".--
	bluetoothMeasurementConfiguration			BluetoothMeasurementConfiguration			OPTIONAL,
	wLANMeasurementConfiguration				WLANMeasurementConfiguration				OPTIONAL,
	sensorMeasurementConfiguration				SensorMeasurementConfiguration				OPTIONAL,
	iE-Extensions				ProtocolExtensionContainer { { ImmediateMDT-NR-ExtIEs} } 	OPTIONAL,
	...
}

ImmediateMDT-NR-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ImplicitFormat	::= SEQUENCE	{
	dUFSlotformatIndex 			DUFSlotformatIndex,
	iE-Extensions		ProtocolExtensionContainer { { ImplicitFormat-ExtIEs } } OPTIONAL,
	...
}

ImplicitFormat-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


InitiatingCondition-FailureIndication ::= CHOICE {
	rRCReestab					RRCReestab-initiated,
	rRCSetup					RRCSetup-initiated,
	choice-extension				ProtocolIE-Single-Container { {InitiatingCondition-FailureIndication-ExtIEs} }
}

InitiatingCondition-FailureIndication-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

IntendedTDD-DL-ULConfiguration-NR ::= SEQUENCE {
	nrscs							NRSCS,
	nrCyclicPrefix					NRCyclicPrefix,
	nrDL-ULTransmissionPeriodicity	NRDL-ULTransmissionPeriodicity,
	slotConfiguration-List			SlotConfiguration-List,
	iE-Extensions					ProtocolExtensionContainer { {IntendedTDD-DL-ULConfiguration-NR-ExtIEs} }	OPTIONAL,
	...
}

IntendedTDD-DL-ULConfiguration-NR-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

InterfaceInstanceIndication ::= INTEGER (0..255, ...)


I-RNTI ::= CHOICE {
	i-RNTI-full			BIT STRING (SIZE(40)),
	i-RNTI-short		BIT STRING (SIZE(24)),
	choice-extension	ProtocolIE-Single-Container { {I-RNTI-ExtIEs} }
}

I-RNTI-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

IABAuthorizationStatus ::= ENUMERATED {
	authorized,
	not-authorized,
	...
}


-- J


-- K


-- L


Local-NG-RAN-Node-Identifier ::= CHOICE {
	full-I-RNTI-Profile-List					Full-I-RNTI-Profile-List,
	short-I-RNTI-Profile-List					Short-I-RNTI-Profile-List,
	choice-extension							ProtocolIE-Single-Container { { Local-NG-RAN-Node-Identifier-ExtIEs} }
}

Local-NG-RAN-Node-Identifier-ExtIEs XNAP-PROTOCOL-IES ::= {
	{ ID id-Full-and-Short-I-RNTI-Profile-List		CRITICALITY ignore	TYPE Full-and-Short-I-RNTI-Profile-List	PRESENCE mandatory},
	...
}

Full-and-Short-I-RNTI-Profile-List::= SEQUENCE {

	full-I-RNTI-Profile-List					Full-I-RNTI-Profile-List,
	short-I-RNTI-Profile-List					Short-I-RNTI-Profile-List,
	iE-Extensions								ProtocolExtensionContainer { { Full-and-Short-I-RNTI-Profile-List-ExtIEs} }	OPTIONAL,
	...
}

Full-and-Short-I-RNTI-Profile-List-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


Full-I-RNTI-Profile-List ::= CHOICE {
	full-I-RNTI-Profile-0	BIT STRING (SIZE (21)),
	full-I-RNTI-Profile-1	BIT STRING (SIZE (18)),
	full-I-RNTI-Profile-2	BIT STRING (SIZE (15)),
	full-I-RNTI-Profile-3	BIT STRING (SIZE (12)),
	choice-extension		ProtocolIE-Single-Container { { Full-I-RNTI-Profile-List-ExtIEs} }
}

Full-I-RNTI-Profile-List-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

Short-I-RNTI-Profile-List ::= CHOICE {
	short-I-RNTI-Profile-0	BIT STRING (SIZE (8)),
	short-I-RNTI-Profile-1	BIT STRING (SIZE (6)),
	choice-extension		ProtocolIE-Single-Container { { Short-I-RNTI-Profile-List-ExtIEs} }
}

Short-I-RNTI-Profile-List-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


LastVisitedCell-Item ::= CHOICE {
	nG-RAN-Cell						LastVisitedNGRANCellInformation,
	e-UTRAN-Cell					LastVisitedEUTRANCellInformation,
	uTRAN-Cell						LastVisitedUTRANCellInformation,
	gERAN-Cell						LastVisitedGERANCellInformation,
	choice-extension				ProtocolIE-Single-Container { { LastVisitedCell-Item-ExtIEs} }
}

LastVisitedCell-Item-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

LastVisitedEUTRANCellInformation ::= OCTET STRING

LastVisitedGERANCellInformation	::= OCTET STRING

LastVisitedNGRANCellInformation	::= OCTET STRING

LastVisitedUTRANCellInformation	::= OCTET STRING

LastVisitedPSCellInformation	::= OCTET STRING

LastVisitedPSCellList	::= SEQUENCE (SIZE(1..maxnoofPSCellsPerSN)) OF LastVisitedPSCellList-Item

LastVisitedPSCellList-Item	::= SEQUENCE {
	lastVisitedPSCellInformation		LastVisitedPSCellInformation,
	iE-Extensions		ProtocolExtensionContainer { { LastVisitedPSCellList-Item-ExtIEs} } OPTIONAL,
	...
}

LastVisitedPSCellList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


SCGUEHistoryInformation	::= SEQUENCE {
	lastVisitedPSCellList			LastVisitedPSCellList		OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { SCGUEHistoryInformation-ExtIEs} } OPTIONAL,
	...
}

SCGUEHistoryInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


LCID ::= INTEGER (1..32, ...)

Links-to-log ::= ENUMERATED {uplink, downlink, both-uplink-and-downlink, ...}


ListOfCells ::= SEQUENCE (SIZE(1..maxnoofCellsinAoI)) OF CellsinAoI-Item

CellsinAoI-Item ::= SEQUENCE {
	pLMN-Identity			PLMN-Identity,
	ng-ran-cell-id			NG-RAN-Cell-Identity,
	iE-Extensions			ProtocolExtensionContainer { {CellsinAoI-Item-ExtIEs} } OPTIONAL,
	...
}

CellsinAoI-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ListOfRANNodesinAoI ::= SEQUENCE (SIZE(1.. maxnoofRANNodesinAoI)) OF GlobalNG-RANNodesinAoI-Item

GlobalNG-RANNodesinAoI-Item ::= SEQUENCE {
	global-NG-RAN-Node-ID		GlobalNG-RANNode-ID,
	iE-Extensions		ProtocolExtensionContainer { {GlobalNG-RANNodesinAoI-Item-ExtIEs} } OPTIONAL,
	...
}

GlobalNG-RANNodesinAoI-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ListOfTAIsinAoI ::= SEQUENCE (SIZE(1..maxnoofTAIsinAoI)) OF TAIsinAoI-Item

TAIsinAoI-Item ::= SEQUENCE {
	pLMN-Identity		PLMN-Identity,
	tAC					TAC,
	iE-Extensions		ProtocolExtensionContainer { {TAIsinAoI-Item-ExtIEs} } OPTIONAL,
	...
}

TAIsinAoI-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

LocationInformationSNReporting ::= ENUMERATED {
	pSCell,
	...
}

LocationReportingInformation ::= SEQUENCE {
	eventType			EventType,
	reportArea			ReportArea,
	areaOfInterest		AreaOfInterestInformation			OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {LocationReportingInformation-ExtIEs} } OPTIONAL,
	...
}

LocationReportingInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	{ ID id-AdditionLocationInformation	CRITICALITY ignore	EXTENSION AdditionLocationInformation	PRESENCE optional},
	...
}

LoggedEventTriggeredConfig ::= SEQUENCE {
	eventTypeTrigger					EventTypeTrigger,
	iE-Extensions		ProtocolExtensionContainer { { LoggedEventTriggeredConfig-ExtIEs} } OPTIONAL,
	...
}

LoggedEventTriggeredConfig-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

LoggedMDT-NR ::= SEQUENCE {
	loggingInterval						LoggingInterval,
	loggingDuration						LoggingDuration,
	reportType							ReportType,
	bluetoothMeasurementConfiguration	BluetoothMeasurementConfiguration			OPTIONAL,
	wLANMeasurementConfiguration			WLANMeasurementConfiguration			OPTIONAL,
	sensorMeasurementConfiguration		SensorMeasurementConfiguration				OPTIONAL,
	areaScopeOfNeighCellsList			AreaScopeOfNeighCellsList					OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { {LoggedMDT-NR-ExtIEs} } OPTIONAL,
	...
}

LoggedMDT-NR-ExtIEs	XNAP-PROTOCOL-EXTENSION ::= {
	{ID id-earlyMeasurement		CRITICALITY ignore	EXTENSION EarlyMeasurement		PRESENCE optional		},
...
}

LoggingInterval ::= ENUMERATED { ms320, ms640, ms1280, ms2560, ms5120, ms10240, ms20480, ms30720, ms40960, ms61440, infinity,...}

LoggingDuration ::= ENUMERATED {m10, m20, m40, m60, m90, m120}

LowerLayerPresenceStatusChange ::= ENUMERATED {
	release-lower-layers,
	re-establish-lower-layers,
	...,
	suspend-lower-layers,
	resume-lower-layers
}

LTEA2XServicesAuthorized ::= SEQUENCE {
	aerialUE				AerialUE													OPTIONAL,
	aerialControllerUE		AerialControllerUE											OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { {LTEA2XServicesAuthorized-ExtIEs} }	OPTIONAL,
	...
}

LTEA2XServicesAuthorized-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

LTEV2XServicesAuthorized ::= SEQUENCE {
	vehicleUE			VehicleUE											OPTIONAL,
	pedestrianUE 		PedestrianUE										OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {LTEV2XServicesAuthorized-ExtIEs} }	OPTIONAL,
	...
}

LTEV2XServicesAuthorized-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


LTEUESidelinkAggregateMaximumBitRate ::= SEQUENCE {
	uESidelinkAggregateMaximumBitRate		BitRate,
	iE-Extensions					ProtocolExtensionContainer { {LTEUESidelinkAggregateMaximumBitRate-ExtIEs} } OPTIONAL,
	...
}

LTEUESidelinkAggregateMaximumBitRate-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- M

MaxNrofRS-IndexesToReport::= INTEGER (1..64, ...)

MBSCommServiceType ::= ENUMERATED {multicast, broadcast, ...}

MDTAlignmentInfo ::= CHOICE {	
	s-BasedMDT						S-BasedMDT,
	choice-extension				ProtocolIE-Single-Container { {MDTAlignmentInfo-ExtIEs} }
}


MDTAlignmentInfo-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

MeasCollectionEntityIPAddress ::= TransportLayerAddress


M1Configuration ::= SEQUENCE {
	m1reportingTrigger			M1ReportingTrigger,
	m1thresholdeventA2			M1ThresholdEventA2				OPTIONAL,
-- This IE shall be present if the Measurements to Activate IE has the first bit set to "1" and the M1 Reporting Trigger IE is set to "A2event-triggered" or to "A2event-triggered periodic".
	m1periodicReporting			M1PeriodicReporting				OPTIONAL,
-- This IE shall be present if the M1 Reporting Trigger IE is set to "periodic", or to "A2event-triggered periodic".
	iE-Extensions				ProtocolExtensionContainer { { M1Configuration-ExtIEs} } OPTIONAL,
	...
}

M1Configuration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ID id-BeamMeasurementIndicationM1				CRITICALITY ignore	EXTENSION BeamMeasurementIndicationM1					PRESENCE optional		}|
	{ID id-BeamMeasurementsReportConfiguration		CRITICALITY ignore	EXTENSION BeamMeasurementsReportConfiguration		PRESENCE conditional	},
-- This IE shall be present if the Include Beam Measurements Indication IE is set to "true".
	...
}


M1PeriodicReporting ::= SEQUENCE {
	reportInterval				ReportIntervalMDT,
	reportAmount				ReportAmountMDT,
	iE-Extensions				ProtocolExtensionContainer { { M1PeriodicReporting-ExtIEs} } OPTIONAL,
	...
}

M1PeriodicReporting-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ID id-ExtendedReportIntervalMDT		CRITICALITY ignore	EXTENSION ExtendedReportIntervalMDT		PRESENCE optional},
	...
}

M1ReportingTrigger ::= ENUMERATED{
	periodic,
	a2eventtriggered,
	a2eventtriggered-periodic,
	...
	
}

M1ThresholdEventA2 ::= SEQUENCE {
	measurementThreshold	MeasurementThresholdA2,
	iE-Extensions			ProtocolExtensionContainer { { M1ThresholdEventA2-ExtIEs} } OPTIONAL,
	...
}

M1ThresholdEventA2-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}



M4Configuration ::= SEQUENCE {
	m4period			M4period,
	m4-links-to-log		Links-to-log,
	iE-Extensions		ProtocolExtensionContainer { { M4Configuration-ExtIEs} } OPTIONAL,
	...
}

M4Configuration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-M4ReportAmount		CRITICALITY ignore	EXTENSION M4ReportAmountMDT 		PRESENCE optional		},
	...
}

M4ReportAmountMDT ::= ENUMERATED{r1, r2, r4, r8, r16, r32, r64, infinity, ...}

M4period ::= ENUMERATED {ms1024, ms2048, ms5120, ms10240, min1, ... }

M5Configuration ::= SEQUENCE {
	m5period			M5period,
	m5-links-to-log		Links-to-log,
	iE-Extensions		ProtocolExtensionContainer { { M5Configuration-ExtIEs} } OPTIONAL,
	...
}

M5Configuration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-M5ReportAmount		CRITICALITY ignore	EXTENSION M5ReportAmountMDT 		PRESENCE optional		},
	...
}

M5ReportAmountMDT ::= ENUMERATED{r1, r2, r4, r8, r16, r32, r64, infinity, ...}

M5period ::= ENUMERATED {ms1024, ms2048, ms5120, ms10240, min1, ... }

M6Configuration ::= SEQUENCE {
	m6report-Interval	M6report-Interval,
	m6-links-to-log		Links-to-log,
	iE-Extensions		ProtocolExtensionContainer { { M6Configuration-ExtIEs} } OPTIONAL,
	...
}

M6Configuration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-M6ReportAmount							CRITICALITY ignore	EXTENSION M6ReportAmountMDT 									PRESENCE optional}|
	{ ID id-ExcessPacketDelayThresholdConfiguration	CRITICALITY ignore	EXTENSION ExcessPacketDelayThresholdConfiguration		PRESENCE optional},
	...
}

M6ReportAmountMDT ::= ENUMERATED{r1, r2, r4, r8, r16, r32, r64, infinity, ...}

M6report-Interval ::= ENUMERATED { ms120, ms240, ms480, ms640, ms1024, ms2048, ms5120, ms10240, ms20480, ms40960, min1, min6, min12, min30,... }


M7Configuration ::= SEQUENCE {
	m7period			M7period,
	m7-links-to-log		Links-to-log,
	iE-Extensions		ProtocolExtensionContainer { { M7Configuration-ExtIEs} } OPTIONAL,
	...
}

M7Configuration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-M7ReportAmount		CRITICALITY ignore	EXTENSION M7ReportAmountMDT 	PRESENCE optional		},
	...
}

M7ReportAmountMDT ::= ENUMERATED{r1, r2, r4, r8, r16, r32, r64, infinity, ...}

M7period ::= INTEGER(1..60, ...)


MAC-I ::= BIT STRING (SIZE(16))


MaskedIMEISV	::= BIT STRING (SIZE(64))


MaxCHOpreparations ::= INTEGER (1..8, ...)


MaximumDataBurstVolume ::= INTEGER (0..4095, ..., 4096.. 2000000)


MaximumIPdatarate ::= SEQUENCE {
	maxIPrate-UL			MaxIPrate,
	iE-Extensions		ProtocolExtensionContainer { {MaximumIPdatarate-ExtIEs} }	OPTIONAL,
	...
}

MaximumIPdatarate-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
{ ID id-MaxIPrate-DL	CRITICALITY ignore	EXTENSION MaxIPrate	PRESENCE optional},
	...
}

MaxIPrate ::= ENUMERATED {
	bitrate64kbs,
	max-UErate,
	...
}


MBSFNControlRegionLength ::= INTEGER (0..3)


MBSFNSubframeAllocation-E-UTRA ::= CHOICE {
	oneframe				BIT STRING (SIZE(6)),
	fourframes				BIT STRING (SIZE(24)),
	choice-extension		ProtocolIE-Single-Container { {MBSFNSubframeAllocation-E-UTRA-ExtIEs} }
}

MBSFNSubframeAllocation-E-UTRA-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


MBSFNSubframeInfo-E-UTRA ::= SEQUENCE (SIZE(1..maxnoofMBSFNEUTRA)) OF MBSFNSubframeInfo-E-UTRA-Item


MBSFNSubframeInfo-E-UTRA-Item ::= SEQUENCE {
	radioframeAllocationPeriod		ENUMERATED{n1,n2,n4,n8,n16,n32,...},
	radioframeAllocationOffset		INTEGER (0..7, ...),
	subframeAllocation				MBSFNSubframeAllocation-E-UTRA,
	iE-Extensions					ProtocolExtensionContainer { {MBSFNSubframeInfo-E-UTRA-Item-ExtIEs} } OPTIONAL,
	...
}

MBSFNSubframeInfo-E-UTRA-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	...
}

MBS-FrequencySelectionArea-Identity ::= OCTET STRING (SIZE(3))

MBS-Area-Session-ID ::= INTEGER (0..65535, ...)

MBS-MappingandDataForwardingRequestInfofromSource ::= SEQUENCE (SIZE(1..maxnoofMRBs)) OF MBS-MappingandDataForwardingRequestInfofromSource-Item

MBS-MappingandDataForwardingRequestInfofromSource-Item ::= SEQUENCE {
	mRB-ID						MRB-ID,
	mBS-QoSFlow-List			MBS-QoSFlow-List,
	mRB-ProgressInformation		MRB-ProgressInformation				OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { { MBS-MappingandDataForwardingRequestInfofromSource-Item-ExtIEs} } OPTIONAL,
	...
}

MBS-MappingandDataForwardingRequestInfofromSource-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

MBS-DataForwarding-Indicator ::= ENUMERATED{mbs-only, ...}

MBS-DataForwardingResponseInfofromTarget ::= SEQUENCE (SIZE(1..maxnoofMRBs)) OF MBS-DataForwardingResponseInfofromTarget-Item

MBS-DataForwardingResponseInfofromTarget-Item ::= SEQUENCE {
	mRB-ID					MRB-ID,
	dlForwardingUPTNL		UPTransportLayerInformation,
	mRB-ProgressInformation	MRB-ProgressInformation					OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { { MBS-DataForwardingResponseInfofromTarget-Item-ExtIEs} } OPTIONAL,
	...
}

MBS-DataForwardingResponseInfofromTarget-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

MBS-QoSFlow-List ::= SEQUENCE (SIZE(1..maxnoofMBSQoSFlows)) OF QoSFlowIdentifier

MBS-QoSFlowsToAdd-List ::= SEQUENCE (SIZE(1..maxnoofMBSQoSFlows)) OF MBS-QoSFlowsToAdd-Item

MBS-QoSFlowsToAdd-Item ::= SEQUENCE {
	mBS-QosFlowIdentifier				QoSFlowIdentifier,
	mBS-QosFlowLevelQosParameters		QoSFlowLevelQoSParameters,
	iE-Extensions					ProtocolExtensionContainer { { MBS-QoSFlowsToAdd-Item-ExtIEs} } OPTIONAL,
	...
}

MBS-QoSFlowsToAdd-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

MBS-ServiceArea ::= CHOICE {
	locationindependent		MBS-ServiceAreaInformation,
	locationdependent		MBS-ServiceAreaInformationList,
	choice-extension		ProtocolIE-Single-Container { {MBS-ServiceArea-ExtIEs} }
}

MBS-ServiceArea-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

MBS-ServiceAreaCell-List ::= SEQUENCE (SIZE(1.. maxnoofCellsforMBS)) OF NR-CGI

MBS-ServiceAreaInformation ::= SEQUENCE {
	mBS-ServiceAreaCell-List		MBS-ServiceAreaCell-List													OPTIONAL,
	mBS-ServiceAreaTAI-List			MBS-ServiceAreaTAI-List														OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {MBS-ServiceAreaInformation-ExtIEs} }		OPTIONAL,
	...
}

MBS-ServiceAreaInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

MBS-ServiceAreaInformationList ::= SEQUENCE (SIZE(1..maxnoofMBSServiceAreaInformation)) OF MBS-ServiceAreaInformation-Item

MBS-ServiceAreaInformation-Item ::= SEQUENCE {	mBS-Area-Session-ID			MBS-Area-Session-ID,
	mBS-ServiceAreaInformation	MBS-ServiceAreaInformation,
	iE-Extensions			ProtocolExtensionContainer { { MBS-ServiceAreaInformation-Item-ExtIEs} } OPTIONAL,
	...
}

MBS-ServiceAreaInformation-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

MBS-ServiceAreaTAI-List ::= SEQUENCE (SIZE(1.. maxnoofTAIforMBS)) OF MBS-ServiceAreaTAI-Item

MBS-ServiceAreaTAI-Item ::= SEQUENCE {
	plmn-ID					PLMN-Identity,
	tAC						TAC,
	iE-Extensions			ProtocolExtensionContainer { {MBS-ServiceAreaTAI-Item-ExtIEs} } OPTIONAL,
	...
}

MBS-ServiceAreaTAI-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


MBS-Session-ID ::= SEQUENCE {
	tMGI							TMGI,
	nID								NID																OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {MBS-Session-ID-ExtIEs} } 			OPTIONAL,
	...
}

MBS-Session-ID-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

MBS-AssistanceInformation ::= ENUMERATED {true, ...}

MBS-SessionAssociatedInformation ::= SEQUENCE (SIZE(1..maxnoofAssociatedMBSSessions)) OF MBS-SessionAssociatedInformation-Item

MBS-SessionAssociatedInformation-Item ::= SEQUENCE {
	mBS-Session-ID				MBS-Session-ID,
	associated-QoSFlowInfo-List	Associated-QoSFlowInfo-List,
	iE-Extensions				ProtocolExtensionContainer { { MBS-SessionAssociatedInformation-Item-ExtIEs} } OPTIONAL,
	...
}

MBS-SessionAssociatedInformation-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}
MBS-SessionInformation-List ::= SEQUENCE (SIZE(1..maxnoofMBSSessions)) OF MBS-SessionInformation-Item

MBS-SessionInformation-Item ::= SEQUENCE {
	mBS-Session-ID			MBS-Session-ID,
	mBS-Area-Session-ID		MBS-Area-Session-ID														OPTIONAL,
	active-MBS-SessioInformation			Active-MBS-SessionInformation							OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { { MBS-SessionInformation-Item-ExtIEs} }	OPTIONAL,
	...
}

MBS-SessionInformation-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-MBS-AssistanceInformation			CRITICALITY ignore			EXTENSION MBS-AssistanceInformation		PRESENCE	optional	},
	...
}


MBS-SessionInformationResponse-List ::= SEQUENCE (SIZE(1..maxnoofMBSSessions)) OF MBS-SessionInformationResponse-Item

MBS-SessionInformationResponse-Item ::= SEQUENCE {
	mBS-Session-ID						MBS-Session-ID,
	mBS-DataForwardingResponseInfofromTarget		MBS-DataForwardingResponseInfofromTarget					OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { { MBS-SessionInformationResponse-Item-ExtIEs} } 	OPTIONAL,
	...
}

MBS-SessionInformationResponse-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

MRB-ID ::= INTEGER (1..512, ...)

MRB-ProgressInformation ::= CHOICE {
	pdcp-SN12			INTEGER (0..4095),
	pdcp-SN18			INTEGER (0..262143),
	choice-extension		ProtocolIE-Single-Container { { MRB-ProgressInformation-ExtIEs} }
}

MRB-ProgressInformation-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

MDT-Activation 	::= ENUMERATED {
	immediate-MDT-only,
	immediate-MDT-and-Trace,
	logged-MDT-only,
	...
}

MDT-Configuration ::= SEQUENCE {
	mDT-Configuration-NR		MDT-Configuration-NR		OPTIONAL,
	mDT-Configuration-EUTRA		MDT-Configuration-EUTRA		OPTIONAL,
iE-Extensions		ProtocolExtensionContainer { { MDT-Configuration-ExtIEs} } OPTIONAL,
	...
}
MDT-Configuration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
{ ID id-MN-only-MDT-collection	CRITICALITY ignore		EXTENSION MN-only-MDT-collection			PRESENCE optional },
	...
}

MN-only-MDT-collection ::= ENUMERATED {
	mN-Only,
	...
}

MDT-Configuration-NR ::= SEQUENCE {
	mdt-Activation				MDT-Activation,
	areaScopeOfMDT-NR			AreaScopeOfMDT-NR	OPTIONAL,
	mDTMode-NR					MDTMode-NR,
	signallingBasedMDTPLMNList	MDTPLMNList			OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { MDT-Configuration-NR-ExtIEs} } OPTIONAL,
	...
}
MDT-Configuration-NR-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ID id-PNI-NPN-AreaScopeofMDT		CRITICALITY ignore	EXTENSION PNI-NPN-AreaScopeofMDT		PRESENCE optional		},
	...
}

MDT-Configuration-EUTRA ::= SEQUENCE {
	mdt-Activation				MDT-Activation,
	areaScopeOfMDT-EUTRA		AreaScopeOfMDT-EUTRA	OPTIONAL,
	mDTMode-EUTRA				MDTMode-EUTRA,
	signallingBasedMDTPLMNList	MDTPLMNList,
	iE-Extensions		ProtocolExtensionContainer { { MDT-Configuration-EUTRA-ExtIEs} } OPTIONAL,
	...
}
MDT-Configuration-EUTRA-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


MDT-Location-Info ::= BIT STRING (SIZE (8))


MDTPLMNList ::= SEQUENCE (SIZE(1..maxnoofMDTPLMNs)) OF PLMN-Identity

MDTPLMNModificationList ::= SEQUENCE (SIZE(0..maxnoofMDTPLMNs)) OF PLMN-Identity

MDTMode-NR ::= CHOICE {
	immediateMDT				ImmediateMDT-NR,
	loggedMDT					LoggedMDT-NR,
	...,
	mDTMode-NR-Extension			MDTMode-NR-Extension
}

MDTMode-NR-Extension ::= ProtocolIE-Single-Container {{ MDTMode-NR-ExtensionIE }}

MDTMode-NR-ExtensionIE XNAP-PROTOCOL-IES ::= {
	...
}

MDTMode-EUTRA ::= OCTET STRING


MeasObjectContainer ::= OCTET STRING

MeasurementsToActivate ::= BIT STRING (SIZE (8))

MeasurementThresholdA2 ::= CHOICE {
	threshold-RSRP				Threshold-RSRP,
	threshold-RSRQ				Threshold-RSRQ,
	threshold-SINR				Threshold-SINR,
	choice-extension	ProtocolIE-Single-Container { { MeasurementThresholdA2-ExtIEs} }
}

MeasurementThresholdA2-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


Measurement-ID	::= INTEGER (1..4095,...)


MIMOPRBusageInformation ::= SEQUENCE {
	dl-GBR-PRB-usage-for-MIMO					DL-GBR-PRB-usage-for-MIMO,
	ul-GBR-PRB-usage-for-MIMO					UL-GBR-PRB-usage-for-MIMO,
	dl-non-GBR-PRB-usage-for-MIMO				DL-non-GBR-PRB-usage-for-MIMO,
	ul-non-GBR-PRB-usage-for-MIMO				UL-non-GBR-PRB-usage-for-MIMO,
	dl-Total-PRB-usage-for-MIMO					DL-Total-PRB-usage-for-MIMO,
	ul-Total-PRB-usage-for-MIMO					UL-Total-PRB-usage-for-MIMO,
	iE-Extensions							ProtocolExtensionContainer { { MIMOPRBusageInformation-ExtIEs} }	OPTIONAL,
	...
}

MIMOPRBusageInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

MobileIAB-AuthorizationStatus ::= ENUMERATED {authorized, not-authorized,...}

MobileIABCell ::= ENUMERATED {
	true,
	...
}

MobilityInformation	::= BIT STRING (SIZE(32))

MobilityParametersModificationRange ::= SEQUENCE {
	handoverTriggerChangeLowerLimit		INTEGER (-20..20),
	handoverTriggerChangeUpperLimit		INTEGER (-20..20),
	...
}

MobilityParametersInformation ::= SEQUENCE {
	handoverTriggerChange			INTEGER (-20..20),
	...
}


MobilityRestrictionList ::= SEQUENCE {
	serving-PLMN						PLMN-Identity,
	equivalent-PLMNs					SEQUENCE (SIZE(1..maxnoofEPLMNs)) OF PLMN-Identity			OPTIONAL,
	rat-Restrictions					RAT-RestrictionsList										OPTIONAL,
	forbiddenAreaInformation			ForbiddenAreaList											OPTIONAL,
	serviceAreaInformation				ServiceAreaList												OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {MobilityRestrictionList-ExtIEs} }				OPTIONAL,
	...
}

MobilityRestrictionList-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
{ ID id-LastE-UTRANPLMNIdentity					CRITICALITY ignore	EXTENSION PLMN-Identity									PRESENCE optional		}|
{ ID id-CNTypeRestrictionsForServing			CRITICALITY ignore	EXTENSION CNTypeRestrictionsForServing				PRESENCE optional		}|
{ ID id-CNTypeRestrictionsForEquivalent			CRITICALITY ignore	EXTENSION CNTypeRestrictionsForEquivalent			PRESENCE optional		}|
{ ID id-NPNMobilityInformation					CRITICALITY reject	EXTENSION NPNMobilityInformation						PRESENCE optional		},
	...
}

CNTypeRestrictionsForEquivalent ::= SEQUENCE (SIZE(1..maxnoofEPLMNs)) OF CNTypeRestrictionsForEquivalentItem

CNTypeRestrictionsForEquivalentItem ::= SEQUENCE {
	plmn-Identity						PLMN-Identity,
	cn-Type								ENUMERATED {epc-forbidden, fiveGC-forbidden, ...},
	iE-Extensions						ProtocolExtensionContainer { {CNTypeRestrictionsForEquivalentItem-ExtIEs} }				OPTIONAL,
	...
}

CNTypeRestrictionsForEquivalentItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	...
}

CNTypeRestrictionsForServing ::= ENUMERATED {
	epc-forbidden,
	...
}

RAT-RestrictionsList ::= SEQUENCE (SIZE(1..maxnoofPLMNs)) OF RAT-RestrictionsItem


RAT-RestrictionsItem ::= SEQUENCE {
	plmn-Identity					PLMN-Identity,
	rat-RestrictionInformation		RAT-RestrictionInformation,
	iE-Extensions		ProtocolExtensionContainer { {RAT-RestrictionsItem-ExtIEs} } OPTIONAL,
	...
}

RAT-RestrictionsItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	{ ID id-ExtendedRATRestrictionInformation	CRITICALITY ignore	EXTENSION ExtendedRATRestrictionInformation		PRESENCE optional},
	...
}


RAT-RestrictionInformation ::= BIT STRING {e-UTRA (0),nR (1), nR-unlicensed (2), nR-LEO (3), nR-MEO (4), nR-GEO (5), nR-OTHERSAT (6)} (SIZE(8, ...))


ForbiddenAreaList ::= SEQUENCE (SIZE(1..maxnoofPLMNs)) OF ForbiddenAreaItem


ForbiddenAreaItem ::= SEQUENCE {
	plmn-Identity		PLMN-Identity,
	forbidden-TACs		SEQUENCE (SIZE(1..maxnoofForbiddenTACs)) OF TAC,
	iE-Extensions		ProtocolExtensionContainer { {ForbiddenAreaItem-ExtIEs} } OPTIONAL,
	...
}

ForbiddenAreaItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	...
}


ServiceAreaList ::= SEQUENCE (SIZE(1..maxnoofPLMNs)) OF ServiceAreaItem


ServiceAreaItem ::= SEQUENCE {
	plmn-Identity						PLMN-Identity,
	allowed-TACs-ServiceArea			SEQUENCE (SIZE(1..maxnoofAllowedAreas)) OF TAC		OPTIONAL,
	not-allowed-TACs-ServiceArea		SEQUENCE (SIZE(1..maxnoofAllowedAreas)) OF TAC		OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {ServiceAreaItem-ExtIEs} }				OPTIONAL,
	...
}

ServiceAreaItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	...
}

MR-DC-ResourceCoordinationInfo ::= SEQUENCE {
		ng-RAN-Node-ResourceCoordinationInfo			NG-RAN-Node-ResourceCoordinationInfo,
		iE-Extension											ProtocolExtensionContainer { {MR-DC-ResourceCoordinationInfo-ExtIEs}}	OPTIONAL,
		...
}

MR-DC-ResourceCoordinationInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
...
}

NG-RAN-Node-ResourceCoordinationInfo ::= CHOICE {
		eutra-resource-coordination-info					E-UTRA-ResourceCoordinationInfo,
		nr-resource-coordination-info						NR-ResourceCoordinationInfo
}

E-UTRA-ResourceCoordinationInfo ::= SEQUENCE {
		e-utra-cell												E-UTRA-CGI,
		ul-coordination-info									BIT STRING (SIZE (6..4400)),
		dl-coordination-info									BIT STRING (SIZE (6..4400))	OPTIONAL,
		nr-cell													NR-CGI	OPTIONAL,
		e-utra-coordination-assistance-info				E-UTRA-CoordinationAssistanceInfo	OPTIONAL,
		iE-Extension			ProtocolExtensionContainer { {E-UTRA-ResourceCoordinationInfo-ExtIEs} }	OPTIONAL,
	...
}

E-UTRA-ResourceCoordinationInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

E-UTRA-CoordinationAssistanceInfo ::= ENUMERATED {coordination-not-required, ...}

NR-ResourceCoordinationInfo ::= SEQUENCE {
		nr-cell													NR-CGI,
		ul-coordination-info									BIT STRING (SIZE (6..4400)),
		dl-coordination-info									BIT STRING (SIZE (6..4400))	OPTIONAL,
		e-utra-cell												E-UTRA-CGI	OPTIONAL,
		nr-coordination-assistance-info					NR-CoordinationAssistanceInfo		OPTIONAL,
		iE-Extension			ProtocolExtensionContainer { {NR-ResourceCoordinationInfo-ExtIEs} } 	OPTIONAL,
	...
}

NR-ResourceCoordinationInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


NR-CoordinationAssistanceInfo ::= ENUMERATED {coordination-not-required, ...}

MessageOversizeNotification ::= SEQUENCE {
	maximumCellListSize								MaximumCellListSize,
	iE-Extension				ProtocolExtensionContainer { {MessageOversizeNotification-ExtIEs}}	OPTIONAL,
	...
}

MessageOversizeNotification-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

MaximumCellListSize ::= INTEGER(1..16384, ...)

MT-SDT-Information ::= SEQUENCE {
	mT-SDT-Indicator			MT-SDT-Indicator,
	mT-SDT-DataSize			MT-SDT-DataSize,
	iE-Extensions				ProtocolExtensionContainer { { MT-SDT-Information-ExtIEs} } OPTIONAL,
	...
}

MT-SDT-Information-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

MT-SDT-DataSize	::= INTEGER (1..96000, ...)

MT-SDT-Indicator ::= ENUMERATED {true, ...}

MultiplexingInfo 	::=	SEQUENCE{
	iAB-MT-Cell-List 	IAB-MT-Cell-List,
	iE-Extensions		ProtocolExtensionContainer { {MultiplexingInfo-ExtIEs} } OPTIONAL,
	...
}

MultiplexingInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


MeasuredUETrajectory ::= SEQUENCE (SIZE(1..maxnoofCellsTrajectory)) OF MeasuredUETrajectory-Item

MeasuredUETrajectory-Item ::= SEQUENCE{
	measuredtrajectoryCellInfo		MeasuredTrajectoryCellInfo,
	iE-Extensions					ProtocolExtensionContainer { { MeasuredUETrajectory-Item-ExtIEs} }	OPTIONAL,
	...
}

MeasuredUETrajectory-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


MeasuredTrajectoryCellInfo ::= CHOICE {
	nG-RAN-Cell						MeasuredTrajectoryNGRANCellInfo,
	choice-extension				ProtocolIE-Single-Container { { MeasuredTrajectoryCellInfo-ExtIEs} }
}

MeasuredTrajectoryCellInfo-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

MeasuredTrajectoryNGRANCellInfo ::= SEQUENCE {
	globalNG-RANCell-ID				GlobalNG-RANCell-ID,
	timeUEStaysInCell				INTEGER (0..4095),
	iE-Extensions					ProtocolExtensionContainer { { MeasuredTrajectoryNGRANCellInfo-ExtIEs} }	OPTIONAL,
	...
}

MeasuredTrajectoryNGRANCellInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- N

N6JitterInformation ::= SEQUENCE {
	n6JitterLowerBound		INTEGER (-127..127),
	n6JitterUpperBound		INTEGER (-127..127),
	iE-Extensions			ProtocolExtensionContainer { { N6JitterInformationExtIEs } }	OPTIONAL,
	...
}
N6JitterInformationExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

NACellResourceConfigurationList ::= SEQUENCE (SIZE(1..maxnoofHSNASlots)) OF NACellResourceConfiguration-Item

NACellResourceConfiguration-Item ::= SEQUENCE {
	nAdownlink			ENUMERATED {true, false, ...}	OPTIONAL,
	nAuplink			ENUMERATED {true, false, ...}	OPTIONAL,
	nAflexible			ENUMERATED {true, false, ...}	OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { NACellResourceConfiguration-Item-ExtIEs} } OPTIONAL,
	...
}

NACellResourceConfiguration-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


NBIoT-UL-DL-AlignmentOffset ::= ENUMERATED {
	khz-7dot5,
	khz0,
	khz7dot5,
	...
}
NE-DC-TDM-Pattern ::= SEQUENCE {
		subframeAssignment			ENUMERATED {sa0,sa1,sa2,sa3,sa4,sa5,sa6},
		harqOffset					INTEGER (0..9),
		iE-Extension				ProtocolExtensionContainer { {NE-DC-TDM-Pattern-ExtIEs}}	OPTIONAL,
		...
}

NE-DC-TDM-Pattern-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
...
}

NeighbourInformation-E-UTRA ::= SEQUENCE (SIZE(1..maxnoofNeighbours)) OF NeighbourInformation-E-UTRA-Item

NeighbourInformation-E-UTRA-Item ::= SEQUENCE {
	e-utra-PCI			E-UTRAPCI,
	e-utra-cgi			E-UTRA-CGI,
	earfcn				E-UTRAARFCN,
	tac					TAC,
	ranac				RANAC																		OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {NeighbourInformation-E-UTRA-Item-ExtIEs} } 	OPTIONAL,
	...
}

NeighbourInformation-E-UTRA-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	...
}


NeighbourInformation-NR ::= SEQUENCE (SIZE(1..maxnoofNeighbours)) OF NeighbourInformation-NR-Item

NeighbourInformation-NR-Item ::= SEQUENCE {
	nr-PCI								NRPCI,
	nr-cgi								NR-CGI,
	tac									TAC,
	ranac								RANAC														OPTIONAL,
	nr-mode-info						NeighbourInformation-NR-ModeInfo,
	connectivitySupport					Connectivity-Support,
	measurementTimingConfiguration		OCTET STRING,
	iE-Extensions			ProtocolExtensionContainer { {NeighbourInformation-NR-Item-ExtIEs} } 	OPTIONAL,
	...
}

NeighbourInformation-NR-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::={
	{ ID id-MobileIABCell						CRITICALITY ignore	EXTENSION MobileIABCell	PRESENCE optional},
	...
}


NeighbourInformation-NR-ModeInfo ::= CHOICE {
	fdd-info				NeighbourInformation-NR-ModeFDDInfo,
	tdd-info				NeighbourInformation-NR-ModeTDDInfo,
	choice-extension		ProtocolIE-Single-Container { {NeighbourInformation-NR-ModeInfo-ExtIEs} }
}

NeighbourInformation-NR-ModeInfo-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


NeighbourInformation-NR-ModeFDDInfo ::= SEQUENCE {
	ul-NR-FreqInfo		NRFrequencyInfo,
	dl-NR-FequInfo		NRFrequencyInfo,
	ie-Extensions		ProtocolExtensionContainer { {NeighbourInformation-NR-ModeFDDInfo-ExtIEs} } OPTIONAL,
	...
}

NeighbourInformation-NR-ModeFDDInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


NeighbourInformation-NR-ModeTDDInfo ::= SEQUENCE {
	nr-FreqInfo			NRFrequencyInfo,
	ie-Extensions		ProtocolExtensionContainer { {NeighbourInformation-NR-ModeTDDInfo-ExtIEs} } OPTIONAL,
	...
}

NeighbourInformation-NR-ModeTDDInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


Neighbour-NG-RAN-Node-List ::= SEQUENCE (SIZE(0..maxnoofNeighbour-NG-RAN-Nodes)) OF Neighbour-NG-RAN-Node-Item

Neighbour-NG-RAN-Node-Item ::= SEQUENCE {
	globalNG-RANNodeID				GlobalNG-RANNode-ID,
	local-NG-RAN-Node-Identifier	Local-NG-RAN-Node-Identifier,
	ie-Extensions		ProtocolExtensionContainer { {Neighbour-NG-RAN-Node-Item-ExtIEs} } OPTIONAL,
	...
}

Neighbour-NG-RAN-Node-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


NID	::= BIT STRING (SIZE(44))


NRCarrierList ::= SEQUENCE (SIZE(1..maxnoofNRSCSs)) OF NRCarrierItem

NRCarrierItem ::= SEQUENCE {
	carrierSCS					NRSCS,
	offsetToCarrier				INTEGER (0..2199, ...),
	carrierBandwidth			INTEGER (0..maxnoofPhysicalResourceBlocks, ...),
	iE-Extension		ProtocolExtensionContainer { {NRCarrierItem-ExtIEs} } 		OPTIONAL,
	...
}

NRCarrierItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

NRCellPRACHConfig ::= OCTET STRING


NG-RAN-Cell-Identity ::= CHOICE {
	nr						NR-Cell-Identity,
	e-utra					E-UTRA-Cell-Identity,
	choice-extension		ProtocolIE-Single-Container { {NG-RAN-Cell-Identity-ExtIEs} }
}

NG-RAN-Cell-Identity-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


NG-RAN-CellPCI ::= CHOICE {
	nr					NRPCI,
	e-utra				E-UTRAPCI,
	choice-extension	ProtocolIE-Single-Container { {NG-RAN-CellPCI-ExtIEs} }
}

NG-RAN-CellPCI-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


NG-RANnode2SSBOffsetsModificationRange ::= SEQUENCE (SIZE(1..maxnoofSSBAreas)) OF SSBOffsetModificationRange

NG-RANnodeUEXnAPID ::= INTEGER (0.. 4294967295)


NumberofActiveUEs::= INTEGER(0..16777215, ...)


NodeAssociatedInfoResult ::= SEQUENCE {
	energyCost				EnergyCost			OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { { NodeAssociatedInfoResult-ExtIEs} } OPTIONAL,
	...
}

NodeAssociatedInfoResult-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


NodeMeasurementInitiationResult-List ::= SEQUENCE (SIZE(1..maxFailedMeasPerNode)) OF NodeMeasurementInitiationResult-Item


NodeMeasurementInitiationResult-Item ::= SEQUENCE {
	nodemeasurementFailedReportCharacteristics		BIT STRING(SIZE(32)),
	cause											Cause,
	iE-Extensions									ProtocolExtensionContainer { { NodeMeasurementInitiationResult-Item-ExtIEs} } OPTIONAL,
	...
}

NodeMeasurementInitiationResult-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


NoofRRCConnections ::= INTEGER (1..65536,...)


NonDynamic5QIDescriptor ::= SEQUENCE {
	fiveQI						FiveQI,
	priorityLevelQoS			PriorityLevelQoS													OPTIONAL,
	averagingWindow				AveragingWindow														OPTIONAL,
	maximumDataBurstVolume		MaximumDataBurstVolume 												OPTIONAL,
	iE-Extension				ProtocolExtensionContainer { {NonDynamic5QIDescriptor-ExtIEs } }	OPTIONAL,
	...
}

NonDynamic5QIDescriptor-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-CNPacketDelayBudgetDownlink		CRITICALITY ignore	EXTENSION ExtendedPacketDelayBudget	PRESENCE optional}|
	{ ID id-CNPacketDelayBudgetUplink		CRITICALITY ignore	EXTENSION ExtendedPacketDelayBudget	PRESENCE optional},
	...
}


NRARFCN	::= INTEGER (0.. maxNRARFCN)


NG-eNB-RadioResourceStatus	::= SEQUENCE {
	dL-GBR-PRB-usage							DL-GBR-PRB-usage,
	uL-GBR-PRB-usage							UL-GBR-PRB-usage,
	dL-non-GBR-PRB-usage						DL-non-GBR-PRB-usage,
	uL-non-GBR-PRB-usage						UL-non-GBR-PRB-usage,
	dL-Total-PRB-usage							DL-Total-PRB-usage,
	uL-Total-PRB-usage							UL-Total-PRB-usage,
	iE-Extensions							ProtocolExtensionContainer { { NG-eNB-RadioResourceStatus-ExtIEs} } OPTIONAL,
	...
}

NG-eNB-RadioResourceStatus-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-DL-scheduling-PDCCH-CCE-usage		CRITICALITY ignore	EXTENSION DL-scheduling-PDCCH-CCE-usage	PRESENCE optional}|
	{ ID id-UL-scheduling-PDCCH-CCE-usage		CRITICALITY ignore	EXTENSION UL-scheduling-PDCCH-CCE-usage	PRESENCE optional},
	...
}

DL-scheduling-PDCCH-CCE-usage ::= INTEGER (0.. 100)
UL-scheduling-PDCCH-CCE-usage ::= INTEGER (0.. 100)


TNLCapacityIndicator ::= SEQUENCE {
	dLTNLOfferedCapacity					OfferedCapacity,
	dLTNLAvailableCapacity					AvailableCapacity,
	uLTNLOfferedCapacity					OfferedCapacity,
	uLTNLAvailableCapacity					AvailableCapacity,
	iE-Extensions							ProtocolExtensionContainer { { TNLCapacityIndicator-ExtIEs} } OPTIONAL,
	...
}

TNLCapacityIndicator-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

Non-F1-TerminatingTopologyBHInformation	::= SEQUENCE {
	nonF1TerminatingBHInformation-List		NonF1TerminatingBHInformation-List,
	bAPControlPDURLCCH-List					BAPControlPDURLCCH-List			OPTIONAL,
	iE-Extensions							ProtocolExtensionContainer { {Non-F1-TerminatingTopologyBHInformation-ExtIEs} }	OPTIONAL,
	...
}

Non-F1-TerminatingTopologyBHInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

NonF1TerminatingBHInformation-List ::= SEQUENCE (SIZE(1..maxnoofBHInfo)) OF NonF1TerminatingBHInformation-Item

NonF1TerminatingBHInformation-Item ::= SEQUENCE {
	bHInfoIndex						BHInfoIndex,
	dlNon-F1TerminatingBHInfo		DLNonF1Terminating-BHInfo		OPTIONAL,
	ulNon-F1TerminatingBHInfo		ULNonF1Terminating-BHInfo		OPTIONAL,
	iE-Extension			ProtocolExtensionContainer { { NonF1TerminatingBHInformation-Item-ExtIEs} }	OPTIONAL,
	...
}

NonF1TerminatingBHInformation-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


NonUPTraffic ::= CHOICE {
	nonUPTrafficType				NonUPTrafficType,
	controlPlaneTrafficType			ControlPlaneTrafficType,
	choice-extension				ProtocolIE-Single-Container { { NonUPTraffic-ExtIEs} }
}

NonUPTraffic-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

NonUPTrafficType ::= ENUMERATED {ueassociatedf1ap, nonueassociatedf1ap, nonf1, ...}

NoPDUSessionIndication	::= ENUMERATED {true, ...}

NPN-Broadcast-Information ::= CHOICE {
	snpn-Information					NPN-Broadcast-Information-SNPN,
	pni-npn-Information					NPN-Broadcast-Information-PNI-NPN,
	choice-extension					ProtocolIE-Single-Container { {NPN-Broadcast-Information-ExtIEs} }
}

NPN-Broadcast-Information-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

NPN-Broadcast-Information-SNPN ::= SEQUENCE {
	broadcastSNPNID-List		BroadcastSNPNID-List,
	iE-Extension				ProtocolExtensionContainer { {NPN-Broadcast-Information-SNPN-ExtIEs} }	OPTIONAL,
	...
}

NPN-Broadcast-Information-SNPN-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}
NPN-Broadcast-Information-PNI-NPN ::= SEQUENCE {
	broadcastPNI-NPN-ID-Information		BroadcastPNI-NPN-ID-Information,
	iE-Extension						ProtocolExtensionContainer { {NPN-Broadcast-Information-PNI-NPN-ExtIEs} }	OPTIONAL,
	...
}

NPN-Broadcast-Information-PNI-NPN-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

NPNMobilityInformation::= CHOICE {
	snpn-mobility-information			NPNMobilityInformation-SNPN,
	pni-npn-mobility-information		NPNMobilityInformation-PNI-NPN,
	choice-extension					ProtocolIE-Single-Container { {NPNMobilityInformation-ExtIEs} }
}

NPNMobilityInformation-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

NPNMobilityInformation-SNPN ::= SEQUENCE {
	serving-NID					NID,
	iE-Extension				ProtocolExtensionContainer { {NPNMobilityInformation-SNPN-ExtIEs} }	OPTIONAL,
	...
}

NPNMobilityInformation-SNPN-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-EquivalentSNPNs		CRITICALITY reject	EXTENSION EquivalentSNPNs	PRESENCE optional},
	...
}

NPNMobilityInformation-PNI-NPN ::= SEQUENCE {
	allowedPNI-NPN-ID-List		AllowedPNI-NPN-ID-List,
	iE-Extension				ProtocolExtensionContainer { {NPNMobilityInformation-PNI-NPN-ExtIEs} }	OPTIONAL,
	...
}

NPNMobilityInformation-PNI-NPN-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


NPNPagingAssistanceInformation ::= CHOICE {
	pni-npn-Information					NPNPagingAssistanceInformation-PNI-NPN,
	choice-extension					ProtocolIE-Single-Container { {NPNPagingAssistanceInformation-ExtIEs} }
}

NPNPagingAssistanceInformation-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

NPNPagingAssistanceInformation-PNI-NPN ::= SEQUENCE {
	allowedPNI-NPN-ID-List			AllowedPNI-NPN-ID-List,
	iE-Extension					ProtocolExtensionContainer { {NPNPagingAssistanceInformation-PNI-NPN-ExtIEs} }	OPTIONAL,
	...
}

NPNPagingAssistanceInformation-PNI-NPN-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


NPN-Support ::= CHOICE {
	sNPN					NPN-Support-SNPN,
	choice-Extensions		ProtocolIE-Single-Container { {NPN-Support-ExtIEs} }
}

NPN-Support-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

NPN-Support-SNPN ::= SEQUENCE {
	nid					NID,
	ie-Extension		ProtocolExtensionContainer { {NPN-Support-SNPN-ExtIEs} }	OPTIONAL,
	...
}

NPN-Support-SNPN-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

NPRACHConfiguration::= SEQUENCE {
	fdd-or-tdd				CHOICE {
		fdd			NPRACHConfiguration-FDD,
		tdd			NPRACHConfiguration-TDD,
		choice-extension		ProtocolIE-Single-Container { { FDD-or-TDD-in-NPRACHConfiguration-Choice-ExtIEs} }
	},	
	iE-Extensions						ProtocolExtensionContainer { { NPRACHConfiguration-ExtIEs} }	OPTIONAL,
	...
}

NPRACHConfiguration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

FDD-or-TDD-in-NPRACHConfiguration-Choice-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

NPRACHConfiguration-FDD::= SEQUENCE {
	nprach-CP-length						NPRACH-CP-Length,
	anchorCarrier-NPRACHConfig						OCTET STRING,
	anchorCarrier-EDT-NPRACHConfig 					OCTET STRING 						OPTIONAL,
	anchorCarrier-Format2-NPRACHConfig				OCTET STRING						OPTIONAL,
	anchorCarrier-Format2-EDT-NPRACHConfig			OCTET STRING 						OPTIONAL,
	non-anchorCarrier-NPRACHConfig					OCTET STRING 						OPTIONAL,
	non-anchorCarrier-Format2-NPRACHConfig			OCTET STRING 						OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { NPRACHConfiguration-FDD-ExtIEs} }	OPTIONAL,
	...
}

NPRACHConfiguration-FDD-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

NPRACHConfiguration-TDD::= SEQUENCE {
	nprach-preambleFormat							NPRACH-preambleFormat,
	anchorCarrier-NPRACHConfigTDD					OCTET STRING,
	non-anchorCarrierFequencyConfiglist				Non-AnchorCarrierFrequencylist 		OPTIONAL,
	non-anchorCarrier-NPRACHConfigTDD				OCTET STRING 						OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { NPRACHConfiguration-TDD-ExtIEs} }	OPTIONAL,

...
}

NPRACHConfiguration-TDD-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

NPRACH-CP-Length::=		ENUMERATED {
	us66dot7,
	us266dot7,
	...
}

NPRACH-preambleFormat::= 	ENUMERATED {fmt0,fmt1,fmt2,fmt0a,fmt1a,...}

Non-AnchorCarrierFrequencylist ::= SEQUENCE (SIZE(1..maxnoofNonAnchorCarrierFreqConfig)) OF
	SEQUENCE {
		non-anchorCarrierFrquency		OCTET STRING,
		iE-Extensions					ProtocolExtensionContainer { { Non-AnchorCarrierFrequencylist-ExtIEs} } OPTIONAL,
		...
	}

Non-AnchorCarrierFrequencylist-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


NR-Cell-Identity		::= BIT STRING (SIZE (36))


NG-RAN-Cell-Identity-ListinRANPagingArea ::= SEQUENCE (SIZE (1..maxnoofCellsinRNA)) OF NG-RAN-Cell-Identity


NR-CGI ::= SEQUENCE {
	plmn-id				PLMN-Identity,
	nr-CI				NR-Cell-Identity,
	iE-Extension		ProtocolExtensionContainer { {NR-CGI-ExtIEs} } 	OPTIONAL,
	...
}

NR-CGI-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

NR-U-Channel-List ::= SEQUENCE (SIZE (1..maxnoofNR-UChannelIDs)) OF NR-U-Channel-Item

NR-U-Channel-Item ::= SEQUENCE {
	nR-U-ChannelID						NR-U-ChannelID,
	channelOccupancyTimePercentageDL	ChannelOccupancyTimePercentage,
	energyDetectionThresholdDL			EnergyDetectionThreshold,
	iE-Extension		ProtocolExtensionContainer { {NR-U-Channel-Item-ExtIEs} } 	OPTIONAL,
	...
}

NR-U-Channel-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-ChannelOccupancyTimePercentageUL	CRITICALITY ignore	EXTENSION ChannelOccupancyTimePercentage	PRESENCE optional}|
	{ ID id-EnergyDetectionThresholdUL			CRITICALITY ignore	EXTENSION EnergyDetectionThreshold			PRESENCE optional}|
	{ ID id-RadioResourceStatusNR-U				CRITICALITY ignore	EXTENSION RadioResourceStatusNR-U			PRESENCE optional},
	...
}

NR-U-ChannelID ::= INTEGER (1..maxnoofNR-UChannelIDs, ...)

ChannelOccupancyTimePercentage ::= INTEGER (0..100,...)

EnergyDetectionThreshold ::= INTEGER (-100..-50, ...)


NR-U-ChannelInfo-List ::= SEQUENCE (SIZE (1..maxnoofNR-UChannelIDs)) OF NR-U-ChannelInfo-Item

NR-U-ChannelInfo-Item ::= SEQUENCE {
	nR-U-ChannelID	NR-U-ChannelID,
	nRARFCN			NRARFCN,
	bandwidth		Bandwidth,
	iE-Extension		ProtocolExtensionContainer { {NR-U-ChannelInfo-Item-ExtIEs} } 	OPTIONAL,
	...
}

NR-U-ChannelInfo-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


Bandwidth ::= ENUMERATED{mhz10, mhz20, mhz40, mhz60, mhz80, ...,mhz100}

NRA2XServicesAuthorized ::= SEQUENCE {
	aerialUE				AerialUE													OPTIONAL,
	aerialControllerUE	AerialControllerUE										OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { {NRA2XServicesAuthorized-ExtIEs} }	OPTIONAL,
	...
}

NRA2XServicesAuthorized-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

NRCyclicPrefix ::= ENUMERATED {normal, extended, ...}

NRDL-ULTransmissionPeriodicity ::= ENUMERATED {ms0p5, ms0p625, ms1, ms1p25, ms2, ms2p5, ms3, ms4, ms5, ms10, ms20, ms40, ms60, ms80, ms100, ms120, ms140, ms160, ...}

NRFrequencyBand ::= INTEGER (1..1024, ...)


NRFrequencyBand-List ::= SEQUENCE (SIZE(1..maxnoofNRCellBands)) OF NRFrequencyBandItem

NRFrequencyBandItem ::= SEQUENCE {
	nr-frequency-band			NRFrequencyBand,
	supported-SUL-Band-List		SupportedSULBandList											OPTIONAL,
	iE-Extension				ProtocolExtensionContainer { {NRFrequencyBandItem-ExtIEs} } 	OPTIONAL,
	...
}

NRFrequencyBandItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}



NRFrequencyInfo ::= SEQUENCE {
	nrARFCN				NRARFCN,
	sul-information		SUL-Information												OPTIONAL,
	frequencyBand-List	NRFrequencyBand-List,
	iE-Extension		ProtocolExtensionContainer { {NRFrequencyInfo-ExtIEs} }		OPTIONAL,
	...
}

NRFrequencyInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-FrequencyShift7p5khz	CRITICALITY ignore	EXTENSION FrequencyShift7p5khz	PRESENCE optional },...
}

NRMobilityHistoryReport ::= OCTET STRING


NRModeInfo ::= CHOICE {
	fdd							NRModeInfoFDD,
	tdd							NRModeInfoTDD,
	choice-extension			ProtocolIE-Single-Container { {NRModeInfo-ExtIEs} }
}

NRModeInfo-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

NRModeInfoFDD ::= SEQUENCE {
	ulNRFrequencyInfo			NRFrequencyInfo,
	dlNRFrequencyInfo			NRFrequencyInfo,
	ulNRTransmissonBandwidth	NRTransmissionBandwidth,
	dlNRTransmissonBandwidth	NRTransmissionBandwidth,
	iE-Extension		ProtocolExtensionContainer { {NRModeInfoFDD-ExtIEs} } 	OPTIONAL,
	...
}

NRModeInfoFDD-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-ULCarrierList							CRITICALITY ignore	EXTENSION NRCarrierList										PRESENCE optional }|
	{ ID id-DLCarrierList							CRITICALITY ignore	EXTENSION NRCarrierList										PRESENCE optional }|
	{ ID id-UL-GNB-DU-Cell-Resource-Configuration	CRITICALITY ignore	EXTENSION GNB-DU-Cell-Resource-Configuration		PRESENCE optional }|
	{ ID id-DL-GNB-DU-Cell-Resource-Configuration	CRITICALITY ignore	EXTENSION GNB-DU-Cell-Resource-Configuration		PRESENCE optional },
...
}


NRModeInfoTDD ::= SEQUENCE {
	nrFrequencyInfo			NRFrequencyInfo,
	nrTransmissonBandwidth	NRTransmissionBandwidth,
	iE-Extension			ProtocolExtensionContainer { {NRModeInfoTDD-ExtIEs} } 	OPTIONAL,
	...
}

NRModeInfoTDD-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-IntendedTDD-DL-ULConfiguration-NR		CRITICALITY ignore	EXTENSION IntendedTDD-DL-ULConfiguration-NR		PRESENCE optional }|
	{ ID id-TDDULDLConfigurationCommonNR			CRITICALITY ignore	EXTENSION TDDULDLConfigurationCommonNR			PRESENCE optional }|
	{ ID id-CarrierList								CRITICALITY ignore	EXTENSION NRCarrierList									PRESENCE optional }|
	{ ID id-tdd-GNB-DU-Cell-Resource-Configuration	CRITICALITY ignore	EXTENSION GNB-DU-Cell-Resource-Configuration	PRESENCE optional }|
	{ ID id-Transmission-Bandwidth-asymmetric		CRITICALITY ignore	EXTENSION Transmission-Bandwidth-asymmetric		PRESENCE optional },
	...
}


NRNRB ::= ENUMERATED { nrb11, nrb18, nrb24, nrb25, nrb31, nrb32, nrb38, nrb51, nrb52, nrb65, nrb66, nrb78, nrb79, nrb93, nrb106, nrb107, nrb121, nrb132, nrb133, nrb135, nrb160, nrb162, nrb189, nrb216, nrb217, nrb245, nrb264, nrb270, nrb273, ..., nrb33, nrb62, nrb124, nrb148, nrb248, nrb44, nrb58, nrb92, nrb119, nrb188, nrb242, nrb15}


NRPagingeDRXInformation ::= SEQUENCE {
	nRPaging-eDRX-Cycle		NRPaging-eDRX-Cycle,
	nRPaging-Time-Window	NRPaging-Time-Window					OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { {NRPagingeDRXInformation-ExtIEs} }	OPTIONAL,
	...
}

NRPagingeDRXInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

NRPaging-eDRX-Cycle ::= ENUMERATED {
	hfquarter, hfhalf, hf1, hf2, hf4,
	hf8, hf16,
	hf32, hf64, hf128, hf256,
	hf512, hf1024,
	...
}


NRPaging-Time-Window ::= ENUMERATED {
	s1, s2, s3, s4, s5,
	s6, s7, s8, s9, s10,
	s11, s12, s13, s14, s15, s16,
	...,s17, s18, s19, s20, s21, s22,
	s23, s24, s25, s26, s27, s28, s29,
	s30, s31, s32
}

NRPagingeDRXInformationforRRCINACTIVE ::= SEQUENCE {
	nRPaging-eDRX-Cycle-Inactive		NRPaging-eDRX-Cycle-Inactive,
	iE-Extensions						ProtocolExtensionContainer { { NRPagingeDRXInformationforRRCINACTIVE-ExtIEs} }	OPTIONAL,
	...
}

NRPagingeDRXInformationforRRCINACTIVE-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

NRPaging-eDRX-Cycle-Inactive ::= ENUMERATED {
	hfquarter, hfhalf, hf1,
	...
}

NRPagingLongeDRXInformationforRRCINACTIVE ::= SEQUENCE {
	nRPaging-long-eDRX-Cycle-Inactive	NRPaging-long-eDRX-Cycle-Inactive,
	nRPaging-Time-Window-Inactive		NRPaging-Time-Window-Inactive,
	iE-Extensions						ProtocolExtensionContainer { {NRPagingLongeDRXInformationforRRCINACTIVE-ExtIEs} }	OPTIONAL,
	...
}

NRPagingLongeDRXInformationforRRCINACTIVE-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

NRPaging-long-eDRX-Cycle-Inactive ::= ENUMERATED {
	hf2, hf4, hf8, hf16,
	hf32, hf64, hf128, hf256,
	hf512, hf1024,
	...
}


NRPaging-Time-Window-Inactive ::= ENUMERATED {
	s1, s2, s3, s4, s5,
	s6, s7, s8, s9, s10,
	s11, s12, s13, s14, s15, s16,
	s17, s18, s19, s20, s21, s22,
	s23, s24, s25, s26, s27, s28, s29,
	s30, s31, s32, ...
}

NRPCI ::= INTEGER (0..1007, ...)

NRSCS ::= ENUMERATED { scs15, scs30, scs60, scs120, ..., scs480, scs960}


NRTransmissionBandwidth	::= SEQUENCE {
	nRSCS	NRSCS,
	nRNRB	NRNRB,
	iE-Extensions				ProtocolExtensionContainer { {NRTransmissionBandwidth-ExtIEs} } OPTIONAL,
	...
}

NRPPaPositioningInformation ::= SEQUENCE {
	routingID											RoutingID,
	nRPPaTransactionID									INTEGER (0..32767),
	iE-Extension 				ProtocolExtensionContainer { { NRPPaPositioningInformation-ExtIEs} } OPTIONAL,
	...
}

NRPPaPositioningInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


NRTransmissionBandwidth-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

Transmission-Bandwidth-asymmetric ::= SEQUENCE {
	ul-Transmission-Bandwidth	NRTransmissionBandwidth,
	dl-Transmission-Bandwidth	NRTransmissionBandwidth,
	iE-Extensions				ProtocolExtensionContainer { { Transmission-Bandwidth-asymmetric-ExtIEs} } OPTIONAL,
	...
}

Transmission-Bandwidth-asymmetric-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


NumberOfAntennaPorts-E-UTRA ::= ENUMERATED {an1, an2, an4, ...}

NG-RANTraceID 				::=OCTET STRING (SIZE (8))

NonGBRResources-Offered ::= ENUMERATED {true, ...}

NRV2XServicesAuthorized ::= SEQUENCE {
	vehicleUE			VehicleUE							OPTIONAL,
	pedestrianUE 		PedestrianUE						OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {NRV2XServicesAuthorized-ExtIEs} }	OPTIONAL,
	...
}

NRV2XServicesAuthorized-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


NRUESidelinkAggregateMaximumBitRate ::= SEQUENCE {
	uESidelinkAggregateMaximumBitRate		BitRate,
	iE-Extensions					ProtocolExtensionContainer { {NRUESidelinkAggregateMaximumBitRate-ExtIEs} } OPTIONAL,
	...
}

NRUESidelinkAggregateMaximumBitRate-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

NSAG-ID ::= INTEGER (0..255, ...)


-- O


OfferedCapacity ::= INTEGER (1.. 16777216,...)

OffsetOfNbiotChannelNumberToEARFCN ::= ENUMERATED {
		minusTen,
		minusNine,
		minusEightDotFive,
		minusEight,
		minusSeven,
		minusSix,
		minusFive,
		minusFourDotFive,
		minusFour,
		minusThree,
		minusTwo,
		minusOne,
		minusZeroDotFive,
		zero,
		one,
		two,
		three,
		threeDotFive,
		four,
		five,
		six,
		seven,
		sevenDotFive,
		eight,
		nine,
		...
}

-- P

PositioningInformation ::= SEQUENCE {
	requestedSRSTransmissionCharacteristics				RequestedSRSTransmissionCharacteristics,
	routingID											RoutingID,
	nRPPaTransactionID									INTEGER (0..32767),
	iE-Extension 				ProtocolExtensionContainer { { PositioningInformation-ExtIEs} } OPTIONAL,
	...
}

PositioningInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PacketDelayBudget ::= INTEGER (0..1023, ...)


PacketErrorRate ::= SEQUENCE {
	pER-Scalar			PER-Scalar,
	pER-Exponent		PER-Exponent,
	iE-Extensions		ProtocolExtensionContainer { {PacketErrorRate-ExtIEs} }	OPTIONAL,
	...
}

PacketErrorRate-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PagingCause ::= ENUMERATED {
	voice,
	...
}

PedestrianUE ::= ENUMERATED {
	authorized,
	not-authorized,
	...
}

PER-Scalar ::= INTEGER (0..9, ...)

PER-Exponent ::= INTEGER (0..9, ...)

PEIPSassistanceInformation ::= SEQUENCE {
	cNsubgroupID			CNsubgroupID,
	iE-Extensions		ProtocolExtensionContainer { {PEIPSassistanceInformation-ExtIEs} } OPTIONAL,
	...
}

PEIPSassistanceInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


PacketLossRate ::= INTEGER (0..1000, ...)


PagingDRX	::= ENUMERATED {
	v32,
	v64,
	v128,
	v256,
	... ,
	v512,
	v1024
	}


PagingPriority ::= ENUMERATED {
	priolevel1,
	priolevel2,
	priolevel3,
	priolevel4,
	priolevel5,
	priolevel6,
	priolevel7,
	priolevel8,
	...
}

PartialListIndicator ::= ENUMERATED {partial, ...}

PC5QoSParameters ::= SEQUENCE {
	pc5QoSFlowList				PC5QoSFlowList,
	pc5LinkAggregateBitRates	BitRate				OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { PC5QoSParameters-ExtIEs} }	OPTIONAL,
	...
}


PC5QoSParameters-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PC5QoSFlowList ::= SEQUENCE (SIZE(1..maxnoofPC5QoSFlows)) OF PC5QoSFlowItem
-- The size of the PC5 QoS Flow List shall not exceed 2048 items.

PC5QoSFlowItem::= SEQUENCE {
	pQI							FiveQI,
	pc5FlowBitRates				PC5FlowBitRates				OPTIONAL,
	range						Range						OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { PC5QoSFlowItem-ExtIEs} }	OPTIONAL,
	...
}

PC5QoSFlowItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


PC5FlowBitRates ::= SEQUENCE {
	guaranteedFlowBitRate		BitRate,
	maximumFlowBitRate			BitRate,
	iE-Extensions		ProtocolExtensionContainer { { PC5FlowBitRates-ExtIEs} }	OPTIONAL,
	...
}

PC5FlowBitRates-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PDCPChangeIndication ::= CHOICE {
	from-S-NG-RAN-node				ENUMERATED {s-ng-ran-node-key-update-required, pdcp-data-recovery-required, ...},
	from-M-NG-RAN-node				ENUMERATED {pdcp-data-recovery-required, ...},
	choice-extension				ProtocolIE-Single-Container { {PDCPChangeIndication-ExtIEs} }
}

PDCPChangeIndication-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


PDCPDuplicationConfiguration ::= ENUMERATED {
	configured,
	de-configured,
	...
}


PDCPSNLength ::= SEQUENCE {
	ulPDCPSNLength			ENUMERATED {v12bits, v18bits, ...},
	dlPDCPSNLength			ENUMERATED {v12bits, v18bits, ...},
	iE-Extension			ProtocolExtensionContainer { {PDCPSNLength-ExtIEs} }		OPTIONAL,
	...
}

PDCPSNLength-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PDUSetQoSParameters ::= SEQUENCE {
	ulPDUSetQoSInformation					PDUSetQoSInformation	OPTIONAL,
	dlPDUSetQoSInformation					PDUSetQoSInformation	OPTIONAL,
	iE-Extensions							ProtocolExtensionContainer { { PDUSetQoSParameters-ExtIEs } }	OPTIONAL
}

PDUSetQoSParameters-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
	}


PDUSetQoSInformation	::= SEQUENCE {
	pduSetDelayBudget						ExtendedPacketDelayBudget		OPTIONAL,
	pduSetErrorRate							PacketErrorRate					OPTIONAL,
	pduSetIntegratedHandlingInformation		ENUMERATED {true, false, ...}	OPTIONAL,
	iE-Extensions							ProtocolExtensionContainer { { PDUSetQoSInformation-ExtIEs } }	OPTIONAL
}

PDUSetQoSInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PDUSetbasedHandlingIndicator ::= ENUMERATED {
	supported,
	...
}



PDUSessionAggregateMaximumBitRate ::= SEQUENCE {
	downlink-session-AMBR				BitRate,
	uplink-session-AMBR					BitRate,
	iE-Extensions						ProtocolExtensionContainer { {PDUSessionAggregateMaximumBitRate-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionAggregateMaximumBitRate-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


PDUSession-List ::= SEQUENCE (SIZE (1.. maxnoofPDUSessions)) OF PDUSession-ID


PDUSession-List-withCause ::= SEQUENCE (SIZE (1.. maxnoofPDUSessions)) OF PDUSession-List-withCause-Item

PDUSession-List-withCause-Item ::= SEQUENCE {
	pduSessionId		PDUSession-ID,
	cause				Cause				OPTIONAL,
	iE-Extension		ProtocolExtensionContainer { {PDUSession-List-withCause-Item-ExtIEs} }	OPTIONAL,
	...
}

PDUSession-List-withCause-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


PDUSession-List-withDataForwardingFromTarget ::= SEQUENCE (SIZE (1.. maxnoofPDUSessions)) OF
															PDUSession-List-withDataForwardingFromTarget-Item

PDUSession-List-withDataForwardingFromTarget-Item ::= SEQUENCE {
	pduSessionId						PDUSession-ID,
	dataforwardinginfoTarget			DataForwardingInfoFromTargetNGRANnode,
	iE-Extension		ProtocolExtensionContainer { {PDUSession-List-withDataForwardingFromTarget-Item-ExtIEs} }	OPTIONAL,
	...
}

PDUSession-List-withDataForwardingFromTarget-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-DRB-IDs-takenintouse		CRITICALITY reject	EXTENSION DRB-List	PRESENCE optional},
	...
}


PDUSession-List-withDataForwardingRequest ::= SEQUENCE (SIZE (1.. maxnoofPDUSessions)) OF
															PDUSession-List-withDataForwardingRequest-Item

PDUSession-List-withDataForwardingRequest-Item ::= SEQUENCE {
	pduSessionId							PDUSession-ID,
	dataforwardingInfofromSource			DataforwardingandOffloadingInfofromSource					OPTIONAL,
	dRBtoBeReleasedList						DRBToQoSFlowMapping-List									OPTIONAL,
	iE-Extension		ProtocolExtensionContainer { {PDUSession-List-withDataForwardingRequest-Item-ExtIEs} }	OPTIONAL,
	...
}

PDUSession-List-withDataForwardingRequest-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ID id-Cause				CRITICALITY ignore EXTENSION Cause				PRESENCE optional},
...
}


PDUSessionsListToBeReleased-UPError ::= SEQUENCE (SIZE (1.. maxnoofPDUSessions)) OF PDUSessionsListToBeReleased-UPError-Item

PDUSessionsListToBeReleased-UPError-Item ::= SEQUENCE {
	pduSessionId						PDUSession-ID,
	userPlaneErrorIndicator				UserPlaneErrorIndicator,
	iE-Extension		ProtocolExtensionContainer { {PDUSessionsListToBeReleased-UPError-Item-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionsListToBeReleased-UPError-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- PDU Session related message level IEs BEGIN
--
-- **************************************************************


-- **************************************************************
--
-- PDU Session Resources Admitted List
--
-- **************************************************************


PDUSessionResourcesAdmitted-List ::= SEQUENCE (SIZE(1..maxnoofPDUSessions)) OF PDUSessionResourcesAdmitted-Item

PDUSessionResourcesAdmitted-Item ::= SEQUENCE {
	pduSessionId						PDUSession-ID,
	pduSessionResourceAdmittedInfo		PDUSessionResourceAdmittedInfo,
	iE-Extensions						ProtocolExtensionContainer { {PDUSessionResourcesAdmitted-Item-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourcesAdmitted-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


PDUSessionResourceAdmittedInfo ::= SEQUENCE {
	dL-NG-U-TNL-Information-Unchanged		ENUMERATED {true, ...}														OPTIONAL,
	qosFlowsAdmitted-List					QoSFlowsAdmitted-List,
	qosFlowsNotAdmitted-List				QoSFlows-List-withCause														OPTIONAL,
	dataForwardingInfoFromTarget			DataForwardingInfoFromTargetNGRANnode										OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { {PDUSessionResourceAdmittedInfo-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionResourceAdmittedInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
{ ID id-SecondarydataForwardingInfoFromTarget-List	CRITICALITY ignore	EXTENSION SecondarydataForwardingInfoFromTarget-List	PRESENCE optional},
	...
}


-- **************************************************************
--
-- PDU Session Resources Not Admitted List
--
-- **************************************************************


PDUSessionResourcesNotAdmitted-List ::= SEQUENCE (SIZE (1..maxnoofPDUSessions)) OF PDUSessionResourcesNotAdmitted-Item

PDUSessionResourcesNotAdmitted-Item ::= SEQUENCE {
	pduSessionId				PDUSession-ID,
	cause						Cause				OPTIONAL,
	iE-Extension		ProtocolExtensionContainer { {PDUSessionResourcesNotAdmitted-Item-Item-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionResourcesNotAdmitted-Item-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- PDU Session Resources To Be Setup List
--
-- **************************************************************


PDUSessionResourcesToBeSetup-List ::= SEQUENCE (SIZE(1..maxnoofPDUSessions)) OF PDUSessionResourcesToBeSetup-Item

PDUSessionResourcesToBeSetup-Item ::= SEQUENCE {
	pduSessionId					PDUSession-ID,
	s-NSSAI							S-NSSAI,
	pduSessionAMBR					PDUSessionAggregateMaximumBitRate													OPTIONAL,
	uL-NG-U-TNLatUPF				UPTransportLayerInformation,
	source-DL-NG-U-TNL-Information	UPTransportLayerInformation															OPTIONAL,
	securityIndication				SecurityIndication																	OPTIONAL,
	pduSessionType					PDUSessionType,
	pduSessionNetworkInstance		PDUSessionNetworkInstance															OPTIONAL,
	qosFlowsToBeSetup-List			QoSFlowsToBeSetup-List,
	dataforwardinginfofromSource	DataforwardingandOffloadingInfofromSource											OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourcesToBeSetup-Item-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourcesToBeSetup-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-Additional-UL-NG-U-TNLatUPF-List			CRITICALITY ignore	EXTENSION Additional-UL-NG-U-TNLatUPF-List 			PRESENCE optional}|
	{ ID id-PDUSessionCommonNetworkInstance				CRITICALITY ignore	EXTENSION PDUSessionCommonNetworkInstance				PRESENCE optional}|
	{ ID id-Redundant-UL-NG-U-TNLatUPF					CRITICALITY	ignore	EXTENSION UPTransportLayerInformation					PRESENCE optional}|
	{ ID id-Additional-Redundant-UL-NG-U-TNLatUPF-List	CRITICALITY ignore	EXTENSION Additional-UL-NG-U-TNLatUPF-List				PRESENCE optional}|
	{ ID id-RedundantCommonNetworkInstance				CRITICALITY ignore	EXTENSION PDUSessionCommonNetworkInstance				PRESENCE optional}|
	{ ID id-RedundantPDUSessionInformation				CRITICALITY ignore	EXTENSION RedundantPDUSessionInformation				PRESENCE optional}|
	{ ID id-MBS-SessionAssociatedInformation			CRITICALITY ignore	EXTENSION MBS-SessionAssociatedInformation				PRESENCE optional},
	...
}


-- **************************************************************
--
-- PDU Session Resource Setup Info - SN terminated
--
-- **************************************************************


PDUSessionResourceSetupInfo-SNterminated ::= SEQUENCE {
	uL-NG-U-TNLatUPF				UPTransportLayerInformation,
	pduSessionType					PDUSessionType,
	pduSessionNetworkInstance		PDUSessionNetworkInstance																	OPTIONAL,
	qosFlowsToBeSetup-List			QoSFlowsToBeSetup-List-Setup-SNterminated,
	dataforwardinginfofromSource	DataforwardingandOffloadingInfofromSource													OPTIONAL,
	securityIndication				SecurityIndication																			OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceSetupInfo-SNterminated-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionResourceSetupInfo-SNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-SecurityResult						CRITICALITY reject	EXTENSION SecurityResult							PRESENCE optional}|
	{ ID id-PDUSessionCommonNetworkInstance		CRITICALITY ignore	EXTENSION PDUSessionCommonNetworkInstance		PRESENCE optional}|
	{ ID id-DefaultDRB-Allowed					CRITICALITY ignore	EXTENSION DefaultDRB-Allowed						PRESENCE optional}|
	{ ID id-SplitSessionIndicator				CRITICALITY reject	EXTENSION SplitSessionIndicator					PRESENCE optional}|
	{ ID id-NonGBRResources-Offered				CRITICALITY ignore	EXTENSION NonGBRResources-Offered				PRESENCE optional}|
	{ ID id-Redundant-UL-NG-U-TNLatUPF			CRITICALITY	ignore	EXTENSION UPTransportLayerInformation			PRESENCE optional}|
	{ ID id-RedundantCommonNetworkInstance		CRITICALITY ignore	EXTENSION PDUSessionCommonNetworkInstance		PRESENCE optional}|
	{ ID id-RedundantPDUSessionInformation		CRITICALITY ignore	EXTENSION RedundantPDUSessionInformation		PRESENCE optional},
	...
}

QoSFlowsToBeSetup-List-Setup-SNterminated ::= SEQUENCE (SIZE(1..maxnoofQoSFlows)) OF QoSFlowsToBeSetup-List-Setup-SNterminated-Item

QoSFlowsToBeSetup-List-Setup-SNterminated-Item ::= SEQUENCE {
	qfi								QoSFlowIdentifier,
	qosFlowLevelQoSParameters		QoSFlowLevelQoSParameters,
	offeredGBRQoSFlowInfo			GBRQoSFlowInfo																						OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {QoSFlowsToBeSetup-List-Setup-SNterminated-Item-ExtIEs} }	OPTIONAL,
	...
}

QoSFlowsToBeSetup-List-Setup-SNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-TSCTrafficCharacteristics		CRITICALITY ignore	EXTENSION TSCTrafficCharacteristics 	PRESENCE optional}|
	{ ID id-RedundantQoSFlowIndicator		CRITICALITY ignore	EXTENSION RedundantQoSFlowIndicator	PRESENCE optional},
	...
}

-- **************************************************************
--
-- PDU Session Resource Setup Response Info - SN terminated
--
-- **************************************************************


PDUSessionResourceSetupResponseInfo-SNterminated ::= SEQUENCE {
	dL-NG-U-TNLatNG-RAN				UPTransportLayerInformation,
	dRBsToBeSetup					DRBsToBeSetupList-SetupResponse-SNterminated 	OPTIONAL,
	dataforwardinginfoTarget		DataForwardingInfoFromTargetNGRANnode			OPTIONAL,
	qosFlowsNotAdmittedList			QoSFlows-List-withCause							OPTIONAL,
	securityResult					SecurityResult									OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceSetupResponseInfo-SNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceSetupResponseInfo-SNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-DRB-IDs-takenintouse					CRITICALITY reject	EXTENSION DRB-List												PRESENCE optional}|
	{ ID id-Redundant-DL-NG-U-TNLatNG-RAN			CRITICALITY ignore	EXTENSION UPTransportLayerInformation						PRESENCE optional}|
	{ ID id-UsedRSNInformation						CRITICALITY ignore	EXTENSION RedundantPDUSessionInformation					PRESENCE optional}|
	{ ID id-S-CPAC-dataforwardinginfofromSource		CRITICALITY ignore	EXTENSION DataforwardingandOffloadingInfofromSource	PRESENCE optional},
	...
}

DRBsToBeSetupList-SetupResponse-SNterminated ::= SEQUENCE (SIZE(1..maxnoofDRBs)) OF DRBsToBeSetupList-SetupResponse-SNterminated-Item

DRBsToBeSetupList-SetupResponse-SNterminated-Item ::= SEQUENCE {
	drb-ID													DRB-ID,
	sN-UL-PDCP-UP-TNLInfo									UPTransportParameters,
	dRB-QoS													QoSFlowLevelQoSParameters,
	pDCP-SNLength											PDCPSNLength						OPTIONAL,
	rLC-Mode												RLCMode,
	uL-Configuration										ULConfiguration						OPTIONAL,
	secondary-SN-UL-PDCP-UP-TNLInfo							UPTransportParameters				OPTIONAL,
	duplicationActivation									DuplicationActivation				OPTIONAL,
	qoSFlowsMappedtoDRB-SetupResponse-SNterminated			QoSFlowsMappedtoDRB-SetupResponse-SNterminated,
	iE-Extensions					ProtocolExtensionContainer { {DRBsToBeSetupList-SetupResponse-SNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

DRBsToBeSetupList-SetupResponse-SNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-Additional-PDCP-Duplication-TNL-List		CRITICALITY ignore	EXTENSION Additional-PDCP-Duplication-TNL-List	PRESENCE optional}|
	{ ID id-RLCDuplicationInformation					CRITICALITY ignore	EXTENSION RLCDuplicationInformation					PRESENCE optional},
	...
}

QoSFlowsMappedtoDRB-SetupResponse-SNterminated ::= SEQUENCE (SIZE(1..maxnoofQoSFlows)) OF
																		QoSFlowsMappedtoDRB-SetupResponse-SNterminated-Item

QoSFlowsMappedtoDRB-SetupResponse-SNterminated-Item ::= SEQUENCE {
	qoSFlowIdentifier				QoSFlowIdentifier,
	mCGRequestedGBRQoSFlowInfo		GBRQoSFlowInfo														OPTIONAL,
	qosFlowMappingIndication		QoSFlowMappingIndication											OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {QoSFlowsMappedtoDRB-SetupResponse-SNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

QoSFlowsMappedtoDRB-SetupResponse-SNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-CurrentQoSParaSetIndex				CRITICALITY ignore	EXTENSION QoSParaSetIndex				PRESENCE optional }|
	{ ID id-SourceDLForwardingIPAddress			CRITICALITY ignore	EXTENSION TransportLayerAddress		PRESENCE optional},
	...
}


-- **************************************************************
--
-- PDU Session Resource Setup Info - MN terminated
--
-- **************************************************************


PDUSessionResourceSetupInfo-MNterminated ::= SEQUENCE {
	pduSessionType					PDUSessionType,
	dRBsToBeSetup					DRBsToBeSetupList-Setup-MNterminated,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceSetupInfo-MNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceSetupInfo-MNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

DRBsToBeSetupList-Setup-MNterminated ::= SEQUENCE (SIZE(1..maxnoofDRBs)) OF DRBsToBeSetupList-Setup-MNterminated-Item

DRBsToBeSetupList-Setup-MNterminated-Item ::= SEQUENCE {
	drb-ID													DRB-ID,
	mN-UL-PDCP-UP-TNLInfo									UPTransportParameters,
	rLC-Mode												RLCMode,
	uL-Configuration										ULConfiguration						OPTIONAL,
	dRB-QoS													QoSFlowLevelQoSParameters,
	pDCP-SNLength											PDCPSNLength						OPTIONAL,
	secondary-MN-UL-PDCP-UP-TNLInfo							UPTransportParameters				OPTIONAL,
	duplicationActivation									DuplicationActivation				OPTIONAL,
	qoSFlowsMappedtoDRB-Setup-MNterminated		QoSFlowsMappedtoDRB-Setup-MNterminated,
	iE-Extensions					ProtocolExtensionContainer { {DRBsToBeSetupList-Setup-MNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

DRBsToBeSetupList-Setup-MNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-Additional-PDCP-Duplication-TNL-List		CRITICALITY ignore	EXTENSION Additional-PDCP-Duplication-TNL-List	PRESENCE optional}|
	{ ID id-RLCDuplicationInformation					CRITICALITY ignore	EXTENSION RLCDuplicationInformation					PRESENCE optional},
	...
}

QoSFlowsMappedtoDRB-Setup-MNterminated ::= SEQUENCE (SIZE(1..maxnoofQoSFlows)) OF QoSFlowsMappedtoDRB-Setup-MNterminated-Item

QoSFlowsMappedtoDRB-Setup-MNterminated-Item ::= SEQUENCE {
	qoSFlowIdentifier				QoSFlowIdentifier,
	qoSFlowLevelQoSParameters		QoSFlowLevelQoSParameters,
	qosFlowMappingIndication		QoSFlowMappingIndication		OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {QoSFlowsMappedtoDRB-Setup-MNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

QoSFlowsMappedtoDRB-Setup-MNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-TSCTrafficCharacteristics		CRITICALITY ignore	EXTENSION TSCTrafficCharacteristics 	PRESENCE optional},
	...
}


-- **************************************************************
--
-- PDU Session Resource Setup Response Info - MN terminated
--
-- **************************************************************


PDUSessionResourceSetupResponseInfo-MNterminated ::= SEQUENCE {
	dRBsAdmittedList				DRBsAdmittedList-SetupResponse-MNterminated,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceSetupResponseInfo-MNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceSetupResponseInfo-MNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ID id-DRBsNotAdmittedSetupModifyList	CRITICALITY ignore	EXTENSION DRB-List-withCause		PRESENCE optional},
	...
}

DRBsAdmittedList-SetupResponse-MNterminated ::= SEQUENCE (SIZE(1..maxnoofDRBs)) OF DRBsAdmittedList-SetupResponse-MNterminated-Item

DRBsAdmittedList-SetupResponse-MNterminated-Item ::= SEQUENCE {
	drb-ID									DRB-ID,
	sN-DL-SCG-UP-TNLInfo					UPTransportParameters,
	secondary-SN-DL-SCG-UP-TNLInfo			UPTransportParameters				OPTIONAL,
	lCID									LCID								OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {DRBsAdmittedList-SetupResponse-MNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

DRBsAdmittedList-SetupResponse-MNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-Additional-PDCP-Duplication-TNL-List		CRITICALITY ignore	EXTENSION Additional-PDCP-Duplication-TNL-List	PRESENCE optional}|
	{ ID id-QoSFlowsMappedtoDRB-SetupResponse-MNterminated	CRITICALITY ignore	EXTENSION	QoSFlowsMappedtoDRB-SetupResponse-MNterminated	PRESENCE optional},
	...
}

QoSFlowsMappedtoDRB-SetupResponse-MNterminated ::= SEQUENCE (SIZE(1..maxnoofQoSFlows)) OF QoSFlowsMappedtoDRB-SetupResponse-MNterminated-Item

QoSFlowsMappedtoDRB-SetupResponse-MNterminated-Item ::= SEQUENCE {
	qoSFlowIdentifier				QoSFlowIdentifier,
	currentQoSParaSetIndex			QoSParaSetIndex,
	iE-Extensions					ProtocolExtensionContainer { {QoSFlowsMappedtoDRB-SetupResponse-MNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

QoSFlowsMappedtoDRB-SetupResponse-MNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- PDU Session Resource Modification Info - SN terminated
--
-- **************************************************************


PDUSessionResourceModificationInfo-SNterminated ::= SEQUENCE {
	uL-NG-U-TNLatUPF				UPTransportLayerInformation						OPTIONAL,
	pduSessionNetworkInstance		PDUSessionNetworkInstance						OPTIONAL,
	qosFlowsToBeSetup-List			QoSFlowsToBeSetup-List-Setup-SNterminated		OPTIONAL,
	dataforwardinginfofromSource	DataforwardingandOffloadingInfofromSource		OPTIONAL,
	qosFlowsToBeModified-List		QoSFlowsToBeSetup-List-Modified-SNterminated	OPTIONAL,
	qoSFlowsToBeReleased-List		QoSFlows-List-withCause							OPTIONAL,
	drbsToBeModifiedList			DRBsToBeModified-List-Modified-SNterminated		OPTIONAL,
	dRBsToBeReleased				DRB-List-withCause								OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceModificationInfo-SNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceModificationInfo-SNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-PDUSessionCommonNetworkInstance		CRITICALITY ignore	EXTENSION PDUSessionCommonNetworkInstance		PRESENCE optional}|
	{ID id-DefaultDRB-Allowed					CRITICALITY ignore	EXTENSION DefaultDRB-Allowed						PRESENCE optional}|
	{ID id-NonGBRResources-Offered				CRITICALITY ignore	EXTENSION NonGBRResources-Offered				PRESENCE optional}|
	{ID id-Redundant-UL-NG-U-TNLatUPF			CRITICALITY ignore	EXTENSION UPTransportLayerInformation			PRESENCE optional}|
	{ID id-RedundantCommonNetworkInstance		CRITICALITY ignore	EXTENSION PDUSessionCommonNetworkInstance		PRESENCE optional}|
	{ID id-SecurityIndication					CRITICALITY ignore	EXTENSION SecurityIndication						PRESENCE optional},
	...
}

QoSFlowsToBeSetup-List-Modified-SNterminated ::= SEQUENCE (SIZE(1..maxnoofQoSFlows)) OF QoSFlowsToBeSetup-List-Modified-SNterminated-Item

QoSFlowsToBeSetup-List-Modified-SNterminated-Item ::= SEQUENCE {
	qfi								QoSFlowIdentifier,
	qosFlowLevelQoSParameters		QoSFlowLevelQoSParameters							OPTIONAL,
	offeredGBRQoSFlowInfo			GBRQoSFlowInfo										OPTIONAL,
	qosFlowMappingIndication		QoSFlowMappingIndication 							OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {QoSFlowsToBeSetup-List-Modified-SNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

QoSFlowsToBeSetup-List-Modified-SNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-TSCTrafficCharacteristics		CRITICALITY ignore	EXTENSION TSCTrafficCharacteristics 	PRESENCE optional}|
	{ ID id-RedundantQoSFlowIndicator		CRITICALITY ignore	EXTENSION RedundantQoSFlowIndicator	PRESENCE optional},
	...
}

DRBsToBeModified-List-Modified-SNterminated ::= SEQUENCE (SIZE(1..maxnoofDRBs)) OF DRBsToBeModified-List-Modified-SNterminated-Item

DRBsToBeModified-List-Modified-SNterminated-Item ::= SEQUENCE {
	drb-ID									DRB-ID,
	mN-DL-SCG-UP-TNLInfo					UPTransportParameters		OPTIONAL,
	secondary-MN-DL-SCG-UP-TNLInfo			UPTransportParameters		OPTIONAL,
	lCID									LCID						OPTIONAL,
	rlc-status								RLC-Status					OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {DRBsToBeModified-List-Modified-SNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

DRBsToBeModified-List-Modified-SNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-Additional-PDCP-Duplication-TNL-List		CRITICALITY ignore	EXTENSION Additional-PDCP-Duplication-TNL-List	PRESENCE optional},
	...
}

-- **************************************************************
--
-- PDU Session Resource Modification Response Info - SN terminated
--
-- **************************************************************


PDUSessionResourceModificationResponseInfo-SNterminated ::= SEQUENCE {
	dL-NG-U-TNLatNG-RAN				UPTransportLayerInformation								OPTIONAL,
	dRBsToBeSetup					DRBsToBeSetupList-SetupResponse-SNterminated			OPTIONAL,
	dataforwardinginfoTarget		DataForwardingInfoFromTargetNGRANnode					OPTIONAL,
	dRBsToBeModified				DRBsToBeModifiedList-ModificationResponse-SNterminated	OPTIONAL,
	dRBsToBeReleased				DRB-List-withCause										OPTIONAL,
	dataforwardinginfofromSource	DataforwardingandOffloadingInfofromSource				OPTIONAL,
	qosFlowsNotAdmittedTBAdded		QoSFlows-List-withCause									OPTIONAL,
	qosFlowsReleased				QoSFlows-List-withCause									OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceModificationResponseInfo-SNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceModificationResponseInfo-SNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-DRB-IDs-takenintouse			CRITICALITY reject	EXTENSION DRB-List								PRESENCE optional}|
	{ ID id-Redundant-DL-NG-U-TNLatNG-RAN	CRITICALITY ignore	EXTENSION UPTransportLayerInformation		PRESENCE optional}|
	{ ID id-SecurityResult					CRITICALITY ignore	EXTENSION SecurityResult						PRESENCE optional},
	...
}

DRBsToBeModifiedList-ModificationResponse-SNterminated ::= SEQUENCE (SIZE(1..maxnoofDRBs)) OF
																				DRBsToBeModifiedList-ModificationResponse-SNterminated-Item

DRBsToBeModifiedList-ModificationResponse-SNterminated-Item ::= SEQUENCE {
	drb-ID													DRB-ID,
	sN-UL-PDCP-UP-TNLInfo									UPTransportParameters									OPTIONAL,
	dRB-QoS													QoSFlowLevelQoSParameters								OPTIONAL,
	qoSFlowsMappedtoDRB-SetupResponse-SNterminated			QoSFlowsMappedtoDRB-SetupResponse-SNterminated		OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {DRBsToBeModifiedList-ModificationResponse-SNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

DRBsToBeModifiedList-ModificationResponse-SNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-Additional-PDCP-Duplication-TNL-List		CRITICALITY ignore	EXTENSION Additional-PDCP-Duplication-TNL-List	PRESENCE optional}|
	{ ID id-RLCDuplicationInformation					CRITICALITY ignore	EXTENSION RLCDuplicationInformation					PRESENCE optional}|
	{ ID id-secondary-SN-UL-PDCP-UP-TNLInfo				CRITICALITY ignore	EXTENSION UPTransportParameters						PRESENCE optional}|
	{ ID id-pdcpDuplicationConfiguration				CRITICALITY ignore	EXTENSION PDCPDuplicationConfiguration				PRESENCE optional}|
	{ ID id-duplicationActivation						CRITICALITY ignore	EXTENSION DuplicationActivation						PRESENCE optional},
	...
}


-- **************************************************************
--
-- PDU Session Resource Modification Info - MN terminated
--
-- **************************************************************


PDUSessionResourceModificationInfo-MNterminated ::= SEQUENCE {
	pduSessionType					PDUSessionType,
	dRBsToBeSetup					DRBsToBeSetupList-Setup-MNterminated						OPTIONAL,
	dRBsToBeModified				DRBsToBeModifiedList-Modification-MNterminated				OPTIONAL,
	dRBsToBeReleased				DRB-List-withCause											OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceModificationInfo-MNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceModificationInfo-MNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

DRBsToBeModifiedList-Modification-MNterminated ::= SEQUENCE (SIZE(1..maxnoofDRBs)) OF
																					DRBsToBeModifiedList-Modification-MNterminated-Item

DRBsToBeModifiedList-Modification-MNterminated-Item ::= SEQUENCE {
	drb-ID													DRB-ID,
	mN-UL-PDCP-UP-TNLInfo									UPTransportParameters						OPTIONAL,
	dRB-QoS													QoSFlowLevelQoSParameters					OPTIONAL,
	secondary-MN-UL-PDCP-UP-TNLInfo							UPTransportParameters						OPTIONAL,
	uL-Configuration										ULConfiguration								OPTIONAL,
	pdcpDuplicationConfiguration							PDCPDuplicationConfiguration 				OPTIONAL,
	duplicationActivation									DuplicationActivation						OPTIONAL,
	qoSFlowsMappedtoDRB-Setup-MNterminated					QoSFlowsMappedtoDRB-Setup-MNterminated	OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {DRBsToBeModifiedList-Modification-MNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

DRBsToBeModifiedList-Modification-MNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-Additional-PDCP-Duplication-TNL-List		CRITICALITY ignore	EXTENSION Additional-PDCP-Duplication-TNL-List	PRESENCE optional}|
	{ ID id-RLCDuplicationInformation					CRITICALITY ignore	EXTENSION RLCDuplicationInformation	PRESENCE optional},
	...
}


-- **************************************************************
--
-- PDU Session Resource Modification Response Info - MN terminated
--
-- **************************************************************


PDUSessionResourceModificationResponseInfo-MNterminated ::= SEQUENCE {
	dRBsAdmittedList						DRBsAdmittedList-ModificationResponse-MNterminated,
	dRBsReleasedList						DRB-List																		OPTIONAL,
	dRBsNotAdmittedSetupModifyList			DRB-List-withCause																OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceModificationResponseInfo-MNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceModificationResponseInfo-MNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

DRBsAdmittedList-ModificationResponse-MNterminated ::= SEQUENCE (SIZE(1..maxnoofDRBs)) OF DRBsAdmittedList-ModificationResponse-MNterminated-Item

DRBsAdmittedList-ModificationResponse-MNterminated-Item ::= SEQUENCE {
	drb-ID									DRB-ID,
	sN-DL-SCG-UP-TNLInfo					UPTransportParameters				OPTIONAL,
	secondary-SN-DL-SCG-UP-TNLInfo			UPTransportParameters				OPTIONAL,
	lCID									LCID								OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {DRBsAdmittedList-ModificationResponse-MNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

DRBsAdmittedList-ModificationResponse-MNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-Additional-PDCP-Duplication-TNL-List		CRITICALITY ignore	EXTENSION Additional-PDCP-Duplication-TNL-List	PRESENCE optional}|
	{ ID id-QoSFlowsMappedtoDRB-SetupResponse-MNterminated	CRITICALITY ignore	EXTENSION	QoSFlowsMappedtoDRB-SetupResponse-MNterminated	PRESENCE optional},
	...
}


-- **************************************************************
--
-- PDU Session Resource Change Required Info - SN terminated
--
-- **************************************************************


PDUSessionResourceChangeRequiredInfo-SNterminated ::= SEQUENCE {
	dataforwardinginfofromSource	DataforwardingandOffloadingInfofromSource											OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceChangeRequiredInfo-SNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceChangeRequiredInfo-SNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- PDU Session Resource Change Confirm Info - SN terminated
--
-- **************************************************************


PDUSessionResourceChangeConfirmInfo-SNterminated ::= SEQUENCE {
	dataforwardinginfoTarget		DataForwardingInfoFromTargetNGRANnode				OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceChangeConfirmInfo-SNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceChangeConfirmInfo-SNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-DRB-IDs-takenintouse	CRITICALITY reject	EXTENSION DRB-List	PRESENCE optional},
	...
}


-- **************************************************************
--
-- PDU Session Resource Change Required Info - MN terminated
--
-- **************************************************************


PDUSessionResourceChangeRequiredInfo-MNterminated ::= SEQUENCE {
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceChangeRequiredInfo-MNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceChangeRequiredInfo-MNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- PDU Session Resource Change Confirm Info - MN terminated
--
-- **************************************************************


PDUSessionResourceChangeConfirmInfo-MNterminated ::= SEQUENCE {
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceChangeConfirmInfo-MNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceChangeConfirmInfo-MNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- PDU Session Resource Modification Required Info - SN terminated
--
-- **************************************************************


PDUSessionResourceModRqdInfo-SNterminated ::= SEQUENCE {
	dL-NG-U-TNLatNG-RAN				UPTransportLayerInformation						OPTIONAL,
	qoSFlowsToBeReleased-List		QoSFlows-List-withCause							OPTIONAL,
	dataforwardinginfofromSource	DataforwardingandOffloadingInfofromSource		OPTIONAL,
	drbsToBeSetupList				DRBsToBeSetup-List-ModRqd-SNterminated			OPTIONAL,
	drbsToBeModifiedList			DRBsToBeModified-List-ModRqd-SNterminated		OPTIONAL,
	dRBsToBeReleased				DRB-List-withCause								OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceModRqdInfo-SNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceModRqdInfo-SNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

DRBsToBeSetup-List-ModRqd-SNterminated ::= SEQUENCE (SIZE(1..maxnoofDRBs)) OF DRBsToBeSetup-List-ModRqd-SNterminated-Item

DRBsToBeSetup-List-ModRqd-SNterminated-Item ::= SEQUENCE {
	drb-ID											DRB-ID,
	pDCP-SNLength									PDCPSNLength										OPTIONAL,
	sn-UL-PDCP-UPTNLinfo							UPTransportParameters,
	dRB-QoS											QoSFlowLevelQoSParameters,
	secondary-SN-UL-PDCP-UP-TNLInfo					UPTransportParameters								OPTIONAL,
	duplicationActivation							DuplicationActivation								OPTIONAL,
	uL-Configuration								ULConfiguration										OPTIONAL,
	qoSFlowsMappedtoDRB-ModRqd-SNterminated			QoSFlowsSetupMappedtoDRB-ModRqd-SNterminated,
	rLC-Mode										RLCMode,
	iE-Extensions					ProtocolExtensionContainer { {DRBsToBeSetup-List-ModRqd-SNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

DRBsToBeSetup-List-ModRqd-SNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-Additional-PDCP-Duplication-TNL-List		CRITICALITY ignore	EXTENSION Additional-PDCP-Duplication-TNL-List	PRESENCE optional}|
	{ ID id-RLCDuplicationInformation					CRITICALITY ignore	EXTENSION RLCDuplicationInformation					PRESENCE optional},
	...
}

QoSFlowsSetupMappedtoDRB-ModRqd-SNterminated ::= SEQUENCE (SIZE(1..maxnoofQoSFlows)) OF
																		QoSFlowsSetupMappedtoDRB-ModRqd-SNterminated-Item

QoSFlowsSetupMappedtoDRB-ModRqd-SNterminated-Item ::= SEQUENCE {
	qoSFlowIdentifier				QoSFlowIdentifier,
	mCGRequestedGBRQoSFlowInfo		GBRQoSFlowInfo														OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {QoSFlowsSetupMappedtoDRB-ModRqd-SNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

QoSFlowsSetupMappedtoDRB-ModRqd-SNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ID id-QosFlowMappingIndication	CRITICALITY ignore	EXTENSION QoSFlowMappingIndication		PRESENCE optional},
	...
}

DRBsToBeModified-List-ModRqd-SNterminated ::= SEQUENCE (SIZE(1..maxnoofDRBs)) OF DRBsToBeModified-List-ModRqd-SNterminated-Item

DRBsToBeModified-List-ModRqd-SNterminated-Item ::= SEQUENCE {
	drb-ID											DRB-ID,
	sN-UL-PDCP-UP-TNLInfo							UPTransportParameters									OPTIONAL,
	dRB-QoS											QoSFlowLevelQoSParameters								OPTIONAL,
	secondary-SN-UL-PDCP-UP-TNLInfo					UPTransportParameters									OPTIONAL,
	uL-Configuration								ULConfiguration											OPTIONAL,
	pdcpDuplicationConfiguration					PDCPDuplicationConfiguration							OPTIONAL,
	duplicationActivation							DuplicationActivation									OPTIONAL,
	qoSFlowsMappedtoDRB-ModRqd-SNterminated		QoSFlowsModifiedMappedtoDRB-ModRqd-SNterminated				OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {DRBsToBeModified-List-ModRqd-SNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

DRBsToBeModified-List-ModRqd-SNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-Additional-PDCP-Duplication-TNL-List		CRITICALITY ignore	EXTENSION Additional-PDCP-Duplication-TNL-List	PRESENCE optional}|
	{ ID id-RLCDuplicationInformation					CRITICALITY ignore	EXTENSION RLCDuplicationInformation					PRESENCE optional},
	...
}

QoSFlowsModifiedMappedtoDRB-ModRqd-SNterminated ::= SEQUENCE (SIZE(1..maxnoofQoSFlows)) OF
																		QoSFlowsModifiedMappedtoDRB-ModRqd-SNterminated-Item

QoSFlowsModifiedMappedtoDRB-ModRqd-SNterminated-Item ::= SEQUENCE {
	qoSFlowIdentifier					QoSFlowIdentifier,
	mCGRequestedGBRQoSFlowInfo			GBRQoSFlowInfo														OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {QoSFlowsModifiedMappedtoDRB-ModRqd-SNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

QoSFlowsModifiedMappedtoDRB-ModRqd-SNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ID id-QosFlowMappingIndication	CRITICALITY ignore	EXTENSION QoSFlowMappingIndication		PRESENCE optional},
	...
}


-- **************************************************************
--
-- PDU Session Resource Modification Confirm Info - SN terminated
--
-- **************************************************************


PDUSessionResourceModConfirmInfo-SNterminated ::= SEQUENCE {
	uL-NG-U-TNLatUPF						UPTransportLayerInformation								OPTIONAL,
	dRBsAdmittedList						DRBsAdmittedList-ModConfirm-SNterminated,
	dRBsNotAdmittedSetupModifyList			DRB-List-withCause										OPTIONAL,
	dataforwardinginfoTarget				DataForwardingInfoFromTargetNGRANnode					OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceModConfirmInfo-SNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceModConfirmInfo-SNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-DRB-IDs-takenintouse		CRITICALITY reject	EXTENSION DRB-List	PRESENCE optional},
	...
}

DRBsAdmittedList-ModConfirm-SNterminated ::= SEQUENCE (SIZE(1..maxnoofDRBs)) OF
																				DRBsAdmittedList-ModConfirm-SNterminated-Item

DRBsAdmittedList-ModConfirm-SNterminated-Item ::= SEQUENCE {
	drb-ID											DRB-ID,
	mN-DL-CG-UP-TNLInfo								UPTransportParameters							OPTIONAL,
	secondary-MN-DL-CG-UP-TNLInfo					UPTransportParameters							OPTIONAL,
	lCID											LCID											OPTIONAL,
	iE-Extensions				ProtocolExtensionContainer { {DRBsAdmittedList-ModConfirm-SNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

DRBsAdmittedList-ModConfirm-SNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-Additional-PDCP-Duplication-TNL-List		CRITICALITY ignore	EXTENSION Additional-PDCP-Duplication-TNL-List	PRESENCE optional},
	...
}


-- **************************************************************
--
-- PDU Session Resource Modification Required Info - MN terminated
--
-- **************************************************************


PDUSessionResourceModRqdInfo-MNterminated ::= SEQUENCE {
	dRBsToBeModified				DRBsToBeModified-List-ModRqd-MNterminated				OPTIONAL,
	dRBsToBeReleased				DRB-List-withCause											OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceModRqdInfo-MNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceModRqdInfo-MNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


DRBsToBeModified-List-ModRqd-MNterminated ::= SEQUENCE (SIZE(1..maxnoofDRBs)) OF DRBsToBeModified-List-ModRqd-MNterminated-Item

DRBsToBeModified-List-ModRqd-MNterminated-Item ::= SEQUENCE {
	drb-ID								DRB-ID,
	sN-DL-SCG-UP-TNLInfo				UPTransportLayerInformation,
	secondary-SN-DL-SCG-UP-TNLInfo		UPTransportLayerInformation		OPTIONAL,
	lCID								LCID							OPTIONAL,
	rlc-status							RLC-Status						OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {DRBsToBeModified-List-ModRqd-MNterminated-Item-ExtIEs} } OPTIONAL,
	...
}

DRBsToBeModified-List-ModRqd-MNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-Additional-PDCP-Duplication-TNL-List		CRITICALITY ignore	EXTENSION Additional-PDCP-Duplication-TNL-List	PRESENCE optional},
	...
}


-- **************************************************************
--
-- PDU Session Resource Modification Confirm Info - MN terminated
--
-- **************************************************************


PDUSessionResourceModConfirmInfo-MNterminated ::= SEQUENCE {
	iE-Extensions					ProtocolExtensionContainer { {PDUSessionResourceModConfirmInfo-MNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceModConfirmInfo-MNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- **************************************************************
--
-- PDU Session Resource Setup Complete Info - SN terminated
--
-- **************************************************************

PDUSessionResourceBearerSetupCompleteInfo-SNterminated ::= SEQUENCE {
	dRBsToBeSetupList 			SEQUENCE (SIZE(1..maxnoofDRBs)) OF DRBsToBeSetupList-BearerSetupComplete-SNterminated-Item,
	iE-Extensions				ProtocolExtensionContainer { {PDUSessionResourceBearerSetupCompleteInfo-SNterminated-ExtIEs} } 	OPTIONAL,
	...
}

PDUSessionResourceBearerSetupCompleteInfo-SNterminated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

DRBsToBeSetupList-BearerSetupComplete-SNterminated-Item ::= SEQUENCE {
	dRB-ID						DRB-ID,
	mN-Xn-U-TNLInfoatM			UPTransportLayerInformation,
	iE-Extensions				ProtocolExtensionContainer { {DRBsToBeSetupList-BearerSetupComplete-SNterminated-Item-ExtIEs} } 	OPTIONAL,
	...
}

DRBsToBeSetupList-BearerSetupComplete-SNterminated-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ID id-Secondary-MN-Xn-U-TNLInfoatM	CRITICALITY ignore	EXTENSION UPTransportLayerInformation		PRESENCE optional},
	...
}

-- **************************************************************
--
-- PDU Session related message level IEs END
--
-- **************************************************************

PDUSessionResourceSecondaryRATUsageList ::= SEQUENCE (SIZE(1..maxnoofPDUSessions)) OF PDUSessionResourceSecondaryRATUsageItem

PDUSessionResourceSecondaryRATUsageItem ::= SEQUENCE {
	pDUSessionID								PDUSession-ID,
	secondaryRATUsageInformation						SecondaryRATUsageInformation,
	iE-Extensions		ProtocolExtensionContainer { {PDUSessionResourceSecondaryRATUsageItem-ExtIEs} }	OPTIONAL,
	...
}

PDUSessionResourceSecondaryRATUsageItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PDUSessionUsageReport ::= SEQUENCE {
	rATType								ENUMERATED {nr, eutra, ..., nr-unlicensed, e-utra-unlicensed},
	pDUSessionTimedReportList			VolumeTimedReportList,
	iE-Extensions						ProtocolExtensionContainer { {PDUSessionUsageReport-ExtIEs} } OPTIONAL,
...
}

PDUSessionUsageReport-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PDUSessionType ::= ENUMERATED {ipv4, ipv6, ipv4v6, ethernet, unstructured, ...}

PDUSession-ID	::= INTEGER (0..255)

PDUSessionNetworkInstance	::= INTEGER (1..256, ...)

PDUSessionCommonNetworkInstance	::= OCTET STRING

PDUSession-PairID	::= INTEGER (0..255, ...)



Periodical ::= SEQUENCE {
	iE-Extensions		ProtocolExtensionContainer { { Periodical-ExtIEs} } OPTIONAL,
	...
}



Periodical-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

Permutation ::= ENUMERATED {dfu, ufd, ...}


PLMN-Identity ::= OCTET STRING (SIZE(3))

PLMNAreaBasedQMC ::= SEQUENCE {
	plmnListforQMC		PLMNListforQMC,
	iE-Extensions		ProtocolExtensionContainer { {PLMNAreaBasedQMC-ExtIEs} } OPTIONAL,
	...
}

PLMNAreaBasedQMC-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PLMNListforQMC ::= SEQUENCE (SIZE(1..maxnoofPLMNforQMC)) OF PLMN-Identity

PCIListForMDT ::= SEQUENCE (SIZE(1.. maxnoofNeighPCIforMDT)) OF NRPCI


PNI-NPN-Restricted-Information ::= ENUMERATED { restriced, not-restricted, ...}

PortNumber ::= BIT STRING (SIZE (16))

PosPartialUEContextInfo ::= SEQUENCE {
	requestedSRSTransmissionCharacteristics		RequestedSRSTransmissionCharacteristics	OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { PosPartialUEContextInfo-ExtIEs} }	OPTIONAL,
	...
}

PosPartialUEContextInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


PredictedUETrajectory-Item ::= SEQUENCE{
	predictedtrajectoryCellInfo		PredictedTrajectoryCellInfo,
	iE-Extensions					ProtocolExtensionContainer { { PredictedUETrajectory-Item-ExtIEs} }	OPTIONAL,
	...
}

PredictedUETrajectory-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


PredictedTrajectoryCellInfo::= CHOICE {
	nG-RAN-Cell-Predicted			PredictedTrajectoryNGRANCellInfo,
	choice-extension				ProtocolIE-Single-Container { { PredictedTrajectoryCellInfo-ExtIEs} }
}

PredictedTrajectoryCellInfo-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

PredictedTrajectoryNGRANCellInfo ::= SEQUENCE {
	globalNG-RANCell-ID				GlobalNG-RANCell-ID,
	predictedTimeUEStaysInCell		INTEGER (0..4095)	OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { PredictedTrajectoryNGRANCellInfo-ExtIEs} }	OPTIONAL,
	...
}

PredictedTrajectoryNGRANCellInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}



PriorityLevelQoS ::= INTEGER (1..127, ...)


ProtectedE-UTRAResourceIndication ::= SEQUENCE {
	activationSFN					ActivationSFN,
	protectedResourceList			ProtectedE-UTRAResourceList,
	mbsfnControlRegionLength		MBSFNControlRegionLength					OPTIONAL,
	pDCCHRegionLength				INTEGER (1..3),
	iE-Extensions					ProtocolExtensionContainer { {ProtectedE-UTRAResourceIndication-ExtIEs} } 	OPTIONAL,
	...
}

ProtectedE-UTRAResourceIndication-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ProtectedE-UTRAResourceList ::= SEQUENCE (SIZE (1.. maxnoofProtectedResourcePatterns)) OF ProtectedE-UTRAResource-Item

ProtectedE-UTRAResource-Item ::= SEQUENCE {
	resourceType							ENUMERATED {downlinknonCRS, cRS, uplink, ...},
	intra-PRBProtectedResourceFootprint		BIT STRING (SIZE(84, ...)),
	protectedFootprintFrequencyPattern		BIT STRING (SIZE(6..110, ...)),
	protectedFootprintTimePattern			ProtectedE-UTRAFootprintTimePattern,
	iE-Extensions					ProtocolExtensionContainer { {ProtectedE-UTRAResource-Item-ExtIEs} } 	OPTIONAL,
	...
}

ProtectedE-UTRAResource-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ProtectedE-UTRAFootprintTimePattern ::= SEQUENCE {
	protectedFootprintTimeperiodicity			INTEGER (1..320, ...),
	protectedFootrpintStartTime					INTEGER (1..20, ...),
	iE-Extensions					ProtocolExtensionContainer { {ProtectedE-UTRAFootprintTimePattern-ExtIEs} } 	OPTIONAL,
	...
}

ProtectedE-UTRAFootprintTimePattern-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PrivacyIndicator ::= ENUMERATED {
	immediate-MDT,
	logged-MDT,
	...
}


PSCellChangeHistory ::= ENUMERATED {reporting-full-history, ...}

PSCellHistoryInformationRetrieve ::= ENUMERATED {query, ...}

PSCellListContainer ::= OCTET STRING

PNI-NPN-AreaScopeofMDT ::= SEQUENCE {
	cAGListforMDT		CAGListforMDT,
	iE-Extensions		ProtocolExtensionContainer { {PNI-NPN-AreaScopeofMDT-ExtIEs} } OPTIONAL,
	...
}

PNI-NPN-AreaScopeofMDT-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

PNI-NPNBasedMDT::= SEQUENCE {
	cAGListforMDT		CAGListforMDT,
	iE-Extensions		ProtocolExtensionContainer { {PNI-NPNBasedMDT-ExtIEs} } OPTIONAL,
	...
}

PNI-NPNBasedMDT-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- Q


QMCConfigInfo ::= SEQUENCE {
	uEAppLayerMeasInfoList			UEAppLayerMeasInfoList,
	iE-Extensions					ProtocolExtensionContainer { {QMCConfigInfo-ExtIEs} }	OPTIONAL,
	...
}

QMCConfigInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

UEAppLayerMeasInfoList ::= SEQUENCE (SIZE(1..maxnoofUEAppLayerMeas)) OF UEAppLayerMeasInfo-Item

UEAppLayerMeasInfo-Item ::= SEQUENCE {
	uEAppLayerMeasConfigInfo	UEAppLayerMeasConfigInfo,
	iE-Extensions				ProtocolExtensionContainer { { UEAppLayerMeasInfo-Item-ExtIEs} } 	OPTIONAL,
	...
}

UEAppLayerMeasInfo-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

QMCCoordinationRequest ::= SEQUENCE {
	mN-to-SN-QMCCoordRequestList	MN-to-SN-QMCCoordRequestList	OPTIONAL,
	sN-to-MN-QMCCoordRequestList	SN-to-MN-QMCCoordRequestList	OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {QMCCoordinationRequest-ExtIEs} }	OPTIONAL,
	...
}

QMCCoordinationRequest-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


MN-to-SN-QMCCoordRequestList ::= SEQUENCE (SIZE(1..maxnoofUEAppLayerMeas)) OF MN-to-SN-QMCCoordRequestList-Item


MN-to-SN-QMCCoordRequestList-Item ::= SEQUENCE {
	qOEReference					QOEReference,
	qOEMeasConfigAppLayerID			QOEMeasConfAppLayerID						OPTIONAL,
	measCollectionEntityIPAddress	MeasCollectionEntityIPAddress				OPTIONAL,
	qoEReportingPathRequest			ENUMERATED{srb4, srb5, ...}					OPTIONAL,
	rVQoEReportingPathRequest		ENUMERATED{srb4, srb5, ...}					OPTIONAL,
	furtherRVQoEInterestInquiry		ENUMERATED{true, ...}						OPTIONAL,
	furtherRVQoEReportingPathInquiry		ENUMERATED{true, ...}				OPTIONAL,
	currentRVQoEConfig				RVQoEConfig									OPTIONAL,
	availableRVQoEMetrics			AvailableRVQoEMetrics						OPTIONAL,
	configReleaseIndication			ENUMERATED{rvqoe,qoe-and-rvqoe, ...}		OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { MN-to-SN-QMCCoordRequestList-Item-ExtIEs} }	OPTIONAL,
	...
}

MN-to-SN-QMCCoordRequestList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SN-to-MN-QMCCoordRequestList ::= SEQUENCE (SIZE(1..maxnoofUEAppLayerMeas)) OF SN-to-MN-QMCCoordRequestList-Item


SN-to-MN-QMCCoordRequestList-Item ::= SEQUENCE {
	qOEReference					QOEReference,
	measCollectionEntityIPAddress	MeasCollectionEntityIPAddress				OPTIONAL,
	qoEReportingPathRequest			ENUMERATED{srb4, srb5, ...}					OPTIONAL,
	rVQoEReportingPathRequest		ENUMERATED{srb4, srb5, ...}					OPTIONAL,
	furtherRVQoEInterestInquiry		ENUMERATED{true, ...}						OPTIONAL,
	furtherRVQoEReportingPathInquiry		ENUMERATED{true, ...}				OPTIONAL,
	currentRVQoEConfig				RVQoEConfig									OPTIONAL,
	availableRVQoEMetrics		AvailableRVQoEMetrics							OPTIONAL,
	configReleaseIndication			ENUMERATED{rvqoe,qoe-and-rvqoe, ...}		OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { SN-to-MN-QMCCoordRequestList-Item-ExtIEs} }	OPTIONAL,
	...
}

SN-to-MN-QMCCoordRequestList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


QMCCoordinationResponse ::= SEQUENCE {
	mN-to-SN-QMCCoordResponseList	MN-to-SN-QMCCoordResponseList	OPTIONAL,
	sN-to-MN-QMCCoordResponseList	SN-to-MN-QMCCoordResponseList	OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {QMCCoordinationResponse-ExtIEs} }	OPTIONAL,
	...
}

QMCCoordinationResponse-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


MN-to-SN-QMCCoordResponseList ::= SEQUENCE (SIZE(1..maxnoofUEAppLayerMeas)) OF MN-to-SN-QMCCoordResponseList-Item


MN-to-SN-QMCCoordResponseList-Item ::= SEQUENCE {
	qOEReference					QOEReference,
	qOEMeasConfigAppLayerID			QOEMeasConfAppLayerID						OPTIONAL,
	qoEConfigSendingPath			ENUMERATED{mn, sn, ...}						OPTIONAL,
	qoEReportingPathResponse		ENUMERATED{accepted, rejected, ...}			OPTIONAL,
	rVQoEReportingPathResponse		ENUMERATED{accepted, rejected, ...} 			OPTIONAL,
	furtherRVQoEInterestResponse	ENUMERATED{interested, not-interested, ...}	OPTIONAL,
	furtherRVQoEReportingPathResponse		ENUMERATED{srb4, srb5, ...}			OPTIONAL,
	preferredRVQoEConfig			RVQoEConfig									OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { MN-to-SN-QMCCoordResponseList-Item-ExtIEs} }	OPTIONAL,
	...
}

MN-to-SN-QMCCoordResponseList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SN-to-MN-QMCCoordResponseList ::= SEQUENCE (SIZE(1..maxnoofUEAppLayerMeas)) OF SN-to-MN-QMCCoordResponseList-Item


SN-to-MN-QMCCoordResponseList-Item ::= SEQUENCE {
	qOEReference					QOEReference,
	qoEReportingPathResponse		ENUMERATED{accepted, rejected, ...}										OPTIONAL,
	rVQoEReportingPathResponse		ENUMERATED{accepted, rejected, ...} 										OPTIONAL,
	furtherRVQoEInterestResponse	ENUMERATED{interested, not-interested, ...}								OPTIONAL,
	furtherRVQoEReportingPathResponse		ENUMERATED{srb4, srb5, ...}										OPTIONAL,
	preferredRVQoEConfig			RVQoEConfig																OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { SN-to-MN-QMCCoordResponseList-Item-ExtIEs} }	OPTIONAL,
	...
}

SN-to-MN-QMCCoordResponseList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


QoERVQoEReportingPaths ::= SEQUENCE {
	qoEReportingPath					ENUMERATED{srb4, srb5, ...}	OPTIONAL,
	rVQoEReportingPath					ENUMERATED{srb4, srb5, ...}	OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { {QoERVQoEReportingPaths-ExtIEs} } OPTIONAL,
	...
}

QoERVQoEReportingPaths-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

RVQoEConfig ::= SEQUENCE {
	availableRANVisibleQoEMetrics		AvailableRVQoEMetrics			OPTIONAL,
	reportingPeriodicity				RVQoEReportingPeriodicity		OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { {RVQoEConfig-ExtIEs} } OPTIONAL,
	...
}

RVQoEConfig-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

RVQoEReportingPeriodicity ::= ENUMERATED {
	ms120,
	ms240,
	ms480,
	ms640,
	ms1024,
	...
}

QOEMeasConfAppLayerID ::= INTEGER (0..15, ...)

QOEMeasStatus ::= ENUMERATED {ongoing, ...}

QOEReference ::= OCTET STRING (SIZE (6))

QoSCharacteristics ::= CHOICE {
	non-dynamic						NonDynamic5QIDescriptor,
	dynamic							Dynamic5QIDescriptor,
	choice-extension				ProtocolIE-Single-Container { {QoSCharacteristics-ExtIEs} }
}

QoSCharacteristics-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


QoSFlowIdentifier	::= INTEGER (0..63, ...)


QoSFlowLevelQoSParameters ::= SEQUENCE {
	qos-characteristics			QoSCharacteristics,
	allocationAndRetentionPrio	AllocationandRetentionPriority,
	gBRQoSFlowInfo				GBRQoSFlowInfo														OPTIONAL,
	reflectiveQoS				ReflectiveQoSAttribute												OPTIONAL,
	additionalQoSflowInfo		ENUMERATED {more-likely, ...}										OPTIONAL,
	iE-Extensions				ProtocolExtensionContainer { {QoSFlowLevelQoSParameters-ExtIEs} } OPTIONAL,
	...
}

QoSFlowLevelQoSParameters-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ID id-QoSMonitoringRequest					CRITICALITY ignore	EXTENSION QosMonitoringRequest					PRESENCE optional}|
	{ID id-QosMonitoringReportingFrequency		CRITICALITY ignore	EXTENSION QosMonitoringReportingFrequency		PRESENCE optional}|
	{ID id-QoSMonitoringDisabled				CRITICALITY ignore	EXTENSION QoSMonitoringDisabled					PRESENCE optional}|
	{ID id-PDUSetQoSParameters					CRITICALITY ignore	EXTENSION PDUSetQoSParameters						PRESENCE optional},
	...
}


QoSFlowMappingIndication ::= ENUMERATED {
	ul,
	dl,
	...
}


QoSFlowNotificationControlIndicationInfo ::= SEQUENCE (SIZE (1..maxnoofQoSFlows)) OF QoSFlowNotify-Item

QoSFlowNotify-Item ::= SEQUENCE {
	qosFlowIdentifier			QoSFlowIdentifier,
	notificationInformation		ENUMERATED {fulfilled, not-fulfilled, ...},
	iE-Extensions				ProtocolExtensionContainer { {QoSFlowNotificationControlIndicationInfo-ExtIEs} } OPTIONAL,
	...
}

QoSFlowNotificationControlIndicationInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
{	ID id-CurrentQoSParaSetIndex	CRITICALITY ignore	EXTENSION QoSParaSetNotifyIndex		PRESENCE optional },
	...
}


QoSFlows-List ::= SEQUENCE (SIZE (1..maxnoofQoSFlows)) OF QoSFlow-Item

QoSFlow-Item ::= SEQUENCE {
	qfi								QoSFlowIdentifier,
	qosFlowMappingIndication		QoSFlowMappingIndication					OPTIONAL,
	iE-Extension		ProtocolExtensionContainer { {QoSFlow-Item-ExtIEs} }	OPTIONAL,
	...
}

QoSFlow-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


QoSFlows-List-withCause ::= SEQUENCE (SIZE (1..maxnoofQoSFlows)) OF QoSFlowwithCause-Item

QoSFlowwithCause-Item ::= SEQUENCE {
	qfi					QoSFlowIdentifier,
	cause				Cause					OPTIONAL,
	iE-Extension		ProtocolExtensionContainer { {QoSFlowwithCause-Item-ExtIEs} }	OPTIONAL,
	...
}

QoSFlowwithCause-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

QoS-Mapping-Information ::= SEQUENCE {
	dscp							BIT STRING (SIZE(6))			OPTIONAL,
	flow-label						BIT STRING (SIZE(20))			OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {QoS-Mapping-Information-ExtIEs} }	OPTIONAL,
	...
}

QoS-Mapping-Information-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

QoSParaSetIndex ::= INTEGER (1..8,...)
QoSParaSetNotifyIndex ::= INTEGER (0..8,...)


QoSFlowsAdmitted-List ::= SEQUENCE (SIZE (1..maxnoofQoSFlows)) OF QoSFlowsAdmitted-Item

QoSFlowsAdmitted-Item ::= SEQUENCE {
	qfi								QoSFlowIdentifier,
	iE-Extension		ProtocolExtensionContainer { {QoSFlowsAdmitted-Item-ExtIEs} }	OPTIONAL,
	...
}

QoSFlowsAdmitted-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
{ ID id-CurrentQoSParaSetIndex	CRITICALITY ignore	EXTENSION QoSParaSetIndex	PRESENCE optional	},
	...
}


QoSFlowsToBeSetup-List ::= SEQUENCE (SIZE (1..maxnoofQoSFlows)) OF QoSFlowsToBeSetup-Item

QoSFlowsToBeSetup-Item ::= SEQUENCE {
	qfi								QoSFlowIdentifier,
	qosFlowLevelQoSParameters		QoSFlowLevelQoSParameters,
	e-RAB-ID						E-RAB-ID											OPTIONAL,
	iE-Extension		ProtocolExtensionContainer { {QoSFlowsToBeSetup-Item-ExtIEs} }	OPTIONAL,
	...
}

QoSFlowsToBeSetup-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-TSCTrafficCharacteristics								CRITICALITY ignore	EXTENSION TSCTrafficCharacteristics 	PRESENCE optional}|
	{ ID id-RedundantQoSFlowIndicator								CRITICALITY ignore	EXTENSION RedundantQoSFlowIndicator	PRESENCE optional}|
	{ ID id-ECNMarkingorCongestionInformationReportingRequest		CRITICALITY ignore	EXTENSION ECNMarkingorCongestionInformationReportingRequest	PRESENCE optional},
	...
}

QoSFlowsUsageReportList ::= SEQUENCE (SIZE(1..maxnoofQoSFlows)) OF QoSFlowsUsageReport-Item

QoSFlowsUsageReport-Item ::= SEQUENCE {
	qosFlowIdentifier					QoSFlowIdentifier,
	rATType								ENUMERATED {nr, eutra, ..., nr-unlicensed, e-utra-unlicensed},
	qoSFlowsTimedReportList				VolumeTimedReportList,
	iE-Extensions						ProtocolExtensionContainer { {QoSFlowsUsageReport-Item-ExtIEs} } OPTIONAL,
...
}

QoSFlowsUsageReport-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

QosMonitoringRequest ::= ENUMERATED {ul, dl, both}
QoSMonitoringDisabled ::= ENUMERATED {true, ...}
QosMonitoringReportingFrequency ::= INTEGER (1..1800, ...)

-- R

RACH-Config-Common	::= OCTET STRING

RACH-Config-Common-IAB	::= OCTET STRING

RAReport	::= SEQUENCE (SIZE(1.. maxnoofRAReports)) OF RAReportList-Item
RAReportList-Item	::= SEQUENCE {
	rAReport				RAReportContainer,
	iE-Extensions						ProtocolExtensionContainer { { RAReportList-Item-ExtIEs} }	OPTIONAL,
	...
}

RAReportList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-UEAssistantIdentifier	CRITICALITY ignore	EXTENSION NG-RANnodeUEXnAPID 	PRESENCE optional}|
	{ ID id-PSCellListContainer		CRITICALITY ignore	EXTENSION PSCellListContainer	PRESENCE optional},
	...
}

RAReportContainer	::= OCTET STRING


RadioResourceStatus	::= CHOICE {
	ng-eNB-RadioResourceStatus	NG-eNB-RadioResourceStatus,
	gNB-RadioResourceStatus		GNB-RadioResourceStatus,
	choice-extension			ProtocolIE-Single-Container { { RadioResourceStatus-ExtIEs} }

}

RadioResourceStatus-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


RANAC ::= INTEGER (0..255)


RANAreaID ::= SEQUENCE {
	tAC					TAC,
	rANAC				RANAC												OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {RANAreaID-ExtIEs} } 	OPTIONAL,
	...
}

RANAreaID-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


RANAreaID-List ::= SEQUENCE (SIZE(1..maxnoofRANAreasinRNA)) OF RANAreaID

Range ::= ENUMERATED {m50, m80, m180, m200, m350, m400, m500, m700, m1000, ...}

RANPagingArea ::= SEQUENCE {
	pLMN-Identity			PLMN-Identity,
	rANPagingAreaChoice		RANPagingAreaChoice,
	iE-Extensions			ProtocolExtensionContainer { {RANPagingArea-ExtIEs} } OPTIONAL,
	...
}

RANPagingArea-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

RANPagingAreaChoice ::= CHOICE {
	cell-List			NG-RAN-Cell-Identity-ListinRANPagingArea,
	rANAreaID-List		RANAreaID-List,
	choice-extension	ProtocolIE-Single-Container { {RANPagingAreaChoice-ExtIEs} }
}

RANPagingAreaChoice-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


RANPagingAttemptInfo ::= SEQUENCE {
	pagingAttemptCount					INTEGER (1..16, ...),
	intendedNumberOfPagingAttempts		INTEGER (1..16, ...),
	nextPagingAreaScope					ENUMERATED {same, changed, ...}	OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { {RANPagingAttemptInfo-ExtIEs} } OPTIONAL,
	...
}

RANPagingAttemptInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

RANPagingFailure		::= 	ENUMERATED {
	true,
	...
}

RBsetConfiguration ::= SEQUENCE {
	subcarrierSpacing 		SubcarrierSpacing,
	rBsetSize				ENUMERATED {rb2, rb4, rb8, rb16, rb32, rb64},
	numberofRBSets			INTEGER(1.. maxnoofRBsetsPerCell),
	iE-Extensions				ProtocolExtensionContainer { { RBsetConfiguration-ExtIEs} } OPTIONAL,
	...
}

RBsetConfiguration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


Redcap-Bcast-Information ::= BIT STRING(SIZE(8))

RedundantQoSFlowIndicator ::= ENUMERATED {true, false}

RedundantPDUSessionInformation ::= SEQUENCE {
	rSN					RSN,
	iE-Extensions		ProtocolExtensionContainer { {RedundantPDUSessionInformation-ExtIEs} }	OPTIONAL,
	...
}

RedundantPDUSessionInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-PDUSession-PairID	CRITICALITY ignore	EXTENSION PDUSession-PairID	PRESENCE optional	},
	...
}

RSN ::= ENUMERATED {v1, v2, ...}


ReflectiveQoSAttribute ::= ENUMERATED {subject-to-reflective-QoS, ...}

RequestedSRSTransmissionCharacteristics ::= OCTET STRING


RoutingID ::= OCTET STRING

ReplacingCells ::= SEQUENCE (SIZE(0.. maxnoofCellsinNG-RANnode)) OF ReplacingCells-Item

ReplacingCells-Item ::= SEQUENCE {
	globalNG-RANCell-ID				GlobalCell-ID,
	iE-Extensions		ProtocolExtensionContainer { {ReplacingCells-Item-ExtIEs} }	OPTIONAL,
	...
}

ReplacingCells-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ReportAmountMDT ::= ENUMERATED{r1, r2, r4, r8, r16, r32, r64, infinity, ...}


ReportArea ::= ENUMERATED {
	cell,
	...
}

ReportConfigContainer ::= OCTET STRING

ReportIntervalMDT ::= ENUMERATED {ms120, ms240, ms480, ms640, ms1024, ms2048, ms5120, ms10240, min1, min6, min12, min30, min60, ...}

ReportType ::= CHOICE {
	periodical					Periodical,
	eventTriggered				EventTriggered,
	...,
	choice-extension		ProtocolIE-Single-Container { {ReportType-ExtIEs} }
}

ReportType-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


ExtendedReportIntervalMDT ::= ENUMERATED {
	ms20480,
	ms40960,
	...
}


ReportCharacteristics ::= BIT STRING(SIZE(32))

ReportCharacteristicsForDataCollection ::= BIT STRING(SIZE(32))



ReportingPeriodicity ::= ENUMERATED {
	half-thousand-ms,
	one-thousand-ms,
	two-thousand-ms,
	five-thousand-ms,
	ten-thousand-ms,
	...
}

ReportingPeriodicityForDataCollection ::= ENUMERATED {
	half-thousand-ms,
	one-thousand-ms,
	two-thousand-ms,
	five-thousand-ms,
	ten-thousand-ms,
	...
}

RequestedPredictionTime ::= INTEGER (1..60, ...)


RegistrationRequest ::= ENUMERATED {start, stop, add, ... }

RegistrationRequestForDataCollection ::= ENUMERATED {start, stop, ... }


RequestReferenceID ::= INTEGER (1..64, ...)


ReservedSubframePattern ::= SEQUENCE {
	subframeType					ENUMERATED {mbsfn, non-mbsfn, ...},
	reservedSubframePattern			BIT STRING (SIZE(10..160)),
	mbsfnControlRegionLength		MBSFNControlRegionLength					OPTIONAL,
	iE-Extension					ProtocolExtensionContainer { {ReservedSubframePattern-ExtIEs} } OPTIONAL,
	...
}

ReservedSubframePattern-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}



ResetRequestTypeInfo ::= CHOICE {
	fullReset			ResetRequestTypeInfo-Full,
	partialReset		ResetRequestTypeInfo-Partial,
	choice-extension	ProtocolIE-Single-Container { {ResetRequestTypeInfo-ExtIEs} }
}

ResetRequestTypeInfo-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

ResetRequestTypeInfo-Full ::= SEQUENCE {
	iE-Extension					ProtocolExtensionContainer { {ResetRequestTypeInfo-Full-ExtIEs} } OPTIONAL,
	...
}

ResetRequestTypeInfo-Full-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ResetRequestTypeInfo-Partial ::= SEQUENCE {
	ue-contexts-ToBeReleasedList	ResetRequestPartialReleaseList,
	iE-Extension					ProtocolExtensionContainer { {ResetRequestTypeInfo-Partial-ExtIEs} } OPTIONAL,
	...
}

ResetRequestTypeInfo-Partial-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ResetRequestPartialReleaseList ::= SEQUENCE (SIZE(1..maxnoofUEContexts)) OF ResetRequestPartialReleaseItem

ResetRequestPartialReleaseItem ::= SEQUENCE {
	ng-ran-node1UEXnAPID						NG-RANnodeUEXnAPID			OPTIONAL,
	ng-ran-node2UEXnAPID						NG-RANnodeUEXnAPID			OPTIONAL,
	iE-Extensions							ProtocolExtensionContainer { {ResetRequestPartialReleaseItem-ExtIEs} } OPTIONAL,
	...
}

ResetRequestPartialReleaseItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ResetResponseTypeInfo ::= CHOICE {
	fullReset			ResetResponseTypeInfo-Full,
	partialReset		ResetResponseTypeInfo-Partial,
	choice-extension	ProtocolIE-Single-Container { {ResetResponseTypeInfo-ExtIEs} }
}

ResetResponseTypeInfo-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

ResetResponseTypeInfo-Full ::= SEQUENCE {
	iE-Extension					ProtocolExtensionContainer { {ResetResponseTypeInfo-Full-ExtIEs} } OPTIONAL,
	...
}

ResetResponseTypeInfo-Full-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ResetResponseTypeInfo-Partial ::= SEQUENCE {
	ue-contexts-AdmittedToBeReleasedList	ResetResponsePartialReleaseList,
	iE-Extension					ProtocolExtensionContainer { {ResetResponseTypeInfo-Partial-ExtIEs} } OPTIONAL,
	...
}

ResetResponseTypeInfo-Partial-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ResetResponsePartialReleaseList ::= SEQUENCE (SIZE(1..maxnoofUEContexts)) OF ResetResponsePartialReleaseItem

ResetResponsePartialReleaseItem ::= SEQUENCE {
	ng-ran-node1UEXnAPID						NG-RANnodeUEXnAPID		OPTIONAL,
	ng-ran-node2UEXnAPID						NG-RANnodeUEXnAPID		OPTIONAL,
	iE-Extensions							ProtocolExtensionContainer { {ResetResponsePartialReleaseItem-ExtIEs} } OPTIONAL,
	...
}

ResetResponsePartialReleaseItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


RLCMode	::= ENUMERATED {
	rlc-am,
	rlc-um-bidirectional,
	rlc-um-unidirectional-ul,
	rlc-um-unidirectional-dl,
	...
	}


RLC-Status ::= SEQUENCE {
	reestablishment-Indication 	Reestablishment-Indication,
	iE-Extensions					ProtocolExtensionContainer { {RLC-Status-ExtIEs} } OPTIONAL,
	...
}

RLC-Status-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

RLCDuplicationInformation ::= 		SEQUENCE {
	rLCDuplicationStateList 		RLCDuplicationStateList,
	rLC-PrimaryIndicator 		ENUMERATED {true, false}		OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { {RLCDuplicationInformation-ItemExtIEs} }	OPTIONAL
}

RLCDuplicationInformation-ItemExtIEs 	XNAP-PROTOCOL-EXTENSION ::= {
	...
}

RLCDuplicationStateList	::= 	SEQUENCE (SIZE(1..maxnoofRLCDuplicationstate)) OF RLCDuplicationState-Item

RLCDuplicationState-Item ::=	SEQUENCE {
	duplicationState			ENUMERATED {active,inactive, ...},
	iE-Extensions	ProtocolExtensionContainer { {RLCDuplicationState-ItemExtIEs } }	OPTIONAL,
	...
}

RLCDuplicationState-ItemExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

Reestablishment-Indication ::= ENUMERATED {
	reestablished,
	...
}


RFSP-Index ::= INTEGER (1..256)


RRCConfigIndication ::= ENUMERATED {
	full-config,
	delta-config,
	...
}

RRCConnections::= SEQUENCE {
	noofRRCConnections							NoofRRCConnections,
	availableRRCConnectionCapacityValue			AvailableRRCConnectionCapacityValue,
	iE-Extensions				ProtocolExtensionContainer { { RRCConnections-ExtIEs} }	OPTIONAL,
	...
}

RRCConnections-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


RRCConnReestab-Indicator ::= ENUMERATED { reconfigurationFailure, handoverFailure, otherFailure, ...}

RRCReestab-initiated ::= SEQUENCE {
	rRRCReestab-initiated-reporting	RRCReestab-Initiated-Reporting,
	iE-Extensions			ProtocolExtensionContainer { { RRCReestab-initiated-ExtIEs} } OPTIONAL,
	...
}

RRCReestab-initiated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

RRCReestab-Initiated-Reporting ::= CHOICE {
	rRCReestab-reporting-wo-UERLFReport					RRCReestab-Initiated-Reporting-wo-UERLFReport,
	rRCReestab-reporting-with-UERLFReport				RRCReestab-Initiated-Reporting-with-UERLFReport,
	choice-extension				ProtocolIE-Single-Container { {RRCReestab-Initiated-Reporting-ExtIEs} }
}

RRCReestab-Initiated-Reporting-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

RRCReestab-Initiated-Reporting-wo-UERLFReport ::= SEQUENCE {
	failureCellPCI		NG-RAN-CellPCI,
	reestabCellCGI		GlobalNG-RANCell-ID,
	c-RNTI				C-RNTI,
	shortMAC-I			MAC-I,
	iE-Extensions		ProtocolExtensionContainer { { RRCReestab-Initiated-Reporting-wo-UERLFReport-ExtIEs} } OPTIONAL,
	...
}

RRCReestab-Initiated-Reporting-wo-UERLFReport-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-RRCConnReestab-Indicator 	CRITICALITY ignore	EXTENSION RRCConnReestab-Indicator		PRESENCE optional },
	...
}

RRCReestab-Initiated-Reporting-with-UERLFReport ::= SEQUENCE {
	uERLFReportContainer	UERLFReportContainer,
	iE-Extensions			ProtocolExtensionContainer { {RRCReestab-Initiated-Reporting-with-UERLFReport-ExtIEs} } OPTIONAL,
	...
}

RRCReestab-Initiated-Reporting-with-UERLFReport-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

RRCSetup-initiated ::= SEQUENCE {
	rRRCSetup-Initiated-Reporting	RRCSetup-Initiated-Reporting,
	uERLFReportContainer			UERLFReportContainer			OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { { RRCSetup-initiated-ExtIEs} } OPTIONAL,
	...
}

RRCSetup-initiated-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

RRCSetup-Initiated-Reporting ::= CHOICE {
	rRCSetup-reporting-with-UERLFReport				RRCSetup-Initiated-Reporting-with-UERLFReport,
	choice-extension				ProtocolIE-Single-Container { {RRCSetup-Initiated-Reporting-ExtIEs} }
}

RRCSetup-Initiated-Reporting-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

RRCSetup-Initiated-Reporting-with-UERLFReport ::= SEQUENCE {
	uERLFReportContainer	UERLFReportContainer,
	iE-Extensions			ProtocolExtensionContainer { {RRCSetup-Initiated-Reporting-with-UERLFReport-ExtIEs} } OPTIONAL,
	...
}

RRCSetup-Initiated-Reporting-with-UERLFReport-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


RRCResumeCause ::= ENUMERATED {
	rna-Update,
	...
}


RaReportIndicationList ::= SEQUENCE (SIZE(1..maxnoofUEsforRAReportIndications)) OF RaReportIndicationList-Item

RaReportIndicationList-Item ::= SEQUENCE {
	m-NG-RAN-node-UE-XnAP-ID			NG-RANnodeUEXnAPID,
	iE-Extensions						ProtocolExtensionContainer { { RaReportIndicationList-Item-ExtIEs} }	OPTIONAL,
	...
}

RaReportIndicationList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}



RadioResourceStatusNR-U	::= SEQUENCE {
	dL-Total-PRB-usage			INTEGER (0..100),
	uL-Total-PRB-usage			INTEGER (0..100),
	iE-Extensions		ProtocolExtensionContainer {{ RadioResourceStatusNR-U-ExtIEs}} OPTIONAL,
	...
}

RadioResourceStatusNR-U-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- S

SCGreconfigNotification ::= ENUMERATED {executed, ... , executed-deleted, deleted }

S-NSSAIListQoE ::= SEQUENCE (SIZE(1..maxnoofSNSSAIforQMC)) OF S-NSSAI

S-BasedMDT ::= SEQUENCE {
	ng-ran-TraceID				NG-RANTraceID,
	iE-Extension				ProtocolExtensionContainer { {S-BasedMDT-ExtIEs} }	OPTIONAL,
	...
}

S-BasedMDT-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

S-CPAC-Request ::= ENUMERATED {initiation, ...}

S-CPAC-Request-Info ::= SEQUENCE {
	s-CPAC-Security-Config-List			S-CPAC-SecurityConfig-List,
	s-CPAC-MultiTargetSN-List			S-CPAC-MultiTargetSN-List			OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { {S-CPAC-Request-Info-ExtIEs} } OPTIONAL,
	...
}

S-CPAC-Request-Info-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

S-CPAC-ReferenceConfig-Request ::= ENUMERATED {request, ...}

S-CPAC-SecurityConfig-List ::= SEQUENCE (SIZE(1..maxnoofSecurityConfigurations)) OF S-CPAC-SecurityConfig-Item

S-CPAC-SecurityConfig-Item ::= SEQUENCE {
	s-ng-RANnode-SecurityKey			S-NG-RANnode-SecurityKey,
	sk-counter							SK-COUNTER,
	iE-Extensions		ProtocolExtensionContainer { {S-CPAC-SecurityConfig-Item-ExtIEs} } OPTIONAL,
	...
}

S-CPAC-SecurityConfig-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

S-CPAC-MultiTargetSN-List ::= SEQUENCE (SIZE(1..maxnoofTargetSNsMinusOne)) OF S-CPAC-MultiTargetSN-Item

S-CPAC-MultiTargetSN-Item ::= SEQUENCE {
	target-S-NG-RANnodeID				GlobalNG-RANNode-ID,
	recommendedCandidatePSCells			OCTET STRING,
	iE-Extensions		ProtocolExtensionContainer { {S-CPAC-MultiTargetSN-Item-ExtIEs} } OPTIONAL,
	...
}

S-CPAC-MultiTargetSN-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

S-CPAC-InterSN-ExecutionNotify ::= ENUMERATED {executed, ...}

ServiceType ::= ENUMERATED{
	qMC-for-streaming-service,
	qMC-for-MTSI-service,
	qMC-for-VR-service,
	...
}

SecondarydataForwardingInfoFromTarget-Item::= SEQUENCE {
	secondarydataForwardingInfoFromTarget				DataForwardingInfoFromTargetNGRANnode,
	iE-Extensions		ProtocolExtensionContainer { { SecondarydataForwardingInfoFromTarget-Item-ExtIEs} }	OPTIONAL,
	...
}

SecondarydataForwardingInfoFromTarget-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SecondarydataForwardingInfoFromTarget-List ::= SEQUENCE (SIZE(1..maxnoofMultiConnectivityMinusOne)) OF SecondarydataForwardingInfoFromTarget-Item

SCGActivationRequest ::= ENUMERATED {activate-scg, deactivate-scg, ...}

SCGActivationStatus ::= ENUMERATED {scg-activated, scg-deactivated, ...}

SCGConfigurationQuery	::= ENUMERATED {true, ...}

SCGIndicator	::=	ENUMERATED{released, ...}

SCGFailureReportContainer	::=	OCTET STRING

SDTSupportRequest ::= SEQUENCE {
	sdtindicator				SDTIndicator,
	sdtAssistantInfo			SDTAssistantInfo		OPTIONAL,
	iE-Extensions				ProtocolExtensionContainer { { SDTSupportRequest-ExtIEs} } OPTIONAL,
	...
}

SDTSupportRequest-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SDTIndicator ::= ENUMERATED {true, ...}

SDTAssistantInfo ::= ENUMERATED {single-packet, multiple-packets, ...}

SDT-Termination-Request	::= ENUMERATED {radio-link-problem, normal, ..., large-sdt-volume-from-BSR}

SDTPartialUEContextInfo ::= SEQUENCE {
	dRBsToBeSetup					SDT-DRBsToBeSetupList		OPTIONAL,
	sRBsToBeSetup					SDT-SRBsToBeSetupList,
	iE-Extensions					ProtocolExtensionContainer { { SDTPartialUEContextInfo-ExtIEs} } 	OPTIONAL,
	...
}
SDTPartialUEContextInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SDT-DRBsToBeSetupList ::= SEQUENCE (SIZE(1..maxnoofDRBs)) OF SDT-DRBsToBeSetupList-Item

SDT-DRBsToBeSetupList-Item	::= SEQUENCE {
	drb-ID							DRB-ID,
	uL-TNLInfo						UPTransportLayerInformation,
	dRB-RLC-Bearer-Configuration	OCTET STRING,
	dRB-QoS							QoSFlowLevelQoSParameters,
	rLC-Mode						RLCMode,
	s-nssai							S-NSSAI,
	pDCP-SNLength					PDCPSNLength,
	flows-Mapped-To-DRB-List		Flows-Mapped-To-DRB-List,
	iE-Extensions		ProtocolExtensionContainer { { SDT-DRBsToBeSetupList-Item-ExtIEs} } OPTIONAL,
	...
}

SDT-DRBsToBeSetupList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SDT-SRBsToBeSetupList ::= SEQUENCE (SIZE(1..maxnoofSRBs)) OF SDT-SRBsToBeSetupList-Item

SDT-SRBsToBeSetupList-Item	::= SEQUENCE {
	srb-ID							SRB-ID,
	sRB-RLC-Bearer-Configuration	OCTET STRING,
	iE-Extensions					ProtocolExtensionContainer { { SDT-SRBsToBeSetupList-Item-ExtIEs} } OPTIONAL,
	...
}

SDT-SRBsToBeSetupList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SRB-ID ::= INTEGER (0..4, ...)

SDTDataForwardingDRBList ::= SEQUENCE (SIZE(1..maxnoofDRBs)) OF SDTDataForwardingDRBList-Item

SDTDataForwardingDRBList-Item ::= SEQUENCE {
	drb-ID							DRB-ID,
	dL-TNLInfo						UPTransportLayerInformation		OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { SDTDataForwardingDRBList-Item-ExtIEs} } OPTIONAL,
	...
}

SDTDataForwardingDRBList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SecondaryRATUsageInformation ::= SEQUENCE {
	pDUSessionUsageReport		PDUSessionUsageReport				OPTIONAL,
	qosFlowsUsageReportList		QoSFlowsUsageReportList				OPTIONAL,
	iE-Extension				ProtocolExtensionContainer { {SecondaryRATUsageInformation-ExtIEs} }	OPTIONAL,
	...
}

SecondaryRATUsageInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SecurityIndication ::= SEQUENCE {
	integrityProtectionIndication			ENUMERATED {required, preferred, not-needed, ...},
	confidentialityProtectionIndication		ENUMERATED {required, preferred, not-needed, ...},
	maximumIPdatarate						MaximumIPdatarate												OPTIONAL,
-- This IE shall be present if the Integrity Protection IE within the Security Indication IE is present and set to "required" or "preferred". --
	iE-Extensions							ProtocolExtensionContainer { {SecurityIndication-ExtIEs} } OPTIONAL,
	...
}

SecurityIndication-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


SecurityResult ::= SEQUENCE {
	integrityProtectionResult				ENUMERATED {performed, not-performed, ...},
	confidentialityProtectionResult			ENUMERATED {performed, not-performed, ...},
	iE-Extensions							ProtocolExtensionContainer { {SecurityResult-ExtIEs} } OPTIONAL,
	...
}

SecurityResult-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SensorMeasurementConfiguration ::= SEQUENCE {
	sensorMeasConfig 				SensorMeasConfig,
	sensorMeasConfigNameList		SensorMeasConfigNameList			OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { SensorMeasurementConfiguration-ExtIEs } } OPTIONAL,
	...
}

SensorMeasurementConfiguration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SensorMeasConfigNameList ::= SEQUENCE (SIZE(1..maxnoofSensorName)) OF SensorName

SensorMeasConfig::= ENUMERATED {setup,...}

SensorName ::= SEQUENCE {
	uncompensatedBarometricConfig	ENUMERATED {true, ...}		OPTIONAL,
	ueSpeedConfig					ENUMERATED {true, ...}		OPTIONAL,
	ueOrientationConfig				ENUMERATED {true, ...}		OPTIONAL,
	iE-Extensions				ProtocolExtensionContainer { {SensorNameConfig-ExtIEs} } OPTIONAL,
...
}

SensorNameConfig-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- Served Cells E-UTRA IEs


ServedCellInformation-E-UTRA ::= SEQUENCE {
	e-utra-pci								E-UTRAPCI,
	e-utra-cgi								E-UTRA-CGI,
	tac										TAC,
	ranac									RANAC																					OPTIONAL,
	broadcastPLMNs							SEQUENCE (SIZE(1..maxnoofBPLMNs)) OF ServedCellInformation-E-UTRA-perBPLMN,
	e-utra-mode-info						ServedCellInformation-E-UTRA-ModeInfo,
	numberofAntennaPorts					NumberOfAntennaPorts-E-UTRA																OPTIONAL,
	prach-configuration						E-UTRAPRACHConfiguration																OPTIONAL,
	mBSFNsubframeInfo						MBSFNSubframeInfo-E-UTRA																OPTIONAL,
	multibandInfo							E-UTRAMultibandInfoList																	OPTIONAL,
	freqBandIndicatorPriority				ENUMERATED {not-broadcast, broadcast, ...} 												OPTIONAL,
	bandwidthReducedSI						ENUMERATED {scheduled, ...}																OPTIONAL,
	protectedE-UTRAResourceIndication		ProtectedE-UTRAResourceIndication														OPTIONAL,
	iE-Extensions				ProtocolExtensionContainer { {ServedCellInformation-E-UTRA-ExtIEs} }					OPTIONAL,
	...
}

ServedCellInformation-E-UTRA-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-BPLMN-ID-Info-EUTRA		CRITICALITY ignore	EXTENSION BPLMN-ID-Info-EUTRA		PRESENCE optional }|
	{ ID id-NPRACHConfiguration		CRITICALITY ignore	EXTENSION	NPRACHConfiguration		PRESENCE optional},
	...
}


ServedCellInformation-E-UTRA-perBPLMN ::= SEQUENCE {
	plmn-id					PLMN-Identity,
	iE-Extensions			ProtocolExtensionContainer { {ServedCellInformation-E-UTRA-perBPLMN-ExtIEs} } OPTIONAL,
	...
}

ServedCellInformation-E-UTRA-perBPLMN-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ServedCellInformation-E-UTRA-ModeInfo ::= CHOICE {
	fdd					ServedCellInformation-E-UTRA-FDDInfo,
	tdd					ServedCellInformation-E-UTRA-TDDInfo,
	choice-extension	ProtocolIE-Single-Container{ {ServedCellInformation-E-UTRA-ModeInfo-ExtIEs} }
}

ServedCellInformation-E-UTRA-ModeInfo-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


ServedCellInformation-E-UTRA-FDDInfo ::= SEQUENCE {
	ul-earfcn			E-UTRAARFCN,
	dl-earfcn			E-UTRAARFCN,
	ul-e-utraTxBW		E-UTRATransmissionBandwidth,
	dl-e-utraTxBW		E-UTRATransmissionBandwidth,
	iE-Extensions		ProtocolExtensionContainer { {ServedCellInformation-E-UTRA-FDDInfo-ExtIEs} } OPTIONAL,
	...
}

ServedCellInformation-E-UTRA-FDDInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-OffsetOfNbiotChannelNumberToDL-EARFCN	CRITICALITY reject	EXTENSION OffsetOfNbiotChannelNumberToEARFCN		PRESENCE optional}|
	{ ID id-OffsetOfNbiotChannelNumberToUL-EARFCN	CRITICALITY reject	EXTENSION OffsetOfNbiotChannelNumberToEARFCN		PRESENCE optional},
	...
}


ServedCellInformation-E-UTRA-TDDInfo ::= SEQUENCE {
	earfcn					E-UTRAARFCN,
	e-utraTxBW				E-UTRATransmissionBandwidth,
	subframeAssignmnet		ENUMERATED {sa0,sa1,sa2,sa3,sa4,sa5,sa6,...},
	specialSubframeInfo		SpecialSubframeInfo-E-UTRA,
	iE-Extensions			ProtocolExtensionContainer { {ServedCellInformation-E-UTRA-TDDInfo-ExtIEs} } OPTIONAL,
	...
}

ServedCellInformation-E-UTRA-TDDInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-OffsetOfNbiotChannelNumberToDL-EARFCN	CRITICALITY reject	EXTENSION OffsetOfNbiotChannelNumberToEARFCN		PRESENCE optional}|
	{ ID id-NBIoT-UL-DL-AlignmentOffset				CRITICALITY reject	EXTENSION NBIoT-UL-DL-AlignmentOffset					PRESENCE optional},
	...
}


ServedCells-E-UTRA ::= SEQUENCE (SIZE (1..maxnoofCellsinNG-RANnode)) OF ServedCells-E-UTRA-Item

ServedCells-E-UTRA-Item ::= SEQUENCE {
	served-cell-info-E-UTRA		ServedCellInformation-E-UTRA,
	neighbour-info-NR			NeighbourInformation-NR									OPTIONAL,
	neighbour-info-E-UTRA		NeighbourInformation-E-UTRA								OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {ServedCells-E-UTRA-Item-ExtIEs} } OPTIONAL,
	...
}

ServedCells-E-UTRA-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-SFN-Offset				CRITICALITY ignore EXTENSION SFN-Offset					PRESENCE optional },
...
}


ServedCellsToUpdate-E-UTRA ::= SEQUENCE {
	served-Cells-ToAdd-E-UTRA		ServedCells-E-UTRA																OPTIONAL,
	served-Cells-ToModify-E-UTRA	ServedCells-ToModify-E-UTRA														OPTIONAL,
	served-Cells-ToDelete-E-UTRA	SEQUENCE (SIZE (1..maxnoofCellsinNG-RANnode)) OF E-UTRA-CGI 						OPTIONAL,
iE-Extensions					ProtocolExtensionContainer { {ServedCellsToUpdate-E-UTRA-ExtIEs} }	OPTIONAL,
	...
}

ServedCellsToUpdate-E-UTRA-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ServedCells-ToModify-E-UTRA ::= SEQUENCE (SIZE (1..maxnoofCellsinNG-RANnode)) OF ServedCells-ToModify-E-UTRA-Item

ServedCells-ToModify-E-UTRA-Item ::= SEQUENCE {
	old-ECGI					E-UTRA-CGI,
	served-cell-info-E-UTRA		ServedCellInformation-E-UTRA,
	neighbour-info-NR			NeighbourInformation-NR											OPTIONAL,
	neighbour-info-E-UTRA		NeighbourInformation-E-UTRA										OPTIONAL,
	deactivation-indication		ENUMERATED {deactivated, ...}									OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {Served-cells-ToModify-E-UTRA-Item-ExtIEs} }	OPTIONAL,
	...
}

Served-cells-ToModify-E-UTRA-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-SFN-Offset				CRITICALITY ignore EXTENSION SFN-Offset					PRESENCE optional },
	...
}


-- Served Cells NR IEs


ServedCellInformation-NR ::= SEQUENCE {
	nrPCI								NRPCI,
	cellID								NR-CGI,
	tac									TAC,
	ranac								RANAC						OPTIONAL,
	broadcastPLMN						BroadcastPLMNs,
	nrModeInfo							NRModeInfo,
	measurementTimingConfiguration		OCTET STRING,
	connectivitySupport					Connectivity-Support,	
	iE-Extensions						ProtocolExtensionContainer { {ServedCellInformation-NR-ExtIEs} } OPTIONAL,
	...
}

ServedCellInformation-NR-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-BPLMN-ID-Info-NR				CRITICALITY ignore	EXTENSION BPLMN-ID-Info-NR						PRESENCE optional }|
	{ ID id-ConfiguredTACIndication			CRITICALITY ignore	EXTENSION ConfiguredTACIndication				PRESENCE optional }|
	{ ID id-SSB-PositionsInBurst			CRITICALITY ignore	EXTENSION SSB-PositionsInBurst					PRESENCE optional }|
	{ ID id-NRCellPRACHConfig				CRITICALITY ignore	EXTENSION NRCellPRACHConfig						PRESENCE optional }|
	{ ID id-NPN-Broadcast-Information		CRITICALITY reject	EXTENSION NPN-Broadcast-Information			PRESENCE optional }|
	{ ID id-CSI-RSTransmissionIndication	CRITICALITY ignore	EXTENSION CSI-RSTransmissionIndication		PRESENCE optional }|
	{ ID id-SFN-Offset						CRITICALITY ignore	EXTENSION SFN-Offset							PRESENCE optional }|
	{ ID id-Supported-MBS-FSA-ID-List		CRITICALITY ignore	EXTENSION Supported-MBS-FSA-ID-List			PRESENCE optional }|
	{ ID id-NR-U-ChannelInfo-List			CRITICALITY ignore	EXTENSION NR-U-ChannelInfo-List					PRESENCE optional }|
	{ ID id-Additional-Measurement-Timing-Configuration-List		CRITICALITY ignore	EXTENSION Additional-Measurement-Timing-Configuration-List		PRESENCE optional }|
	{ ID id-Redcap-Bcast-Information		CRITICALITY ignore	EXTENSION Redcap-Bcast-Information			PRESENCE optional }|
	{ ID id-eRedcap-Bcast-Information		CRITICALITY ignore	EXTENSION ERedcap-Bcast-Information			PRESENCE optional }|
	{ ID id-MobileIABCell					CRITICALITY ignore	EXTENSION MobileIABCell							PRESENCE optional }|
	{ ID id-XR-Bcast-Information			CRITICALITY ignore	EXTENSION XR-Bcast-Information					PRESENCE optional }|
	{ ID id-BarringExemptionforEmerCallInfo	CRITICALITY ignore	EXTENSION BarringExemptionforEmerCallInfo	PRESENCE optional },
	...
}

SFN-Offset ::= SEQUENCE {
	sFN-Time-Offset					BIT STRING (SIZE(24)),
	
	iE-Extensions		ProtocolExtensionContainer { {SFN-Offset-ExtIEs} } OPTIONAL,
	...
}
SFN-Offset-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
		...
}


ServedCells-NR ::= SEQUENCE (SIZE (1..maxnoofCellsinNG-RANnode)) OF ServedCells-NR-Item

ServedCells-NR-Item ::= SEQUENCE {
	served-cell-info-NR			ServedCellInformation-NR,
	neighbour-info-NR			NeighbourInformation-NR				OPTIONAL,
	neighbour-info-E-UTRA		NeighbourInformation-E-UTRA			OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {ServedCells-NR-Item-ExtIEs} } OPTIONAL,
	...
}

ServedCells-NR-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-ServedCellSpecificInfoReq-NR	CRITICALITY ignore EXTENSION	ServedCellSpecificInfoReq-NR	PRESENCE optional },
	...
}


ServedCells-ToModify-NR ::= SEQUENCE (SIZE (1..maxnoofCellsinNG-RANnode)) OF ServedCells-ToModify-NR-Item

ServedCells-ToModify-NR-Item ::= SEQUENCE {
	old-NR-CGI					NR-CGI,
	served-cell-info-NR			ServedCellInformation-NR,
	neighbour-info-NR			NeighbourInformation-NR											OPTIONAL,
	neighbour-info-E-UTRA		NeighbourInformation-E-UTRA										OPTIONAL,
	deactivation-indication		ENUMERATED {deactivated, ...}									OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {Served-cells-ToModify-NR-Item-ExtIEs} } 	OPTIONAL,
	...
}

Served-cells-ToModify-NR-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ServedCellSpecificInfoReq-NR	::= SEQUENCE (SIZE(1..maxnoofCellsinNG-RANnode)) OF ServedCellSpecificInfoReq-NR-Item

ServedCellSpecificInfoReq-NR-Item	::= SEQUENCE {
	nRCGI									NR-CGI,
	additionalMTCListRequestIndicator		ENUMERATED {additionalMTCListRequested, ...}			OPTIONAL,
	iE-Extensions							ProtocolExtensionContainer { { ServedCellSpecificInfoReq-NR-Item-ExtIEs} }	OPTIONAL,
	...
}

ServedCellSpecificInfoReq-NR-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {	
	...
}

ServedCellsToUpdate-NR ::= SEQUENCE {
	served-Cells-ToAdd-NR		ServedCells-NR															OPTIONAL,
	served-Cells-ToModify-NR	ServedCells-ToModify-NR													OPTIONAL,
	served-Cells-ToDelete-NR	SEQUENCE (SIZE (1..maxnoofCellsinNG-RANnode)) OF NR-CGI 					OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {ServedCellsToUpdate-NR-ExtIEs} } OPTIONAL,
	...
}

ServedCellsToUpdate-NR-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}




SharedResourceType ::= CHOICE {
	ul-onlySharing				SharedResourceType-UL-OnlySharing,
	ul-and-dl-Sharing			SharedResourceType-ULDL-Sharing,
	choice-extension			ProtocolIE-Single-Container { {SharedResourceType-ExtIEs} }
}

SharedResourceType-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

SharedResourceType-UL-OnlySharing ::= SEQUENCE {
	ul-resourceBitmap			DataTrafficResources,
	iE-Extensions			ProtocolExtensionContainer { {SharedResourceType-UL-OnlySharing-ExtIEs} } OPTIONAL,
	...
}

SharedResourceType-UL-OnlySharing-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SharedResourceType-ULDL-Sharing ::= CHOICE {
	ul-resources				SharedResourceType-ULDL-Sharing-UL-Resources,
	dl-resources				SharedResourceType-ULDL-Sharing-DL-Resources,
	choice-extension			ProtocolIE-Single-Container { {SharedResourceType-ULDL-Sharing-ExtIEs} }
}

SharedResourceType-ULDL-Sharing-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

SharedResourceType-ULDL-Sharing-UL-Resources ::= CHOICE {
	unchanged					NULL,
	changed						SharedResourceType-ULDL-Sharing-UL-ResourcesChanged,
	choice-extension			ProtocolIE-Single-Container { {SharedResourceType-ULDL-Sharing-UL-Resources-ExtIEs} }
}

SharedResourceType-ULDL-Sharing-UL-Resources-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

SharedResourceType-ULDL-Sharing-UL-ResourcesChanged ::= SEQUENCE {
	ul-resourceBitmap			DataTrafficResources,
	iE-Extensions			ProtocolExtensionContainer { {SharedResourceType-ULDL-Sharing-UL-ResourcesChanged-ExtIEs} } OPTIONAL,
	...
}

SharedResourceType-ULDL-Sharing-UL-ResourcesChanged-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SharedResourceType-ULDL-Sharing-DL-Resources ::= CHOICE {
	unchanged					NULL,
	changed						SharedResourceType-ULDL-Sharing-DL-ResourcesChanged,
	choice-extension			ProtocolIE-Single-Container { {SharedResourceType-ULDL-Sharing-DL-Resources-ExtIEs} }
}

SharedResourceType-ULDL-Sharing-DL-Resources-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

SharedResourceType-ULDL-Sharing-DL-ResourcesChanged ::= SEQUENCE {
	dl-resourceBitmap			DataTrafficResources,
	iE-Extensions			ProtocolExtensionContainer { {SharedResourceType-ULDL-Sharing-DL-ResourcesChanged-ExtIEs} } OPTIONAL,
	...
}

SharedResourceType-ULDL-Sharing-DL-ResourcesChanged-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SK-COUNTER ::= INTEGER (0..65535)

SliceAvailableCapacity ::= SEQUENCE (SIZE(1..maxnoofBPLMNs)) OF SliceAvailableCapacity-Item

SliceAvailableCapacity-Item	::= SEQUENCE {
	pLMNIdentity						PLMN-Identity,
	sNSSAIAvailableCapacity-List		SNSSAIAvailableCapacity-List,
	iE-Extensions						ProtocolExtensionContainer { { SliceAvailableCapacity-Item-ExtIEs} }	OPTIONAL,
	...
}


SliceAvailableCapacity-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SNSSAIAvailableCapacity-List ::= SEQUENCE (SIZE(1.. maxnoofSliceItems)) OF SNSSAIAvailableCapacity-Item

SNSSAIAvailableCapacity-Item ::= SEQUENCE {
	sNSSAI		S-NSSAI,
	sliceAvailableCapacityValueDownlink	INTEGER (0..100),
	sliceAvailableCapacityValueUplink	INTEGER (0..100),
	iE-Extensions				ProtocolExtensionContainer { { SNSSAIAvailableCapacity-Item-ExtIEs } }	OPTIONAL
}

SNSSAIAvailableCapacity-Item-ExtIEs	XNAP-PROTOCOL-EXTENSION ::= {
	...
}


SliceRadioResourceStatus-List ::= SEQUENCE (SIZE(1..maxnoofBPLMNs)) OF SliceRadioResourceStatus-Item

SliceRadioResourceStatus-Item	::= SEQUENCE {
	plmn-Identity						PLMN-Identity,
	sNSSAIRadioResourceStatus-List		SNSSAIRadioResourceStatus-List,
	iE-Extensions						ProtocolExtensionContainer { { SliceRadioResourceStatus-Item-ExtIEs} }	OPTIONAL,
	...
}

SliceRadioResourceStatus-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}
SLPositioning-Ranging-Services-Info::= SEQUENCE{
	sLPositioning-Ranging-Authorized	SLPositioning-Ranging-Authorized,
	rSPP-transport-QoS-parameters		RSPP-transport-QoS-parameters		OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { SLPositioning-Ranging-Services-Info-ExtIEs} }		OPTIONAL
}

SLPositioning-Ranging-Services-Info-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SLPositioning-Ranging-Authorized ::= ENUMERATED { 
	authorized,
	not-authorized,
	...
}

RSPP-transport-QoS-parameters ::= SEQUENCE {
	rSPPQoSFlowList				RSPPQoSFlowList,
	rSPPLinkAggregateBitRates	BitRate												OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { RSPP-transport-QoS-parameters-ExtIEs} }	OPTIONAL,
	...
}

RSPP-transport-QoS-parameters-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
...
}

RSPPQoSFlowList ::= SEQUENCE (SIZE(1..maxnoofRSPPQoSFlows)) OF RSPPQoSFlowItem

RSPPQoSFlowItem ::= SEQUENCE {
	pQI					FiveQI,
	rSPPFlowBitRates	RSPPFlowBitRates										OPTIONAL,
	range				Range													OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { RSPPQoSFlowItem-ExtIEs} }	OPTIONAL,
	...
}

RSPPQoSFlowItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
...
}

RSPPFlowBitRates ::= SEQUENCE {
	guaranteedFlowBitRate		BitRate,
	maximumFlowBitRate			BitRate,
	iE-Extensions		ProtocolExtensionContainer { { RSPPFlowBitRates-ExtIEs} }	OPTIONAL,
	...
}

RSPPFlowBitRates-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SNSSAIRadioResourceStatus-List ::= SEQUENCE (SIZE(1..maxnoofSliceItems)) OF SNSSAIRadioResourceStatus-Item

SNSSAIRadioResourceStatus-Item	::= SEQUENCE {
	sNSSAI								S-NSSAI,
	slice-DL-GBR-PRB-Usage				Slice-DL-GBR-PRB-Usage,
	slice-UL-GBR-PRB-Usage				Slice-UL-GBR-PRB-Usage,
	slice-DL-non-GBR-PRB-Usage			Slice-DL-non-GBR-PRB-Usage,
	slice-UL-non-GBR-PRB-Usage			Slice-UL-non-GBR-PRB-Usage,
	slice-DL-Total-PRB-Allocation		Slice-DL-Total-PRB-Allocation,
	slice-UL-Total-PRB-Allocation		Slice-UL-Total-PRB-Allocation,
	iE-Extensions						ProtocolExtensionContainer { { SNSSAIRadioResourceStatus-Item-ExtIEs} }	OPTIONAL,
	...
}

SNSSAIRadioResourceStatus-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

Slice-DL-GBR-PRB-Usage 			::= INTEGER (0..100)

Slice-UL-GBR-PRB-Usage 			::= INTEGER (0..100)

Slice-DL-non-GBR-PRB-Usage 		::= INTEGER (0..100)

Slice-UL-non-GBR-PRB-Usage 		::= INTEGER (0..100)

Slice-DL-Total-PRB-Allocation 	::= INTEGER (0..100)

Slice-UL-Total-PRB-Allocation 	::= INTEGER (0..100)


SliceSupport-List	::= SEQUENCE (SIZE(1..maxnoofSliceItems)) OF S-NSSAI

SliceToReport-List ::= SEQUENCE (SIZE(1..maxnoofBPLMNs)) OF SliceToReport-List-Item

SliceToReport-List-Item	::= SEQUENCE {
	pLMNIdentity				PLMN-Identity,
	sNSSAIlist					SNSSAI-list,
	iE-Extensions						ProtocolExtensionContainer { { SliceToReport-List-Item-ExtIEs} }	OPTIONAL,
	...
}


SliceToReport-List-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SNSSAI-list ::= SEQUENCE (SIZE(1.. maxnoofSliceItems)) OF SNSSAI-Item

SNSSAI-Item ::= SEQUENCE {
	sNSSAI		S-NSSAI,
	iE-Extensions				ProtocolExtensionContainer { { SNSSAI-Item-ExtIEs } }	OPTIONAL
}

SNSSAI-Item-ExtIEs	XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SlotConfiguration-List ::= SEQUENCE (SIZE (1..maxnoofslots)) OF SlotConfiguration-List-Item

SlotConfiguration-List-Item ::= SEQUENCE {
	slotIndex						INTEGER (0..5119),
	symbolAllocation-in-Slot		SymbolAllocation-in-Slot,
	iE-Extensions				ProtocolExtensionContainer { {SlotConfiguration-List-Item-ExtIEs} }	OPTIONAL,
	...
}

SlotConfiguration-List-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

S-NG-RANnode-SecurityKey ::= BIT STRING (SIZE(256))

S-NG-RANnode-Addition-Trigger-Ind ::= ENUMERATED {
	sn-change,
	inter-MN-HO,
	intra-MN-HO,
	...
}

S-NSSAI ::= SEQUENCE {
	sst						OCTET STRING (SIZE(1)),
	sd						OCTET STRING (SIZE(3))							OPTIONAL,
	iE-Extensions			ProtocolExtensionContainer { {S-NSSAI-ExtIEs} } OPTIONAL,
	...
}

S-NSSAI-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SNMobilityInformation ::= BIT STRING (SIZE(32))


SNPNIdentity ::= SEQUENCE {
	plmnID				PLMN-Identity,
	nid					NID,
	iE-Extensions		ProtocolExtensionContainer { {SNPNIdentity-ExtIEs} }	OPTIONAL,
	...
}

SNPNIdentity-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


SNTriggered ::=ENUMERATED{
	true,
	...
}

SpecialSubframeInfo-E-UTRA ::= SEQUENCE {
	specialSubframePattern	SpecialSubframePatterns-E-UTRA,
	cyclicPrefixDL			CyclicPrefix-E-UTRA-DL,
	cyclicPrefixUL			CyclicPrefix-E-UTRA-UL,
	iE-Extensions			ProtocolExtensionContainer { {SpecialSubframeInfo-E-UTRA-ExtIEs} } OPTIONAL,
	...
}

SpecialSubframeInfo-E-UTRA-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


SpecialSubframePatterns-E-UTRA ::= ENUMERATED {
	ssp0,
	ssp1,
	ssp2,
	ssp3,
	ssp4,
	ssp5,
	ssp6,
	ssp7,
	ssp8,
	ssp9,
	ssp10,
	...
}


SpectrumSharingGroupID ::= INTEGER (1..maxnoofCellsinNG-RANnode)

SplitSessionIndicator ::= ENUMERATED {
	split,
	...
}

SplitSRBsTypes ::= ENUMERATED {srb1, srb2, srb1and2, ...}

SPRAvailability ::= ENUMERATED {spr-available, ...}

SRSPositioningConfigOrActivationRequest::= ENUMERATED {true, ...}


SRSConfiguration ::= OCTET STRING

SSBAreaCapacityValue-List ::= SEQUENCE (SIZE(1..maxnoofSSBAreas)) OF SSBAreaCapacityValue-List-Item

SSBAreaCapacityValue-List-Item	::= SEQUENCE {
	sSBIndex				INTEGER(0..63),
	ssbAreaCapacityValue	INTEGER (0..100),
	iE-Extensions						ProtocolExtensionContainer { { SSBAreaCapacityValue-List-Item-ExtIEs} }	OPTIONAL,
	...
}


SSBAreaCapacityValue-List-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


SSBAreaRadioResourceStatus-List ::= SEQUENCE (SIZE(1..maxnoofSSBAreas)) OF SSBAreaRadioResourceStatus-List-Item

SSBAreaRadioResourceStatus-List-Item	::= SEQUENCE {
	sSBIndex								INTEGER(0..63),
	ssb-Area-DL-GBR-PRB-usage	DL-GBR-PRB-usage,
	ssb-Area-UL-GBR-PRB-usage	UL-GBR-PRB-usage,
	ssb-Area-dL-non-GBR-PRB-usage		DL-non-GBR-PRB-usage,
	ssb-Area-uL-non-GBR-PRB-usage		UL-non-GBR-PRB-usage,
	ssb-Area-dL-Total-PRB-usage			DL-Total-PRB-usage,
	ssb-Area-uL-Total-PRB-usage			UL-Total-PRB-usage,
	iE-Extensions						ProtocolExtensionContainer { { SSBAreaRadioResourceStatus-List-Item-ExtIEs} }	OPTIONAL,
	...
}


SSBAreaRadioResourceStatus-List-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-DL-scheduling-PDCCH-CCE-usage		CRITICALITY ignore	EXTENSION DL-scheduling-PDCCH-CCE-usage	PRESENCE optional}|
	{ ID id-UL-scheduling-PDCCH-CCE-usage		CRITICALITY ignore	EXTENSION UL-scheduling-PDCCH-CCE-usage	PRESENCE optional},
	...
}

SSB-Coverage-Modification-List ::= SEQUENCE (SIZE (0..maxnoofSSBAreas)) OF SSB-Coverage-Modification-List-Item

SSB-Coverage-Modification-List-Item ::= SEQUENCE {
	sSBIndex						INTEGER(0..63),
	sSBCoverageState				INTEGER (0..15, ...),
	iE-Extension			ProtocolExtensionContainer { { SSB-Coverage-Modification-List-Item-ExtIEs} } OPTIONAL,
	
	...
}


SSB-Coverage-Modification-List-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


SSB-PositionsInBurst ::= CHOICE {
	shortBitmap						BIT STRING (SIZE (4)),
	mediumBitmap					BIT STRING (SIZE (8)),
	longBitmap						BIT STRING (SIZE (64)),
	choice-extension				ProtocolIE-Single-Container { {SSB-PositionsInBurst-ExtIEs} }
}

SSB-PositionsInBurst-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

SSB-freqInfo ::= INTEGER (0..maxNRARFCN)

SSB-subcarrierSpacing ::= ENUMERATED {kHz15, kHz30, kHz120, kHz240, spare3, spare2, spare1, ...}

SSBOffsets-List ::= SEQUENCE (SIZE(1..maxnoofSSBAreas)) OF SSBOffsets-Item

SSBOffsets-Item ::= SEQUENCE {
	nG-RANnode1SSBOffsets			SSBOffsetInformation										OPTIONAL,
	nG-RANnode2ProposedSSBOffsets	SSBOffsetInformation,
	iE-Extensions					ProtocolExtensionContainer { { SSBOffsets-Item-ExtIEs} }	OPTIONAL,
	...
}

SSBOffsets-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


SSBOffsetInformation	::= SEQUENCE {
	sSBIndex			INTEGER(0..63),
	sSBTriggeringOffset			MobilityParametersInformation,
	iE-Extensions				ProtocolExtensionContainer { { SSBOffsetInformation-ExtIEs} }	OPTIONAL,
	...
}

SSBOffsetInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SSBOffsetModificationRange	::= SEQUENCE {
	sSBIndex				INTEGER(0..63),
	sSBobilityParametersModificationRange		MobilityParametersModificationRange,
	iE-Extensions				ProtocolExtensionContainer { { SSBOffsetModificationRange-ExtIEs} }	OPTIONAL,
	...
}

SSBOffsetModificationRange-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


SSBToReport-List ::= SEQUENCE (SIZE(1..maxnoofSSBAreas)) OF SSBToReport-List-Item

SSBToReport-List-Item	::= SEQUENCE {
	sSBIndex				INTEGER(0..63),
	iE-Extensions						ProtocolExtensionContainer { { SSBToReport-List-Item-ExtIEs} }	OPTIONAL,
	...
}


SSBToReport-List-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SSB-transmissionPeriodicity	::= ENUMERATED {sf10, sf20, sf40, sf80, sf160, sf320, sf640, ..., sf5}

SSB-transmissionTimingOffset ::= INTEGER (0..127, ...)

SSB-transmissionBitmap ::= CHOICE {
	shortBitmap			BIT STRING (SIZE (4)),
	mediumBitmap		BIT STRING (SIZE (8)),
	longBitmap			BIT STRING (SIZE (64)),
	choice-extension	ProtocolIE-Single-Container { { SSB-transmisisonBitmap-ExtIEs} }
}

SSB-transmisisonBitmap-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

SuccessfulHOReportInformation	::= SEQUENCE (SIZE(1.. maxnoofSuccessfulHOReports)) OF SuccessfulHOReportList-Item
SuccessfulHOReportList-Item	::= SEQUENCE {
	successfulHOReport					SuccessfulHOReportContainer,
	iE-Extensions						ProtocolExtensionContainer { { SuccessfulHOReportList-Item-ExtIEs} }	OPTIONAL,
	...
}

SuccessfulHOReportList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SuccessfulHOReportContainer	::= OCTET STRING

SuccessfulPSCellChangeReportInformation	::= SEQUENCE (SIZE(1.. maxnoofSuccessfulPSCellChangeReports)) OF SuccessfulPSCellChangeReportList-Item
SuccessfulPSCellChangeReportList-Item	::= SEQUENCE {
	successfulPSCellChangeReport		SuccessfulPSCellChangeReportContainer,
	sNMobilityInformation				SNMobilityInformation															OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { { SuccessfulPSCellChangeReportList-Item-ExtIEs} }	OPTIONAL,
	...
}

SuccessfulPSCellChangeReportList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SuccessfulPSCellChangeReportContainer ::= OCTET STRING


SUL-FrequencyBand ::= INTEGER (1..1024)


SUL-Information ::= SEQUENCE {
	sulFrequencyInfo			NRARFCN,
	sulTransmissionBandwidth	NRTransmissionBandwidth,
	iE-Extensions				ProtocolExtensionContainer { {SUL-Information-ExtIEs} } OPTIONAL,
	...
}

SUL-Information-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-CarrierList				CRITICALITY ignore	EXTENSION NRCarrierList			PRESENCE optional }|
	{ ID id-FrequencyShift7p5khz	CRITICALITY ignore	EXTENSION FrequencyShift7p5khz	PRESENCE optional },

...
}

Supported-MBS-FSA-ID-List ::= SEQUENCE (SIZE(1..maxnoofMBSFSAs)) OF MBS-FrequencySelectionArea-Identity

SupportedSULBandList ::= SEQUENCE (SIZE(1..maxnoofNRCellBands)) OF SupportedSULBandItem

SupportedSULBandItem ::= SEQUENCE {
	sulBandItem					SUL-FrequencyBand,
	iE-Extensions				ProtocolExtensionContainer { {SupportedSULBandItem-ExtIEs} } OPTIONAL,
	...
}

SupportedSULBandItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SurvivalTime ::= INTEGER (0..1920000, ...)


SymbolAllocation-in-Slot ::= CHOICE {
	allDL				SymbolAllocation-in-Slot-AllDL,
	allUL				SymbolAllocation-in-Slot-AllUL,
	bothDLandUL			SymbolAllocation-in-Slot-BothDLandUL,
	choice-extension	ProtocolIE-Single-Container { {SymbolAllocation-in-Slot-ExtIEs} }
}

SymbolAllocation-in-Slot-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


SymbolAllocation-in-Slot-AllDL ::= SEQUENCE {
	iE-Extension		ProtocolExtensionContainer { {SymbolAllocation-in-Slot-AllDL-ExtIEs} }	OPTIONAL,
	...
}

SymbolAllocation-in-Slot-AllDL-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


SymbolAllocation-in-Slot-AllUL ::= SEQUENCE {
	iE-Extension		ProtocolExtensionContainer { {SymbolAllocation-in-Slot-AllUL-ExtIEs} }	OPTIONAL,
	...
}

SymbolAllocation-in-Slot-AllUL-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


SymbolAllocation-in-Slot-BothDLandUL ::= SEQUENCE {
	numberofDLSymbols	INTEGER (0..13),
	numberofULSymbols	INTEGER (0..13),
	iE-Extension		ProtocolExtensionContainer { {SymbolAllocation-in-Slot-BothDLandUL-ExtIEs} }	OPTIONAL,
	...
}

SymbolAllocation-in-Slot-BothDLandUL-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-permutation		CRITICALITY ignore	EXTENSION Permutation	PRESENCE optional },
	...
}


SNPN-CellBasedMDT::= SEQUENCE {
	sNPN-CellIdListforMDT		SNPN-CellIdListforMDT,
	iE-Extensions				ProtocolExtensionContainer { {SNPN-CellBasedMDT-ExtIEs} } OPTIONAL,
	...
}

SNPN-CellBasedMDT-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SNPN-CellIdListforMDT ::= SEQUENCE (SIZE(1..maxnoofCellIDforMDT)) OF SNPN-CellIdforMDT-Item

SNPN-CellIdforMDT-Item ::= SEQUENCE {
	nRCGI				NR-CGI,
	nID					NID,
	iE-Extensions		ProtocolExtensionContainer { {SNPN-CellIdforMDT-Item-ExtIEs} }	OPTIONAL,
	...
}

SNPN-CellIdforMDT-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


SNPN-TAIBasedMDT ::= SEQUENCE {
	sNPN-TAIListforMDT		SNPN-TAIListforMDT,
	iE-Extensions			ProtocolExtensionContainer { {SNPN-TAIBasedMDT-ExtIEs} } OPTIONAL,
	...
}

SNPN-TAIBasedMDT-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SNPN-TAIListforMDT ::= SEQUENCE (SIZE(1..maxnoofTAforMDT)) OF SNPN-TAIforMDT-Item

SNPN-TAIforMDT-Item ::= SEQUENCE {
	plmn-ID				PLMN-Identity,
	tAC					TAC,
	nID					NID,
	iE-Extensions			ProtocolExtensionContainer { {SNPN-TAIforMDT-Item-ExtIEs} } OPTIONAL,
	...
}

SNPN-TAIforMDT-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


SNPN-BasedMDT::= SEQUENCE {
	sNPNListforMDT		SNPNListforMDT,
	iE-Extensions		ProtocolExtensionContainer { {SNPN-BasedMDT-ExtIEs} } OPTIONAL,
	...
}

SNPN-BasedMDT-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SNPNListforMDT ::= SEQUENCE (SIZE(1.. maxnoofMDTSNPNs)) OF SNPNforMDT-Item

SNPNforMDT-Item ::= SEQUENCE {
	plmn-ID				PLMN-Identity,
	nID					NID,
	iE-Extensions		ProtocolExtensionContainer {{SNPNforMDT-Item-ExtIEs}}		OPTIONAL,
	...
}

SNPNforMDT-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


SubcarrierSpacing ::= ENUMERATED {kHz15, kHz30, kHz120, kHz240, spare3, spare2, spare1, ..., kHz60}


SemipersistentPositioningInformation ::= SEQUENCE {
	sRSTransmissionType					ENUMERATED {activate, deactivate, ...},
	sRSResourceSetID					OCTET STRING,
	sRSSpatialRelation					OCTET STRING		OPTIONAL,
	spatialRelationInforperSRSResource	OCTET STRING		OPTIONAL,
	iE-Extensions						ProtocolExtensionContainer { { SemipersistentPositioningInformation-ExtIEs} } OPTIONAL,
	...
}

SemipersistentPositioningInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- T

TABasedMDT ::= SEQUENCE {
	tAListforMDT		TAListforMDT,
	iE-Extensions		ProtocolExtensionContainer { {TABasedMDT-ExtIEs} } OPTIONAL,
	...
}

TABasedMDT-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}




TAIBasedMDT ::= SEQUENCE {
	tAIListforMDT			TAIListforMDT,
	iE-Extensions			ProtocolExtensionContainer { {TAIBasedMDT-ExtIEs} } OPTIONAL,
	...
}

TAIBasedMDT-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

TAIListforMDT ::= SEQUENCE (SIZE(1..maxnoofTAforMDT)) OF TAIforMDT-Item

TAIforMDT-Item ::= SEQUENCE {
	plmn-ID				PLMN-Identity,
	tAC						TAC,
	iE-Extensions			ProtocolExtensionContainer { {TAIforMDT-Item-ExtIEs} } OPTIONAL,
	...
}

TAIforMDT-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

TAC ::= OCTET STRING (SIZE (3))

TAINSAGSupportList ::= SEQUENCE (SIZE(1..maxnoofNSAGs)) OF TAINSAGSupportItem

TAINSAGSupportItem ::= SEQUENCE {
	nSAG-ID								NSAG-ID,
	nSAGSliceSupportList			ExtendedSliceSupportList,
	iE-Extensions		ProtocolExtensionContainer { {TAINSAGSupportItem-ExtIEs} } OPTIONAL,
	...
}

TAINSAGSupportItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


TAISliceUnavailableCellList	::= SEQUENCE (SIZE(1..maxnoofExtSliceItems)) OF TAISliceUnavailableCellItem

TAISliceUnavailableCellItem ::= SEQUENCE {
	sNSSAI					S-NSSAI,
	sliceAvailabilityList	SliceAvailabilityList,
	iE-Extensions			ProtocolExtensionContainer { {TAISliceUnavailableCellItem-ExtIEs} }	OPTIONAL,
	...
}

TAISliceUnavailableCellItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

SliceAvailabilityList ::= CHOICE {	
	unavailableCellList			UnavailableCellList,
	availableCellList			AvailableCellList,
	choice-extension		ProtocolIE-Single-Container { {SliceAvailabilityList-ExtIEs} },
	...
}

SliceAvailabilityList-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


AvailableCellList ::= SEQUENCE {
	availableNRCellList					AvailableNRCellList,
	iE-Extensions			ProtocolExtensionContainer { {AvailableCellList-ExtIEs} }	OPTIONAL,
	...
}

AvailableCellList-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

AvailableNRCellList ::= SEQUENCE (SIZE (1..maxnoofCellsinNG-RANnode)) OF NR-CGI

UnavailableCellList ::= SEQUENCE {
	unavailableNRCellList					UnavailableNRCellList,
	iE-Extensions			ProtocolExtensionContainer { {UnavailableCellList-ExtIEs} }	OPTIONAL,
	...
}

UnavailableCellList-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


UnavailableNRCellList ::= SEQUENCE (SIZE (1..maxnoofCellsinNG-RANnode)) OF NR-CGI


TAISupport-List	::= SEQUENCE (SIZE(1..maxnoofsupportedTACs)) OF TAISupport-Item

TAISupport-Item ::= SEQUENCE {
	tac								TAC,
	broadcastPLMNs					SEQUENCE (SIZE(1..maxnoofsupportedPLMNs)) OF BroadcastPLMNinTAISupport-Item,
	iE-Extensions					ProtocolExtensionContainer { {TAISupport-Item-ExtIEs} } OPTIONAL,
	...
}

TAISupport-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

TAListforMDT ::= SEQUENCE (SIZE(1..maxnoofTAforMDT)) OF TAC


TABasedQMC ::= SEQUENCE {
	tAListforQMC		TAListforQMC,
	iE-Extensions		ProtocolExtensionContainer { {TABasedQMC-ExtIEs} } OPTIONAL,
	...
}

TABasedQMC-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

TAListforQMC ::= SEQUENCE (SIZE(1..maxnoofTAforQMC)) OF TAC


TAIBasedQMC ::= SEQUENCE {
	tAIListforQMC		TAIListforQMC,
	iE-Extensions		ProtocolExtensionContainer { {TAIBasedQMC-ExtIEs} } OPTIONAL,
	...
}

TAIBasedQMC-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

TAIListforQMC ::= SEQUENCE (SIZE(1..maxnoofTAforQMC)) OF TAI-Item


TAI-Item ::= SEQUENCE {
	tAC					TAC,
	pLMN-Identity		PLMN-Identity,
	iE-Extensions		ProtocolExtensionContainer { {TAI-Item-ExtIEs} } OPTIONAL,
	...
}

TAI-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


TargetCellinEUTRAN ::= OCTET STRING -- This IE is to be encoded according to Global Cell ID in the Last Visited E-UTRAN Cell Information IE, as defined in TS 36.413 [31]



Target-CGI ::= CHOICE {
	nr							NR-CGI,
	e-utra						E-UTRA-CGI,
	choice-extension			ProtocolIE-Single-Container { {TargetCGI-ExtIEs} }
}

TargetCGI-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


TDDULDLConfigurationCommonNR ::= OCTET STRING


TargetCellList ::= SEQUENCE (SIZE(1..maxnoofCHOcells)) OF TargetCellList-Item

TargetCellList-Item ::= SEQUENCE {
	target-cell								Target-CGI,
	iE-Extensions							ProtocolExtensionContainer { { TargetCellList-Item-ExtIEs} } OPTIONAL
}

TargetCellList-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

Threshold-RSRQ ::= INTEGER(0..127)
Threshold-RSRP ::= INTEGER(0..127)
Threshold-SINR ::= INTEGER(0..127)

TimeSinceFailure ::= INTEGER (0..172800, ...)

TimeSynchronizationAssistanceInformation ::= SEQUENCE {
	timeDistributionIndication					ENUMERATED {enabled, disabled, ...},
	uuTimeSynchronizationErrorBudget			INTEGER (0..1000000, ...)				OPTIONAL,
	-- This IE shall be present if the Time Distribution Indication IE is set to “enabled”.
	ie-Extension								ProtocolExtensionContainer { { TimeSynchronizationAssistanceInformation-ExtIEs} } OPTIONAL,
	...	
}

TimeSynchronizationAssistanceInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {

	{ ID id-ClockQualityReportingControlInfo	CRITICALITY ignore	EXTENSION ClockQualityReportingControlInfo		PRESENCE optional},
	...
}

TimeToTrigger ::= ENUMERATED {ms0, ms40, ms64, ms80, ms100, ms128, ms160, ms256, ms320, ms480, ms512, ms640, ms1024, ms1280, ms2560, ms5120}


TimeToWait ::= ENUMERATED {
	v1s,
	v2s,
	v5s,
	v10s,
	v20s,
	v60s,
	...
}


TMGI ::= OCTET STRING (SIZE(6))


TNLConfigurationInfo ::= SEQUENCE {
	extendedUPTransportLayerAddressesToAdd			ExtTLAs										OPTIONAL,
	extendedUPTransportLayerAddressesToRemove		ExtTLAs										OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {TNLConfigurationInfo-ExtIEs} }	OPTIONAL,
	...
}

TNLConfigurationInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

TNLA-To-Add-List ::= SEQUENCE (SIZE(1..maxnoofTNLAssociations)) OF TNLA-To-Add-Item

TNLA-To-Add-Item ::= SEQUENCE {
	tNLAssociationTransportLayerAddress		CPTransportLayerInformation,
	tNLAssociationUsage						TNLAssociationUsage,
	iE-Extensions							ProtocolExtensionContainer { { TNLA-To-Add-Item-ExtIEs} } OPTIONAL
}

TNLA-To-Add-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


TNLA-To-Update-List ::= SEQUENCE (SIZE(1..maxnoofTNLAssociations)) OF TNLA-To-Update-Item

TNLA-To-Update-Item::= SEQUENCE {
	tNLAssociationTransportLayerAddress		CPTransportLayerInformation,
	tNLAssociationUsage						TNLAssociationUsage 	OPTIONAL,
	iE-Extensions							ProtocolExtensionContainer { { TNLA-To-Update-Item-ExtIEs} } OPTIONAL
}

TNLA-To-Update-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

TNLA-To-Remove-List ::= SEQUENCE (SIZE(1..maxnoofTNLAssociations)) OF TNLA-To-Remove-Item

TNLA-To-Remove-Item::= SEQUENCE {
	tNLAssociationTransportLayerAddress		CPTransportLayerInformation,
	iE-Extensions							ProtocolExtensionContainer { { TNLA-To-Remove-Item-ExtIEs} } OPTIONAL
}

TNLA-To-Remove-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


TNLA-Setup-List ::= SEQUENCE (SIZE(1..maxnoofTNLAssociations)) OF TNLA-Setup-Item

TNLA-Setup-Item ::= SEQUENCE {
	tNLAssociationTransportLayerAddress		CPTransportLayerInformation,
	iE-Extensions							ProtocolExtensionContainer { { TNLA-Setup-Item-ExtIEs} } OPTIONAL,
	...
}

TNLA-Setup-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


TNLA-Failed-To-Setup-List ::= SEQUENCE (SIZE(1..maxnoofTNLAssociations)) OF TNLA-Failed-To-Setup-Item

TNLA-Failed-To-Setup-Item ::= SEQUENCE {
	tNLAssociationTransportLayerAddress		CPTransportLayerInformation,
	cause									Cause,
	iE-Extensions							ProtocolExtensionContainer { { TNLA-Failed-To-Setup-Item-ExtIEs} } OPTIONAL
}

TNLA-Failed-To-Setup-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


TNLAssociationUsage ::= ENUMERATED {
	ue,
	non-ue,
	both,
	...
}


TransportLayerAddress ::= BIT STRING (SIZE(1..160, ...))


TraceActivation ::= SEQUENCE {
	ng-ran-TraceID			NG-RANTraceID,
	interfaces-to-trace 	BIT STRING { ng-c (0), x-nc (1), uu (2), f1-c (3), e1 (4)} (SIZE(8)),
	trace-depth 			Trace-Depth,
	trace-coll-address		TransportLayerAddress,
	ie-Extension 			ProtocolExtensionContainer { {TraceActivation-ExtIEs} } OPTIONAL,
	...
}

TraceActivation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
-- Extension to support MDT –
	{ ID id-TraceCollectionEntityURI	CRITICALITY ignore	EXTENSION URIaddress				PRESENCE optional}|
	{ ID id-MDT-Configuration			CRITICALITY ignore	EXTENSION MDT-Configuration			PRESENCE optional},
	...
}


Trace-Depth ::= ENUMERATED {
	minimum,
	medium,
	maximum,
	minimumWithoutVendorSpecificExtension,
	mediumWithoutVendorSpecificExtension,
	maximumWithoutVendorSpecificExtension,
	...
}

TrafficIndex ::= INTEGER (1..1024, ...)

TrafficProfile ::= CHOICE {
	uPTraffic					QoSFlowLevelQoSParameters,
	nonUPTraffic				NonUPTraffic,
	choice-extension			ProtocolIE-Single-Container { {TrafficProfile-ExtIEs} }
}

TrafficProfile-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

TrafficReleaseType ::= CHOICE {
	fullRelease				AllTrafficIndication,
	partialRelease			TrafficToBeRelease-List,
	choice-extension		ProtocolIE-Single-Container { {TrafficReleaseType-ExtIEs} }
}

TrafficReleaseType-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


TrafficToBeReleaseInformation ::= SEQUENCE {
	releaseType			TrafficReleaseType,
	ie-Extensions 			ProtocolExtensionContainer { {TrafficToBeReleaseInformation-ExtIEs} } OPTIONAL,
	...
}

TrafficToBeReleaseInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

TrafficToBeRelease-List ::= SEQUENCE (SIZE(1..maxnoofTrafficIndexEntries)) OF TrafficToBeRelease-Item

TrafficToBeRelease-Item ::= SEQUENCE {
	trafficIndex			TrafficIndex,
	bHInfoList				BHInfoList		OPTIONAL,
	iE-Extension			ProtocolExtensionContainer { {TrafficToBeRelease-Item-ExtIEs} }	OPTIONAL,
	...
}

TrafficToBeRelease-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

TSCTrafficCharacteristics ::= SEQUENCE {
	tSCAssistanceInformationDownlink	TSCAssistanceInformation OPTIONAL,
	tSCAssistanceInformationUplink		TSCAssistanceInformation OPTIONAL,
	ie-Extension 			ProtocolExtensionContainer { {TSCTrafficCharacteristics-ExtIEs} } OPTIONAL,
	...
}

TSCTrafficCharacteristics-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

TSCAssistanceInformation ::= SEQUENCE {
	periodicity			INTEGER (0.. 640000, ...),
	burstArrivalTime	OCTET STRING				OPTIONAL,
	ie-Extension 			ProtocolExtensionContainer { { TSCAssistanceInformation-ExtIEs} } OPTIONAL,
	...	
}

TSCAssistanceInformation-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{	ID id-SurvivalTime						CRITICALITY ignore	EXTENSION SurvivalTime							PRESENCE optional}|
	{	ID id-CapabilityForBATAdaptation		CRITICALITY ignore	EXTENSION CapabilityForBATAdaptation		PRESENCE optional}|
	{	ID id-N6JitterInformation				CRITICALITY ignore	EXTENSION N6JitterInformation					PRESENCE optional},
	...
}


TypeOfError ::= ENUMERATED {
	not-understood,
	missing,
	...
}


-- U


UEAggregateMaximumBitRate ::= SEQUENCE {
	dl-UE-AMBR				BitRate,
	ul-UE-AMBR				BitRate,
	iE-Extension			ProtocolExtensionContainer { {UEAggregateMaximumBitRate-ExtIEs} } OPTIONAL,
	...
}

UEAggregateMaximumBitRate-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


UEAppLayerMeasConfigInfo ::= SEQUENCE {
	qOEReference					QOEReference,
	qOEMeasConfigAppLayerID			QOEMeasConfAppLayerID				OPTIONAL,
	serviceType						ServiceType,
	qOEMeasStatus					QOEMeasStatus						OPTIONAL,
	containerAppLayerMeasConfig	ContainerAppLayerMeasConfig				OPTIONAL,
	mDTAlignmentInfo				MDTAlignmentInfo					OPTIONAL,
	measCollectionEntityIPAddress	MeasCollectionEntityIPAddress		OPTIONAL,
	areaScopeOfQMC					AreaScopeOfQMC						OPTIONAL,
	s-NSSAIListQoE					S-NSSAIListQoE						OPTIONAL,
	availableRVQoEMetrics			AvailableRVQoEMetrics				OPTIONAL,
	iE-Extension					ProtocolExtensionContainer { {UEAppLayerMeasConfigInfo-ExtIEs} } OPTIONAL,
	...
}

UEAppLayerMeasConfigInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-MBSCommServiceType				CRITICALITY ignore	EXTENSION MBSCommServiceType							PRESENCE optional }|
	{ ID id-AssistanceInformationQoE-Meas	CRITICALITY ignore	EXTENSION AssistanceInformationQoE-Meas				PRESENCE optional }|
	{ ID id-QoERVQoEReportingPaths			CRITICALITY ignore	EXTENSION QoERVQoEReportingPaths						PRESENCE optional },
	...
}


UEContextKeptIndicator ::= ENUMERATED {true, ...}


UEContextID ::= CHOICE {
	rRCResume				UEContextIDforRRCResume,
	rRRCReestablishment		UEContextIDforRRCReestablishment,
	choice-extension		ProtocolIE-Single-Container { {UEContextID-ExtIEs} }
}

UEContextID-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


UEContextIDforRRCResume ::= SEQUENCE {
	i-rnti					I-RNTI,
	allocated-c-rnti			C-RNTI,
	accessPCI				NG-RAN-CellPCI,
	iE-Extension			ProtocolExtensionContainer { {UEContextIDforRRCResume-ExtIEs} } OPTIONAL,
	...
}

UEContextIDforRRCResume-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


UEContextIDforRRCReestablishment ::= SEQUENCE {
	c-rnti					C-RNTI,
	failureCellPCI			NG-RAN-CellPCI,
	iE-Extension			ProtocolExtensionContainer { {UEContextIDforRRCReestablishment-ExtIEs} } OPTIONAL,
	...
}

UEContextIDforRRCReestablishment-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


UEContextInfoRetrUECtxtResp ::= SEQUENCE {
	ng-c-UE-signalling-ref					AMF-UE-NGAP-ID,
	signalling-TNL-at-source				CPTransportLayerInformation,
	ueSecurityCapabilities					UESecurityCapabilities,
	securityInformation						AS-SecurityInformation,
	ue-AMBR									UEAggregateMaximumBitRate,
	pduSessionResourcesToBeSetup-List		PDUSessionResourcesToBeSetup-List,
	rrc-Context								OCTET STRING,
	mobilityRestrictionList					MobilityRestrictionList									OPTIONAL,
	indexToRatFrequencySelectionPriority	RFSP-Index												OPTIONAL,
	iE-Extension			ProtocolExtensionContainer { {UEContextInfoRetrUECtxtResp-ExtIEs} } 	OPTIONAL,
	...
}

UEContextInfoRetrUECtxtResp-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ ID id-FiveGCMobilityRestrictionListContainer 	CRITICALITY ignore	EXTENSION FiveGCMobilityRestrictionListContainer		PRESENCE optional }|
	{ ID id-NRUESidelinkAggregateMaximumBitRate		CRITICALITY ignore	EXTENSION NRUESidelinkAggregateMaximumBitRate			PRESENCE optional }|
	{ ID id-LTEUESidelinkAggregateMaximumBitRate	CRITICALITY ignore	EXTENSION LTEUESidelinkAggregateMaximumBitRate			PRESENCE optional }|
	{ ID id-UERadioCapabilityID						CRITICALITY reject	EXTENSION UERadioCapabilityID								PRESENCE optional }|
	{ ID id-MBS-SessionInformation-List				CRITICALITY ignore	EXTENSION MBS-SessionInformation-List						PRESENCE optional }|
	{ ID id-NoPDUSessionIndication					CRITICALITY ignore	EXTENSION NoPDUSessionIndication							PRESENCE optional }|
	{ ID id-FiveGProSeUEPC5AggregateMaximumBitRate	CRITICALITY ignore	EXTENSION NRUESidelinkAggregateMaximumBitRate			PRESENCE optional }|
	{ ID id-UESliceMaximumBitRateList				CRITICALITY ignore	EXTENSION UESliceMaximumBitRateList						PRESENCE optional }|
	{ ID id-PositioningInformation					CRITICALITY ignore	EXTENSION PositioningInformation							PRESENCE optional }|
	{ ID id-NRA2XUEPC5AggregateMaximumBitRate		CRITICALITY ignore	EXTENSION NRUESidelinkAggregateMaximumBitRate			PRESENCE optional }|
	{ ID id-LTEA2XUEPC5AggregateMaximumBitRate		CRITICALITY ignore	EXTENSION LTEUESidelinkAggregateMaximumBitRate			PRESENCE optional }|
	{ ID id-NRPPaPositioningInformation				CRITICALITY ignore	EXTENSION NRPPaPositioningInformation						PRESENCE optional }|
	{ ID id-SemipersistentPositioningInformation	CRITICALITY ignore	EXTENSION SemipersistentPositioningInformation			PRESENCE optional },
	...
}


UEHistoryInformation ::= SEQUENCE (SIZE(1..maxnoofCellsinUEHistoryInfo)) OF LastVisitedCell-Item


UEHistoryInformationFromTheUE ::= CHOICE {
	nR						NRMobilityHistoryReport,
	choice-extension			ProtocolIE-Single-Container { {UEHistoryInformationFromTheUE-ExtIEs} }
}

UEHistoryInformationFromTheUE-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


UEIdentityIndexValue ::= CHOICE {
	indexLength10				BIT STRING (SIZE(10)),
	choice-extension			ProtocolIE-Single-Container { {UEIdentityIndexValue-ExtIEs} }
}

UEIdentityIndexValue-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}

UEIdentityIndexList-MBSGroupPaging ::= SEQUENCE (SIZE(1..maxnoofUEIDIndicesforMBSPaging)) OF UEIdentityIndexList-MBSGroupPaging-Item

UEIdentityIndexList-MBSGroupPaging-Item ::= SEQUENCE {
	ueIdentityIndexList-MBSGroupPagingValue		UEIdentityIndexList-MBSGroupPagingValue,
	pagingDRX									UESpecificDRX		OPTIONAL,
	iE-Extension			ProtocolExtensionContainer { {UEIdentityIndexList-MBSGroupPaging-Item-ExtIEs} } 	OPTIONAL,
	...
}

UEIdentityIndexList-MBSGroupPaging-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


UEIdentityIndexList-MBSGroupPagingValue ::= CHOICE {
	uEIdentityIndexValueMBSGroupPaging		BIT STRING (SIZE(10)),
	choice-extension			ProtocolIE-Single-Container { {UEIdentityIndexValueMBSGroupPaging-ExtIEs} }
}

UEIdentityIndexValueMBSGroupPaging-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


UERadioCapabilityForPaging ::= SEQUENCE {
	uERadioCapabilityForPagingOfNR			UERadioCapabilityForPagingOfNR			OPTIONAL,
	uERadioCapabilityForPagingOfEUTRA		UERadioCapabilityForPagingOfEUTRA		OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { {UERadioCapabilityForPaging-ExtIEs} }	OPTIONAL,
	...
}

UERadioCapabilityForPaging-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

UERadioCapabilityForPagingOfNR ::= OCTET STRING

UERadioCapabilityForPagingOfEUTRA ::= OCTET STRING

UERadioCapabilityID ::= OCTET STRING

UERANPagingIdentity ::= CHOICE {
	i-RNTI-full			BIT STRING ( SIZE (40)),
	choice-extension	ProtocolIE-Single-Container { {UERANPagingIdentity-ExtIEs} }
}

UERANPagingIdentity-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


UERLFReportContainer ::= CHOICE {
	nR-UERLFReportContainer			UERLFReportContainerNR,
	lTE-UERLFReportContainer		UERLFReportContainerLTE,
	choice-Extension		ProtocolIE-Single-Container { {UERLFReportContainer-ExtIEs} }
}
UERLFReportContainer-ExtIEs XNAP-PROTOCOL-IES ::= {
	{ID id-UERLFReportContainerLTEExtension	CRITICALITY ignore	TYPE UERLFReportContainerLTEExtension		PRESENCE mandatory},
	...
}

UERLFReportContainerLTE ::= OCTET STRING
-- This IE is a transparent container and includes the rlf-Report-r9 contained in the UEInformationResponse message as defined in TS 36.331 [14].


UERLFReportContainerLTEExtension ::= SEQUENCE {
	ueRLFReportContainerLTE					UERLFReportContainerLTE,
	ueRLFReportContainerLTEExtendBand		UERLFReportContainerLTEExtendBand,
	iE-Extensions					ProtocolExtensionContainer { { UERLFReportContainerLTEExtension-ExtIEs} } OPTIONAL,
	...
}


UERLFReportContainerLTEExtendBand ::= OCTET STRING
-- This IE is a transparent container and includes the rlf-Report-v9e0 contained in the UEInformationResponse message as defined in TS 36.331 [14].


UERLFReportContainerLTEExtension-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


UERLFReportContainerNR ::= OCTET STRING
-- This IE is a transparent container and includes the nr-RLF-Report IE contained in the UEInformationResponse message as defined in TS 38.331 [10].


UESliceMaximumBitRateList ::= SEQUENCE (SIZE(1.. maxnoofSMBR)) OF UESliceMaximumBitRate-Item

UESliceMaximumBitRate-Item ::= SEQUENCE {
	s-NSSAI						S-NSSAI,
	dl-UE-Slice-MBR				BitRate,
	ul-UE-Slice-MBR				BitRate,
	iE-Extensions						ProtocolExtensionContainer { { UESliceMaximumBitRate-Item-ExtIEs} } OPTIONAL,
	...
}

UESliceMaximumBitRate-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

UESecurityCapabilities ::= SEQUENCE {
	nr-EncyptionAlgorithms					BIT STRING {nea1-128(1),
														nea2-128(2),
														nea3-128(3)} (SIZE(16, ...)),
	nr-IntegrityProtectionAlgorithms		BIT STRING {nia1-128(1),
														nia2-128(2),
														nia3-128(3)} (SIZE(16, ...)),
	e-utra-EncyptionAlgorithms				BIT STRING {eea1-128(1),
														eea2-128(2),
														eea3-128(3)} (SIZE(16, ...)),
	e-utra-IntegrityProtectionAlgorithms	BIT STRING {eia1-128(1),
														eia2-128(2),
														eia3-128(3)} (SIZE(16, ...)),
	iE-Extension			ProtocolExtensionContainer { {UESecurityCapabilities-ExtIEs} } OPTIONAL,
	...
}

UESecurityCapabilities-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

UESpecificDRX ::= ENUMERATED {
	v32,
	v64,
	v128,
	v256,
	...
}

ULConfiguration::= SEQUENCE {
	uL-PDCP							UL-UE-Configuration,
	iE-Extensions					ProtocolExtensionContainer { {ULConfiguration-ExtIEs} } OPTIONAL,
	...
}

ULConfiguration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

UL-UE-Configuration::= ENUMERATED {no-data, shared, only, ...}

ULF1Terminating-BHInfo ::= SEQUENCE {
	ingressBAPRoutingID				BAPRoutingID,
	ingressBHRLCCHID				BHRLCChannelID,
	iE-Extensions		ProtocolExtensionContainer { { ULF1Terminating-BHInfo-ExtIEs} } OPTIONAL,
	...
}

ULF1Terminating-BHInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

ULNonF1Terminating-BHInfo ::= SEQUENCE {
	egressBAPRoutingID			BAPRoutingID,
	egressBHRLCCHID				BHRLCChannelID,
	nexthopBAPAddress			BAPAddress,
	iE-Extensions		ProtocolExtensionContainer { { ULNonF1Terminating-BHInfo-ExtIEs} } OPTIONAL,
	...
}

ULNonF1Terminating-BHInfo-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


ULForwarding	::= ENUMERATED {ul-forwarding-proposed, ...}

ULForwardingProposal	::= ENUMERATED {ul-forwarding-proposed, ...}


UL-GBR-PRB-usage::= INTEGER (0..100)


UL-GBR-PRB-usage-for-MIMO::= INTEGER (0..100)

UL-non-GBR-PRB-usage::= INTEGER (0..100)


UL-non-GBR-PRB-usage-for-MIMO::= INTEGER (0..100)

UL-Total-PRB-usage::= INTEGER (0..100)


UL-Total-PRB-usage-for-MIMO::= INTEGER (0..100)


UPTransportLayerInformation ::= CHOICE {
	gtpTunnel					GTPtunnelTransportLayerInformation,
	choice-extension			ProtocolIE-Single-Container { {UPTransportLayerInformation-ExtIEs} }
}

UPTransportLayerInformation-ExtIEs XNAP-PROTOCOL-IES ::= {
	...
}


UPTransportParameters ::= SEQUENCE (SIZE(1..maxnoofSCellGroupsplus1)) OF UPTransportParametersItem

UPTransportParametersItem ::= SEQUENCE {
	upTNLInfo		UPTransportLayerInformation,
	cellGroupID		CellGroupID,
	iE-Extension	ProtocolExtensionContainer { {UPTransportParametersItem-ExtIEs} } OPTIONAL,
	...
}

UPTransportParametersItem-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
		...
}

UserPlaneErrorIndicator ::= ENUMERATED {gtpu-error-indication-received, ...}

UserPlaneTrafficActivityReport ::= ENUMERATED {inactive, re-activated, ...}

UserPlaneFailureIndication ::= SEQUENCE {
	userPlaneFailureType		UserPlaneFailureType,
	dL-NG-U-TNLatNG-RAN			UPTransportLayerInformation,
	uL-NG-U-TNLatNG-RAN			UPTransportLayerInformation,
	iE-Extensions		ProtocolExtensionContainer {{ UserPlaneFailureIndication-ExtIEs} }	OPTIONAL,
	...
}

UserPlaneFailureIndication-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

UserPlaneFailureType ::= ENUMERATED {
	gtp-u-error-indication-received,
	up-path-failure,
	...
}

URIaddress ::= VisibleString

UEAssociatedInfoResult-List ::= SEQUENCE (SIZE(1..maxnoofUEReports)) OF UEAssociatedInfoResult-Item

UEAssociatedInfoResult-Item ::= SEQUENCE {
	uEAssistantIdentifier						NG-RANnodeUEXnAPID,	
	uEPerformance								UEPerformance			OPTIONAL,
	measuredUETrajectory						MeasuredUETrajectory	OPTIONAL,
	iE-Extensions								ProtocolExtensionContainer { { UEAssociatedInfoResult-Item-ExtIEs} } OPTIONAL,
	...
}

UEAssociatedInfoResult-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

UEPerformance ::= SEQUENCE {
	dL-UE-AverageThroughput						BitRate					OPTIONAL,
	uL-UE-AverageThroughput						BitRate					OPTIONAL,
	uE-AveragePacketDelay						AveragePacketDelay		OPTIONAL,
	uE-AveragePacketDropDL					INTEGER(0..1000000, ...)			OPTIONAL,
	iE-Extensions								ProtocolExtensionContainer { { UEPerformance-ExtIEs} } OPTIONAL,
	...
}

UEPerformance-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	{ID id-UEAveragePacketLossUL		CRITICALITY ignore	EXTENSION UEAveragePacketLossUL			PRESENCE optional },
	...
}

UEAveragePacketLossUL ::= INTEGER (0..1000000,...)

UEPerformanceCollectionConfiguration ::= SEQUENCE {
	collectionTimeDurationForUEPerformance				INTEGER(1..5000, ...),
	iE-Extensions								ProtocolExtensionContainer { { UEPerformanceCollectionConfiguration-ExtIEs} } OPTIONAL,
	...
}

UEPerformanceCollectionConfiguration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

UETrajectoryCollectionConfiguration ::= SEQUENCE {
	collectionTimeDurationForUETrajectory								INTEGER (1..4096, ...),
	numberOfVisitedCells								INTEGER (1..16, ...)							OPTIONAL,
	iE-Extensions					ProtocolExtensionContainer { { UETrajectoryCollectionConfiguration-ExtIEs} }		OPTIONAL,
	...
}

UETrajectoryCollectionConfiguration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}


-- V

VehicleUE ::= ENUMERATED {
	authorized,
	not-authorized,
	...
}

VolumeTimedReportList ::= SEQUENCE (SIZE(1..maxnooftimeperiods)) OF VolumeTimedReport-Item

VolumeTimedReport-Item ::= SEQUENCE {
	startTimeStamp					OCTET STRING (SIZE(4)),
	endTimeStamp					OCTET STRING (SIZE(4)),
	usageCountUL					INTEGER (0..18446744073709551615),
	usageCountDL					INTEGER (0..18446744073709551615),
	iE-Extensions				ProtocolExtensionContainer { {VolumeTimedReport-Item-ExtIEs} } OPTIONAL,
...
}

VolumeTimedReport-Item-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

-- W

WLANMeasurementConfiguration ::= SEQUENCE {
	wlanMeasConfig				WLANMeasConfig,
	wlanMeasConfigNameList		WLANMeasConfigNameList				OPTIONAL,
	wlan-rssi					ENUMERATED {true, ...}				OPTIONAL,
	wlan-rtt					ENUMERATED {true, ...}				OPTIONAL,
	iE-Extensions		ProtocolExtensionContainer { { WLANMeasurementConfiguration-ExtIEs } } OPTIONAL,
	...
}

WLANMeasurementConfiguration-ExtIEs XNAP-PROTOCOL-EXTENSION ::= {
	...
}

WLANMeasConfigNameList ::= SEQUENCE (SIZE(1..maxnoofWLANName)) OF WLANName

WLANMeasConfig::= ENUMERATED {setup,...}

WLANName ::= OCTET STRING (SIZE (1..32))


-- X

XnBenefitValue ::= INTEGER (1..8, ...)

XR-Bcast-Information ::= ENUMERATED {true,...}


-- Y


-- Z


END
-- ASN1STOP

9.3.6	Common definitions
-- ASN1START
-- **************************************************************
--
-- Common definitions
--
-- **************************************************************

XnAP-CommonDataTypes {
itu-t (0) identified-organization (4) etsi (0) mobileDomain (0)
ngran-access (22) modules (3) xnap (2) version1 (1) xnap-CommonDataTypes (3) }

DEFINITIONS AUTOMATIC TAGS ::=

BEGIN

-- **************************************************************
--
-- Extension constants
--
-- **************************************************************

maxPrivateIEs 									INTEGER ::= 65535
maxProtocolExtensions 							INTEGER ::= 65535
maxProtocolIEs									INTEGER ::= 65535

-- **************************************************************
--
-- Common Data Types
--
-- **************************************************************

Criticality		::= ENUMERATED { reject, ignore, notify }

Presence		::= ENUMERATED { optional, conditional, mandatory }

PrivateIE-ID	::= CHOICE {
	local				INTEGER (0.. maxPrivateIEs),
	global				OBJECT IDENTIFIER
}

ProcedureCode		::= INTEGER (0..255)


ProtocolIE-ID		::= INTEGER (0..maxProtocolIEs)


TriggeringMessage	::= ENUMERATED { initiating-message, successful-outcome, unsuccessful-outcome}

END
-- ASN1STOP

9.3.7	Constant definitions
-- ASN1START
-- **************************************************************
--
-- Constant definitions
--
-- **************************************************************

XnAP-Constants {
itu-t (0) identified-organization (4) etsi (0) mobileDomain (0)
ngran-Access (22) modules (3) xnap (2) version1 (1) xnap-Constants (4) }

DEFINITIONS AUTOMATIC TAGS ::=

BEGIN

IMPORTS
	ProcedureCode,
	ProtocolIE-ID
FROM XnAP-CommonDataTypes;

-- **************************************************************
--
-- Elementary Procedures
--
-- **************************************************************

id-handoverPreparation											ProcedureCode ::= 0
id-sNStatusTransfer												ProcedureCode ::= 1
id-handoverCancel												ProcedureCode ::= 2
id-retrieveUEContext											ProcedureCode ::= 3
id-rANPaging													ProcedureCode ::= 4
id-xnUAddressIndication											ProcedureCode ::= 5
id-uEContextRelease												ProcedureCode ::= 6
id-sNGRANnodeAdditionPreparation								ProcedureCode ::= 7
id-sNGRANnodeReconfigurationCompletion							ProcedureCode ::= 8
id-mNGRANnodeinitiatedSNGRANnodeModificationPreparation			ProcedureCode ::= 9
id-sNGRANnodeinitiatedSNGRANnodeModificationPreparation			ProcedureCode ::= 10
id-mNGRANnodeinitiatedSNGRANnodeRelease							ProcedureCode ::= 11
id-sNGRANnodeinitiatedSNGRANnodeRelease							ProcedureCode ::= 12
id-sNGRANnodeCounterCheck										ProcedureCode ::= 13
id-sNGRANnodeChange												ProcedureCode ::= 14
id-rRCTransfer													ProcedureCode ::= 15
id-xnRemoval													ProcedureCode ::= 16
id-xnSetup														ProcedureCode ::= 17
id-nGRANnodeConfigurationUpdate									ProcedureCode ::= 18
id-cellActivation												ProcedureCode ::= 19
id-reset														ProcedureCode ::= 20
id-errorIndication												ProcedureCode ::= 21
id-privateMessage												ProcedureCode ::= 22
id-notificationControl											ProcedureCode ::= 23
id-activityNotification											ProcedureCode ::= 24
id-e-UTRA-NR-CellResourceCoordination							ProcedureCode ::= 25
id-secondaryRATDataUsageReport									ProcedureCode ::= 26
id-deactivateTrace												ProcedureCode ::= 27
id-traceStart													ProcedureCode ::= 28
id-handoverSuccess												ProcedureCode ::= 29
id-conditionalHandoverCancel									ProcedureCode ::= 30
id-earlyStatusTransfer											ProcedureCode ::= 31
id-failureIndication											ProcedureCode ::= 32
id-handoverReport												ProcedureCode ::= 33
id-resourceStatusReportingInitiation							ProcedureCode ::= 34
id-resourceStatusReporting										ProcedureCode ::= 35
id-mobilitySettingsChange										ProcedureCode ::= 36
id-accessAndMobilityIndication									ProcedureCode ::= 37
id-cellTrafficTrace												ProcedureCode ::= 38
id-RANMulticastGroupPaging										ProcedureCode ::= 39
id-scgFailureInformationReport									ProcedureCode ::= 40
id-ProcedureCode41-NotToBeUsed									ProcedureCode ::= 41
id-scgFailureTransfer											ProcedureCode ::= 42
id-f1CTrafficTransfer											ProcedureCode ::= 43
id-iABTransportMigrationManagement								ProcedureCode ::= 44
id-iABTransportMigrationModification							ProcedureCode ::= 45
id-iABResourceCoordination										ProcedureCode ::= 46
id-retrieveUEContextConfirm										ProcedureCode ::= 47
id-cPCCancel													ProcedureCode ::= 48
id-partialUEContextTransfer										ProcedureCode ::= 49
id-rachIndication												ProcedureCode ::= 50
id-dataCollectionReportingInitiation							ProcedureCode ::= 51
id-dataCollectionReporting										ProcedureCode ::= 52



-- **************************************************************
--
-- Lists
--
-- **************************************************************

maxEARFCN									INTEGER ::= 262143
maxnoofAllowedAreas							INTEGER ::= 16
maxnoofAMFRegions							INTEGER ::= 16
maxnoofAoIs									INTEGER ::= 64
maxnoofBluetoothName						INTEGER ::= 4
maxnoofBPLMNs								INTEGER ::= 12
maxnoofCAGs									INTEGER ::= 12
maxnoofCAGsperPLMN							INTEGER ::= 256
maxnoofCellIDforMDT							INTEGER ::= 32
maxnoofCellsinAoI							INTEGER ::= 256
maxnoofCellsinUEHistoryInfo					INTEGER ::= 16
maxnoofCellsinNG-RANnode					INTEGER ::= 16384
maxnoofCellsinRNA							INTEGER ::= 32
maxnoofCellsUEMovingTrajectory				INTEGER ::= 16
maxnoofDRBs									INTEGER ::= 32
maxnoofEUTRABands							INTEGER ::= 16
maxnoofEUTRABPLMNs							INTEGER ::= 6
maxnoofEPLMNs								INTEGER ::= 15
maxnoofExtSliceItems						INTEGER ::= 65535
maxnoofEPLMNsplus1							INTEGER ::= 16
maxnoofForbiddenTACs						INTEGER ::= 4096
maxnoofFreqforMDT							INTEGER ::= 8
maxnoofMBSFNEUTRA							INTEGER ::= 8
maxnoofMDTPLMNs								INTEGER ::= 16
maxnoofMultiConnectivityMinusOne			INTEGER ::= 3
maxnoofNeighbours							INTEGER ::= 1024
maxnoofNeighPCIforMDT						INTEGER ::= 32
maxnoofNIDs									INTEGER ::= 12
maxnoofNRCellBands							INTEGER ::= 32
maxnoofPLMNs								INTEGER ::= 16
maxnoofPDUSessions							INTEGER ::= 256
maxnoofProtectedResourcePatterns			INTEGER ::= 16
maxnoofQoSFlows								INTEGER ::= 64
maxnoofQoSParaSets							INTEGER ::= 8
maxnoofRANAreaCodes							INTEGER ::= 32
maxnoofRANAreasinRNA						INTEGER ::= 16
maxnoofRANNodesinAoI						INTEGER ::= 64
maxnoofSCellGroups							INTEGER ::= 3
maxnoofSCellGroupsplus1						INTEGER ::= 4
maxnoofSensorName							INTEGER ::= 3
maxnoofSliceItems							INTEGER ::= 1024
maxnoofSNPNIDs								INTEGER ::= 12
maxnoofsupportedPLMNs						INTEGER ::= 12
maxnoofsupportedTACs						INTEGER ::= 256
maxnoofTAforMDT								INTEGER ::= 8
maxnoofTAI									INTEGER ::= 16
maxnoofTAIsinAoI							INTEGER ::= 16
maxnooftimeperiods							INTEGER ::= 2
maxnoofTNLAssociations						INTEGER ::= 32
maxnoofUEContexts							INTEGER ::= 8192
maxNRARFCN									INTEGER ::= 3279165
maxNrOfErrors								INTEGER ::= 256
maxnoofslots								INTEGER ::= 5120
maxnoofExtTLAs								INTEGER ::= 16
maxnoofGTPTLAs								INTEGER ::= 16
maxnoofCHOcells								INTEGER ::= 8
maxnoofPC5QoSFlows 							INTEGER ::= 2064
maxnoofSSBAreas								INTEGER ::= 64
maxnoofRAReports							INTEGER ::= 64
maxnoofNRSCSs								INTEGER ::= 5
maxnoofPhysicalResourceBlocks				INTEGER ::= 275
maxnoofAdditionalPDCPDuplicationTNL			INTEGER ::= 2
maxnoofRLCDuplicationstate					INTEGER ::= 3
maxnoofWLANName								INTEGER ::= 4
maxnoofNonAnchorCarrierFreqConfig			INTEGER ::= 15
maxnoofDataForwardingTunneltoE-UTRAN		INTEGER ::= 256
maxnoofMBSFSAs								INTEGER ::= 256
maxnoofUEIDIndicesforMBSPaging				INTEGER ::= 4096
maxnoofMBSQoSFlows							INTEGER ::= 64
maxnoofMRBs									INTEGER ::= 32
maxnoofCellsforMBS							INTEGER ::= 8192
maxnoofMBSServiceAreaInformation			INTEGER ::= 256
maxnoofTAIforMBS							INTEGER ::= 1024
maxnoofAssociatedMBSSessions				INTEGER ::= 32
maxnoofMBSSessions							INTEGER ::= 256
maxnoofSuccessfulHOReports					INTEGER ::= 64
maxnoofPSCellsPerSN							INTEGER ::= 8
maxnoofNR-UChannelIDs						INTEGER ::= 16
maxnoofCellsinCHO							INTEGER ::= 8
maxnoofCHOexecutioncond						INTEGER ::= 2
maxnoofServedCellsIAB						INTEGER ::= 512
maxnoofServingCells							INTEGER ::= 32
maxnoofBHInfo								INTEGER ::= 1024
maxnoofTrafficIndexEntries					INTEGER	::=	1024
maxnoofTLAsIAB								INTEGER	::=	1024
maxnoofBAPControlPDURLCCHs					INTEGER ::= 2
maxnoofIABSTCInfo							INTEGER ::= 45
maxnoofSymbols								INTEGER ::= 14
maxnoofDUFSlots								INTEGER ::= 320
maxnoofHSNASlots							INTEGER ::= 5120
maxnoofRBsetsPerCell						INTEGER ::= 8
maxnoofRBsetsPerCell1						INTEGER ::= 7
maxnoofChildIABNodes						INTEGER ::= 1024
maxnoofPSCellCandidates						INTEGER ::= 8
maxnoofTargetSNs							INTEGER ::= 8
maxnoofUEAppLayerMeas						INTEGER ::= 16
maxnoofSNSSAIforQMC							INTEGER ::= 16
maxnoofCellIDforQMC							INTEGER ::= 32
maxnoofPLMNforQMC							INTEGER ::= 16
maxnoofTAforQMC								INTEGER ::= 8
maxnoofMTCItems								INTEGER ::= 16
maxnoofCSIRSconfigurations					INTEGER ::= 96
maxnoofCSIRSneighbourCells					INTEGER ::= 16
maxnoofCSIRSneighbourCellsInMTC				INTEGER ::= 16
maxnoofNeighbour-NG-RAN-Nodes				INTEGER ::= 256
maxnoofSRBs									INTEGER ::= 5
maxnoofSMBR									INTEGER ::= 8
maxnoofNSAGs								INTEGER ::= 256
maxnoofTargetSNsMinusOne					INTEGER ::= 7
maxnoofThresholdsForExcessPacketDelay		INTEGER ::= 255
maxnoofESNPNs								INTEGER ::= 15
maxnoofSuccessfulPSCellChangeReports		INTEGER ::= 64
maxnoofUEsforRAReportIndications			INTEGER ::= 64
maxnoofPSCellsinCPAC						INTEGER ::= 8
maxnoofCPACexecutioncond					INTEGER ::= 2
maxnoofLBTFailureInformation				INTEGER ::= 64
maxnoofCellsTrajectoryPredict				INTEGER ::= 16
maxnoofCellsTrajectory						INTEGER	::=	16
maxFailedCellMeasObjects					INTEGER ::= 124
maxFailedMeasPerNode						INTEGER ::= 124
maxnoofUEReports							INTEGER ::= 16
maxnoofCandidateRelayUEs					INTEGER ::= 32
maxnoofCAGforMDT							INTEGER ::= 256
maxnoofMDTSNPNs								INTEGER ::= 16
maxnoofSecurityConfigurations				INTEGER ::= 8
maxnoofRSPPQoSFlows							INTEGER ::= 2048


-- **************************************************************
--
-- IEs
--
-- **************************************************************

id-ActivatedServedCells																				ProtocolIE-ID ::= 0
id-ActivationIDforCellActivation																	ProtocolIE-ID ::= 1
id-admittedSplitSRB																					ProtocolIE-ID ::= 2
id-admittedSplitSRBrelease																			ProtocolIE-ID ::= 3
id-AMF-Region-Information																			ProtocolIE-ID ::= 4
id-AssistanceDataForRANPaging																		ProtocolIE-ID ::= 5
id-BearersSubjectToCounterCheck																		ProtocolIE-ID ::= 6
id-Cause																							ProtocolIE-ID ::= 7
id-cellAssistanceInfo-NR																			ProtocolIE-ID ::= 8
id-ConfigurationUpdateInitiatingNodeChoice															ProtocolIE-ID ::= 9
id-CriticalityDiagnostics																			ProtocolIE-ID ::= 10
id-XnUAddressInfoperPDUSession-List																	ProtocolIE-ID ::= 11
id-DRBsSubjectToStatusTransfer-List																	ProtocolIE-ID ::= 12
id-ExpectedUEBehaviour																				ProtocolIE-ID ::= 13
id-GlobalNG-RAN-node-ID																				ProtocolIE-ID ::= 14
id-GUAMI																							ProtocolIE-ID ::= 15
id-indexToRatFrequSelectionPriority																	ProtocolIE-ID ::= 16
id-initiatingNodeType-ResourceCoordRequest															ProtocolIE-ID ::= 17
id-List-of-served-cells-E-UTRA																		ProtocolIE-ID ::= 18
id-List-of-served-cells-NR																			ProtocolIE-ID ::= 19
id-LocationReportingInformation																		ProtocolIE-ID ::= 20
id-MAC-I																							ProtocolIE-ID ::= 21
id-MaskedIMEISV																						ProtocolIE-ID ::= 22
id-M-NG-RANnodeUEXnAPID																				ProtocolIE-ID ::= 23
id-MN-to-SN-Container																				ProtocolIE-ID ::= 24
id-MobilityRestrictionList																			ProtocolIE-ID ::= 25
id-new-NG-RAN-Cell-Identity																			ProtocolIE-ID ::= 26
id-newNG-RANnodeUEXnAPID																			ProtocolIE-ID ::= 27
id-UEReportRRCTransfer																				ProtocolIE-ID ::= 28
id-oldNG-RANnodeUEXnAPID																			ProtocolIE-ID ::= 29
id-OldtoNewNG-RANnodeResumeContainer																ProtocolIE-ID ::= 30
id-PagingDRX																						ProtocolIE-ID ::= 31
id-PCellID																							ProtocolIE-ID ::= 32
id-PDCPChangeIndication																				ProtocolIE-ID ::= 33
id-PDUSessionAdmittedAddedAddReqAck																	ProtocolIE-ID ::= 34
id-PDUSessionAdmittedModSNModConfirm																ProtocolIE-ID ::= 35
id-PDUSessionAdmitted-SNModResponse																	ProtocolIE-ID ::= 36
id-PDUSessionNotAdmittedAddReqAck																	ProtocolIE-ID ::= 37
id-PDUSessionNotAdmitted-SNModResponse																ProtocolIE-ID ::= 38
id-PDUSessionReleasedList-RelConf																	ProtocolIE-ID ::= 39
id-PDUSessionReleasedSNModConfirm																	ProtocolIE-ID ::= 40
id-PDUSessionResourcesActivityNotifyList															ProtocolIE-ID ::= 41
id-PDUSessionResourcesAdmitted-List																	ProtocolIE-ID ::= 42
id-PDUSessionResourcesNotAdmitted-List																ProtocolIE-ID ::= 43
id-PDUSessionResourcesNotifyList																	ProtocolIE-ID ::= 44
id-PDUSession-SNChangeConfirm-List																	ProtocolIE-ID ::= 45
id-PDUSession-SNChangeRequired-List																	ProtocolIE-ID ::= 46
id-PDUSessionToBeAddedAddReq																		ProtocolIE-ID ::= 47
id-PDUSessionToBeModifiedSNModRequired																ProtocolIE-ID ::= 48
id-PDUSessionToBeReleasedList-RelRqd																ProtocolIE-ID ::= 49
id-PDUSessionToBeReleased-RelReq																	ProtocolIE-ID ::= 50
id-PDUSessionToBeReleasedSNModRequired																ProtocolIE-ID ::= 51
id-RANPagingArea																					ProtocolIE-ID ::= 52
id-PagingPriority																					ProtocolIE-ID ::= 53
id-requestedSplitSRB																				ProtocolIE-ID ::= 54
id-requestedSplitSRBrelease																			ProtocolIE-ID ::= 55
id-ResetRequestTypeInfo																				ProtocolIE-ID ::= 56
id-ResetResponseTypeInfo																			ProtocolIE-ID ::= 57
id-RespondingNodeTypeConfigUpdateAck																ProtocolIE-ID ::= 58
id-respondingNodeType-ResourceCoordResponse															ProtocolIE-ID ::= 59
id-ResponseInfo-ReconfCompl																			ProtocolIE-ID ::= 60
id-RRCConfigIndication																				ProtocolIE-ID ::= 61
id-RRCResumeCause																					ProtocolIE-ID ::= 62
id-SCGConfigurationQuery																			ProtocolIE-ID ::= 63
id-selectedPLMN																						ProtocolIE-ID ::= 64
id-ServedCellsToActivate																			ProtocolIE-ID ::= 65
id-servedCellsToUpdate-E-UTRA																		ProtocolIE-ID ::= 66
id-ServedCellsToUpdateInitiatingNodeChoice															ProtocolIE-ID ::= 67
id-servedCellsToUpdate-NR																			ProtocolIE-ID ::= 68
id-s-ng-RANnode-SecurityKey																			ProtocolIE-ID ::= 69
id-S-NG-RANnodeUE-AMBR																				ProtocolIE-ID ::= 70
id-S-NG-RANnodeUEXnAPID																				ProtocolIE-ID ::= 71
id-SN-to-MN-Container																				ProtocolIE-ID ::= 72
id-sourceNG-RANnodeUEXnAPID																			ProtocolIE-ID ::= 73
id-SplitSRB-RRCTransfer																				ProtocolIE-ID ::= 74
id-TAISupport-list																					ProtocolIE-ID ::= 75
id-TimeToWait																						ProtocolIE-ID ::= 76
id-Target2SourceNG-RANnodeTranspContainer															ProtocolIE-ID ::= 77
id-targetCellGlobalID																				ProtocolIE-ID ::= 78
id-targetNG-RANnodeUEXnAPID																			ProtocolIE-ID ::= 79
id-target-S-NG-RANnodeID																			ProtocolIE-ID ::= 80
id-TraceActivation																					ProtocolIE-ID ::= 81
id-UEContextID																						ProtocolIE-ID ::= 82
id-UEContextInfoHORequest																			ProtocolIE-ID ::= 83
id-UEContextInfoRetrUECtxtResp																		ProtocolIE-ID ::= 84
id-UEContextInfo-SNModRequest																		ProtocolIE-ID ::= 85
id-UEContextKeptIndicator																			ProtocolIE-ID ::= 86
id-UEContextRefAtSN-HORequest																		ProtocolIE-ID ::= 87
id-UEHistoryInformation																				ProtocolIE-ID ::= 88
id-UEIdentityIndexValue																				ProtocolIE-ID ::= 89
id-UERANPagingIdentity																				ProtocolIE-ID ::= 90
id-UESecurityCapabilities																			ProtocolIE-ID ::= 91
id-UserPlaneTrafficActivityReport																	ProtocolIE-ID ::= 92
id-XnRemovalThreshold																				ProtocolIE-ID ::= 93
id-DesiredActNotificationLevel																		ProtocolIE-ID ::= 94
id-AvailableDRBIDs																					ProtocolIE-ID ::= 95
id-AdditionalDRBIDs																					ProtocolIE-ID ::= 96
id-SpareDRBIDs 																						ProtocolIE-ID ::= 97
id-RequiredNumberOfDRBIDs																			ProtocolIE-ID ::= 98
id-TNLA-To-Add-List																					ProtocolIE-ID ::= 99
id-TNLA-To-Update-List																				ProtocolIE-ID ::= 100
id-TNLA-To-Remove-List																				ProtocolIE-ID ::= 101
id-TNLA-Setup-List																					ProtocolIE-ID ::= 102
id-TNLA-Failed-To-Setup-List																		ProtocolIE-ID ::= 103
id-PDUSessionToBeReleased-RelReqAck																	ProtocolIE-ID ::= 104
id-S-NG-RANnodeMaxIPDataRate-UL																		ProtocolIE-ID ::= 105
id-PDUSessionResourceSecondaryRATUsageList															ProtocolIE-ID ::= 107
id-Additional-UL-NG-U-TNLatUPF-List																	ProtocolIE-ID ::= 108
id-SecondarydataForwardingInfoFromTarget-List														ProtocolIE-ID ::= 109
id-LocationInformationSNReporting																	ProtocolIE-ID ::= 110
id-LocationInformationSN																			ProtocolIE-ID ::= 111
id-LastE-UTRANPLMNIdentity																			ProtocolIE-ID ::= 112
id-S-NG-RANnodeMaxIPDataRate-DL																		ProtocolIE-ID ::= 113
id-MaxIPrate-DL																						ProtocolIE-ID ::= 114
id-SecurityResult																					ProtocolIE-ID ::= 115
id-S-NSSAI																							ProtocolIE-ID ::= 116
id-MR-DC-ResourceCoordinationInfo																	ProtocolIE-ID ::= 117
id-AMF-Region-Information-To-Add																	ProtocolIE-ID ::= 118
id-AMF-Region-Information-To-Delete																	ProtocolIE-ID ::= 119
id-OldQoSFlowMap-ULendmarkerexpected																ProtocolIE-ID ::= 120
id-RANPagingFailure																					ProtocolIE-ID ::= 121
id-UERadioCapabilityForPaging																		ProtocolIE-ID ::= 122
id-PDUSessionDataForwarding-SNModResponse															ProtocolIE-ID ::= 123
id-DRBsNotAdmittedSetupModifyList																	ProtocolIE-ID ::= 124
id-Secondary-MN-Xn-U-TNLInfoatM																		ProtocolIE-ID ::= 125
id-NE-DC-TDM-Pattern																				ProtocolIE-ID ::= 126
id-PDUSessionCommonNetworkInstance																	ProtocolIE-ID ::= 127
id-BPLMN-ID-Info-EUTRA																				ProtocolIE-ID ::= 128
id-BPLMN-ID-Info-NR																					ProtocolIE-ID ::= 129
id-InterfaceInstanceIndication																		ProtocolIE-ID ::= 130
id-S-NG-RANnode-Addition-Trigger-Ind																ProtocolIE-ID ::= 131
id-DefaultDRB-Allowed																				ProtocolIE-ID ::= 132
id-DRB-IDs-takenintouse																				ProtocolIE-ID ::= 133
id-SplitSessionIndicator																			ProtocolIE-ID ::= 134
id-CNTypeRestrictionsForEquivalent																	ProtocolIE-ID ::= 135
id-CNTypeRestrictionsForServing																		ProtocolIE-ID ::= 136
id-DRBs-transferred-to-MN																			ProtocolIE-ID ::= 137
id-ULForwardingProposal																				ProtocolIE-ID ::= 138
id-EndpointIPAddressAndPort																			ProtocolIE-ID ::= 139
id-IntendedTDD-DL-ULConfiguration-NR																ProtocolIE-ID ::= 140
id-TNLConfigurationInfo																				ProtocolIE-ID ::= 141
id-PartialListIndicator-NR																			ProtocolIE-ID ::= 142
id-MessageOversizeNotification																		ProtocolIE-ID ::= 143
id-CellAndCapacityAssistanceInfo-NR																	ProtocolIE-ID ::= 144
id-NG-RANTraceID																					ProtocolIE-ID ::= 145
id-NonGBRResources-Offered																			ProtocolIE-ID ::= 146
id-FastMCGRecoveryRRCTransfer-SN-to-MN																ProtocolIE-ID ::= 147
id-RequestedFastMCGRecoveryViaSRB3																	ProtocolIE-ID ::= 148
id-AvailableFastMCGRecoveryViaSRB3																	ProtocolIE-ID ::= 149
id-RequestedFastMCGRecoveryViaSRB3Release															ProtocolIE-ID ::= 150
id-ReleaseFastMCGRecoveryViaSRB3																	ProtocolIE-ID ::= 151
id-FastMCGRecoveryRRCTransfer-MN-to-SN																ProtocolIE-ID ::= 152
id-ExtendedRATRestrictionInformation																ProtocolIE-ID ::= 153
id-QoSMonitoringRequest																				ProtocolIE-ID ::= 154
id-FiveGCMobilityRestrictionListContainer															ProtocolIE-ID ::= 155
id-PartialListIndicator-EUTRA																		ProtocolIE-ID ::= 156
id-CellAndCapacityAssistanceInfo-EUTRA																ProtocolIE-ID ::= 157
id-CHOinformation-Req																				ProtocolIE-ID ::= 158
id-CHOinformation-Ack																				ProtocolIE-ID ::= 159
id-targetCellsToCancel																				ProtocolIE-ID ::= 160
id-requestedTargetCellGlobalID																		ProtocolIE-ID ::= 161
id-procedureStage																					ProtocolIE-ID ::= 162
id-DAPSRequestInfo																					ProtocolIE-ID ::= 163
id-DAPSResponseInfo-List																			ProtocolIE-ID ::= 164
id-CHO-MRDC-Indicator																				ProtocolIE-ID ::= 165
id-OffsetOfNbiotChannelNumberToDL-EARFCN															ProtocolIE-ID ::= 166
id-OffsetOfNbiotChannelNumberToUL-EARFCN															ProtocolIE-ID ::= 167
id-NBIoT-UL-DL-AlignmentOffset																		ProtocolIE-ID ::= 168
id-LTEV2XServicesAuthorized																			ProtocolIE-ID ::= 169
id-NRV2XServicesAuthorized																			ProtocolIE-ID ::= 170
id-LTEUESidelinkAggregateMaximumBitRate																ProtocolIE-ID ::= 171
id-NRUESidelinkAggregateMaximumBitRate																ProtocolIE-ID ::= 172
id-PC5QoSParameters																					ProtocolIE-ID ::= 173
id-AlternativeQoSParaSetList																		ProtocolIE-ID ::= 174
id-CurrentQoSParaSetIndex																			ProtocolIE-ID ::= 175
id-MobilityInformation																				ProtocolIE-ID ::= 176
id-InitiatingCondition-FailureIndication															ProtocolIE-ID ::= 177
id-UEHistoryInformationFromTheUE																	ProtocolIE-ID ::= 178
id-HandoverReportType																				ProtocolIE-ID ::= 179
id-HandoverCause																					ProtocolIE-ID ::= 180
id-SourceCellCGI																					ProtocolIE-ID ::= 181
id-TargetCellCGI																					ProtocolIE-ID ::= 182
id-ReEstablishmentCellCGI																			ProtocolIE-ID ::= 183
id-TargetCellinEUTRAN																				ProtocolIE-ID ::= 184
id-SourceCellCRNTI																					ProtocolIE-ID ::= 185
id-UERLFReportContainer																				ProtocolIE-ID ::= 186
id-NGRAN-Node1-Measurement-ID																		ProtocolIE-ID ::= 187
id-NGRAN-Node2-Measurement-ID																		ProtocolIE-ID ::= 188
id-RegistrationRequest																				ProtocolIE-ID ::= 189
id-ReportCharacteristics																				ProtocolIE-ID ::= 190
id-CellToReport																								ProtocolIE-ID ::= 191
id-ReportingPeriodicity																					ProtocolIE-ID ::= 192
id-CellMeasurementResult																				ProtocolIE-ID ::= 193
id-NG-RANnode1CellID																						ProtocolIE-ID ::= 194
id-NG-RANnode2CellID																				ProtocolIE-ID ::= 195
id-NG-RANnode1MobilityParameters																	ProtocolIE-ID ::= 196
id-NG-RANnode2ProposedMobilityParameters															ProtocolIE-ID ::= 197
id-MobilityParametersModificationRange																ProtocolIE-ID ::= 198
id-TDDULDLConfigurationCommonNR																		ProtocolIE-ID ::= 199
id-CarrierList																						ProtocolIE-ID ::= 200
id-ULCarrierList																					ProtocolIE-ID ::= 201
id-FrequencyShift7p5khz																				ProtocolIE-ID ::= 202
id-SSB-PositionsInBurst																				ProtocolIE-ID ::= 203
id-NRCellPRACHConfig																				ProtocolIE-ID ::= 204
id-RAReport																							ProtocolIE-ID ::= 205
id-IABNodeIndication																				ProtocolIE-ID ::= 206
id-Redundant-UL-NG-U-TNLatUPF																		ProtocolIE-ID ::= 207
id-CNPacketDelayBudgetDownlink																		ProtocolIE-ID ::= 208
id-CNPacketDelayBudgetUplink																		ProtocolIE-ID ::= 209
id-Additional-Redundant-UL-NG-U-TNLatUPF-List														ProtocolIE-ID ::= 210
id-RedundantCommonNetworkInstance																	ProtocolIE-ID ::= 211
id-TSCTrafficCharacteristics																		ProtocolIE-ID ::= 212
id-RedundantQoSFlowIndicator																		ProtocolIE-ID ::= 213
id-Redundant-DL-NG-U-TNLatNG-RAN																	ProtocolIE-ID ::= 214
id-ExtendedPacketDelayBudget																		ProtocolIE-ID ::= 215
id-Additional-PDCP-Duplication-TNL-List																ProtocolIE-ID ::= 216
id-RedundantPDUSessionInformation																	ProtocolIE-ID ::= 217
id-UsedRSNInformation																				ProtocolIE-ID ::= 218
id-RLCDuplicationInformation																		ProtocolIE-ID ::= 219
id-NPN-Broadcast-Information																		ProtocolIE-ID ::= 220
id-NPNPagingAssistanceInformation																	ProtocolIE-ID ::= 221
id-NPNMobilityInformation																			ProtocolIE-ID ::= 222
id-NPN-Support																						ProtocolIE-ID ::= 223
id-MDT-Configuration																				ProtocolIE-ID ::= 224
id-MDTPLMNList																						ProtocolIE-ID ::= 225
id-TraceCollectionEntityURI																			ProtocolIE-ID ::= 226
id-UERadioCapabilityID																				ProtocolIE-ID ::= 227
id-CSI-RSTransmissionIndication																		ProtocolIE-ID ::= 228
id-SNTriggered																						ProtocolIE-ID ::= 229
id-DLCarrierList																					ProtocolIE-ID ::= 230
id-ExtendedTAISliceSupportList																		ProtocolIE-ID ::= 231
id-cellAssistanceInfo-EUTRA																			ProtocolIE-ID ::= 232
id-ConfiguredTACIndication																			ProtocolIE-ID ::= 233
id-secondary-SN-UL-PDCP-UP-TNLInfo																	ProtocolIE-ID ::= 234
id-pdcpDuplicationConfiguration																		ProtocolIE-ID ::= 235
id-duplicationActivation																			ProtocolIE-ID ::= 236
id-NPRACHConfiguration																				ProtocolIE-ID ::= 237
id-QosMonitoringReportingFrequency																	ProtocolIE-ID ::= 238
id-QoSFlowsMappedtoDRB-SetupResponse-MNterminated													ProtocolIE-ID ::= 239
id-DL-scheduling-PDCCH-CCE-usage																	ProtocolIE-ID ::= 240
id-UL-scheduling-PDCCH-CCE-usage																	ProtocolIE-ID ::= 241
id-SFN-Offset																						ProtocolIE-ID ::= 242
id-QoSMonitoringDisabled																			ProtocolIE-ID ::= 243
id-ExtendedUEIdentityIndexValue																		ProtocolIE-ID ::= 244
id-EUTRAPagingeDRXInformation																		ProtocolIE-ID ::= 245
id-CHO-MRDC-EarlyDataForwarding																		ProtocolIE-ID ::= 246
id-SCGIndicator																						ProtocolIE-ID ::= 247
id-UESpecificDRX																					ProtocolIE-ID ::= 248
id-PDUSessionExpectedUEActivityBehaviour															ProtocolIE-ID ::= 249
id-QoS-Mapping-Information																			ProtocolIE-ID ::= 250
id-AdditionLocationInformation																		ProtocolIE-ID ::= 251
id-dataForwardingInfoFromTargetE-UTRANnode															ProtocolIE-ID ::= 252
id-DirectForwardingPathAvailability																	ProtocolIE-ID ::= 253
id-SourceNG-RAN-node-ID																				ProtocolIE-ID ::= 254
id-SourceDLForwardingIPAddress																		ProtocolIE-ID ::= 255
id-SourceNodeDLForwardingIPAddress																	ProtocolIE-ID ::= 256
id-ExtendedReportIntervalMDT																		ProtocolIE-ID ::= 257
id-SecurityIndication																				ProtocolIE-ID ::= 258
id-RRCConnReestab-Indicator																			ProtocolIE-ID ::= 259
id-TargetNodeID																						ProtocolIE-ID ::= 260
id-ManagementBasedMDTPLMNList																		ProtocolIE-ID ::= 261
id-PrivacyIndicator																					ProtocolIE-ID ::= 262
id-TraceCollectionEntityIPAddress																	ProtocolIE-ID ::= 263
id-M4ReportAmount																					ProtocolIE-ID ::= 264
id-M5ReportAmount																					ProtocolIE-ID ::= 265
id-M6ReportAmount																					ProtocolIE-ID ::= 266
id-M7ReportAmount																					ProtocolIE-ID ::= 267
id-BeamMeasurementIndicationM1																		ProtocolIE-ID ::= 268
id-MBS-Session-ID																					ProtocolIE-ID ::= 269
id-UEIdentityIndexList-MBSGroupPaging																	ProtocolIE-ID ::= 270
id-MulticastRANPagingArea																			ProtocolIE-ID ::= 271
id-Supported-MBS-FSA-ID-List																		ProtocolIE-ID ::= 272
id-MBS-SessionInformation-List																		ProtocolIE-ID ::= 273
id-MBS-SessionInformationResponse-List																ProtocolIE-ID ::= 274
id-MBS-SessionAssociatedInformation																	ProtocolIE-ID ::= 275
id-SuccessfulHOReportInformation																	ProtocolIE-ID ::= 276
id-SliceRadioResourceStatus-List																	ProtocolIE-ID ::= 277
id-CompositeAvailableCapacitySupplementaryUplink													ProtocolIE-ID ::= 278
id-SCGUEHistoryInformation																			ProtocolIE-ID ::= 279
id-SSBOffsets-List																					ProtocolIE-ID ::= 280
id-NG-RANnode2SSBOffsetModificationRange															ProtocolIE-ID ::= 281
id-Coverage-Modification-List																		ProtocolIE-ID ::= 282
id-NR-U-Channel-List																				ProtocolIE-ID ::= 283
id-SourcePSCellCGI																					ProtocolIE-ID ::= 284
id-FailedPSCellCGI																					ProtocolIE-ID ::= 285
id-SCGFailureReportContainer																		ProtocolIE-ID ::= 286
id-SNMobilityInformation																			ProtocolIE-ID ::= 287
id-SourcePSCellID																					ProtocolIE-ID ::= 288
id-SuitablePSCellCGI																				ProtocolIE-ID ::= 289
id-PSCellChangeHistory																				ProtocolIE-ID ::= 290
id-CHOConfiguration																					ProtocolIE-ID ::= 291
id-NR-U-ChannelInfo-List																			ProtocolIE-ID ::= 292
id-PSCellHistoryInformationRetrieve																	ProtocolIE-ID ::= 293
id-NG-RANnode2SSBOffsetsModificationRange															ProtocolIE-ID ::= 294
id-MIMOPRBusageInformation																			ProtocolIE-ID ::= 295
id-F1CTrafficContainer																				ProtocolIE-ID ::= 296
id-IAB-MT-Cell-List																					ProtocolIE-ID ::= 297
id-NoPDUSessionIndication																			ProtocolIE-ID ::= 298
id-IAB-TNL-Address-Request																			ProtocolIE-ID ::= 299
id-IAB-TNL-Address-Response																			ProtocolIE-ID ::= 300
id-TrafficToBeAddedList																				ProtocolIE-ID ::= 301
id-TrafficToBeModifiedList																			ProtocolIE-ID ::= 302
id-TrafficToBeReleaseInformation																	ProtocolIE-ID ::= 303
id-TrafficAddedList																					ProtocolIE-ID ::= 304
id-TrafficModifiedList																				ProtocolIE-ID ::= 305
id-TrafficNotAddedList																				ProtocolIE-ID ::= 306
id-TrafficNotModifiedList																			ProtocolIE-ID ::= 307
id-TrafficRequiredToBeModifiedList																	ProtocolIE-ID ::= 308
id-TrafficRequiredModifiedList																		ProtocolIE-ID ::= 309
id-TrafficReleasedList																				ProtocolIE-ID ::= 310
id-IABTNLAddressToBeAdded																			ProtocolIE-ID ::= 311
id-IABTNLAddressToBeReleasedList																	ProtocolIE-ID ::= 312
id-nonF1-Terminating-IAB-DonorUEXnAPID																ProtocolIE-ID ::= 313
id-F1-Terminating-IAB-DonorUEXnAPID																	ProtocolIE-ID ::= 314
id-BoundaryNodeCellsList																			ProtocolIE-ID ::= 315
id-ParentNodeCellsList																				ProtocolIE-ID ::= 316
id-tdd-GNB-DU-Cell-Resource-Configuration															ProtocolIE-ID ::= 317
id-UL-GNB-DU-Cell-Resource-Configuration															ProtocolIE-ID ::= 318
id-DL-GNB-DU-Cell-Resource-Configuration															ProtocolIE-ID ::= 319
id-permutation																						ProtocolIE-ID ::= 320
id-IABTNLAddressException																			ProtocolIE-ID ::= 321
id-CHOinformation-AddReq																			ProtocolIE-ID ::= 322
id-CHOinformation-ModReq																			ProtocolIE-ID ::= 323
id-SurvivalTime																						ProtocolIE-ID ::= 324
id-TimeSynchronizationAssistanceInformation															ProtocolIE-ID ::= 325
id-SCGActivationRequest																				ProtocolIE-ID ::= 326
id-SCGActivationStatus																				ProtocolIE-ID ::= 327
id-CPAInformationRequest																			ProtocolIE-ID ::= 328
id-CPAInformationAck																				ProtocolIE-ID ::= 329
id-CPCInformationRequired																			ProtocolIE-ID ::= 330
id-CPCInformationConfirm																			ProtocolIE-ID ::= 331
id-CPAInformationModReq																				ProtocolIE-ID ::= 332
id-CPAInformationModReqAck																			ProtocolIE-ID ::= 333
id-CPC-DataForwarding-Indicator																		ProtocolIE-ID ::= 334
id-CPCInformationUpdate																				ProtocolIE-ID ::= 335
id-CPACInformationModRequired																		ProtocolIE-ID ::= 336
id-QMCConfigInfo																					ProtocolIE-ID ::= 337
id-ProtocolIE-ID338-NotToBeUsed																		ProtocolIE-ID ::= 338
id-Additional-Measurement-Timing-Configuration-List													ProtocolIE-ID ::= 339
id-PDUSession-PairID																				ProtocolIE-ID ::= 340
id-Local-NG-RAN-Node-Identifier																		ProtocolIE-ID ::= 341
id-Neighbour-NG-RAN-Node-List																		ProtocolIE-ID ::= 342
id-Local-NG-RAN-Node-Identifier-Removal																ProtocolIE-ID ::= 343
id-FiveGProSeAuthorized																				ProtocolIE-ID ::= 344
id-FiveGProSePC5QoSParameters																		ProtocolIE-ID ::= 345
id-FiveGProSeUEPC5AggregateMaximumBitRate															ProtocolIE-ID ::= 346
id-ServedCellSpecificInfoReq-NR																		ProtocolIE-ID ::= 347
id-NRPagingeDRXInformation																			ProtocolIE-ID ::= 348
id-NRPagingeDRXInformationforRRCINACTIVE															ProtocolIE-ID ::= 349
id-Redcap-Bcast-Information																			ProtocolIE-ID ::= 350
id-SDTSupportRequest																				ProtocolIE-ID ::= 351
id-SDT-SRB-between-NewNode-OldNode																	ProtocolIE-ID ::= 352
id-SDT-Termination-Request																			ProtocolIE-ID ::= 353
id-SDTPartialUEContextInfo																			ProtocolIE-ID ::= 354
id-SDTDataForwardingDRBList																			ProtocolIE-ID ::= 355
id-PagingCause																						ProtocolIE-ID ::= 356
id-PEIPSassistanceInformation																		ProtocolIE-ID ::= 357
id-UESliceMaximumBitRateList																		ProtocolIE-ID ::= 358
id-S-NG-RANnodeUE-Slice-MBR																			ProtocolIE-ID ::= 359
id-PositioningInformation																			ProtocolIE-ID ::= 360
id-UEAssistantIdentifier																			ProtocolIE-ID ::= 361
id-ManagementBasedMDTPLMNModificationList															ProtocolIE-ID ::= 362
id-F1-terminatingIAB-donorIndicator																	ProtocolIE-ID ::= 363
id-TAINSAGSupportList																				ProtocolIE-ID ::= 364
id-SCGreconfigNotification																			ProtocolIE-ID ::= 365
id-earlyMeasurement																					ProtocolIE-ID ::= 366
id-BeamMeasurementsReportConfiguration																ProtocolIE-ID ::= 367
id-CoverageModificationCause																		ProtocolIE-ID ::= 368
id-AdditionalListofPDUSessionResourceChangeConfirmInfo-SNterminated									ProtocolIE-ID ::= 369
id-UERLFReportContainerLTEExtension																ProtocolIE-ID ::= 370
id-ExcessPacketDelayThresholdConfiguration															ProtocolIE-ID ::= 371
id-HashedUEIdentityIndexValue																		ProtocolIE-ID ::= 372
id-QosFlowMappingIndication																			ProtocolIE-ID ::= 373
id-Full-and-Short-I-RNTI-Profile-List																ProtocolIE-ID ::= 374
id-MBS-DataForwarding-Indicator																		ProtocolIE-ID ::= 375
id-IABAuthorizationStatus																			ProtocolIE-ID ::= 376
id-EquivalentSNPNs																					ProtocolIE-ID ::= 377
id-SelectedNID																						ProtocolIE-ID ::= 378
id-MT-SDT-Information																				ProtocolIE-ID ::= 379
id-PosPartialUEContextInfo																			ProtocolIE-ID ::= 380
id-SRSConfiguration																					ProtocolIE-ID ::= 381
id-CHOTimeBasedInformation																			ProtocolIE-ID ::= 382
id-ChannelOccupancyTimePercentageUL																	ProtocolIE-ID ::= 383
id-EnergyDetectionThresholdUL																		ProtocolIE-ID ::= 384
id-SuccessfulPSCellChangeReportInformation															ProtocolIE-ID ::= 385
id-PSCellListContainer																				ProtocolIE-ID ::= 386
id-RadioResourceStatusNR-U																			ProtocolIE-ID ::= 387
id-CPACConfiguration																				ProtocolIE-ID ::= 388
id-RaReportIndicationList																			ProtocolIE-ID ::= 389
id-SPRAvailability																					ProtocolIE-ID ::= 390
id-DLLBTFailureInformationRequest																	ProtocolIE-ID ::= 391
id-DLLBTFailureInformationList																		ProtocolIE-ID ::= 392
id-TargetCellCRNTI																					ProtocolIE-ID ::= 393
id-TimeSinceFailure																					ProtocolIE-ID ::= 394
id-AerialUESubscriptionInformation																	ProtocolIE-ID ::= 395
id-LTEA2XServicesAuthorized																			ProtocolIE-ID ::= 396
id-NRA2XServicesAuthorized																			ProtocolIE-ID ::= 397
id-LTEA2XUEPC5AggregateMaximumBitRate																ProtocolIE-ID ::= 398
id-NRA2XUEPC5AggregateMaximumBitRate																ProtocolIE-ID ::= 399
id-A2XPC5QoSParameters																				ProtocolIE-ID ::= 400
id-CellBasedUETrajectoryPrediction																	ProtocolIE-ID ::= 401
id-DataCollectionID																					ProtocolIE-ID ::= 402
id-RequestedPredictionTime																			ProtocolIE-ID ::= 403
id-NodeMeasurementInitiationResult-List																ProtocolIE-ID ::= 404
id-CellMeasurementInitiationResult-List																ProtocolIE-ID ::= 405
id-UEAssociatedInfoResult-List																		ProtocolIE-ID ::= 406
id-ProtocolIE-ID-407-not-to-be-used																	ProtocolIE-ID ::= 407
id-UETrajectoryCollectionConfiguration																ProtocolIE-ID ::= 408
id-UEPerformanceCollectionConfiguration																ProtocolIE-ID ::= 409
id-CellMeasurementResultForDataCollection-List															ProtocolIE-ID ::= 410
id-CellToReportForDataCollection-List																ProtocolIE-ID ::= 411
id-FiveGProSeLayer2Multipath																		ProtocolIE-ID ::= 412
id-FiveGProSeLayer2UEtoUERelay																		ProtocolIE-ID ::= 413
id-FiveGProSeLayer2UEtoUERemote																		ProtocolIE-ID ::= 414
id-CandidateRelayUEInfoList																			ProtocolIE-ID ::= 415
id-NRCellsAndSSBsList																				ProtocolIE-ID ::= 416
id-ActivatedNRCellsAndSSBsList																		ProtocolIE-ID ::= 417
id-ClockQualityReportingControlInfo																	ProtocolIE-ID ::= 418
id-CapabilityForBATAdaptation																		ProtocolIE-ID ::= 419
id-PNI-NPN-AreaScopeofMDT																			ProtocolIE-ID ::= 420
id-PNI-NPNBasedMDT																					ProtocolIE-ID ::= 421
id-SNPN-CellBasedMDT																				ProtocolIE-ID ::= 422
id-SNPN-TAIBasedMDT																					ProtocolIE-ID ::= 423
id-SNPN-BasedMDT																					ProtocolIE-ID ::= 424
id-S-CPAC-Request																					ProtocolIE-ID ::= 425
id-S-CPAC-Request-Info																				ProtocolIE-ID ::= 426
id-S-CPAC-ReferenceConfigRequest																	ProtocolIE-ID ::= 427
id-S-CPAC-InterSN-ExecutionNotify																	ProtocolIE-ID ::= 428
id-S-CPAC-dataforwardinginfofromSource																ProtocolIE-ID ::= 429
id-CPACcandidatePSCells-wotherInfo-list																ProtocolIE-ID ::= 430
id-eRedcap-Bcast-Information																		ProtocolIE-ID ::= 431
id-NRPagingLongeDRXInformationforRRCINACTIVE														ProtocolIE-ID ::= 432
id-MBS-AssistanceInformation																		ProtocolIE-ID ::= 433
id-QMCCoordinationRequest																			ProtocolIE-ID ::= 434
id-QMCCoordinationResponse																			ProtocolIE-ID ::= 435
id-QoE-Measurement-Results																			ProtocolIE-ID ::= 436
id-MBSCommServiceType																				ProtocolIE-ID ::= 437
id-AssistanceInformationQoE-Meas																	ProtocolIE-ID ::= 438
id-ProtocolIE-ID-439-not-to-be-used																	ProtocolIE-ID ::= 439
id-QoERVQoEReportingPaths																			ProtocolIE-ID ::= 440
id-Src-SN-to-Tgt-SNQMCInfoInquiry																	ProtocolIE-ID ::= 441
id-DirectForwardingPathAvailabilityWithSourceMN														ProtocolIE-ID ::= 442
id-CHO-Maxnoof-CondReconfig																			ProtocolIE-ID ::= 443
id-accessed-PSCellID																				ProtocolIE-ID ::= 444
id-conditional-Reconfig-ToCancel-List																ProtocolIE-ID ::= 445
id-CHOinformation-AddReqAck																			ProtocolIE-ID ::= 446
id-CHO-CPAC-Info																					ProtocolIE-ID ::= 447
id-PDUSetQoSParameters																				ProtocolIE-ID ::= 448
id-N6JitterInformation																				ProtocolIE-ID ::= 449
id-ECNMarkingorCongestionInformationReportingRequest												ProtocolIE-ID ::= 450
id-PDUSetbasedHandlingIndicator																		ProtocolIE-ID ::= 451
id-TAISliceUnavailableCellList																		ProtocolIE-ID ::= 452
id-MobileIAB-AuthorizationStatus																	ProtocolIE-ID ::= 453
id-MIAB-MT-BAP-Address																				ProtocolIE-ID ::= 454
id-MobileIABCell																					ProtocolIE-ID ::= 455
id-sk-Counter																						ProtocolIE-ID ::= 456
id-Source-M-NG-RANnodeID																			ProtocolIE-ID ::= 457
id-ProtocolIE-ID458-NotToBeUsed																		ProtocolIE-ID ::= 458
id-SourceSN-to-TargetSN-QMCInfo																		ProtocolIE-ID ::= 459
id-RegistrationRequestForDataCollection																ProtocolIE-ID ::= 460
id-ReportCharacteristicsForDataCollection															ProtocolIE-ID ::= 461
id-ReportingPeriodicityForDataCollection															ProtocolIE-ID ::= 462
id-NodeAssociatedInfoResult																			ProtocolIE-ID ::= 463
id-SLPositioning-Ranging-Services-Info																ProtocolIE-ID ::= 464
id-XR-Bcast-Information					 															ProtocolIE-ID ::= 465
id-PDUSessionsListToBeReleased-UPError																ProtocolIE-ID ::= 466
id-MaximumDataBurstVolume																			ProtocolIE-ID ::= 467
id-CPAC-Preparation-Type																			ProtocolIE-ID ::= 468
id-UserPlaneFailureIndication																		ProtocolIE-ID ::= 469
id-MN-only-MDT-collection																			ProtocolIE-ID ::= 470
id-BarringExemptionforEmerCallInfo																	ProtocolIE-ID ::= 471
id-Transmission-Bandwidth-asymmetric																ProtocolIE-ID ::= 472
id-SRSPositioningConfigOrActivationRequest															ProtocolIE-ID ::= 473
id-NRPPaPositioningInformation																		ProtocolIE-ID ::= 474
id-SemipersistentPositioningInformation																	ProtocolIE-ID ::= 545
id-UEAveragePacketLossUL																			ProtocolIE-ID ::= 546


END
-- ASN1STOP

9.3.8	Container definitions
-- ASN1START
-- **************************************************************
--
-- Container definitions
--
-- **************************************************************

XnAP-Containers {
itu-t (0) identified-organization (4) etsi (0) mobileDomain (0)
ngran-access (22) modules (3) xnap (2) version1 (1) xnap-Containers (5) }

DEFINITIONS AUTOMATIC TAGS ::=

BEGIN

-- **************************************************************
--
-- IE parameter types from other modules.
--
-- **************************************************************

IMPORTS
	maxPrivateIEs,
	maxProtocolExtensions,
	maxProtocolIEs,
	Criticality,
	Presence,
	PrivateIE-ID,
	ProtocolIE-ID	
FROM XnAP-CommonDataTypes;

-- **************************************************************
--
-- Class Definition for Protocol IEs
--
-- **************************************************************

XNAP-PROTOCOL-IES ::= CLASS {
	&id				ProtocolIE-ID 			UNIQUE,
	&criticality	Criticality,
	&Value,
	&presence		Presence
}
WITH SYNTAX {
	ID				&id
	CRITICALITY		&criticality
	TYPE			&Value
	PRESENCE		&presence
}

-- **************************************************************
--
-- Class Definition for Protocol IE pairs
--
-- **************************************************************

XNAP-PROTOCOL-IES-PAIR ::= CLASS {
	&id						ProtocolIE-ID 		UNIQUE,
	&firstCriticality		Criticality,
	&FirstValue,
	&secondCriticality		Criticality,
	&SecondValue,
	&presence				Presence
}
WITH SYNTAX {
	ID						&id
	FIRST CRITICALITY 		&firstCriticality
	FIRST TYPE				&FirstValue
	SECOND CRITICALITY 		&secondCriticality
	SECOND TYPE				&SecondValue
	PRESENCE				&presence
}

-- **************************************************************
--
-- Class Definition for Protocol Extensions
--
-- **************************************************************

XNAP-PROTOCOL-EXTENSION ::= CLASS {
	&id					ProtocolIE-ID 		UNIQUE,
	&criticality		Criticality,
	&Extension,
	&presence			Presence
}
WITH SYNTAX {
	ID					&id
	CRITICALITY			&criticality
	EXTENSION			&Extension
	PRESENCE			&presence
}

-- **************************************************************
--
-- Class Definition for Private IEs
--
-- **************************************************************

XNAP-PRIVATE-IES ::= CLASS {
	&id					PrivateIE-ID,
	&criticality		Criticality,
	&Value,
	&presence			Presence
}
WITH SYNTAX {
	ID					&id
	CRITICALITY			&criticality
	TYPE				&Value
	PRESENCE			&presence
}

-- **************************************************************
--
-- Container for Protocol IEs
--
-- **************************************************************

ProtocolIE-Container {XNAP-PROTOCOL-IES : IEsSetParam} ::=
	SEQUENCE (SIZE (0..maxProtocolIEs)) OF
	ProtocolIE-Field {{IEsSetParam}}

ProtocolIE-Single-Container {XNAP-PROTOCOL-IES : IEsSetParam} ::= 	ProtocolIE-Field {{IEsSetParam}}

ProtocolIE-Field {XNAP-PROTOCOL-IES : IEsSetParam} ::= SEQUENCE {
	id				XNAP-PROTOCOL-IES.&id					({IEsSetParam}),
	criticality		XNAP-PROTOCOL-IES.&criticality			({IEsSetParam}{@id}),
	value			XNAP-PROTOCOL-IES.&Value				({IEsSetParam}{@id})
}

-- **************************************************************
--
-- Container for Protocol IE Pairs
--
-- **************************************************************

ProtocolIE-ContainerPair {XNAP-PROTOCOL-IES-PAIR : IEsSetParam} ::=
	SEQUENCE (SIZE (0..maxProtocolIEs)) OF
	ProtocolIE-FieldPair {{IEsSetParam}}

ProtocolIE-FieldPair {XNAP-PROTOCOL-IES-PAIR : IEsSetParam} ::= SEQUENCE {
	id					XNAP-PROTOCOL-IES-PAIR.&id					({IEsSetParam}),
	firstCriticality	XNAP-PROTOCOL-IES-PAIR.&firstCriticality	({IEsSetParam}{@id}),
	firstValue			XNAP-PROTOCOL-IES-PAIR.&FirstValue			({IEsSetParam}{@id}),
	secondCriticality	XNAP-PROTOCOL-IES-PAIR.&secondCriticality	({IEsSetParam}{@id}),
	secondValue			XNAP-PROTOCOL-IES-PAIR.&SecondValue			({IEsSetParam}{@id})
}

-- **************************************************************
--
-- Container Lists for Protocol IE Containers
--
-- **************************************************************

ProtocolIE-ContainerList {INTEGER : lowerBound, INTEGER : upperBound, XNAP-PROTOCOL-IES : IEsSetParam} ::=
	SEQUENCE (SIZE (lowerBound..upperBound)) OF
	ProtocolIE-Container {{IEsSetParam}}

ProtocolIE-ContainerPairList {INTEGER : lowerBound, INTEGER : upperBound, XNAP-PROTOCOL-IES-PAIR : IEsSetParam} ::=
	SEQUENCE (SIZE (lowerBound..upperBound)) OF
	ProtocolIE-ContainerPair {{IEsSetParam}}

-- **************************************************************
--
-- Container for Protocol Extensions
--
-- **************************************************************

ProtocolExtensionContainer {XNAP-PROTOCOL-EXTENSION : ExtensionSetParam} ::=	SEQUENCE (SIZE (1..maxProtocolExtensions)) OF
	ProtocolExtensionField {{ExtensionSetParam}}

ProtocolExtensionField {XNAP-PROTOCOL-EXTENSION : ExtensionSetParam} ::= SEQUENCE {
	id					XNAP-PROTOCOL-EXTENSION.&id				({ExtensionSetParam}),
	criticality			XNAP-PROTOCOL-EXTENSION.&criticality	({ExtensionSetParam}{@id}),
	extensionValue		XNAP-PROTOCOL-EXTENSION.&Extension		({ExtensionSetParam}{@id})
}

-- **************************************************************
--
-- Container for Private IEs
--
-- **************************************************************

PrivateIE-Container {XNAP-PRIVATE-IES : IEsSetParam} ::=
	SEQUENCE (SIZE (1..maxPrivateIEs)) OF
	PrivateIE-Field {{IEsSetParam}}

PrivateIE-Field {XNAP-PRIVATE-IES : IEsSetParam} ::= SEQUENCE {
	id				XNAP-PRIVATE-IES.&id			({IEsSetParam}),
	criticality		XNAP-PRIVATE-IES.&criticality	({IEsSetParam}{@id}),
	value			XNAP-PRIVATE-IES.&Value			({IEsSetParam}{@id})
}

END
-- ASN1STOP
