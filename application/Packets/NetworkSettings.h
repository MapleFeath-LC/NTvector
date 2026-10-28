#pragma once
#include "PacketBase.h"
class NetworkSettings :
    public PacketBase
{
public:
	uint32_t ID() override;
	void Deserializ(std::vector<unsigned char> pack) override;
	uint16_t CompressionThreshold;
	uint16_t CompressionAlgorithm;
	bool ClientThrottleEnabled;
	uint8_t ClientThrottleThreshold;
	float ClientThrottleScalar;
};

