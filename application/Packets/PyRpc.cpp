#include "PyRpc.h"
#include "Logger.h"

uint32_t PyRpc::ID()
{
    return IDPyRpc;
}

void PyRpc::Deserializ(std::vector<unsigned char> pack)
{
    BinaryReader br(pack.data(), pack.size());
    int lengt = br.ReadVarInt();
    Data = string((char*)br.Read(lengt), lengt);
    MsgID = br.ReadUInt32();
}

std::vector<unsigned char> PyRpc::Serializ()
{
    int chainsize = Data.size();
    BinaryWriter bw(chainsize + 10);
    bw.WriteVarUInt32(ID());
    bw.WriteVarUInt(chainsize);
    bw.Write(Data.data(), chainsize);
    bw.WriteUInt32(PyRpcClientID);
    return bw.vect();
}
