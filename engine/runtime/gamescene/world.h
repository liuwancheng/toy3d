#pragma once

#include "gamescene/actor.h"

#include <memory>
#include <vector>

namespace toy3d
{
    class World
    {
    public:
        World() = default;
        ~World() = default;

        World(const World&) = delete;
        World& operator=(const World&) = delete;

        Actor& create_actor();
        void update_transforms();

    private:
        std::vector<std::unique_ptr<Actor>> actors_;
    };
}
