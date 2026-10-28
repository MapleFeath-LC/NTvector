#pragma once
#include "PacketBase.h"
class PlayStatus :
    public PacketBase
{
public:
	uint32_t ID() override;
	void Deserializ(std::vector<unsigned char> pack) override;
	int32_t Status;
};

