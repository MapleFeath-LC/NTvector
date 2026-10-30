#include "CommandOutput.h"
#include "ProtocolRead.h"
#include <cstring>  // for memcpy

namespace {

const char* const kOutputTypeNames[] = { "none", "lastoutput", "silent", "alloutput", "dataset" };
constexpr size_t kOutputTypeCount = sizeof(kOutputTypeNames) / sizeof(kOutputTypeNames[0]);

const char* outputTypeName(uint8_t type)
{
    return type < kOutputTypeCount ? kOutputTypeNames[type] : kOutputTypeNames[CommandOutputTypeNone];
}

uint8_t outputTypeFromName(const std::string& name)
{
    for (size_t i = 0; i < kOutputTypeCount; ++i) {
        if (name == kOutputTypeNames[i]) {
            return static_cast<uint8_t>(i);
        }
    }
    return CommandOutputTypeNone;
}

} // namespace

uint32_t CommandOutput::ID()
{
    return IDCommandOutput;
}

void CommandOutput::Deserializ(std::vector<unsigned char> pack)
{
    BinaryReader br(pack.data(), pack.size());

    // 1. 读取 CommandOrigin
    std::string originType;
    br.ReadStringUTF(originType);
    Origin.Origin = CommandOriginPlayer;

    // UUID (16 bytes, 固定长度)
    void* uuidData = br.Read(16);
    if (uuidData) {
        memcpy(Origin.UUID.data(), uuidData, 16);
    }

    // RequestID (string)
    br.ReadStringUTF(Origin.RequestID);

    // PlayerUniqueID (int64 小端, 恒定存在)
    Origin.PlayerUniqueID = br.ReadInt64();

    // 2. 读取 OutputType
    std::string typeName;
    br.ReadStringUTF(typeName);
    OutputType = outputTypeFromName(typeName);

    // 3. 读取 SuccessCount
    SuccessCount = br.ReadUInt32();

    // 4. 读取 OutputMessages 数组
    const uint32_t messageCount = br.ReadVarUInt();
    OutputMessages.clear();
    OutputMessages.reserve(messageCount);

    for (uint32_t i = 0; i < messageCount; i++) {
        CommandOutputMessage msg;

        br.ReadStringUTF(msg.MessageID);
        br.ReadBool(msg.Internal);

        const uint32_t paramCount = br.ReadVarUInt();
        msg.Parameters.reserve(paramCount);
        for (uint32_t j = 0; j < paramCount; j++) {
            std::string param;
            br.ReadStringUTF(param);
            msg.Parameters.push_back(std::move(param));
        }

        OutputMessages.push_back(std::move(msg));
    }

    // 5. 读取 DataSet (包尾可选)
    if (br.m_pointer < pack.size() && br.ReadUInt8() != 0) {
        br.ReadStringUTF(DataSet);
    }
}

std::vector<unsigned char> CommandOutput::Serializ()
{
    // CommandOutput 通常是服务端发送的，客户端一般不需要序列化
    // 但为了完整性，这里提供序列化实现
    BinaryWriter bw(256);

    bw.WriteVarUInt32(ID());

    // 1. 写入 CommandOrigin
    bw.WriteStringUTF(std::string("player"));
    bw.Write(Origin.UUID.data(), 16);
    bw.WriteStringUTF(Origin.RequestID);
    bw.WriteInt64(Origin.PlayerUniqueID);

    // 2. 写入 OutputType
    bw.WriteStringUTF(std::string(outputTypeName(OutputType)));

    // 3. 写入 SuccessCount
    bw.WriteUInt32(SuccessCount);

    // 4. 写入 OutputMessages
    bw.WriteVarUInt(static_cast<uint32_t>(OutputMessages.size()));
    for (const auto& msg : OutputMessages) {
        bw.WriteStringUTF(msg.MessageID);
        bw.WriteBool(msg.Internal);

        bw.WriteVarUInt(static_cast<uint32_t>(msg.Parameters.size()));
        for (const auto& param : msg.Parameters) {
            bw.WriteStringUTF(param);
        }
    }

    // 5. 写入 DataSet
    const bool hasData = !DataSet.empty();
    bw.WriteBool(hasData);
    if (hasData) {
        bw.WriteStringUTF(DataSet);
    }

    return bw.vect();
}

std::vector<std::string> CommandOutput::GetAllMessages() const
{
    std::vector<std::string> result;
    result.reserve(OutputMessages.size());
    for (const auto& msg : OutputMessages) {
        result.push_back(msg.MessageID);
    }
    return result;
}
