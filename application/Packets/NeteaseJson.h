#pragma once
#include "PacketBase.h"
class NeteaseJson :
    public PacketBase
{
public:
	uint32_t ID() override;
	std::vector<unsigned char> Serializ() override;
	string Json;
};

