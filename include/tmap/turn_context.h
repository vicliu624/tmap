#pragma once
#include "tmap/section_file.h"
#include "tmap/turn_rules.h"
namespace tmap
{
class TurnContextReader
{
  public:
    TurnContextReader(OutdoorPackage& package, uint64_t utc, int32_t offset);
    TurnContextReader(const TurnContextReader&) = delete;
    TurnContextReader& operator=(const TurnContextReader&) = delete;
    Result begin(uint32_t context, uint32_t outgoing, uint8_t mode, uint32_t edges,
                 uint32_t max_matches = 4096, uint32_t max_records = 4096);
    Result step(size_t budget, bool& allowed);
    void cancel();
    bool validFor(const OutdoorPackage& package, uint64_t utc, int32_t offset) const;
    Result read(uint32_t context, uint32_t outgoing, uint8_t mode, uint32_t edges, bool& allowed,
                uint32_t max_matches = 4096, uint32_t max_records = 4096);

  private:
    OutdoorPackage& package_;
    PackageSectionFile records_;
    union Scratch
    {
        TurnProgramReadCursor descriptor;
        TurnRuleCursor rules;
        Scratch() : descriptor{} {}
        ~Scratch() {}
    } scratch_;
    TurnDecisionCursor decision_{};
    TurnContextMatch match_{};
    TurnRelation relation_{};
    std::array<RoadRuleRange, 2> programs_{};
    std::array<uint8_t, 24> bytes_{};
    uint64_t generation_ = 0, utc_ = 0, first_ = 0, end_ = 0, index_ = 0;
    int32_t offset_ = 0;
    uint32_t context_ = 0, edges_ = 0, maximum_ = 4096, max_records_ = 4096;
    uint8_t stage_ = 0, program_mode_ = 0;
    bool rules_active_ = false;
    Result terminal_ = Result::Invalid;
    bool valid() const;
};
} // namespace tmap
