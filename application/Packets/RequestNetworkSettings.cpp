#include "RequestNetworkSettings.h"

uint32_t RequestNetworkSettings::ID() {
	return IDRequestNetworkSettings;
}
std::vector<unsigned char> RequestNetworkSettings::Serializ() {
	BinaryWriter Write(6);
	Write.WriteVarUInt32(ID());
	Write.WriteUInt32_Big(ProtocolVersion);
	return Write.vect();
}