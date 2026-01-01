#include <windows.h>
#include <setupapi.h>
#include <winhttp.h>
#include <iostream>
#include <vector>
#include <string>
#include <map>

struct UsbDeviceInfo
{
    std::string vid;
    std::string pid;
    std::string DeviceName;
    std::string VendorName;
    int count = 1;
};

std::string StringTrim(const std::string& str)
{
    const char* whitespace = " \t\n\r\f\v";
    size_t start = str.find_first_not_of(whitespace);
    size_t end = str.find_last_not_of(whitespace);
    if (start == std::string::npos || end == std::string::npos)
        return "";
    return str.substr(start, end - start + 1);
}

std::string ExtractHtmlValue(const std::string& html, const std::string& begin, const std::string& end)
{
    size_t posStart = html.find(begin);
    if (posStart == std::string::npos) return "";
    posStart += begin.length();
    size_t posEnd = html.find(end, posStart);
    if (posEnd == std::string::npos) return "";
    return StringTrim(html.substr(posStart, posEnd - posStart));
}

std::string WideToUtf8(const std::wstring& wstr)
{
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], size, nullptr, nullptr);
    return result;
}

std::string HttpGetRequest(const std::wstring& host, const std::wstring& path)
{
    std::string response;
    HINTERNET session = WinHttpOpen(L"USBDetector", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return "";

    HINTERNET connection = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr))
    {
        DWORD bytesAvailable = 0;
        while (WinHttpQueryDataAvailable(request, &bytesAvailable) && bytesAvailable > 0)
        {
            std::vector<char> buffer(bytesAvailable);
            DWORD bytesRead = 0;
            WinHttpReadData(request, buffer.data(), bytesAvailable, &bytesRead);
            response.append(buffer.data(), bytesRead);
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    return response;
}

std::vector<UsbDeviceInfo> EnumerateUsbDevices()
{
    std::vector<UsbDeviceInfo> devices;
    HDEVINFO devInfoSet = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (devInfoSet == INVALID_HANDLE_VALUE) return devices;

    SP_DEVINFO_DATA devInfo{};
    devInfo.cbSize = sizeof(devInfo);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfoSet, i, &devInfo); i++)
    {
        wchar_t hwid[1024]{};
        DWORD reqSize = 0;
        if (!SetupDiGetDeviceRegistryPropertyW(devInfoSet, &devInfo, SPDRP_HARDWAREID, nullptr, reinterpret_cast<PBYTE>(hwid), sizeof(hwid), &reqSize))
            continue;

        std::wstring strHwid = hwid;
        size_t vidPos = strHwid.find(L"VID_");
        size_t pidPos = strHwid.find(L"PID_");
        if (vidPos == std::wstring::npos || pidPos == std::wstring::npos) continue;

        UsbDeviceInfo usb{};
        usb.vid = WideToUtf8(strHwid.substr(vidPos + 4, 4));
        usb.pid = WideToUtf8(strHwid.substr(pidPos + 4, 4));
        devices.push_back(usb);
    }

    SetupDiDestroyDeviceInfoList(devInfoSet);
    return devices;
}

int main()
{
    SetConsoleTitleA("USB Detector by Diff uwu");
    std::cout << "Hello <')))><\n\n";

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    auto usbDevices = EnumerateUsbDevices();

    std::map<std::string, UsbDeviceInfo> deviceCache;

    for (auto& device : usbDevices)
    {
        std::string key = device.vid + ":" + device.pid;

        if (deviceCache.find(key) != deviceCache.end())
        {
            deviceCache[key].count++;
            continue;
        }

        std::wstring queryPath = L"/view/type/usb/vendor/" + std::wstring(device.vid.begin(), device.vid.end()) + L"/device/" + std::wstring(device.pid.begin(), device.pid.end());
        std::string html = HttpGetRequest(L"devicehunt.com", queryPath);

        device.DeviceName = ExtractHtmlValue(html, "details__heading'>", "</h3><table");
        device.VendorName = ExtractHtmlValue(html, "details --type-vendor --auto-link\"><h3 class='details__heading'>", "</h3><table");

        if (device.DeviceName.empty()) device.DeviceName = "Unknown device";
        if (device.VendorName.empty()) device.VendorName = "Unknown vendor";

        deviceCache[key] = device;
    }

    std::vector<UsbDeviceInfo> known, unknown;
    for (const auto& pair : deviceCache)
    {
        if (pair.second.DeviceName == "Unknown device" || pair.second.VendorName == "Unknown vendor")
            unknown.push_back(pair.second);
        else
            known.push_back(pair.second);
    }

    for (const auto& d : known)
    {
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "[+] USB Detected (" << d.count << ")\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "    Device : ";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << d.DeviceName << "\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "    Vendor : " << d.VendorName << "\n";
        std::cout << "    VID    : " << d.vid << "\n";
        std::cout << "    PID    : " << d.pid << "\n\n";
    }

    if (!unknown.empty())
    {
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
        for (const auto& d : unknown)
        {
            std::cout << "[!] Unable to get " << d.vid << " + " << d.pid;
            if (d.count > 1) std::cout << " (" << d.count << " devices)\n";
        }
    }

    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    system("pause");
    return 0;
}
