/**
 * @file uds_compat.c
 * @brief ISO 14229-1 语义兼容层实现 —— 详见 uds_compat.h 顶部的完整说明。
 *
 * ⚠️ 本文件由 VVOS 新增，不属于上游资产；`../uds/` 下的文件保持与源仓逐字节相同。
 */

#include "uds_compat.h"

#include <stdint.h>

/* TesterPresent 的 sub-function 抑制位（ISO 14229-1 suppressPosRspMsgIndicationBit） */
#define UDS_COMPAT_SUPPRESS_BIT      (0x80U)
#define UDS_COMPAT_SUBFUNC_MASK      (0x7FU)

/* 上游把 0x00 当作"抑制响应"，0x01..0x7F 才回正响应（见 uds_compat.h 的说明） */
#define UDS_UPSTREAM_SUBFUNC_RESPOND (0x01U)
#define UDS_UPSTREAM_SUBFUNC_SUPPRESS (0x00U)

bool UDS_ProcessRequestCompat(uds_instance_t *uds,
                              const uds_request_t *req,
                              uds_response_t *resp)
{
    /* 参数校验交给上游（它对 NULL / 未初始化都有统一处理），
     * 这里只做"要不要介入"的判断，避免重复一套校验逻辑。 */
    if ((req == NULL) || (req->sid != (uint8_t)UDS_SID_TESTER_PRESENT)) {
        return UDS_ProcessRequest(uds, req, resp);
    }

    const uint8_t rawSubFunc = req->subFunc;
    const bool suppressRequested = ((rawSubFunc & (uint8_t)UDS_COMPAT_SUPPRESS_BIT) != 0U);

    /* 拷贝一份请求再改写 sub-function，绝不修改调用方的结构体 */
    uds_request_t mapped = *req;
    mapped.subFunc = suppressRequested
                         ? (uint8_t)UDS_UPSTREAM_SUBFUNC_SUPPRESS
                         : (uint8_t)UDS_UPSTREAM_SUBFUNC_RESPOND;

    const bool sendResponse = UDS_ProcessRequest(uds, &mapped, resp);

    if (suppressRequested) {
        /* 上游会返回 true 但把 length 置 0（"空响应"）。直接照搬会让调用方发出
         * 零长度帧（协议违规），所以翻译成 false = 不发送。 */
        return false;
    }

    /* 正响应里 data[0] 回显的是"我们改写过的 sub-function"，纠正成调用方原始值
     * （ISO 要求回显请求里的 sub-function 低 7 位）。 */
    if (sendResponse && (resp != NULL) && (resp->length >= 1U)) {
        resp->data[0] = (uint8_t)(rawSubFunc & (uint8_t)UDS_COMPAT_SUBFUNC_MASK);
    }

    return sendResponse;
}
