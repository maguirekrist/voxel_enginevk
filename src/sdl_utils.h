
#pragma once
#include <SDL_events.h>

class SdlUtils {
public:
    static void PrintKeyInfo(SDL_KeyboardEvent *key) {
        if(key->type == SDL_KEYUP) {
            std::print("Release:-");
        } else {
            std::print("Press:-");
        }

        std::println("Name: {}", SDL_GetKeyName(key->keysym.sym));
    }
};