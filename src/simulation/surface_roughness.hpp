#pragma once

#include <cstdint>

namespace toca::simulation {
//42e6f0 / DAT_004d5634: one process-lifetime cursor for every car's
//rough-surface contacts. Car/grid resets do not reset this shared sequence.
class SurfaceRoughness {
    std::uint16_t cursor_ = 0;

public:
    //Returns the current signed Q16 height offset, then advances modulo15.
    std::int32_t next();
    //Next table index, always0..14. Explicit import is diagnostic state only.
    std::uint16_t current() const;
    void import_cursor(std::uint32_t cursor);
};
} // namespace toca::simulation
