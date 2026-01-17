#ifndef PACKED_BOARD_IO_H
#define PACKED_BOARD_IO_H

#include "packed_board.h"
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>

enum class PackedFormat {
    V1,
    V2
};

#pragma pack(push, 1)
struct PackedBoardFileHeader {
    char magic[4];
    uint8_t version;
    uint8_t flags;
    uint8_t endianness;
    uint8_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(PackedBoardFileHeader) == 8, "PackedBoardFileHeader must be 8 bytes");

struct PackedBoardReadInfo {
    PackedFormat format = PackedFormat::V1;
    bool has_header = false;
    PackedBoardFileHeader header{};
    size_t record_size = 0;
    bool has_ply = false;
};

constexpr uint8_t kPackedBoardVersionV2 = 2;
constexpr uint8_t kPackedBoardFlagHasPly = 0x01;
constexpr uint8_t kPackedBoardEndianLittle = 1;

PackedBoardFileHeader make_packed_board_header(uint8_t flags);
bool read_packed_board_header(std::ifstream& in, PackedBoardFileHeader& header);
bool write_packed_board_header(std::ofstream& out, const PackedBoardFileHeader& header);
size_t packed_board_record_size(PackedFormat format, uint8_t flags);
bool detect_packed_board_read_info(std::ifstream& in, std::optional<PackedFormat> forced_format,
    PackedBoardReadInfo& info, std::string& error);

bool convert_packed_v1_to_v2(const std::string& input_path, const std::string& output_path,
    bool include_ply, std::string& error);
bool verify_packed_board_file(const std::string& path, std::optional<PackedFormat> forced_format,
    std::string& error);

#endif // PACKED_BOARD_IO_H
