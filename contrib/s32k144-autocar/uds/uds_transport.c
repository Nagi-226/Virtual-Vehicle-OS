/**
 * @file uds_transport.c
 * @brief UDS Transport Layer - Implementation
 * @version v0.5.0
 * @target S32K144 (ARM Cortex-M4F)
 *
 * MISRA C:2012 compliant (ASIL B mandatory rules)
 */

#include "uds_transport.h"

/* =========================================================================
 * Private: Service Handler Declarations
 * ========================================================================= */

static void UDS_HandleSessionCtrl(uds_instance_t *uds,
                                   const uds_request_t *req,
                                   uds_response_t *resp);
static void UDS_HandleEcuReset(uds_instance_t *uds,
                                const uds_request_t *req,
                                uds_response_t *resp);
static void UDS_HandleReadDataById(uds_instance_t *uds,
                                    const uds_request_t *req,
                                    uds_response_t *resp);
static void UDS_HandleSecurityAccess(uds_instance_t *uds,
                                      const uds_request_t *req,
                                      uds_response_t *resp);
static void UDS_HandleTesterPresent(uds_instance_t *uds,
                                     const uds_request_t *req,
                                     uds_response_t *resp);

/* =========================================================================
 * Private: Negative Response Builder
 * ========================================================================= */

static void UDS_SendNegative(uds_response_t *resp, uint8_t reqSid, uint8_t nrc)
{
    resp->sid    = UDS_SID_NEGATIVE_RESP;
    resp->data[0] = reqSid;
    resp->data[1] = nrc;
    resp->length  = 3U;
}

static void UDS_SendPositive(uds_response_t *resp, uint8_t reqSid,
                              const uint8_t *data, uint8_t dataLen)
{
    uint8_t i;

    resp->sid = reqSid | UDS_SID_POSITIVE_RESP_MASK;
    resp->length = (uint8_t)(1U + dataLen);
    if (resp->length > 8U) {
        resp->length = 8U;
    }

    for (i = 1U; i < resp->length; i++) {
        resp->data[i - 1U] = data[i - 1U];
    }
}

/* =========================================================================
 * Private: Security Access — Simple Seed/Key
 * ========================================================================= */

static uint8_t g_securitySeed[4] = { 0x00U, 0x00U, 0x00U, 0x00U };

/**
 * @brief Generate a simple 32-bit seed (NOT cryptographically secure)
 *
 * Production implementation MUST use a hardware-secured algorithm
 * (e.g., AES-128 via CSEc on S32K144).
 */
static void UDS_GenerateSeed(uint8_t *seedOut)
{
    /* Simple LFSR-based seed generator */
    static uint32_t lfsr = 0xDEADBEEFU;

    lfsr = (lfsr >> 1U) ^ ((lfsr & 1U) != 0U ? 0xA3000000U : 0U);
    lfsr ^= (uint32_t)g_securitySeed[0] << 24U;

    seedOut[0] = (uint8_t)(lfsr >> 24U);
    seedOut[1] = (uint8_t)(lfsr >> 16U);
    seedOut[2] = (uint8_t)(lfsr >> 8U);
    seedOut[3] = (uint8_t)(lfsr);

    g_securitySeed[0] = seedOut[0];
    g_securitySeed[1] = seedOut[1];
    g_securitySeed[2] = seedOut[2];
    g_securitySeed[3] = seedOut[3];
}

/**
 * @brief Compute expected key from seed (simple bit inversion)
 *
 * WARNING: This is a placeholder. Production must use
 * CSEc-based authentication (AES-128 CMAC).
 */
static uint32_t UDS_ComputeKey(uint32_t seed)
{
    /* Simple key: ~seed XOR 0xC5A5F1E0 */
    return (~seed) ^ 0xC5A5F1E0U;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void UDS_Init(uds_instance_t *uds)
{
    uds->session            = UDS_SESSION_DEFAULT;
    uds->securityLevel      = UDS_SEC_LEVEL_LOCKED;
    uds->lastTesterPresentMs = 0U;
    uds->sessionTimeoutMs   = UDS_S3_TIMEOUT_MS;
    uds->securityAttempts   = 0U;
    uds->initialized        = true;
}

bool UDS_ProcessRequest(uds_instance_t *uds,
                         const uds_request_t *req,
                         uds_response_t *resp)
{
    bool sendResponse = true;

    if ((uds == NULL) || (req == NULL) || (resp == NULL) || (!uds->initialized)) {
        return false;
    }

    /* Validate minimum length (SID + at least one more byte) */
    if (req->length < 2U) {
        UDS_SendNegative(resp, req->sid, UDS_NRC_INCORRECT_LENGTH);
        return true;
    }

    /* Session & security access control per ISO 14229-1 */
    switch (req->sid) {

    case UDS_SID_ECU_RESET:
        /* ECUReset requires non-Default session (ISO 14229-1 Table 39) */
        if (uds->session == UDS_SESSION_DEFAULT) {
            UDS_SendNegative(resp, req->sid, UDS_NRC_CONDITIONS_NOT_CORR);
            sendResponse = true;
            return sendResponse;
        }
        break;

    case UDS_SID_READ_DATA_BY_ID:
        /* Security-sensitive DIDs require unlocked security level */
        {
            uint16_t did = (uint16_t)((uint16_t)req->subFunc << 8U) |
                           (uint16_t)req->data[0];
            if ((did == UDS_DID_FINGERPRINT) &&
                (uds->securityLevel == UDS_SEC_LEVEL_LOCKED)) {
                UDS_SendNegative(resp, req->sid, UDS_NRC_SECURITY_ACCESS_DEN);
                sendResponse = true;
                return sendResponse;
            }
        }
        break;

    case UDS_SID_DIAG_SESSION_CTRL:
    case UDS_SID_SECURITY_ACCESS:
    case UDS_SID_TESTER_PRESENT:
        /* These services are always permitted */
        break;

    default:
        UDS_SendNegative(resp, req->sid, UDS_NRC_SERVICE_NOT_SUPP);
        sendResponse = true;
        return sendResponse;
    }

    /* Route to service handler */
    switch (req->sid) {

    case UDS_SID_DIAG_SESSION_CTRL:
        UDS_HandleSessionCtrl(uds, req, resp);
        break;

    case UDS_SID_ECU_RESET:
        UDS_HandleEcuReset(uds, req, resp);
        break;

    case UDS_SID_READ_DATA_BY_ID:
        UDS_HandleReadDataById(uds, req, resp);
        break;

    case UDS_SID_SECURITY_ACCESS:
        UDS_HandleSecurityAccess(uds, req, resp);
        break;

    case UDS_SID_TESTER_PRESENT:
        UDS_HandleTesterPresent(uds, req, resp);
        break;

    default:
        /* Already handled above; defensive fallback */
        UDS_SendNegative(resp, req->sid, UDS_NRC_SERVICE_NOT_SUPP);
        break;
    }

    /* TesterPresent keeps session alive */
    if (req->sid == UDS_SID_TESTER_PRESENT) {
        uds->lastTesterPresentMs = 0U;  /* Reset by caller with current time */
    }

    return sendResponse;
}

void UDS_CheckTimeout(uds_instance_t *uds, uint32_t currentTimeMs)
{
    if ((uds->session != UDS_SESSION_DEFAULT) &&
        ((currentTimeMs - uds->lastTesterPresentMs) > uds->sessionTimeoutMs)) {
        /* S3 timeout — return to default session */
        uds->session       = UDS_SESSION_DEFAULT;
        uds->securityLevel = UDS_SEC_LEVEL_LOCKED;
    }
}

void UDS_DeInit(uds_instance_t *uds)
{
    uds->initialized = false;
}

/* =========================================================================
 * Service Handlers
 * ========================================================================= */

static void UDS_HandleSessionCtrl(uds_instance_t *uds,
                                   const uds_request_t *req,
                                   uds_response_t *resp)
{
    uint8_t sessionType = req->subFunc;

    switch (sessionType) {
    case UDS_SESSION_DEFAULT:
    case UDS_SESSION_PROGRAMMING:
    case UDS_SESSION_EXTENDED:
        uds->session = (uds_session_t)sessionType;
        uds->securityLevel = UDS_SEC_LEVEL_LOCKED;
        uds->securityAttempts = 0U;
        {
            uint8_t respData[4] = { sessionType, 0x00, 0x00, (uint8_t)(UDS_P2_TIMEOUT_MS) };
            UDS_SendPositive(resp, req->sid, respData, 4U);
        }
        break;

    default:
        UDS_SendNegative(resp, req->sid, UDS_NRC_SUBFUNC_NOT_SUPP);
        break;
    }
}

static void UDS_HandleEcuReset(uds_instance_t *uds,
                                const uds_request_t *req,
                                uds_response_t *resp)
{
    uint8_t resetType = req->subFunc;

    switch (resetType) {
    case 0x01U:  /* Hard reset */
    case 0x02U:  /* Key-off on reset */
    case 0x03U:  /* Soft reset */
        /* Send positive response before reset */
        {
            uint8_t respData[1] = { resetType };
            UDS_SendPositive(resp, req->sid, respData, 1U);
        }
        /* Reset is triggered by the caller after response is sent */
        break;

    default:
        UDS_SendNegative(resp, req->sid, UDS_NRC_SUBFUNC_NOT_SUPP);
        break;
    }
}

static void UDS_HandleReadDataById(uds_instance_t *uds,
                                    const uds_request_t *req,
                                    uds_response_t *resp)
{
    uint16_t did = (uint16_t)((uint16_t)req->subFunc << 8U) | (uint16_t)req->data[0];
    uint8_t respData[6];
    uint8_t respLen = 0U;

    /* DID is in subFunc (MSB) + data[0] (LSB) */
    respData[0] = req->subFunc;
    respData[1] = req->data[0];
    respLen = 2U;

    switch (did) {
    case UDS_DID_SW_VERSION:
        /* Return project version (v0.5.0 = 0x00 0x05 0x00) */
        respData[2] = 0x00U;
        respData[3] = 0x05U;
        respData[4] = 0x00U;
        respLen = 5U;
        break;

    case UDS_DID_MOTOR_SPEED:
        /* Stub: 0 RPM */
        respData[2] = 0x00U;
        respData[3] = 0x00U;
        respLen = 4U;
        break;

    case UDS_DID_FOC_STATE:
        /* Stub: STOPPED */
        respData[2] = 0x00U;
        respLen = 3U;
        break;

    case UDS_DID_FAULT_CODE:
        /* Stub: no fault */
        respData[2] = 0x00U;
        respData[3] = 0x00U;
        respData[4] = 0x00U;
        respData[5] = 0x00U;
        respLen = 6U;
        break;

    case UDS_DID_DC_BUS_VOLTAGE:
        /* Stub: 48V */
        respData[2] = 0x00U;
        respData[3] = 0x30U;
        respLen = 4U;
        break;

    default:
        UDS_SendNegative(resp, req->sid, UDS_NRC_REQ_OUT_OF_RANGE);
        return;
    }

    UDS_SendPositive(resp, req->sid, respData, respLen);
}

static void UDS_HandleSecurityAccess(uds_instance_t *uds,
                                      const uds_request_t *req,
                                      uds_response_t *resp)
{
    uint8_t accessType = req->subFunc;

    if (uds->securityLevel != UDS_SEC_LEVEL_LOCKED) {
        /* Already unlocked — requested level <= current level */
        {
            uint8_t respData[1] = { accessType };
            UDS_SendPositive(resp, req->sid, respData, 1U);
        }
        return;
    }

    if ((accessType & 0x01U) != 0U) {
        /* Odd sub-function: Request Seed */
        uint8_t seed[4];
        uint8_t respData[5];

        if (uds->securityAttempts >= 3U) {
            UDS_SendNegative(resp, req->sid, UDS_NRC_EXCEED_NUM_ATTEMPTS);
            return;
        }

        UDS_GenerateSeed(seed);

        respData[0] = accessType;
        respData[1] = seed[0];
        respData[2] = seed[1];
        respData[3] = seed[2];
        respData[4] = seed[3];
        UDS_SendPositive(resp, req->sid, respData, 5U);
    } else {
        /* Even sub-function: Send Key */
        uint32_t receivedKey;
        uint32_t seed;
        uint32_t expectedKey;

        if (req->length < 6U) {
            UDS_SendNegative(resp, req->sid, UDS_NRC_INCORRECT_LENGTH);
            return;
        }

        seed = ((uint32_t)g_securitySeed[0] << 24U) |
               ((uint32_t)g_securitySeed[1] << 16U) |
               ((uint32_t)g_securitySeed[2] << 8U)  |
               ((uint32_t)g_securitySeed[3]);

        receivedKey = ((uint32_t)req->data[0] << 24U) |
                      ((uint32_t)req->data[1] << 16U) |
                      ((uint32_t)req->data[2] << 8U)  |
                      ((uint32_t)req->data[3]);

        expectedKey = UDS_ComputeKey(seed);

        if (receivedKey == expectedKey) {
            uds->securityLevel = (uds_security_level_t)(accessType >> 1U);
            uds->securityAttempts = 0U;
            {
                uint8_t respData[1] = { accessType };
                UDS_SendPositive(resp, req->sid, respData, 1U);
            }
        } else {
            uds->securityAttempts++;
            UDS_SendNegative(resp, req->sid, UDS_NRC_INVALID_KEY);
        }
    }
}

static void UDS_HandleTesterPresent(uds_instance_t *uds,
                                     const uds_request_t *req,
                                     uds_response_t *resp)
{
    uint8_t respData[1];

    /* Sub-function 0x00 = no response required, 0x01-0x7F = response */
    if (req->subFunc == 0x00U) {
        resp->length = 0U;  /* Suppress response */
        return;
    }

    respData[0] = req->subFunc;
    UDS_SendPositive(resp, req->sid, respData, 1U);
}
