#include "ResourcePackClientResponse.h"

uint32_t ResourcePackClientResponse::ID()
{
    return IDResourcePackClientResponse;
}

std::vector<unsigned char> ResourcePackClientResponse::Serializ()
{
	BinaryWriter Write(4);
	Write.WriteVarUInt32(ID());
	Write.WriteUInt8(Response);
	Write.WriteUInt16(PacksToDownload.size());
	for (int i = 0;i < PacksToDownload.size();i++) {
		Write.Write(PacksToDownload[i].data(), PacksToDownload[i].size());
	}
	return Write.vect();
}
