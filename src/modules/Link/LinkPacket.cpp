#include "LinkPacket.h"

#include <algorithm>
#include <array>


namespace Radiocom::Link
{
namespace
{

constexpr std::array<uint8_t, 4> packetMagic = {0x52, 0x43, 0x4f, 0x4d};
constexpr uint8_t packetVersion = 1;

void appendInteger(Bytes_t& bytes, uint64_t value, size_t size)
{
    for (size_t index = size; index > 0; --index)
    {
        bytes.push_back(uint8_t(value >> ((index - 1) * 8)));
    }
}

uint64_t readInteger(std::span<const uint8_t> bytes)
{
    uint64_t value = 0;
    for (const uint8_t byte : bytes)
    {
        value = (value << 8) | byte;
    }
    return value;
}

} // namespace

Bytes_t encodePacket(const Packet& packet)
{
    Bytes_t bytes(packetMagic.begin(), packetMagic.end());
    bytes.reserve(packetHeaderSize + packet.data.size());
    bytes.push_back(packetVersion);
    bytes.push_back(uint8_t(packet.type));
    bytes.push_back(0);
    bytes.push_back(0);
    appendInteger(bytes, packet.session, 8);
    appendInteger(bytes, packet.sequence, 4);
    bytes.insert(bytes.end(), packet.data.begin(), packet.data.end());
    return bytes;
}

std::optional<Packet> decodePacket(std::span<const uint8_t> bytes)
{
    if (bytes.size() < packetHeaderSize || !std::equal(packetMagic.begin(), packetMagic.end(), bytes.begin()) ||
        bytes[4] != packetVersion || bytes[6] != 0 || bytes[7] != 0)
    {
        return std::nullopt;
    }
    if (bytes[5] != uint8_t(PacketType::Request) && bytes[5] != uint8_t(PacketType::Response))
    {
        return std::nullopt;
    }
    return Packet{PacketType(bytes[5]), readInteger(bytes.subspan(8, 8)), uint32_t(readInteger(bytes.subspan(16, 4))),
                  Bytes_t(bytes.begin() + ptrdiff_t(packetHeaderSize), bytes.end())};
}

bool isEchoResponse(const Packet& request, const Packet& response)
{
    return request.type == PacketType::Request && response.type == PacketType::Response &&
           request.session == response.session && request.sequence == response.sequence &&
           request.data == response.data;
}

} // namespace Radiocom::Link
