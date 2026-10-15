#include "tmap/outdoor_session.h"
namespace tmap
{
Result OutdoorSession::begin(RandomAccessFile& file, SessionOptions options)
{
    if (terminal_ == Result::More || terminal_ == Result::Ok) return Result::Invalid;
    if (requiredBytes(options) > options.max_object_bytes) return terminal_ = Result::BudgetExceeded;
    file_ = &file;
    options_ = options;
    stage_ = 0;
    return terminal_ = Result::More;
}
Result OutdoorSession::initializeStep(size_t budget)
{
    if (terminal_ != Result::More) return terminal_;
    while (budget && stage_ < 7)
    {
        --budget;
        auto result = Result::Ok;
        switch (stage_)
        {
        case 0:
            result = workspace_.create();
            break;
        case 1:
            result = package_.create(*workspace_.get());
            break;
        case 2:
            result = package_.get()->open(*file_, 1);
            break;
        case 3:
            if (options_.require_graph) result = graph_.create(*package_.get());
            break;
        case 4:
            if (options_.require_graph) result = graph_.get()->initialize();
            break;
        case 5:
            if (options_.require_terrain) result = terrain_.create(*package_.get());
            break;
        case 6:
            if (options_.require_terrain) result = terrain_.get()->initialize();
            break;
        }
        if (result == Result::Busy || result == Result::More) return result;
        if (result != Result::Ok)
        {
            reset();
            return terminal_ = result;
        }
        ++stage_;
    }
    if (stage_ == 7) terminal_ = Result::Ok;
    return terminal_;
}
void OutdoorSession::reset()
{
    terrain_.reset();
    graph_.reset();
    if (package_.get()) package_.get()->close();
    package_.reset();
    workspace_.reset();
    file_ = nullptr;
    stage_ = 0;
    terminal_ = Result::Invalid;
}
void OutdoorSession::cancel()
{
    reset();
    terminal_ = Result::Cancelled;
}
} // namespace tmap
