#include "platform/FileAssociation.h"

#include "platform/Paths.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

#include <algorithm>
#include <cctype>
#include <vector>

namespace dd::platform {

namespace {

constexpr const wchar_t* kProgId = L"davedown.Markdown";
constexpr const wchar_t* kAppName = L"davedown";
const wchar_t* const kExtensions[] = { L".md", L".markdown", L".mdown", L".mkd" };

std::wstring widen(const std::string& s)
{
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

std::wstring exePath()
{
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    return std::wstring(buf, n);
}

bool setValue(HKEY root, const std::wstring& key, const wchar_t* name, const std::wstring& value, std::string& error)
{
    HKEY h = nullptr;
    LONG rc = RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr);
    if (rc != ERROR_SUCCESS) {
        error = "Could not create registry key (error " + std::to_string(rc) + ").";
        return false;
    }
    rc = RegSetValueExW(h, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                        (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    if (rc != ERROR_SUCCESS) {
        error = "Could not write registry value (error " + std::to_string(rc) + ").";
        return false;
    }
    return true;
}

} // namespace

bool fileAssociationSupported() { return true; }

bool registerMarkdownHandler(std::string& error)
{
    const std::wstring exe = exePath();
    const std::wstring quoted = L"\"" + exe + L"\"";
    const std::wstring command = quoted + L" \"%1\"";
    const std::wstring icon = quoted + L",0";
    const std::wstring classes = L"Software\\Classes\\";

    // The ProgID: what a file "is" when davedown handles it.
    const std::wstring prog = classes + kProgId;
    if (!setValue(HKEY_CURRENT_USER, prog, nullptr, L"Markdown Document", error)) return false;
    if (!setValue(HKEY_CURRENT_USER, prog, L"FriendlyTypeName", L"Markdown Document", error)) return false;
    if (!setValue(HKEY_CURRENT_USER, prog + L"\\DefaultIcon", nullptr, icon, error)) return false;
    if (!setValue(HKEY_CURRENT_USER, prog + L"\\shell\\open", nullptr, L"Open with davedown", error)) return false;
    if (!setValue(HKEY_CURRENT_USER, prog + L"\\shell\\open\\command", nullptr, command, error)) return false;

    // Application entry: "Open with" list and per-application defaults.
    const std::wstring app = classes + L"Applications\\davedown.exe";
    if (!setValue(HKEY_CURRENT_USER, app, L"FriendlyAppName", kAppName, error)) return false;
    if (!setValue(HKEY_CURRENT_USER, app + L"\\shell\\open\\command", nullptr, command, error)) return false;
    if (!setValue(HKEY_CURRENT_USER, app + L"\\DefaultIcon", nullptr, icon, error)) return false;

    // Capabilities: what Settings > Default apps lists.
    const std::wstring caps = L"Software\\davedown\\Capabilities";
    if (!setValue(HKEY_CURRENT_USER, caps, L"ApplicationName", kAppName, error)) return false;
    if (!setValue(HKEY_CURRENT_USER, caps, L"ApplicationDescription", L"Markdown viewer and editor", error)) return false;
    if (!setValue(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", kAppName, caps, error)) return false;

    for (const wchar_t* ext : kExtensions) {
        if (!setValue(HKEY_CURRENT_USER, classes + ext + L"\\OpenWithProgids", kProgId, L"", error)) return false;
        if (!setValue(HKEY_CURRENT_USER, app + L"\\SupportedTypes", ext, L"", error)) return false;
        if (!setValue(HKEY_CURRENT_USER, caps + L"\\FileAssociations", ext, kProgId, error)) return false;
        // Only claim the extension's default ProgID when nothing owns it yet;
        // an existing choice belongs to the person, not to us.
        HKEY probe = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, (classes + ext).c_str(), 0, KEY_QUERY_VALUE, &probe) == ERROR_SUCCESS) {
            wchar_t existing[256] = {};
            DWORD size = sizeof(existing);
            const bool has = RegQueryValueExW(probe, nullptr, nullptr, nullptr,
                                              reinterpret_cast<BYTE*>(existing), &size) == ERROR_SUCCESS && existing[0];
            RegCloseKey(probe);
            if (has) continue;
        }
        if (!setValue(HKEY_CURRENT_USER, classes + ext, nullptr, kProgId, error)) return false;
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSH, nullptr, nullptr);
    error.clear();
    return true;
}

bool isDefaultMarkdownHandler()
{
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD n = (DWORD)(sizeof(buf) / sizeof(buf[0]));
    if (FAILED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L".md", L"open", buf, &n))) return false;
    std::wstring found(buf), mine = exePath();
    auto lower = [](std::wstring& s) { for (wchar_t& c : s) c = (wchar_t)towlower(c); };
    lower(found);
    lower(mine);
    return found == mine;
}

bool promptMakeDefault(std::string& error)
{
    if (!registerMarkdownHandler(error)) return false;
    // Windows 10/11 keep the per-user default in a hash-protected key that
    // only the Settings app may write, so the supported path is to open our
    // page there, where "Set default" assigns every registered extension.
    const HINSTANCE h = ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps?registeredAppUser=davedown",
                                      nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(h) <= 32) {
        error = "Windows could not open Settings > Default apps.";
        return false;
    }
    error.clear();
    return true;
}

} // namespace dd::platform

#else

namespace dd::platform {
bool fileAssociationSupported() { return false; }
bool registerMarkdownHandler(std::string& error) { error = "Not available on this platform."; return false; }
bool promptMakeDefault(std::string& error) { error = "Not available on this platform."; return false; }
bool isDefaultMarkdownHandler() { return false; }
} // namespace dd::platform

#endif
