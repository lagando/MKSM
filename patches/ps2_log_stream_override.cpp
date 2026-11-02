// Replaces the weak ps2_log::log_stream() emitted by every generated function.
//
// The function tracker writes and flushes on each guest call, which dominates
// run time. Rebuilding 13,651 files to compile it out takes hours, so this
// strong definition sends it to a null stream unless PS2X_TRACE=1 is set.
// It must not include ps2_log.h, which defines the same function inline.
#include <cstdlib>
#include <fstream>
#include <ostream>

namespace ps2_log
{
    std::ostream &log_stream()
    {
        static std::ostream nullStream(nullptr);
        static std::ofstream traceFile;
        static const bool traceEnabled = []
        {
            const char *trace = std::getenv("PS2X_TRACE");
            if (trace == nullptr || trace[0] != '1')
            {
                return false;
            }
            traceFile.open("ps2_log.txt", std::ios::out);
            return traceFile.is_open();
        }();
        return traceEnabled ? static_cast<std::ostream &>(traceFile) : nullStream;
    }
}
