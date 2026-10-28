#include "ClientCacheStatus.h"

uint32_t ClientCacheStatus::ID()
{
    return IDClientCacheStatus;
}

std::vector<unsigned char> ClientCacheStatus::Serializ()
{
	BinaryWriter Write(4);
	Write.WriteVarUInt32(ID());
	Write.WriteBool(Supported);
	return Write.vect();
}
