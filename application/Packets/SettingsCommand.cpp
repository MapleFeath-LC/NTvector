#include "SettingsCommand.h"

uint32_t SettingsCommand::ID()
{
    return IDSettingsCommand;
}

std::vector<unsigned char> SettingsCommand::Serializ()
{
    int chainsize = command.size();
    BinaryWriter bw(chainsize + 4);
    bw.WriteVarUInt32(ID());
    bw.WriteVarUInt(chainsize);
    bw.Write(command.data(), chainsize);
    bw.WriteUInt8(SuppressOutput);
    return bw.vect();
}
