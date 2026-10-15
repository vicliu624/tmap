#include "tmap/file_identity.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
using namespace tmap;
#define CHECK(x)                                                \
    do                                                          \
    {                                                           \
        if (!(x))                                               \
        {                                                       \
            std::fprintf(stderr, "FAIL %d %s\n", __LINE__, #x); \
            std::exit(1);                                       \
        }                                                       \
    } while (false)
class File : public RandomAccessFile
{
  public:
    uint8_t bytes[130]{};
    size_t length = sizeof(bytes), calls = 0;
    uint64_t generation = 1;
    uint64_t size() const override { return length; }
    Status readAt(uint64_t offset, uint8_t* output, size_t count) override
    {
        if (++calls % 2) return Status::Busy;
        std::memcpy(output, bytes + size_t(offset), count);
        return Status::Ok;
    }
};
uint64_t epoch(void* file) { return static_cast<File*>(file)->generation; }
int main()
{
    File file;
    for (size_t i = 0; i < sizeof(file.bytes); ++i) file.bytes[i] = uint8_t(i);
    FileIdentity expected;
    expected.bytes = file.size();
    Sha256 hash;
    hash.update(file.bytes, sizeof(file.bytes));
    hash.finish(expected.sha256);
    auto cursor = std::make_unique<FileIdentityCursor>();
    CHECK(beginFileIdentity(file, expected, *cursor, 130, epoch, &file) == Result::More);
    CHECK(fileIdentityStep(*cursor, 0) == Result::Invalid && !cursor->offset);
    Result result = Result::More;
    size_t steps = 0;
    while (result == Result::More || result == Result::Busy)
    {
        const auto before = file.calls;
        result = fileIdentityStep(*cursor, 1);
        CHECK(file.calls - before <= 1 && ++steps < 20);
    }
    CHECK(result == Result::Ok && cursor->offset == 130 && file.calls == 6);
    CHECK(fileIdentityStep(*cursor, 1) == Result::Ok && file.calls == 6);
    ++file.generation;
    CHECK(fileIdentityStep(*cursor, 1) == Result::Cancelled);
    file.length = 0;
    FileIdentity empty;
    hash.reset();
    hash.finish(empty.sha256);
    const auto before_empty = file.calls;
    CHECK(beginFileIdentity(file, empty, *cursor, 0) == Result::More);
    CHECK(fileIdentityStep(*cursor, 1) == Result::Ok && file.calls == before_empty);
    file.length = 130;
    CHECK(beginFileIdentity(file, expected, *cursor, 129) == Result::BudgetExceeded);
    CHECK(beginFileIdentity(file, expected, *cursor) == Result::More);
    cancel(*cursor);
    CHECK(fileIdentityStep(*cursor, 1) == Result::Cancelled);
    file.bytes[129] ^= 1;
    CHECK(beginFileIdentity(file, expected, *cursor) == Result::More);
    result = Result::More;
    while (result == Result::More || result == Result::Busy) result = fileIdentityStep(*cursor, 1);
    CHECK(result == Result::Incompatible);
    file.length = 129;
    CHECK(beginFileIdentity(file, expected, *cursor) == Result::Incompatible);
    file.length = 130;
    CHECK(beginFileIdentity(file, expected, *cursor, 130, epoch, &file) == Result::More);
    ++file.generation;
    CHECK(fileIdentityStep(*cursor, 1) == Result::Cancelled);
    std::printf("PASS bounded source reads, Busy progress, full-file digest mismatch, size/epoch, cancellation/restart; cursor=%zu\n", sizeof(FileIdentityCursor));
}
