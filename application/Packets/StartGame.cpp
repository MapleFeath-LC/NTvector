#include "StartGame.h"
#include "Logger.h"
#include "ProtocolRead.h"

uint32_t StartGame::ID()
{
    return IDStartGame;
}

void StartGame::Deserializ(std::vector<unsigned char> pack)
{
    LOG(LOG_SERVER, "[StartGame] Deserializing start game packet, size: ", pack.size());
    BinaryReader br(pack.data(), pack.size());
    playerEntityID = pkt_io::ReadZigZag64(br);
    EntityRuntimeID = br.ReadVarInt64();
	gamemode = pkt_io::ReadZigZag32(br);
	PlayerPosition = br.ReadVec3();
	Pitch = br.ReadFloat();
	Yaw = br.ReadFloat();
    LOG(LOG_INFO, "[StartGame] Player Entity ID: ", playerEntityID);
}
