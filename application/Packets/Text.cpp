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

const char* const kTagsMessageOnly[6] = {
    "raw", "tip", "systemMessage", "textObjectWhisper", "textObjectAnnouncement", "textObject"
};
const char* const kTagsAuthorAndMessage[3] = { "chat", "whisper", "announcement" };
const char* const kTagsMessageAndParams[3] = { "translate", "popup", "jukeboxPopup" };

void skipTags(BinaryReader& br, const char* const* tags, int count)
{
    std::string value;
    for (int i = 0; i < count; ++i) {
        br.ReadStringUTF(value);
    }
}

void writeTags(BinaryWriter& bw, const char* const* tags, int count)
{
    for (int i = 0; i < count; ++i) {
        bw.WriteStringUTF(tags[i]);
    }
}

void readStringArray(BinaryReader& br, std::vector<std::string>& out)
{
    out.clear();
    const unsigned int count = br.ReadVarUInt();
    std::string value;
    for (unsigned int i = 0; i < count; ++i) {
        br.ReadStringUTF(value);
        out.push_back(value);
    }
}

void writeStringArray(BinaryWriter& bw, const std::vector<std::string>& values)
{
    bw.WriteVarUInt(static_cast<unsigned int>(values.size()));
    for (const std::string& value : values) {
        bw.WriteStringUTF(value);
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
    NeedsTranslation = false;
    Variant = 1;

    NeedsTranslation = br.ReadUInt8() != 0;
    Variant = br.ReadUInt8();

    if (Variant == 0u) {
        skipTags(br, kTagsMessageOnly, 6);
        Type = br.ReadUInt8();
        br.ReadStringUTF(Message);
    }
    else if (Variant == 2u) {
        skipTags(br, kTagsMessageAndParams, 3);
        Type = br.ReadUInt8();
        br.ReadStringUTF(Message);
        readStringArray(br, Parameters);
    }
    else {
        Variant = 1;
        skipTags(br, kTagsAuthorAndMessage, 3);
        Type = br.ReadUInt8();
        br.ReadStringUTF(SourceName);
        br.ReadStringUTF(Message);
        if (br.m_pointer < pack.size()) {
            readStringArray(br, Parameters);
        }
    }

    br.ReadStringUTF(XUID);
    br.ReadStringUTF(PlatformChatID);
    if (br.m_pointer < pack.size() && br.ReadUInt8() != 0) {
        br.ReadStringUTF(FilteredMessage);
    }

    LOG(LOG_SERVER, "[Text] Message type: ", (int)Type, ", variant: ", (int)Variant);
}

std::vector<unsigned char> Text::Serializ()
{
    BinaryWriter bw(SourceName.size() + Message.size() + 64);
    bw.WriteVarUInt32(ID());

    const std::string body = Message.empty() ? std::string(" ") : Message;

    bw.WriteUInt8(0);

    if (isChatLike(Type)) {
        bw.WriteUInt8(1);
        writeTags(bw, kTagsAuthorAndMessage, 3);
        bw.WriteUInt8(Type);
        bw.WriteStringUTF(SourceName);
        bw.WriteStringUTF(body);
        writeStringArray(bw, Parameters);
    }
    else if (isParamsLike(Type)) {
        bw.WriteUInt8(2);
        writeTags(bw, kTagsMessageAndParams, 3);
        bw.WriteUInt8(Type);
        bw.WriteStringUTF(body);
        writeStringArray(bw, Parameters);
    }
    else {
        bw.WriteUInt8(0);
        writeTags(bw, kTagsMessageOnly, 6);
        bw.WriteUInt8(Type);
        bw.WriteStringUTF(body);
    }

    bw.WriteStringUTF(XUID);
    bw.WriteStringUTF(PlatformChatID);
    bw.WriteUInt8(0);

    return bw.vect();
}
