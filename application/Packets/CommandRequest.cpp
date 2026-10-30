#include "CommandRequest.h"

namespace {
constexpr size_t CommandUuidSize = 16;
constexpr const char* OriginPlayer = "player";
constexpr const char* CommandVersionLatest = "latest";

void writeUuid(BinaryWriter& bw, const std::string& uuid)
{
    const size_t bytesToWrite = uuid.size() < CommandUuidSize ? uuid.size() : CommandUuidSize;
    bw.Write(uuid.data(), bytesToWrite);

    if (bytesToWrite < CommandUuidSize) {
        const unsigned char zeros[CommandUuidSize] = {};
        bw.Write(zeros, CommandUuidSize - bytesToWrite);
    }
}
}

uint32_t CommandRequest::ID()
{
    return IDCommandRequest;
}

std::vector<unsigned char> CommandRequest::Serializ()
{
    BinaryWriter bw(m_command.size() + m_requestID.size() + 64);
    bw.WriteVarUInt32(ID());
    bw.WriteStringUTF(m_command);
    bw.WriteStringUTF(std::string(OriginPlayer));
    writeUuid(bw, m_randomUUID);
    bw.WriteStringUTF(m_requestID);
    bw.WriteInt64(m_playerID < 0 ? 0 : m_playerID);
    bw.WriteBool(m_internalSource);
    bw.WriteBool(m_unlimit);
    bw.WriteStringUTF(std::string(CommandVersionLatest));

    return bw.vect();
}
