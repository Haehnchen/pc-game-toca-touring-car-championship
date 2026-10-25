#pragma once

#include "scene/prop_fragments.hpp"

namespace toca::scene {
// 409fa0/432210's48-byte spark request: wall mode0, ground mode1.
struct ScrapeSparkRequest {
    std::array<std::int32_t, 3> position{}, direction{}, velocity{};
    std::int32_t intensity_q16 = 0;
    bool ground = false;
};

struct ContactSparkLine {
    std::array<std::int32_t, 3> from{}, to{};
    std::uint32_t color = 0xfffff096;
};

class ContactSparks {
    struct Spark {
        ContactSparkLine line, previous_line;
        std::array<std::int32_t, 3> velocity{};
        std::uint32_t remaining = 6;
    };

    PropFragmentTables& tables_;
    std::vector<Spark> sparks_;
    mutable std::vector<ContactSparkLine> lines_;

public:
    explicit ContactSparks(PropFragmentTables& tables);
    void emit(const ScrapeSparkRequest& request); // 418570, original64-record pool.
    void update(); // 418430 once per simulation tick, before queued emissions.
    void clear(); // 415d50 preserves the shared cyclic table cursor.
    std::span<const ContactSparkLine> lines() const;
    std::vector<ContactSparkLine> presentation_lines(float alpha) const;
    std::size_t size() const;
};
} // namespace toca::scene
