// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 11 / 13 / 2026
//
// Updated At  : 11 / 13 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : manifest.cpp
//
// Explanation : Reads YAML manifests and validates program
//               metadata and local file copy steps.
//
// =============================================

#include "zeqvorn/zf/manifest.hpp"

#include <yaml-cpp/yaml.h>

#include <array>
#include <fstream>
#include <set>
#include <stdexcept>
#include <utility>

namespace zeqvorn::zf
{
    namespace
    {
        // YAML 모듈 안에서 필드 이름을 포함한 진단을 만든다.
        // 공개 함수는 예외를 기존 Zeqvorn Result 형식으로 변환한다.
        YAML::Node mapping(const YAML::Node& node, const std::string& field)
        {
            if (!node.IsDefined() || !node.IsMap())
            {
                throw std::invalid_argument(field + ": expected a mapping");
            }

            std::set<std::string> keys;
            for (const auto& pair : node)
            {
                if (!pair.first.IsScalar())
                {
                    throw std::invalid_argument(field + ": expected a scalar key");
                }

                if (!keys.insert(pair.first.Scalar()).second)
                {
                    throw std::invalid_argument(
                        field + ": duplicate key " + pair.first.Scalar());
                }
            }

            return node;
        }

        std::string scalar(const YAML::Node& node, const std::string& field)
        {
            if (!node.IsDefined() || !node.IsScalar() || node.Scalar().empty())
            {
                throw std::invalid_argument(field + ": expected a non-empty scalar");
            }

            const auto& value = node.Scalar();
            if (value.find('\0') != std::string::npos)
            {
                throw std::invalid_argument(field + ": contains a NUL byte");
            }

            return value;
        }

        // Linux 경로는 '/' 단위로 검사해 호스트 OS와 같은 결과를 낸다.
        // 복사 원본은 매니페스트 디렉터리 기준이며 "./"를 허용한다.
        bool valid_file_path(std::string_view path, bool absolute)
        {
            if (path.starts_with('/') != absolute ||
                path.find_first_of("\\:") != std::string_view::npos)
            {
                return false;
            }

            if (absolute) path.remove_prefix(1);
            else if (path.starts_with("./")) path.remove_prefix(2);

            for (;;)
            {
                const auto slash = path.find('/');
                const auto part = path.substr(0, slash);
                if (part.empty() || part == "." || part == "..") return false;
                if (slash == std::string_view::npos) return true;
                path.remove_prefix(slash + 1);
            }
        }
    }

    Result<Manifest> parse_manifest(std::string_view yaml)
    {
        try
        {
            const auto documents = YAML::LoadAll(std::string(yaml));
            if (documents.size() != 1)
            {
                return Error(ErrorCode::InvalidArgument, "Expected one YAML document");
            }

            const auto root = mapping(documents.front(), "manifest");
            const auto version = root["schema-version"];
            if (scalar(version, "schema-version") != "1" ||
                (version.Tag() != "?" && version.Tag() != "tag:yaml.org,2002:int"))
            {
                return Error(ErrorCode::InvalidArgument, "schema-version: expected integer 1");
            }

            const auto program = mapping(root["program"], "program");
            const auto build = mapping(root["build"], "build");

            Manifest manifest;
            manifest.program.id = scalar(program["id"], "program.id");
            manifest.program.version = scalar(program["version"], "program.version");
            manifest.program.name = scalar(program["name"], "program.name");
            manifest.program.description = scalar(program["description"], "program.description");
            manifest.program.executable = scalar(program["executable"], "program.executable");

            if (!valid_file_path(manifest.program.executable, true))
            {
                return Error(ErrorCode::InvalidArgument,
                    "program.executable: expected an absolute Linux file path");
            }

            manifest.platform = scalar(build["platform"], "build.platform");
            if (manifest.platform != "linux/arm64" && manifest.platform != "linux/amd64")
            {
                return Error(ErrorCode::Unsupported,
                    "build.platform: expected linux/arm64 or linux/amd64");
            }

            const auto steps = build["steps"];
            if (!steps.IsDefined() || !steps.IsSequence() || steps.size() == 0)
            {
                return Error(ErrorCode::InvalidArgument,
                    "build.steps: expected at least one copy step");
            }

            // 같은 목적지에 쓰는 단계가 둘이면 어느 파일을 넣을지 모호하다.
            std::set<std::string> destinations;
            for (std::size_t i = 0; i < steps.size(); ++i)
            {
                const auto field = "build.steps[" + std::to_string(i) + "]";
                const auto step = mapping(steps[i], field);
                if (scalar(step["type"], field + ".type") != "copy")
                {
                    return Error(ErrorCode::Unsupported, field + ".type: expected copy");
                }

                CopyStep copy{
                    scalar(step["from"], field + ".from"),
                    scalar(step["to"], field + ".to"),
                    scalar(step["mode"], field + ".mode")
                };

                if (!valid_file_path(copy.from, false))
                {
                    return Error(ErrorCode::InvalidArgument,
                        field + ".from: expected a relative path");
                }

                if (!valid_file_path(copy.to, true))
                {
                    return Error(ErrorCode::InvalidArgument,
                        field + ".to: expected an absolute Linux file path");
                }

                if (copy.mode.size() != 4 || copy.mode.front() != '0' ||
                    copy.mode.find_first_not_of("01234567") != std::string::npos)
                {
                    return Error(ErrorCode::InvalidArgument,
                        field + ".mode: expected octal mode such as 0755");
                }

                if (!destinations.insert(copy.to).second)
                {
                    return Error(ErrorCode::AlreadyExists,
                        field + ".to: duplicate destination " + copy.to);
                }

                manifest.steps.push_back(std::move(copy));
            }

            return manifest;
        }
        catch (const YAML::Exception& error)
        {
            return Error(ErrorCode::InvalidArgument, error.what());
        }
        catch (const std::invalid_argument& error)
        {
            return Error(ErrorCode::InvalidArgument, error.what());
        }
    }

    Result<Manifest> load_manifest(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return Error(ErrorCode::IoError, "Cannot open manifest");

        std::string yaml;
        std::array<char, 4096> buffer;
        while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0)
        {
            yaml.append(buffer.data(), static_cast<std::size_t>(file.gcount()));
        }

        if (file.bad() || !file.eof())
        {
            return Error(ErrorCode::IoError, "Cannot read manifest");
        }

        return parse_manifest(yaml);
    }
}