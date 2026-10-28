#pragma once
#include "PacketBase.h"
class Disconnect :
    public PacketBase
{
public:
	uint32_t ID() override;
	void Deserializ(std::vector<unsigned char> pack) override;
	string message;
};

