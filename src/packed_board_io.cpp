#include "packed_board_io.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>

namespace {

constexpr std::array<char, 4> kPackedBoardMagic = {'A', 'E', 'T', 'H'};

bool is_little_endian() {
    uint16_t value = 0x1;
    return *reinterpret_cast<uint8_t*>(&value) == 0x1;
}

} // namespace

PackedBoardFileHeader make_packed_board_header(uint8_t flags) {
    PackedBoardFileHeader header{};
    std::memcpy(header.magic, kPackedBoardMagic.data(), kPackedBoardMagic.size());
    header.version = kPackedBoardVersionV2;
    header.flags = flags;
    header.endianness = kPackedBoardEndianLittle;
    header.reserved = 0;
    return header;
}

bool read_packed_board_header(std::ifstream& in, PackedBoardFileHeader& header) {
    std::streampos start = in.tellg();
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in) {
        in.clear();
        in.seekg(start);
        return false;
    }

    if (std::memcmp(header.magic, kPackedBoardMagic.data(), kPackedBoardMagic.size()) != 0) {
        in.clear();
        in.seekg(start);
        return false;
    }

    return true;
}

bool write_packed_board_header(std::ofstream& out, const PackedBoardFileHeader& header) {
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    return static_cast<bool>(out);
}

size_t packed_board_record_size(PackedFormat format, uint8_t flags) {
    if (format == PackedFormat::V1) {
        return sizeof(PackedBoardV1);
    }
    return (flags & kPackedBoardFlagHasPly) ? sizeof(PackedBoardV2) : sizeof(PackedBoardV2NoPly);
}

bool detect_packed_board_read_info(std::ifstream& in, std::optional<PackedFormat> forced_format,
    PackedBoardReadInfo& info, std::string& error) {
    PackedBoardFileHeader header{};
    bool has_header = read_packed_board_header(in, header);
    if (forced_format.has_value()) {
        if (forced_format.value() == PackedFormat::V2 && !has_header) {
            error = "expected v2 header but none was found";
            return false;
        }
        if (forced_format.value() == PackedFormat::V1 && has_header) {
            error = "file has a v2 header but v1 format was requested";
            return false;
        }
    }

    if (has_header) {
        if (header.version != kPackedBoardVersionV2) {
            error = "unsupported packed board version";
            return false;
        }
        if (header.endianness != kPackedBoardEndianLittle || !is_little_endian()) {
            error = "endianness mismatch for packed board file";
            return false;
        }
        info.format = PackedFormat::V2;
        info.has_header = true;
        info.header = header;
        info.has_ply = (header.flags & kPackedBoardFlagHasPly) != 0;
        info.record_size = packed_board_record_size(info.format, header.flags);
        return true;
    }

    info.format = PackedFormat::V1;
    info.has_header = false;
    info.header = PackedBoardFileHeader{};
    info.has_ply = false;
    info.record_size = packed_board_record_size(info.format, 0);
    return true;
}

bool convert_packed_v1_to_v2(const std::string& input_path, const std::string& output_path,
    bool include_ply, std::string& error) {
    std::ifstream in(input_path, std::ios::binary);
    if (!in.is_open()) {
        error = "failed to open input file";
        return false;
    }

    PackedBoardReadInfo info;
    if (!detect_packed_board_read_info(in, std::optional<PackedFormat>(PackedFormat::V1), info,
            error)) {
        return false;
    }

    std::ofstream out(output_path, std::ios::binary);
    if (!out.is_open()) {
        error = "failed to open output file";
        return false;
    }

    uint8_t flags = include_ply ? kPackedBoardFlagHasPly : 0;
    PackedBoardFileHeader header = make_packed_board_header(flags);
    if (!write_packed_board_header(out, header)) {
        error = "failed to write output header";
        return false;
    }

    PackedBoardV1 record{};
    while (in.read(reinterpret_cast<char*>(&record), sizeof(record))) {
        if (include_ply) {
            PackedBoardV2 converted{};
            converted.occupancy = record.occupancy;
            std::memcpy(converted.pieces, record.pieces, sizeof(record.pieces));
            converted.stm_ep = record.stm_ep;
            converted.halfmove = record.halfmove;
            converted.fullmove = record.fullmove;
            converted.score_cp = record.score_cp;
            converted.wdl = record.wdl;
            converted.result = record.result;
            converted.depth_reached = 0;
            converted.bestmove = 0;
            converted.ply = 0;
            out.write(reinterpret_cast<const char*>(&converted), sizeof(converted));
        } else {
            PackedBoardV2NoPly converted{};
            converted.occupancy = record.occupancy;
            std::memcpy(converted.pieces, record.pieces, sizeof(record.pieces));
            converted.stm_ep = record.stm_ep;
            converted.halfmove = record.halfmove;
            converted.fullmove = record.fullmove;
            converted.score_cp = record.score_cp;
            converted.wdl = record.wdl;
            converted.result = record.result;
            converted.depth_reached = 0;
            converted.bestmove = 0;
            out.write(reinterpret_cast<const char*>(&converted), sizeof(converted));
        }
        if (!out) {
            error = "failed while writing output records";
            return false;
        }
    }

    if (!in.eof()) {
        error = "failed while reading input records";
        return false;
    }

    return true;
}

bool verify_packed_board_file(const std::string& path, std::optional<PackedFormat> forced_format,
    std::string& error) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        error = "failed to open file";
        return false;
    }

    std::streamsize file_size = in.tellg();
    in.seekg(0, std::ios::beg);

    PackedBoardReadInfo info;
    if (!detect_packed_board_read_info(in, forced_format, info, error)) {
        return false;
    }

    size_t header_size = info.has_header ? sizeof(PackedBoardFileHeader) : 0;
    if (file_size < static_cast<std::streamsize>(header_size)) {
        error = "file is too small to contain header";
        return false;
    }

    size_t data_size = static_cast<size_t>(file_size) - header_size;
    if (info.record_size == 0 || data_size % info.record_size != 0) {
        error = "file size is not a multiple of record size";
        return false;
    }

    uint64_t count = 0;
    uint64_t wdl_counts[3] = {0, 0, 0};
    uint64_t result_counts[3] = {0, 0, 0};
    uint64_t invalid_wdl = 0;
    uint64_t invalid_result = 0;
    uint64_t bestmove_nonzero = 0;
    uint64_t depth_total = 0;
    uint8_t depth_min = 255;
    uint8_t depth_max = 0;
    uint16_t ply_min = 65535;
    uint16_t ply_max = 0;

    auto update_common_stats = [&](uint8_t wdl, uint8_t result) {
        if (wdl <= 2) {
            wdl_counts[wdl] += 1;
        } else {
            invalid_wdl += 1;
        }
        if (result <= 2) {
            result_counts[result] += 1;
        } else {
            invalid_result += 1;
        }
    };

    if (info.format == PackedFormat::V1) {
        PackedBoardV1 record{};
        while (in.read(reinterpret_cast<char*>(&record), sizeof(record))) {
            update_common_stats(record.wdl, record.result);
            count += 1;
        }
    } else {
        if (info.has_ply) {
            PackedBoardV2 record{};
            while (in.read(reinterpret_cast<char*>(&record), sizeof(record))) {
                update_common_stats(record.wdl, record.result);
                depth_total += record.depth_reached;
                depth_min = std::min(depth_min, record.depth_reached);
                depth_max = std::max(depth_max, record.depth_reached);
                if (record.bestmove != 0) {
                    bestmove_nonzero += 1;
                }
                ply_min = std::min(ply_min, record.ply);
                ply_max = std::max(ply_max, record.ply);
                count += 1;
            }
        } else {
            PackedBoardV2NoPly record{};
            while (in.read(reinterpret_cast<char*>(&record), sizeof(record))) {
                update_common_stats(record.wdl, record.result);
                depth_total += record.depth_reached;
                depth_min = std::min(depth_min, record.depth_reached);
                depth_max = std::max(depth_max, record.depth_reached);
                if (record.bestmove != 0) {
                    bestmove_nonzero += 1;
                }
                count += 1;
            }
        }
    }

    if (!in.eof()) {
        error = "failed while reading records";
        return false;
    }

    std::cout << "Packed board format: " << (info.format == PackedFormat::V1 ? "v1" : "v2")
              << "\n";
    std::cout << "Records: " << count << "\n";
    std::cout << "WDL distribution: "
              << "loss=" << wdl_counts[0]
              << " draw=" << wdl_counts[1]
              << " win=" << wdl_counts[2];
    if (invalid_wdl > 0) {
        std::cout << " invalid=" << invalid_wdl;
    }
    std::cout << "\n";
    std::cout << "Result distribution: "
              << "loss=" << result_counts[0]
              << " draw=" << result_counts[1]
              << " win=" << result_counts[2];
    if (invalid_result > 0) {
        std::cout << " invalid=" << invalid_result;
    }
    std::cout << "\n";

    if (info.format == PackedFormat::V2) {
        if (count > 0) {
            double depth_avg = static_cast<double>(depth_total) / static_cast<double>(count);
            std::cout << "Depth reached: min=" << static_cast<int>(depth_min)
                      << " max=" << static_cast<int>(depth_max)
                      << " avg=" << depth_avg << "\n";
        }
        std::cout << "Bestmove nonzero: " << bestmove_nonzero << "\n";
        if (info.has_ply && count > 0) {
            std::cout << "Ply: min=" << ply_min
                      << " max=" << ply_max << "\n";
        }
    }

    return true;
}
