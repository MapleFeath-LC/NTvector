#pragma once
#include "PacketBase.h"
class ResourcePacksInfo :
    public PacketBase
{
public:
	uint32_t ID() override;
	void Deserializ(std::vector<unsigned char> pack) override;
	bool TexturePackRequired;
	bool HasScripts;
	//std::vector<BehaviourPackInfo> BehaviourPacks;
};

