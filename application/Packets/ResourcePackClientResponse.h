#pragma once
#include "PacketBase.h"
class ResourcePackClientResponse :
    public PacketBase
{
public:
	uint32_t ID() override;
	std::vector<unsigned char> Serializ() override;
	char Response;
	vector<string> PacksToDownload;
};

