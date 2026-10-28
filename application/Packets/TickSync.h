#pragma once
#include "PacketBase.h"
class TickSync : public PacketBase
{
public:
    virtual uint32_t ID() override {
        return IDTickSync;
    }
    virtual std::vector<unsigned char> Serializ() override {
        BinaryWriter bw(25);
        bw.WriteVarUInt32(ID());
        bw.WriteInt64(ClientRequestTick);
        bw.WriteInt64(ServerReceptionTick);
        return bw.vect();
    }
    virtual void Deserializ(std::vector<unsigned char> pack) override {
        BinaryReader br(pack.data(), static_cast<int>(pack.size()));
        ClientRequestTick = br.ReadInt64();
        ServerReceptionTick = br.ReadInt64();
    }
    __int64 ClientRequestTick = 0;
    __int64 ServerReceptionTick = 0;
};
