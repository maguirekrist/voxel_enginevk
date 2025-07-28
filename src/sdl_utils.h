
#pragma once
#include <SDL_events.h>

class SdlUtils {
public:
    static void PrintKeyInfo(SDL_KeyboardEvent *key) {
        if(key->type == SDL_KEYUP) {
            fmt::print("Release:-");
        } else {
            fmt::print("Press:-");
        }

        fmt::print("Name: {}", SDL_GetKeyName(key->keysym.sym));
        fmt::println("");
    }
};