#pragma once

// A/B comparison of two complete parameter sets (SPEC.md section 5). No JUCE dependency.

#include <array>
#include <map>
#include <string>

namespace sasari
{

struct AbSlots
{
    using Values = std::map<std::string, float>;

    std::array<Values, 2> slots;
    int active = 0;

    /**
     * Switch to `target`. The current settings are stored in the slot being left.
     * Returns the values to apply, or nullptr when nothing changes (same slot, or the
     * target slot is still empty and simply takes the current settings).
     */
    const Values* switchTo (int target, const Values& current)
    {
        if (target == active || target < 0 || target > 1)
            return nullptr;
        slots[(size_t) active] = current;
        active = target;
        if (slots[(size_t) target].empty())
        {
            slots[(size_t) target] = current;
            return nullptr;
        }
        return &slots[(size_t) target];
    }

    /** Copy the current settings into both slots (A->B or B->A). */
    void copyToOther (const Values& current)
    {
        slots[(size_t) active] = current;
        slots[(size_t) (1 - active)] = current;
    }
};

} // namespace sasari
