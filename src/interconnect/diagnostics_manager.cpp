#include "interconnect/diagnostics_manager.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <fstream>
#include <string>

namespace vr {
namespace interconnect {

namespace {

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

}  // namespace

std::string DiagnosticsManager::BuildUnknownCommandResult() const {
    return "diag:{error=unknown_command}";
}

void DiagnosticsManager::RecordSnapshot(const std::string& path,
                                        const std::uint32_t limit,
                                        const std::string& event,
                                        const MessageEnvelope* envelope) const {
    if (path.empty()) {
        return;
    }

    std::deque<std::string> lines;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
    }

    std::string entry = "{\"ts\":" + std::to_string(NowMs()) +
        ",\"event\":\"" + event + "\"";
    if (envelope != nullptr) {
        entry += ",\"topic\":\"" + envelope->topic + "\"";
        entry += ",\"source\":\"" + envelope->source + "\"";
        entry += ",\"trace_id\":\"" + envelope->trace_id + "\"";
        entry += ",\"sequence\":" + std::to_string(envelope->sequence);
    }
    entry += "}";

    lines.push_back(entry);

    const std::size_t max_keep = std::max<std::size_t>(1U, limit);
    while (lines.size() > max_keep) {
        lines.pop_front();
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    for (const auto& item : lines) {
        out << item << "\n";
    }
}

}  // namespace interconnect
}  // namespace vr
