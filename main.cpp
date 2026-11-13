// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 11 / 11 / 2026
//
// Updated At  : 11 / 13 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : main.cpp
//
// Explanation : Compresses files or directories and
//               bundles Zeqvorn packages as gzip-compressed tar archives.
//
// =============================================

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{
    void check_archive(int status, archive* writer, const char* operation)
    {
        if (status == ARCHIVE_OK) return;

        const char* message = archive_error_string(writer);
        throw std::runtime_error(std::string(operation) + ": " +
            (message ? message : "unknown archive error"));
    }

    /** @brief Writes one regular file or directory using a relative tar path. */
    void write_entry(archive* writer, const fs::path& source, const fs::path& name)
    {
        const auto status = fs::symlink_status(source);
        const bool is_directory = fs::is_directory(status);
        if (!is_directory && !fs::is_regular_file(status))
        {
            throw std::runtime_error("Unsupported input: " + source.string());
        }

        std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(
            archive_entry_new(), archive_entry_free);
        if (!entry) throw std::runtime_error("Cannot allocate a tar entry.");

        const auto size = is_directory ? 0 : fs::file_size(source);
        if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max()))
        {
            throw std::runtime_error("File is too large: " + source.string());
        }

        // tar 경로는 '/' 구분자를 사용하고, 입력의 UTF-8 바이트를 보존한다.
        const auto utf8_name = name.generic_u8string();
        archive_entry_set_pathname(entry.get(),
            reinterpret_cast<const char*>(utf8_name.c_str()));
        archive_entry_set_filetype(entry.get(), is_directory ? AE_IFDIR : AE_IFREG);
        archive_entry_set_perm(entry.get(), static_cast<int>(status.permissions() & fs::perms::all));
        archive_entry_set_size(entry.get(), static_cast<std::int64_t>(size));
        check_archive(archive_write_header(writer, entry.get()), writer, "Write tar header");

        if (!is_directory)
        {
            std::ifstream input(source, std::ios::binary);
            if (!input) throw std::runtime_error("Cannot read: " + source.string());

            // 큰 파일도 메모리에 한 번에 올리지 않고 나눠서 압축한다.
            std::array<char, 64 * 1024> buffer{};
            auto remaining = size;
            while (remaining > 0)
            {
                const auto count = std::min<std::uintmax_t>(remaining, buffer.size());
                input.read(buffer.data(), static_cast<std::streamsize>(count));
                if (!input) throw std::runtime_error("Cannot read full file: " + source.string());

                const auto written = archive_write_data(writer, buffer.data(), count);
                if (written < 0) check_archive(ARCHIVE_FATAL, writer, "Write file data");
                if (static_cast<std::uintmax_t>(written) != count)
                {
                    throw std::runtime_error("Incomplete tar entry: " + source.string());
                }
                remaining -= count;
            }
        }

        check_archive(archive_write_finish_entry(writer), writer, "Finish tar entry");
    }

    // 두 명령 모두 같은 방식으로 아카이브를 닫고, 실패 시
    // 이 호출에서 생성한 불완전한 출력만 지운다.
    template <typename WriteEntries>
    void write_archive(const fs::path& destination, WriteEntries write_entries)
    {
        // 배타적 생성으로 기존 출력 파일을 덮어쓰지 않는다.
#ifdef _WIN32
        const int fd = _wopen(destination.c_str(),
            _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
        const auto close_fd = _close;
#else
        const int fd = open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
        const auto close_fd = close;
#endif
        if (fd < 0) throw std::system_error(errno, std::generic_category(), "Create output");

        bool fd_open = true;
        try
        {
            std::unique_ptr<archive, decltype(&archive_write_free)> writer(
                archive_write_new(), archive_write_free);
            if (!writer) throw std::runtime_error("Cannot allocate an archive writer.");

            auto* handle = writer.get();
            check_archive(archive_write_set_format_pax_restricted(handle), handle, "Select tar");
            // libarchive가 로캘에 따라 파일명을 변환하거나 정규화하지 않게 한다.
            check_archive(archive_write_set_format_option(handle, "pax", "hdrcharset", "BINARY"),
                handle, "Preserve entry names");
            check_archive(archive_write_add_filter_gzip(handle), handle, "Enable gzip");
            check_archive(archive_write_open_fd(handle, fd), handle, "Open archive");

            write_entries(handle);

            check_archive(archive_write_close(handle), handle, "Finish archive");
            // libarchive는 전달받은 파일 디스크립터를 닫지 않는다.
            const int close_status = close_fd(fd);
            fd_open = false;
            if (close_status != 0)
            {
                throw std::system_error(errno, std::generic_category(), "Close output");
            }
        }
        catch (...)
        {
            if (fd_open) close_fd(fd);
            std::error_code ignored;
            fs::remove(destination, ignored);
            throw;
        }
    }

    void compress(const fs::path& input, const fs::path& output)
    {
        const auto status = fs::symlink_status(input);
        const bool is_directory = fs::is_directory(status);
        if (!is_directory && !fs::is_regular_file(status))
        {
            throw std::runtime_error("Input must be a regular file or directory.");
        }

        const auto source = fs::canonical(input);
        const auto destination = fs::absolute(output);
        if (is_directory)
        {
            // 상위 폴더의 실제 파일 시스템 위치를 비교한다.
            // 입력 안에 출력하면 아카이브가 자기 자신을 포함하게 된다.
            auto parent = fs::canonical(destination.parent_path());
            for (;;)
            {
                if (fs::equivalent(source, parent))
                {
                    throw std::runtime_error("Output must be outside the input directory.");
                }
                if (parent == parent.root_path()) break;
                parent = parent.parent_path();
            }
        }

        write_archive(destination, [&](archive* writer) {
            if (is_directory)
            {
                // 입력 디렉터리 자체 대신 그 아래의 상대 경로만 기록한다.
                for (const auto& child : fs::recursive_directory_iterator(source))
                {
                    write_entry(writer, child.path(), child.path().lexically_relative(source));
                }
            }
            else
            {
                write_entry(writer, source, source.filename());
            }
        });
    }

    /** @brief Packs a manifest and a compressed payload as two opaque files. */
    void pack(const fs::path& manifest, const fs::path& payload, const fs::path& output)
    {
        if (manifest.extension() != ".zf" || !fs::is_regular_file(fs::symlink_status(manifest)))
        {
            throw std::runtime_error("Manifest must be a regular .zf file.");
        }
        if (!payload.filename().string().ends_with(".tar.gz") ||
            !fs::is_regular_file(fs::symlink_status(payload)))
        {
            throw std::runtime_error("Payload must be a regular .tar.gz file.");
        }
        if (output.extension() != ".zf")
        {
            throw std::runtime_error("Package filename must end with .zf.");
        }

        // 원본 파일의 이름과 바이트를 보존한다. 입력의 디렉터리 경로는
        // 기록하지 않으며, 내부 tar.gz도 풀지 않고 하나의 파일로 저장한다.
        write_archive(fs::absolute(output), [&](archive* writer) {
            write_entry(writer, manifest, manifest.filename());
            write_entry(writer, payload, payload.filename());
        });
    }
}

int main(int argc, char* argv[])
{
    const bool pack_command = argc == 5 && std::string_view(argv[1]) == "pack";
    if (argc != 3 && !pack_command)
    {
        std::cerr << "Usage: ZeqFile <input> <output.tar.gz>\n"
                  << "       ZeqFile pack <manifest.zf> <payload.tar.gz> <output.zf>\n";
        return 2;
    }

    try
    {
        const fs::path output = pack_command ? argv[4] : argv[2];
        if (pack_command)
        {
            pack(argv[2], argv[3], output);
        }
        else
        {
            if (!output.filename().string().ends_with(".tar.gz"))
            {
                throw std::runtime_error("Output filename must end with .tar.gz.");
            }
            compress(argv[1], output);
        }

        std::cout << "Created " << output << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ZeqFile: " << error.what() << '\n';
        return 1;
    }
}
