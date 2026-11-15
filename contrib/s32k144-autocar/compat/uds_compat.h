/**
 * @file uds_compat.h
 * @brief 上游 uds_transport 的 ISO 14229-1 语义兼容层
 *
 * ⚠️ 本文件由 VVOS 新增（与 `../uds/` 下的上游资产不同）：`../uds/` 保持与源仓
 *   Simulink_AutoCar@bedb358 逐字节相同，本文件不进那个目录、也不改动它一个字节。
 *
 * ---------------------------------------------------------------------------
 * 为什么需要这一层
 * ---------------------------------------------------------------------------
 * 上游 `UDS_HandleTesterPresent()`（`../uds/uds_transport.c`，源文件内注释原文
 * "Sub-function 0x00 = no response required"）把 **subFunc == 0x00** 当作"抑制响应"，
 * 只有 0x01..0x7F 才回正响应。
 *
 * 而 ISO 14229-1 的 suppressPosRspMsgIndicationBit 是 sub-function 的 **bit7（0x80）**：
 *   - `3E 00` → **应当回** `7E 00`（诊断仪确认 ECU 在线，这是最常见的用法）
 *   - `3E 80` → 抑制响应
 *
 * 后果很具体：任何按标准发 `3E 00` 的工具（CANoe 诊断面板、第三方诊断仪）都拿不到响应，
 * 现象与"ECU 已死"无法区分。
 *
 * ---------------------------------------------------------------------------
 * 本层做什么（只映射入参，不改上游）
 * ---------------------------------------------------------------------------
 * 仅对 SID == 0x3E 生效；其它 SID 原样透传。
 *
 *   raw subFunc bit7 = 0（如 0x00）→ 映射成上游 0x01（上游会回正响应）
 *                                    → 再把 `resp->data[0]` 改回 raw & 0x7F
 *   raw subFunc bit7 = 1（如 0x80）→ 映射成上游 0x00（上游抑制响应）
 *                                    → 本层返回 false（"无需发送响应"）
 *
 * ⚠️ 上一条是本层对上游返回值的**唯一**改写：上游在这种情况下返回 true 但把
 *   `resp->length` 置 0，直接照搬会让调用方发出一帧零长度响应（协议违规）。
 *   因此本层把"抑制"翻译成 false，语义与 ISO 一致，也避免调用方误发空帧。
 *
 * 两个入口的取舍：
 *   - `UDS_ProcessRequest()`        —— 上游原样行为（0x00 抑制），用于需要"与上游完全一致"的场合
 *   - `UDS_ProcessRequestCompat()` —— 符合 ISO 14229-1，**推荐接入方使用这个**
 */

#ifndef UDS_COMPAT_H
#define UDS_COMPAT_H

#include <stdbool.h>

#include "uds_transport.h" /* 上游资产，位于 ../uds/ */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 按 ISO 14229-1 语义处理 UDS 请求（修正 TesterPresent 的抑制位极性）
 *
 * @param uds  已由 UDS_Init() 初始化的实例
 * @param req  请求；SID != 0x3E 时原样透传给上游
 * @param resp 响应输出
 * @return true 表示"需要发送响应"；false 表示"不发送"（含参数非法、未初始化的
 *         上游语义，以及 raw bit7 置位的 TesterPresent 抑制请求）
 *
 * @note 除 SID == 0x3E 的 sub-function 映射与上面说明的返回值改写外，行为与
 *       UDS_ProcessRequest() 完全一致。
 */
bool UDS_ProcessRequestCompat(uds_instance_t *uds,
                              const uds_request_t *req,
                              uds_response_t *resp);

#ifdef __cplusplus
}
#endif

#endif /* UDS_COMPAT_H */
