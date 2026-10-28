#include "RequestChunkRadius.h"

uint32_t RequestChunkRadius::ID()
{
    return IDRequestChunkRadius;
}

std::vector<unsigned char> RequestChunkRadius::Serializ()
{
	BinaryWriter Write(3);
	Write.WriteVarUInt32(ID());
	Write.WriteVarInt(ChunkRadius);
	Write.WriteUInt8(MaxChunkRadius);
	return Write.vect();
}
