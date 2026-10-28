#include "NetworkSettings.h"
#include "Logger.h"

uint32_t NetworkSettings::ID()
{
    return IDNetworkSettings;
}

void NetworkSettings::Deserializ(std::vector<unsigned char> pack)
{
    LOG(LOG_NETWORK, "[NetworkSettings] Deserializing network settings, size: ", pack.size());
    BinaryReader br(pack.data(),pack.size());
    CompressionThreshold = br.ReadUInt16();
    CompressionAlgorithm = br.ReadUInt16();
    ClientThrottleEnabled = br.ReadInt8();
    ClientThrottleThreshold = br.ReadInt8();
    uint32_t scalarBits = br.ReadUInt32();
    std::memcpy(&ClientThrottleScalar, &scalarBits, sizeof(float));
    LOG(LOG_NETWORK, "[NetworkSettings] CompressionThreshold: ", CompressionThreshold);
    LOG(LOG_NETWORK, "[NetworkSettings] CompressionAlgorithm: ", CompressionAlgorithm);
    LOG(LOG_NETWORK, "[NetworkSettings] ClientThrottleEnabled: ", (int)ClientThrottleEnabled);
    LOG(LOG_NETWORK, "[NetworkSettings] ClientThrottleThreshold: ", (int)ClientThrottleThreshold);
    LOG(LOG_NETWORK, "[NetworkSettings] ClientThrottleScalar: ", ClientThrottleScalar);
}
