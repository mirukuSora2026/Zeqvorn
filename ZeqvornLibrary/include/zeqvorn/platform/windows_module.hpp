// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 miruku_sora2026

// =============================================
//
// Created At  : 11 / 12 / 2026
//
// Updated At  : 11 / 12 / 2026
//
// Created By  : miruku_sora2026
//
// File Name   : windows_module.hpp
//
// Explanation : Loads Windows system DLLs and owns
//               their module references.
//
// =============================================

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <windows.h>


#include "zeqvorn/base/result.hpp"

namespace zeqvorn
{
    /** @brief Releases a reference obtained from LoadLibraryExW(). */
    struct ModuleDeleter
    {
        void operator()(HMODULE module) const noexcept
        {
            (void)::FreeLibrary(module);
        }
    };

    /**
     * @brief Owns one reference to a loaded DLL.
     *
     * Keep the owner alive while using pointers obtained by GetProcAddress().
     */
    using UniqueModule = std::unique_ptr<
        std::remove_pointer_t<HMODULE>,
        ModuleDeleter
    >;

    namespace detail
    {
        /** @brief Converts a saved Win32 loader error into a common error. */
        [[nodiscard]]
        inline Error system_module_error(const char* operation, DWORD saved)
        {
            ErrorCode code = ErrorCode::SystemError;

            switch (saved)
            {
                case ERROR_FILE_NOT_FOUND:
                case ERROR_PATH_NOT_FOUND:
                case ERROR_MOD_NOT_FOUND:
                    code = ErrorCode::NotFound;
                    break;

                case ERROR_ACCESS_DENIED:
                case ERROR_PRIVILEGE_NOT_HELD:
                    code = ErrorCode::PermissionDenied;
                    break;

                case ERROR_NOT_ENOUGH_MEMORY:
                case ERROR_OUTOFMEMORY:
                    code = ErrorCode::ResourceExhausted;
                    break;
            }

            return Error(
                code,
                operation,
                {static_cast<int>(saved), std::system_category()}
            );
        }
    }

    /**
     * @brief Loads a DLL from the Windows system directory.
     * @param name A filename such as WinHvPlatform.dll.
     * @return The module owner, or an error containing the Win32 code.
     *
     * 호출자는 파일명만 넘긴다. 시스템 디렉터리에서 절대경로를 만들고
     * 의존 DLL 검색도 시스템 디렉터리로 제한한다.
     */
    [[nodiscard]]
    inline Result<UniqueModule> load_system_module(const wchar_t* name)
    {
        const std::wstring_view filename =
            name ? std::wstring_view(name) : std::wstring_view{};

        if (filename.empty() || filename == L"." || filename == L".." ||
            filename.find_first_of(L"\\/:") != std::wstring_view::npos)
        {
            return Error(ErrorCode::InvalidArgument, "Expected a system DLL filename");
        }

        std::wstring path(MAX_PATH, L'\0');

        for (;;)
        {
            const UINT length = ::GetSystemDirectoryW(
                path.data(), static_cast<UINT>(path.size())
            );

            if (length == 0)
            {
                const DWORD saved = ::GetLastError();
                return detail::system_module_error("GetSystemDirectoryW failed", saved);
            }

            if (length < path.size())
            {
                path.resize(length);
                break;
            }

            path.resize(static_cast<std::size_t>(length) + 1);
        }

        if (path.back() != L'\\')
        {
            path.push_back(L'\\');
        }

        path.append(filename);

        const HMODULE module = ::LoadLibraryExW(
            path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32
        );

        if (!module)
        {
            const DWORD saved = ::GetLastError();
            return detail::system_module_error("LoadLibraryExW failed", saved);
        }

        return UniqueModule(module);
    }
}