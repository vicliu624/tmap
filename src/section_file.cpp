#include "tmap/section_file.h"
namespace tmap
{
PackageSectionFile::PackageSectionFile(OutdoorPackage& package, SectionType type)
    : package_(package), type_(type), generation_(package.generation())
{
    const auto* section = package.section(type);
    if (package.isOpen() && section) bytes_ = section->bytes;
}
uint64_t PackageSectionFile::size() const
{
    return package_.isOpen() && package_.generation() == generation_ ? bytes_ : 0;
}
uint64_t PackageSectionFile::epoch(void* context)
{
    return static_cast<PackageSectionFile*>(context)->package_.generation();
}
void PackageSectionFile::discardPending()
{
    cancelRead(read_);
    pending_ = false;
}
Status PackageSectionFile::readAt(uint64_t offset, uint8_t* output, size_t bytes)
{
    if (!package_.isOpen() || package_.generation() != generation_) return Status::Cancelled;
    if ((!output && bytes) || bytes > package_.payloadBytes()) return Status::Invalid;
    if (pending_ && (read_.offset != offset || read_.output != output || read_.bytes != bytes)) return Status::Invalid;
    Result result;
    if (!pending_)
    {
        result = beginRead(package_, type_, offset, output, bytes, read_);
        if (result != Result::Ok) return result == Result::Missing ? Status::Missing : Status::Invalid;
        pending_ = true;
    }
    result = readStep(package_, read_, 1);
    if (!package_.isOpen() || package_.generation() != generation_) return Status::Cancelled;
    if (result == Result::Busy) return Status::Busy;
    if (result == Result::More) return Status::More;
    pending_ = false;
    switch (result)
    {
    case Result::Ok:
        return Status::Ok;
    case Result::Cancelled:
        return Status::Cancelled;
    case Result::IoError:
        return Status::IoError;
    case Result::Missing:
        return Status::Missing;
    default:
        return Status::Invalid;
    }
}
} // namespace tmap
