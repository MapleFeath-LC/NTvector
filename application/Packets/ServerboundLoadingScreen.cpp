#include "ServerboundLoadingScreen.h"

uint32_t ServerboundLoadingScreen::ID()
{
    return IDServerboundLoadingScreen;
}

std::vector<unsigned char> ServerboundLoadingScreen::Serializ()
{
    BinaryWriter writer(12);
    writer.WriteVarUInt32(ID());
    writer.WriteVarInt(static_cast<int>(action));
    writer.WriteBool(hasLoadingScreenId);
    if (hasLoadingScreenId) {
        writer.WriteInt32(loadingScreenId);
    }
    return writer.vect();
}
