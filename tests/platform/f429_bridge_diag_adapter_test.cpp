#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#include "interconnect/diagnostics_manager.hpp"
#include "platform/stm32f429/f429_bridge_diag_adapter.hpp"

namespace {

bool ExpectTrue(const bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAILED] " << msg << std::endl;
        return false;
    }
    return true;
}

bool TestF429BridgeDiagEventFields() {
    const std::string path = "build/f429_diag_adapter_test.jsonl";
    std::remove(path.c_str());

    vr::interconnect::DiagnosticsManager diagnostics;
    vr::platform::F429SramDiagEvent event;
    event.code = 2U;
    event.audit_summary = "fmc_sram_audit:{status=fail,reason=readback_mismatch,code=2}";

    const bool ok = vr::platform::F429BridgeDiagAdapter::EmitSramBringupEvent(
        diagnostics, path, 10U, event);
    if (!ExpectTrue(ok, "emit should return true")) {
        return false;
    }

    std::ifstream in(path);
    if (!ExpectTrue(in.is_open(), "snapshot file should be created")) {
        return false;
    }

    std::string line;
    if (!ExpectTrue(static_cast<bool>(std::getline(in, line)), "snapshot line should exist")) {
        return false;
    }

    return ExpectTrue(line.find("\"event\":\"platform_fmc_sram_bringup\"") != std::string::npos,
                      "event name missing") &&
           ExpectTrue(line.find("\"platform\":\"stm32f429zgt6\"") != std::string::npos,
                      "platform field missing") &&
           ExpectTrue(line.find("\"component\":\"fmc_sram\"") != std::string::npos,
                      "component field missing") &&
           ExpectTrue(line.find("\"diag_code\":\"2\"") != std::string::npos,
                      "diag_code field missing") &&
           ExpectTrue(line.find("\"audit\":\"fmc_sram_audit:{status=fail,reason=readback_mismatch,code=2}\"") != std::string::npos,
                      "audit field missing");
}

}  // namespace

int main() {
    if (!TestF429BridgeDiagEventFields()) {
        std::cerr << "f429 bridge diag adapter test failed." << std::endl;
        return 1;
    }

    std::cout << "f429 bridge diag adapter test passed." << std::endl;
    return 0;
}
