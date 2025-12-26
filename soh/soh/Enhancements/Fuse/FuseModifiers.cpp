#include "soh/Enhancements/Fuse/FuseModifiers.h"

bool HasModifier(const ModifierSpec* mods, size_t count, ModifierId id, uint8_t* outLevel) {
    if (!mods || count == 0) {
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        if (mods[i].id == id) {
            if (outLevel) {
                *outLevel = mods[i].level;
            }
            return true;
        }
    }

    return false;
}

