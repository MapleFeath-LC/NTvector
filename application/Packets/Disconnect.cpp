#include "Disconnect.h"
#include "Logger.h"
#include <sstream>
#include <iomanip>

uint32_t Disconnect::ID()
{
    return IDDisconnect;
}

void Disconnect::Deserializ(std::vector<unsigned char> pack)
{
    LOG(LOG_WARN, "[Disconnect] Deserializing disconnect packet, size: ", pack.size());
    // 输出原始字节以便排查空消息问题
    {
        std::ostringstream hex_oss;
        hex_oss << "[Disconnect] Raw hex:";
        for (size_t i = 0; i < pack.size(); i++) {
            hex_oss << " " << std::hex << std::setfill('0') << std::setw(2) << (int)pack[i];
        }
        LOG(LOG_WARN, hex_oss.str());
    }
    BinaryReader br(pack.data(), pack.size());
    const int rawReason = br.ReadVarInt();
    const int reason = (rawReason >> 1) ^ -(rawReason & 1);
    uint8_t hide = br.ReadUInt8();
    if (!hide) {
        br.ReadStringUTF(message);
    }
    LOG(LOG_WARN, "[Disconnect] Disconnect message: ", message);
    LOG(LOG_WARN, "[Disconnect] reason: ", reason);
    LOG(LOG_WARN, "[Disconnect] hideDisconnectScreen: ", (int)hide);
}
