#pragma once
#include "PacketBase.h"
class RequestNetworkSettings : public PacketBase
{
public:
	uint32_t ID() override;
	std::vector<unsigned char> Serializ() override;
	uint32_t ProtocolVersion;
};

