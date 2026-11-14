#pragma once

#include "Types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>


namespace Radiocom::Link
{

constexpr size_t packetHeaderSize = 20;

enum class PacketType : uint8_t
{
    Request = 1,
    Response = 2,
};

struct Packet
{
    PacketType type = PacketType::Request;
    uint64_t session = 0;
    uint32_t sequence = 0;
    Bytes_t data;
};

Bytes_t encodePacket(const Packet& packet);
std::optional<Packet> decodePacket(std::span<const uint8_t> bytes);
bool isEchoResponse(const Packet& request, const Packet& response);

} // namespace Radiocom::Link
