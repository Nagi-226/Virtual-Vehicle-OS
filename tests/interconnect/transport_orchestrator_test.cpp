#include <iostream>

#include "interconnect/transport_orchestrator.hpp"

namespace {

bool ExpectTrue(const bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAILED] " << msg << std::endl;
        return false;
    }
    return true;
}

bool TestCircuitOpenAndRecover() {
    vr::interconnect::TransportOrchestrator orchestrator;
    vr::interconnect::TransportOrchestrator::Policy policy;
    policy.circuit_break_threshold = 2U;
    policy.circuit_recover_threshold = 2U;
    orchestrator.SetPolicy(policy);

    orchestrator.OnPrimarySendFailure();
    if (!ExpectTrue(!orchestrator.IsCircuitOpen(), "circuit should remain closed after first fail")) {
        return false;
    }

    orchestrator.OnPrimarySendFailure();
    if (!ExpectTrue(orchestrator.IsCircuitOpen(), "circuit should open after threshold fails")) {
        return false;
    }

    orchestrator.OnPrimarySendSuccess();
    if (!ExpectTrue(orchestrator.IsCircuitOpen(), "circuit should stay open until recover threshold")) {
        return false;
    }

    orchestrator.OnPrimarySendSuccess();
    return ExpectTrue(!orchestrator.IsCircuitOpen(), "circuit should close after recover threshold");
}

}  // namespace

int main() {
    if (!TestCircuitOpenAndRecover()) {
        std::cerr << "transport orchestrator test failed." << std::endl;
        return 1;
    }

    std::cout << "transport orchestrator test passed." << std::endl;
    return 0;
}
