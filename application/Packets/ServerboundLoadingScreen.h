#pragma once
#include "PacketBase.h"
class ServerboundLoadingScreen :
    public PacketBase
{
public:
    enum class Action : unsigned char {
        UNKNOWN = 0,
        START_LOADING_SCREEN = 1,
        END_LOADING_SCREEN = 2,
    };

    uint32_t ID() override;
    std::vector<unsigned char> Serializ() override;

    Action action = Action::END_LOADING_SCREEN;
    bool hasLoadingScreenId = false;
    int loadingScreenId = 0;
};
