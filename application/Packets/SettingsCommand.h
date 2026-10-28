#pragma once
#include "PacketBase.h"
class SettingsCommand :
	public PacketBase
{
public:
	uint32_t ID() override;
	std::vector<unsigned char> Serializ() override;
	std::string command;
	bool SuppressOutput = false;
};

