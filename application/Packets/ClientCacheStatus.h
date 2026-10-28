#pragma once
#include "PacketBase.h"
class ClientCacheStatus :
    public PacketBase
{
public:
	uint32_t ID() override;
	std::vector<unsigned char> Serializ() override;
	bool Supported;
};

