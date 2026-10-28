#pragma once
#include "PacketBase.h"
class Text :
    public PacketBase
{
public:
	uint32_t ID() override;
	void Deserializ(std::vector<unsigned char> pack) override;
	std::vector<unsigned char> Serializ() override;
	uint8_t Type = 0;
	std::string SourceName;
	std::string Message;
	std::vector<std::string> Parameters;
	std::string XUID;
	std::string PlatformChatID;
	std::string FilteredMessage;
};

