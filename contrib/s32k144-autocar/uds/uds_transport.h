/**
 * @file uds_transport.h
 * @brief UDS (ISO 14229) Diagnostic Transport Layer over CAN
 * @version v0.5.0
 * @target S32K144 (ARM Cortex-M4F)
 *
 * Implements ISO 14229-1 Unified Diagnostic Services and
 * ISO 15765-2 CAN transport protocol.
 *
 * Supported services (v0.5.0):
 * - 0x10: DiagnosticSessionControl
 * - 0x11: ECUReset
 * - 0x22: ReadDataByIdentifier
 * - 0x27: SecurityAccess
 * - 0x3E: TesterPresent
 *
 * Planned (v0.5.3):
 * - 0x2E: WriteDataByIdentifier
 * - 0x31: RoutineControl
 * - 0x34/0x36/0x37: RequestDownload/TransferData/RequestTransferExit (OTA)
 *
 * MISRA C:2012 compliant (ASIL B mandatory rules)
 */

#ifndef UDS_TRANSPORT_H
#define UDS_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * Constants
 * ========================================================================= */

#define UDS_CAN_FRAME_SIZE          (8U)      /* Classic CAN payload */
#define UDS_CAN_FD_FRAME_SIZE       (64U)     /* CAN-FD payload */

#define UDS_PHYSICAL_REQ_ID         (0x7E0U)  /* Tester → ECU */
#define UDS_PHYSICAL_RESP_ID        (0x7E8U)  /* ECU → Tester */
#define UDS_FUNCTIONAL_REQ_ID       (0x7DFU)  /* Broadcast */

/* Timing */
#define UDS_P2_TIMEOUT_MS           (50U)     /* P2CAN server response time */
#define UDS_P2_STAR_TIMEOUT_MS      (5000U)   /* P2*CAN extended response */
#define UDS_S3_TIMEOUT_MS           (2000U)   /* Session timeout (no TesterPresent) */

/* =========================================================================
 * UDS Service IDs (ISO 14229-1 Table A.1)
 * ========================================================================= */

#define UDS_SID_DIAG_SESSION_CTRL   (0x10U)
#define UDS_SID_ECU_RESET           (0x11U)
#define UDS_SID_READ_DATA_BY_ID     (0x22U)
#define UDS_SID_SECURITY_ACCESS     (0x27U)
#define UDS_SID_TESTER_PRESENT      (0x3EU)

/* Response SID = Request SID + 0x40 */
#define UDS_SID_POSITIVE_RESP_MASK  (0x40U)
#define UDS_SID_NEGATIVE_RESP       (0x7FU)

/* =========================================================================
 * Negative Response Codes (ISO 14229-1 Table A.2)
 * ========================================================================= */

#define UDS_NRC_GENERAL_REJECT      (0x10U)
#define UDS_NRC_SERVICE_NOT_SUPP    (0x11U)
#define UDS_NRC_SUBFUNC_NOT_SUPP    (0x12U)
#define UDS_NRC_INCORRECT_LENGTH    (0x13U)
#define UDS_NRC_CONDITIONS_NOT_CORR (0x22U)
#define UDS_NRC_REQ_SEQUENCE_ERR    (0x24U)
#define UDS_NRC_REQ_OUT_OF_RANGE    (0x31U)
#define UDS_NRC_SECURITY_ACCESS_DEN (0x33U)
#define UDS_NRC_INVALID_KEY         (0x35U)
#define UDS_NRC_EXCEED_NUM_ATTEMPTS (0x36U)
#define UDS_NRC_REQ_TIME_NOT_EXP    (0x37U)
#define UDS_NRC_UPLOAD_DOWNLOAD_NOT (0x70U)

/* =========================================================================
 * Diagnostic Sessions
 * ========================================================================= */

typedef enum {
    UDS_SESSION_DEFAULT         = 0x01U,
    UDS_SESSION_PROGRAMMING     = 0x02U,
    UDS_SESSION_EXTENDED        = 0x03U,
    UDS_SESSION_SAFETY_SYSTEM   = 0x04U
} uds_session_t;

/* =========================================================================
 * Security Access Levels
 * ========================================================================= */

typedef enum {
    UDS_SEC_LEVEL_LOCKED        = 0x00U,
    UDS_SEC_LEVEL_LEVEL1        = 0x01U,   /* Calibration access */
    UDS_SEC_LEVEL_LEVEL3        = 0x03U,   /* Programming access */
    UDS_SEC_LEVEL_LEVEL5        = 0x05U    /* Vendor-specific */
} uds_security_level_t;

/* =========================================================================
 * Data Identifiers (DID) — Project-Specific
 * ========================================================================= */

#define UDS_DID_VIN                   (0xF190U)  /* Vehicle Identification Number */
#define UDS_DID_SW_VERSION            (0xF1A0U)  /* Software version */
#define UDS_DID_BL_VERSION            (0xF1A1U)  /* Bootloader version */
#define UDS_DID_FINGERPRINT           (0xF1A2U)  /* Software fingerprint (hash) */
#define UDS_DID_MOTOR_SPEED           (0x4000U)  /* Motor speed (RPM) */
#define UDS_DID_MOTOR_CURRENT         (0x4001U)  /* Motor phase current (A) */
#define UDS_DID_DC_BUS_VOLTAGE        (0x4002U)  /* DC bus voltage (V) */
#define UDS_DID_MOTOR_TEMP            (0x4003U)  /* Motor temperature (°C) */
#define UDS_DID_FOC_STATE             (0x4004U)  /* FOC state machine */
#define UDS_DID_FAULT_CODE            (0x4005U)  /* Active fault code */
#define UDS_DID_CAN_BUS_STATS         (0x5000U)  /* CAN bus load / error counts */

/* =========================================================================
 * UDS Message Structure
 * ========================================================================= */

typedef struct {
    uint8_t  sid;             /* Service ID */
    uint8_t  subFunc;         /* Sub-function (or first data byte) */
    uint8_t  data[6];         /* Remaining payload */
    uint8_t  length;          /* Total payload length (2-8) */
} uds_request_t;

typedef struct {
    uint8_t  sid;             /* Response SID (positive = reqSID+0x40, negative = 0x7F) */
    uint8_t  data[7];         /* Response payload */
    uint8_t  length;          /* Total response length (1-8) */
} uds_response_t;

/* =========================================================================
 * Transport Layer Instance
 * ========================================================================= */

typedef struct {
    uds_session_t       session;
    uds_security_level_t securityLevel;
    uint32_t            lastTesterPresentMs;
    uint32_t            sessionTimeoutMs;
    uint8_t             securityAttempts;
    bool                initialized;
} uds_instance_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief Initialize UDS transport layer
 * @param uds Pointer to UDS instance
 */
void UDS_Init(uds_instance_t *uds);

/**
 * @brief Process an incoming UDS request
 * @param uds Pointer to UDS instance
 * @param req Pointer to request data
 * @param resp Pointer to response output
 * @return true if a response should be sent
 *
 * Routes the request to the appropriate service handler.
 * Handles session and security validation.
 */
bool UDS_ProcessRequest(uds_instance_t *uds,
                         const uds_request_t *req,
                         uds_response_t *resp);

/**
 * @brief Check session timeout (call periodically, e.g., every 100ms)
 * @param uds Pointer to UDS instance
 * @param currentTimeMs Current system time in ms
 *
 * If S3 timeout expires, transitions back to Default session.
 */
void UDS_CheckTimeout(uds_instance_t *uds, uint32_t currentTimeMs);

/**
 * @brief De-initialize UDS
 * @param uds Pointer to UDS instance
 */
void UDS_DeInit(uds_instance_t *uds);

#endif /* UDS_TRANSPORT_H */
