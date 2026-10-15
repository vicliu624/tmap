#include "tmap/native_file.h"
#include <limits>
namespace tmap
{
namespace
{
bool seek(std::FILE* file, uint64_t offset, int origin)
{
    if (offset > uint64_t(std::numeric_limits<int64_t>::max())) return false;
#if defined(_WIN32)
    return _fseeki64(file, static_cast<int64_t>(offset), origin) == 0;
#else
    static_assert(sizeof(off_t) >= 8, "Native adapter requires 64-bit file offsets");
    return fseeko(file, static_cast<off_t>(offset), origin) == 0;
#endif
}
} // namespace
bool NativeFile::open(const char* path)
{
    close();
    if (!path) return false;
    file_ = std::fopen(path, "rb");
    if (!file_ || !seek(file_, 0, SEEK_END))
    {
        close();
        return false;
    }
#if defined(_WIN32)
    const auto size = _ftelli64(file_);
#else
    const auto size = ftello(file_);
#endif
    if (size < 0)
    {
        close();
        return false;
    }
    size_ = static_cast<uint64_t>(size);
    return true;
}
void NativeFile::close()
{
    if (file_) std::fclose(file_);
    file_ = nullptr;
    size_ = 0;
}
Status NativeFile::readAt(uint64_t offset, uint8_t* output, size_t bytes)
{
    if (!file_ || (!output && bytes) || offset > size_ || bytes > size_ - offset || !seek(file_, offset, SEEK_SET)) return Status::IoError;
    return std::fread(output, 1, bytes, file_) == bytes ? Status::Ok : Status::IoError;
}
} // namespace tmap
