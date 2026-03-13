#ifndef VR_INTERCONNECT_DIAGNOSTICS_REPORTER_HPP
#define VR_INTERCONNECT_DIAGNOSTICS_REPORTER_HPP

#include <string>

namespace vr {
namespace interconnect {

class IDiagnosticsReporter {
public:
    virtual ~IDiagnosticsReporter() = default;
    virtual void Submit(const std::string& payload) = 0;
};

class NullDiagnosticsReporter final : public IDiagnosticsReporter {
public:
    void Submit(const std::string& payload) override {
        (void)payload;
    }
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_DIAGNOSTICS_REPORTER_HPP
