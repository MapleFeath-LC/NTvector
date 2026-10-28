#include "NeteaseJson.h"

uint32_t NeteaseJson::ID()
{
    return IDNeteaseJson;
}

std::vector<unsigned char> NeteaseJson::Serializ()
{
	size_t size = Json.size();
	BinaryWriter Write(size+5);
	Write.WriteVarUInt32(ID());
	Write.WriteVarUInt(Json.size());
	Write.Write((char*)Json.data(),size);
	return Write.vect();
}
