// contrib/s32k144-autocar 的最小冒烟验证：
//   ① 上游 `uds_transport.c` 能链接进可执行文件、真的处理一帧请求（不只是编过）；
//   ② VVOS 新增的 ISO 14229-1 兼容层把 TesterPresent 的抑制位极性纠正过来。
//
// 不是覆盖率工程，也不替代上游的 MISRA 测试套件。
//
// 背景（详见 contrib/s32k144-autocar/compat/uds_compat.h）：
//   上游把 subFunc==0x00 当"抑制响应"，而标准（ISO 14229-1）的抑制位是 bit7(0x80)。
//   所以这里**分别**验两个入口：上游原样行为、以及兼容层的标准行为。
#include <cstdint>
#include <cstdio>

extern "C" {
#include "uds_compat.h"
#include "uds_transport.h"
}

namespace {

int g_failures = 0;

bool ExpectTrue(const bool condition, const char* const what) {
    if (condition) {
        std::printf("  [OK    ] %s\n", what);
        return true;
    }
    ++g_failures;
    std::printf("  [FAILED] %s\n", what);
    return false;
}

constexpr auto kPositiveTesterPresent =
    static_cast<std::uint8_t>(UDS_SID_TESTER_PRESENT | UDS_SID_POSITIVE_RESP_MASK);  // 0x7E

uds_request_t MakeTesterPresent(const std::uint8_t sub_func) {
    uds_request_t req{};
    req.sid = static_cast<std::uint8_t>(UDS_SID_TESTER_PRESENT);  // 0x3E
    req.subFunc = sub_func;
    req.length = 2U;  // SID + subFunc
    return req;
}

}  // namespace

int main() {
    uds_instance_t uds{};
    UDS_Init(&uds);
    if (!ExpectTrue(uds.initialized, "UDS_Init 后 instance 处于 initialized 状态")) {
        return 1;
    }

    // ---------------------------------------------------------------------
    // ① 上游原样入口：subFunc = 0x01 → 正响应 0x7E（证明资产可链接、可运行）
    // ---------------------------------------------------------------------
    std::printf("-- 上游 UDS_ProcessRequest（原样行为）--\n");
    {
        const uds_request_t req = MakeTesterPresent(0x01U);
        uds_response_t resp{};
        const bool should_send = UDS_ProcessRequest(&uds, &req, &resp);
        std::printf("  [info  ] resp.sid=0x%02X length=%u data[0]=0x%02X\n",
                    static_cast<unsigned>(resp.sid), static_cast<unsigned>(resp.length),
                    static_cast<unsigned>(resp.data[0]));
        ExpectTrue(should_send, "0x3E/0x01 应当产生响应");
        ExpectTrue(resp.sid == kPositiveTesterPresent, "正响应 SID 应为 0x7E");
        ExpectTrue(resp.length == 2U, "响应长度应为 2（SID + subFunc 回显）");
        ExpectTrue(resp.data[0] == 0x01U, "响应 data[0] 应回显 subFunc=0x01");
    }

    // ---------------------------------------------------------------------
    // ② 兼容层：3E 00（标准里"要响应"）→ 必须回 7E 00
    //    上游这里会抑制响应，这正是兼容层要修的那处。
    // ---------------------------------------------------------------------
    std::printf("-- 兼容层 UDS_ProcessRequestCompat：3E 00 应回响应 --\n");
    {
        const uds_request_t req = MakeTesterPresent(0x00U);
        uds_response_t resp{};
        const bool should_send = UDS_ProcessRequestCompat(&uds, &req, &resp);
        std::printf("  [info  ] compat(0x00) send=%d resp.sid=0x%02X length=%u data[0]=0x%02X\n",
                    should_send ? 1 : 0, static_cast<unsigned>(resp.sid),
                    static_cast<unsigned>(resp.length), static_cast<unsigned>(resp.data[0]));
        ExpectTrue(should_send, "compat: 3E 00 应当产生响应（ISO 14229-1）");
        ExpectTrue(resp.sid == kPositiveTesterPresent, "compat: 正响应 SID 应为 0x7E");
        ExpectTrue(resp.length >= 1U, "compat: 响应长度应 >= 1");
        ExpectTrue(resp.data[0] == 0x00U, "compat: data[0] 应回显原始 subFunc=0x00");
    }

    // ---------------------------------------------------------------------
    // ③ 兼容层：3E 80（标准里"抑制响应"）→ 不应发送
    // ---------------------------------------------------------------------
    std::printf("-- 兼容层 UDS_ProcessRequestCompat：3E 80 应抑制响应 --\n");
    {
        const uds_request_t req = MakeTesterPresent(0x80U);
        uds_response_t resp{};
        const bool should_send = UDS_ProcessRequestCompat(&uds, &req, &resp);
        std::printf("  [info  ] compat(0x80) send=%d resp.length=%u\n", should_send ? 1 : 0,
                    static_cast<unsigned>(resp.length));
        ExpectTrue(!should_send || (resp.length == 0U),
                   "compat: 3E 80 不应发出响应（send=false 或 length==0）");
    }

    // ---------------------------------------------------------------------
    // ④ 兼容层对非 0x3E 服务必须原样透传（证明它只介入 TesterPresent）
    // ---------------------------------------------------------------------
    std::printf("-- 兼容层透传：非 0x3E 服务与上游结果一致 --\n");
    {
        uds_request_t req{};
        req.sid = 0x22U;  // ReadDataByIdentifier
        req.subFunc = 0xF1U;
        req.data[0] = 0xA0U;  // DID 0xF1A0 = SW version
        req.length = 3U;

        uds_response_t upstream{};
        uds_response_t compat{};
        const bool up_send = UDS_ProcessRequest(&uds, &req, &upstream);
        const bool co_send = UDS_ProcessRequestCompat(&uds, &req, &compat);
        std::printf("  [info  ] upstream send=%d sid=0x%02X len=%u | compat send=%d sid=0x%02X len=%u\n",
                    up_send ? 1 : 0, static_cast<unsigned>(upstream.sid),
                    static_cast<unsigned>(upstream.length), co_send ? 1 : 0,
                    static_cast<unsigned>(compat.sid), static_cast<unsigned>(compat.length));
        ExpectTrue(up_send == co_send && upstream.sid == compat.sid &&
                       upstream.length == compat.length,
                   "compat: 非 0x3E 服务应与上游完全一致（透传）");
    }

    UDS_DeInit(&uds);
    if (g_failures != 0) {
        std::printf("uds smoke test failed (%d assertions).\n", g_failures);
        return 1;
    }
    std::printf("uds smoke test passed.\n");
    return 0;
}
