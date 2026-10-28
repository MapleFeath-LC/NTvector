#pragma once
#include "PacketBase.h"
class ClientToServerHandshake :
    public PacketBase
{
	uint32_t ID() override;
	std::vector<unsigned char> Serializ() override;
};

