#pragma once
#include "PacketBase.h"
class ServerToClientHandshake :
    public PacketBase
{
public:
	uint32_t ID() override;
	void Deserializ(std::vector<unsigned char> pack) override;
	string JWT;
};

