// contrib/s32k144-autocar 的最小冒烟验证：证明 uds_transport.c 不只是"能编过"，
// 而是被链接进可执行文件后能真的处理一帧请求。
//
// 只做一件事：构造 0x3E TesterPresent 请求 → 走 UDS_ProcessRequest → 校验正响应 0x7E。
// 不是覆盖率工程，也不替代上游的 MISRA 测试套件。
//
// 注意 subFunc = 0x01：该实现对 subFunc == 0x00 会**抑制响应**（见 README「已知语义偏差」，
// 与 ISO 14229-1 的 bit7 抑制位不同）。这里用 0x01 走正响应路径。
#include <cstdint>
#include <cstdio>

extern "C" {
#include "uds_transport.h"
}

namespace {

bool ExpectTrue(const bool condition, const char* const what) {
    if (!condition) {
        std::printf("  [FAILED] %s\n", what);
        return false;
    }
    std::printf("  [OK    ] %s\n", what);
    return true;
}

}  // namespace

int main() {
    uds_instance_t uds{};
    UDS_Init(&uds);
    if (!ExpectTrue(uds.initialized, "UDS_Init 后 instance 处于 initialized 状态")) {
        return 1;
    }

    uds_request_t req{};
    req.sid = static_cast<std::uint8_t>(UDS_SID_TESTER_PRESENT);  // 0x3E
    req.subFunc = 0x01U;                                          // 见文件头注释
    req.length = 2U;                                              // SID + subFunc

    uds_response_t resp{};
    const bool should_send = UDS_ProcessRequest(&uds, &req, &resp);

    std::printf("  [info  ] resp.sid=0x%02X length=%u data[0]=0x%02X\n",
                static_cast<unsigned>(resp.sid), static_cast<unsigned>(resp.length),
                static_cast<unsigned>(resp.data[0]));

    const auto expected_sid =
        static_cast<std::uint8_t>(UDS_SID_TESTER_PRESENT | UDS_SID_POSITIVE_RESP_MASK);  // 0x7E

    const bool ok = ExpectTrue(should_send, "0x3E 请求应当产生响应") &&
                    ExpectTrue(resp.sid == expected_sid, "正响应 SID 应为 0x7E") &&
                    ExpectTrue(resp.length == 2U, "响应长度应为 2（SID + subFunc 回显）") &&
                    ExpectTrue(resp.data[0] == 0x01U, "响应 data[0] 应回显 subFunc=0x01");

    UDS_DeInit(&uds);
    if (!ok) {
        std::printf("uds smoke test failed.\n");
        return 1;
    }
    std::printf("uds smoke test passed.\n");
    return 0;
}
