#include "Text.h"
#include "Logger.h"

namespace {

bool isChatLike(uint8_t textType)
{
    return textType == 1u || textType == 7u || textType == 8u;
}

bool isParamsLike(uint8_t textType)
{
    return textType == 2u || textType == 3u || textType == 4u;
}

void skipStringArray(BinaryReader& br)
{
    const unsigned int count = br.ReadVarUInt();
    std::string ignored;
    for (unsigned int i = 0; i < count; ++i) {
        br.ReadStringUTF(ignored);
    }
}

} // namespace

uint32_t Text::ID()
{
    return IDText;
}

void Text::Deserializ(std::vector<unsigned char> pack)
{
    LOG(LOG_SERVER, "[Text] Deserializing text packet, size: ", pack.size());
    BinaryReader br(pack.data(), pack.size());

    SourceName.clear();
    Message.clear();
    Parameters.clear();
    XUID.clear();
    PlatformChatID.clear();
    FilteredMessage.clear();

    Type = br.ReadUInt8();
    br.ReadUInt8();

    if (isChatLike(Type)) {
        br.ReadStringUTF(SourceName);
        br.ReadStringUTF(Message);
    }
    else if (isParamsLike(Type)) {
        br.ReadStringUTF(Message);
        const unsigned int count = br.ReadVarUInt();
        std::string param;
        for (unsigned int i = 0; i < count; ++i) {
            br.ReadStringUTF(param);
            Parameters.push_back(param);
        }
    }
    else {
        br.ReadStringUTF(Message);
    }

    br.ReadStringUTF(XUID);
    br.ReadStringUTF(PlatformChatID);
    br.ReadStringUTF(FilteredMessage);

    if (br.m_pointer < pack.size()) {
        if (Type == 1u) {
            skipStringArray(br);
        }
        else if (Type == 3u) {
            std::string ignored;
            br.ReadStringUTF(ignored);
        }
    }

    LOG(LOG_SERVER, "[Text] Message type: ", (int)Type);
}

std::vector<unsigned char> Text::Serializ()
{
    BinaryWriter bw(SourceName.size() + Message.size() + 32);
    bw.WriteVarUInt32(ID());
    bw.WriteUInt8(Type);
    bw.WriteBool(false);

    if (isChatLike(Type)) {
        bw.WriteStringUTF(SourceName);
        bw.WriteStringUTF(Message);
    }
    else {
        bw.WriteStringUTF(Message);
    }

    bw.WriteStringUTF(std::string());
    bw.WriteStringUTF(std::string());
    bw.WriteStringUTF(std::string());

    if (Type == 1u) {
        bw.WriteVarUInt(0);
    }

    return bw.vect();
}
