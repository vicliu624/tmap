#include "tmap/integer_leg_elevation.h"
#include "tmap/native_file.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace tmap;
class File : public RandomAccessFile
{
  public:
    NativeFile native;
    size_t calls = 0, successful = 0, every = 0;
    uint64_t bytes = 0;
    uint64_t size() const override { return native.size(); }
    Status readAt(uint64_t at, uint8_t* output, size_t count) override
    {
        ++calls;
        if (every && calls % every == 0) return Status::Busy;
        const auto r = native.readAt(at, output, count);
        if (r == Status::Ok)
        {
            ++successful;
            bytes += count;
        }
        return r;
    }
};
bool retry(Result r) { return r == Result::More || r == Result::Busy; }
int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    char* end = nullptr;
    const auto busy = std::strtoul(argv[2], &end, 10);
    if (!*argv[2] || *end || *argv[2] == '-' || busy == 1) return 2;
    IntegerRouteLeg legs[32]{};
    size_t count = 0;
    while (std::scanf("%u %u %u", &legs[count].edge, &legs[count].first_cm, &legs[count].last_cm) == 3)
        if (++count == 32) return 2;
    auto workspace = std::make_unique<PackageWorkspace>();
    auto cursor = std::make_unique<IntegerLegElevationCursor>();
    OutdoorPackage package(*workspace);
    File file;
    file.every = busy;
    if (!file.native.open(argv[1])) return 1;
    Result r;
    size_t calls = 0;
    do
    {
        r = package.open(file);
        if (++calls > 100000) return 1;
    } while (retry(r));
    if (r != Result::Ok) return 1;
    GraphReader graph(package);
    do
    {
        r = beginIntegerLegElevation(graph, legs, count, *cursor);
        if (++calls > 100000) return 1;
    } while (retry(r));
    IntegerRouteElevation totals{UINT64_MAX, UINT64_MAX, UINT64_MAX};
    if (r == Result::Ok) do
        {
            r = integerLegElevationStep(graph, *cursor, 1, totals);
            if (++calls > 100000) return 1;
            if (retry(r) && totals.length_cm != UINT64_MAX) return 1;
        } while (retry(r));
    const auto retained = sizeof(PackageWorkspace) + sizeof(OutdoorPackage) + sizeof(GraphReader) +
                          sizeof(IntegerLegElevationCursor) + sizeof(IntegerRouteElevation) + count * sizeof(IntegerRouteLeg);
    std::printf("{\"result\":\"%s\",\"length_cm\":%llu,\"ascent_cm\":%llu,\"descent_cm\":%llu,\"continuation_calls\":%zu,\"read_attempts\":%zu,\"successful_bytes\":%llu,\"library_payload_bytes\":%zu,\"probe_leg_capacity\":32}\n",
                resultName(r), static_cast<unsigned long long>(totals.length_cm), static_cast<unsigned long long>(totals.ascent_cm),
                static_cast<unsigned long long>(totals.descent_cm), calls, file.calls, static_cast<unsigned long long>(file.bytes), retained);
    return r == Result::Ok ? 0 : 1;
}
