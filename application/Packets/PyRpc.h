#pragma once
#include "PacketBase.h"
class PyRpc :
    public PacketBase
{
public:
	uint32_t ID() override;
	void Deserializ(std::vector<unsigned char> pack) override;
	std::vector<unsigned char> Serializ() override;
	std::string Data;
	unsigned int MsgID = 0;
};

