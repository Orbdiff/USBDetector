#pragma once
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <winhttp.h>
#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <numeric>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>

struct USBDeviceInfo
{
    std::string name;
    std::string vendor;
    std::string DeviceName;
    std::string VendorName;
    std::string vidpid;
    std::string instanceId;
    std::string connectTime;
    std::string lastRemovalTime;
    std::string status;
    std::string capabilities;
    bool isConnected = false;
};

struct LookupTask
{
    std::string vid;
    std::string pid;
    std::string key;
};

struct USBDetector
{
    std::map<std::string, USBDeviceInfo> deviceCache;
    std::mutex cacheMutex;
    std::queue<LookupTask> lookupQueue;
    std::mutex queueMutex;
    std::condition_variable cv;
    bool done = false;
    std::vector<std::thread> workers;

    std::string WideToUtf8(const std::wstring& w)
    {
        if (w.empty()) return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string s(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), size, nullptr, nullptr);
        return s;
    }

    std::string FileTimeToString(const FILETIME& ft)
    {
        if (!ft.dwLowDateTime && !ft.dwHighDateTime)
            return "";

        SYSTEMTIME utc{}, local{};
        FileTimeToSystemTime(&ft, &utc);
        SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);

        char buf[64];
        sprintf_s(buf, "%04d-%02d-%02d %02d:%02d:%02d",local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond);
        return buf;
    }

    bool GetDevNodeTime(DEVINST devInst, const DEVPROPKEY& key, FILETIME& out)
    {
        DEVPROPTYPE type;
        ULONG size = sizeof(FILETIME);
        memset(&out, 0, sizeof(FILETIME));
        return CM_Get_DevNode_PropertyW(devInst, &key, &type, (PBYTE)&out, &size, 0) == CR_SUCCESS && type == DEVPROP_TYPE_FILETIME;
    }

    std::string CapabilitiesToString(DWORD caps)
    {
        std::vector<std::string> out;

        if (caps & CM_DEVCAP_REMOVABLE)              out.emplace_back("Removable");
        if (caps & CM_DEVCAP_SURPRISEREMOVALOK)      out.emplace_back("SurpriseRemovalOK");
        if (caps & CM_DEVCAP_EJECTSUPPORTED)         out.emplace_back("EjectSupported");
        if (caps & CM_DEVCAP_LOCKSUPPORTED)          out.emplace_back("LockSupported");
        if (caps & CM_DEVCAP_UNIQUEID)               out.emplace_back("UniqueID");
        if (caps & CM_DEVCAP_SILENTINSTALL)          out.emplace_back("SilentInstall");
        if (caps & CM_DEVCAP_RAWDEVICEOK)            out.emplace_back("RawDeviceOK");

        return out.empty() ? "None" : std::accumulate(out.begin(), out.end(), std::string(),
            [](const std::string& a, const std::string& b) { return a.empty() ? b : a + ", " + b; });
    }

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

    void WebLookupWorker()
    {
        try {
            while (true)
            {
                LookupTask task;
                {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    cv.wait(lock, [&]() { return !lookupQueue.empty() || done; });
                    if (lookupQueue.empty() && done) break;
                    task = lookupQueue.front();
                    lookupQueue.pop();
                }

                std::wstring queryPath = L"/view/type/usb/vendor/" + std::wstring(task.vid.begin(), task.vid.end()) + L"/device/" + std::wstring(task.pid.begin(), task.pid.end());
                std::string html = HttpGetRequest(L"devicehunt.com", queryPath);

                std::string deviceName = ExtractHtmlValue(html, "details__heading'>", "</h3><table");
                std::string vendorName = ExtractHtmlValue(html, "details --type-vendor --auto-link\"><h3 class='details__heading'>", "</h3><table");

                USBDeviceInfo info;
                info.DeviceName = deviceName.empty() ? "" : deviceName;
                info.VendorName = vendorName.empty() ? "" : vendorName;

                {
                    std::lock_guard<std::mutex> lock(cacheMutex);
                    deviceCache[task.key] = info;
                }
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Exception in WebLookupWorker: " << e.what() << std::endl; // debug
        }
        catch (...) {
            std::cerr << "Unknown exception in WebLookupWorker" << std::endl; // debug
        }
    }

    bool GetDeviceInfo(SP_DEVINFO_DATA& dev, HDEVINFO& hDevInfo, USBDeviceInfo& deviceInfo, bool isConnected)
    {
        wchar_t buf[512]{};
        std::wstring wInst(MAX_DEVICE_ID_LEN, L'\0');

        if (!SetupDiGetDeviceRegistryPropertyW(hDevInfo, &dev, SPDRP_FRIENDLYNAME, nullptr, (PBYTE)buf, sizeof(buf), nullptr))
            SetupDiGetDeviceRegistryPropertyW(hDevInfo, &dev, SPDRP_DEVICEDESC, nullptr, (PBYTE)buf, sizeof(buf), nullptr);
        deviceInfo.name = WideToUtf8(buf);

        if (deviceInfo.name.find("USB Root Hub") != std::string::npos || deviceInfo.name.find("USB Hub") != std::string::npos)
            return false;

        SetupDiGetDeviceRegistryPropertyW(hDevInfo, &dev, SPDRP_MFG, nullptr, (PBYTE)buf, sizeof(buf), nullptr);
        deviceInfo.vendor = WideToUtf8(buf);

        CM_Get_Device_IDW(dev.DevInst, &wInst[0], MAX_DEVICE_ID_LEN, 0);
        deviceInfo.instanceId = WideToUtf8(wInst);

        size_t v = wInst.find(L"VID_");
        size_t p = wInst.find(L"PID_");
        if (v != std::wstring::npos && p != std::wstring::npos)
            deviceInfo.vidpid = WideToUtf8(wInst.substr(v + 4, 4)) + " / " + WideToUtf8(wInst.substr(p + 4, 4));

        std::string vid = WideToUtf8(wInst.substr(v + 4, 4));
        std::string pid = WideToUtf8(wInst.substr(p + 4, 4));
        std::string key = vid + ":" + pid;

        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            if (deviceCache.find(key) != deviceCache.end())
            {
                deviceInfo.DeviceName = deviceCache[key].DeviceName;
                deviceInfo.VendorName = deviceCache[key].VendorName;
            }
            else
            {
                LookupTask task{ vid, pid, key };
                {
                    std::lock_guard<std::mutex> qLock(queueMutex);
                    lookupQueue.push(task);
                }
                deviceInfo.DeviceName = deviceInfo.name;  // Fallback
                deviceInfo.VendorName = deviceInfo.vendor;  // Fallback
            }
        }

        FILETIME ft{};
        if (GetDevNodeTime(dev.DevInst, DEVPKEY_Device_LastArrivalDate, ft))
            deviceInfo.connectTime = FileTimeToString(ft);

        if (GetDevNodeTime(dev.DevInst, DEVPKEY_Device_LastRemovalDate, ft))
            deviceInfo.lastRemovalTime = FileTimeToString(ft);

        ULONG status = 0, problem = 0;
        CM_Get_DevNode_Status(&status, &problem, dev.DevInst, 0);
        deviceInfo.status = (status & DN_HAS_PROBLEM) ? "No" : "Yes";

        DWORD caps = 0;
        ULONG size = sizeof(DWORD);
        CM_Get_DevNode_Registry_PropertyW(dev.DevInst, CM_DRP_CAPABILITIES, nullptr, &caps, &size, 0);
        deviceInfo.capabilities = CapabilitiesToString(caps);

        deviceInfo.isConnected = isConnected;

        return true;
    }

    std::vector<USBDeviceInfo> GetDevices()
    {
        workers.clear();
        done = false;

        HDEVINFO hDevInfoPresent = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
        HDEVINFO hDevInfoAll = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES);

        SP_DEVINFO_DATA dev{};
        dev.cbSize = sizeof(dev);

        std::set<std::string> presentInstanceIds;

        const int numThreads = 4;
        for (int i = 0; i < numThreads; ++i)
        {
            workers.emplace_back(&USBDetector::WebLookupWorker, this);
        }

        int index = 0;
        while (SetupDiEnumDeviceInfo(hDevInfoPresent, index++, &dev))
        {
            std::wstring wInst(MAX_DEVICE_ID_LEN, L'\0');
            CM_Get_Device_IDW(dev.DevInst, &wInst[0], MAX_DEVICE_ID_LEN, 0);
            presentInstanceIds.insert(WideToUtf8(wInst));
        }

        std::vector<USBDeviceInfo> devices;
        index = 0;
        while (SetupDiEnumDeviceInfo(hDevInfoAll, index++, &dev))
        {
            std::wstring wInst(MAX_DEVICE_ID_LEN, L'\0');
            CM_Get_Device_IDW(dev.DevInst, &wInst[0], MAX_DEVICE_ID_LEN, 0);
            std::string instanceId = WideToUtf8(wInst);
            bool isConnected = presentInstanceIds.count(instanceId) > 0;

            USBDeviceInfo deviceInfo;
            if (GetDeviceInfo(dev, hDevInfoAll, deviceInfo, isConnected))
            {
                devices.push_back(deviceInfo);
            }
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            done = true;
        }
        cv.notify_all();

        for (auto& worker : workers)
        {
            worker.join();
        }

        workers.clear();

        for (auto& deviceInfo : devices)
        {
            size_t v = deviceInfo.instanceId.find("VID_");
            size_t p = deviceInfo.instanceId.find("PID_");
            if (v != std::string::npos && p != std::string::npos)
            {
                std::string vid = deviceInfo.instanceId.substr(v + 4, 4);
                std::string pid = deviceInfo.instanceId.substr(p + 4, 4);
                std::string key = vid + ":" + pid;

                std::lock_guard<std::mutex> lock(cacheMutex);
                if (deviceCache.find(key) != deviceCache.end())
                {
                    if (!deviceCache[key].DeviceName.empty())
                        deviceInfo.DeviceName = deviceCache[key].DeviceName;
                    if (!deviceCache[key].VendorName.empty())
                        deviceInfo.VendorName = deviceCache[key].VendorName;
                }
            }
        }

        SetupDiDestroyDeviceInfoList(hDevInfoPresent);
        SetupDiDestroyDeviceInfoList(hDevInfoAll);

        return devices;
    }
};