// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 11 / 14 / 2026
//
// Updated At  : 11 / 16 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : package.cpp
//
// Explanation : Checks the ZeqFile archive structure
//               and reads its YAML manifest in memory.
//
// =============================================

#include "zeqvorn/zf/package.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace zeqvorn::zf
{
    namespace
    {
        constexpr std::size_t max_manifest_bytes = 1024 * 1024;
        constexpr std::size_t archive_block_bytes = 64 * 1024;

        /** @brief Copies libarchive's diagnostic into a Zeqvorn error. */
        Error archive_error(archive* reader, ErrorCode code, std::string operation)
        {
            if (const char* detail = archive_error_string(reader))
            {
                operation += ": ";
                operation += detail;
            }
            return Error(code, operation);
        }

        /** @brief Reads the current YAML entry after checking its size. */
        Result<std::string> read_manifest(archive* reader, archive_entry* entry)
        {
            const auto size = archive_entry_size(entry);
            if (!archive_entry_size_is_set(entry) || size < 0)
            {
                return Error(ErrorCode::InvalidArgument, "Manifest size is missing or invalid");
            }
            if (size > static_cast<la_int64_t>(max_manifest_bytes))
            {
                return Error(ErrorCode::ResourceExhausted, "Manifest exceeds 1 MiB");
            }

            // 헤더의 크기를 확인한 뒤에만 메모리를 할당한다. 한 번의
            // archive_read_data 호출이 전체 파일을 반환할 필요는 없다.
            std::string yaml(static_cast<std::size_t>(size), '\0');
            std::size_t offset = 0;
            while (offset < yaml.size())
            {
                const auto count = archive_read_data(
                    reader, yaml.data() + offset, yaml.size() - offset);
                if (count < 0)
                {
                    return archive_error(reader, ErrorCode::IoError, "Cannot read manifest");
                }
                if (count == 0)
                {
                    return Error(ErrorCode::InvalidArgument, "Manifest ended before its declared size");
                }
                offset += static_cast<std::size_t>(count);
            }
            return yaml;
        }
    }

    Result<Manifest> load_package_manifest(const std::filesystem::path& path)
    {
        // libarchive에서 빈 파일명은 표준 입력을 의미한다.
        if (path.empty())
        {
            return Error(ErrorCode::InvalidArgument, "Package path is empty");
        }

        std::unique_ptr<archive, decltype(&archive_read_free)> handle(
            archive_read_new(), archive_read_free);
        if (!handle)
        {
            return Error(ErrorCode::ResourceExhausted, "Cannot allocate package reader");
        }
        auto* reader = handle.get();
        if (archive_read_support_filter_gzip(reader) != ARCHIVE_OK ||
            archive_read_support_format_tar(reader) != ARCHIVE_OK)
        {
            return archive_error(reader, ErrorCode::Unsupported, "Cannot enable gzip tar support");
        }

        // Windows에서는 파일 경로를 유니코드 네이티브 형식으로 넘긴다.
#ifdef _WIN32
        const int opened = archive_read_open_filename_w(reader, path.c_str(), archive_block_bytes);
#else
        const int opened = archive_read_open_filename(reader, path.c_str(), archive_block_bytes);
#endif
        if (opened != ARCHIVE_OK)
        {
            return archive_error(reader, ErrorCode::IoError, "Cannot open package");
        }

        // 필터 목록에는 gzip 디코더와 입력 스트림 자체가 들어 있다.
        if (archive_filter_count(reader) != 2 ||
            archive_filter_code(reader, 0) != ARCHIVE_FILTER_GZIP)
        {
            return Error(ErrorCode::InvalidArgument, "Package must use one gzip filter");
        }

        bool manifest_found = false;
        bool payload_found = false;
        std::string yaml;
        archive_entry* entry = nullptr;
        for (;;)
        {
            const int status = archive_read_next_header(reader, &entry);
            if (status == ARCHIVE_EOF) break;
            if (status != ARCHIVE_OK)
            {
                return archive_error(reader, ErrorCode::InvalidArgument, "Cannot read package entry");
            }

            // ZeqFile은 이름을 원래 바이트로 기록한다. 접미사와 경로
            // 구분자만 비교하므로 파일명을 로캘에 따라 변환하지 않는다.
            const char* pathname = archive_entry_pathname(entry);
            const std::string_view name = pathname ? pathname : "";
            if (name.empty() || name.find_first_of("/\\:") != std::string_view::npos ||
                archive_entry_filetype(entry) != AE_IFREG ||
                archive_entry_hardlink(entry) || archive_entry_symlink(entry) ||
                archive_entry_sparse_count(entry) != 0)
            {
                return Error(ErrorCode::InvalidArgument,
                    "Package entries must be top-level regular files");
            }

            if (name.size() > 3 && name.ends_with(".zf"))
            {
                if (manifest_found)
                {
                    return Error(ErrorCode::InvalidArgument, "Package contains multiple manifests");
                }
                auto result = read_manifest(reader, entry);
                if (!result) return result.error();

                yaml.swap(result.value());
                manifest_found = true;
            }
            else if (name.ends_with(".tar.gz"))
            {
                if (payload_found)
                {
                    return Error(ErrorCode::InvalidArgument, "Package contains multiple payloads");
                }
                // 내부 아카이브는 이 단계에서 열지 않는다. 다음 외부
                // 헤더까지 읽어 나머지 항목의 구조도 확인한다.
                if (archive_read_data_skip(reader) != ARCHIVE_OK)
                {
                    return archive_error(reader, ErrorCode::IoError, "Cannot skip payload");
                }
                payload_found = true;
            }
            else
            {
                return Error(ErrorCode::InvalidArgument, "Unexpected package entry: " + std::string(name));
            }
        }

        if (!manifest_found || !payload_found)
        {
            return Error(ErrorCode::InvalidArgument, "Package needs one .zf manifest and one .tar.gz payload");
        }
        if (archive_read_close(reader) != ARCHIVE_OK)
        {
            return archive_error(reader, ErrorCode::IoError, "Cannot close package");
        }

        return parse_manifest(yaml);
    }
}
