#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winhttp.h>
#include <winioctl.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <random>
#include <ctime>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")

const std::wstring SERVER_IP = L"176.97.210.71";
const INTERNET_PORT SERVER_PORT = 3000;
const LPCWSTR USER_AGENT = NULL;

std::mt19937 rng((unsigned int)time(0));

int RandomInt(int min, int max) {
    if (min > max) return min;
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

std::string RandomString(int len, const std::string& chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") {
    std::string res;
    for (int i = 0; i < len; i++) res += chars[RandomInt(0, (int)chars.size() - 1)];
    return res;
}

std::string RandomHex(int len) {
    return RandomString(len, "0123456789ABCDEF");
}

std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(' ');
    if (std::string::npos == first) return str;
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

struct VolumeInfo {
    std::string DriveLetter;
    std::string Serial;
    std::string Name;
    std::string FileSystem;
};

struct DiskInfo {
    std::string Model;
    std::string Serial;
    std::string Interface;
    long long SizeGB;
};

struct DiskTemplate { std::string Model; int SizeGB; std::string Type; };
const std::vector<DiskTemplate> DISK_DB = {
    {"Samsung SSD 970 EVO Plus 500GB", 465, "NVMe"},
    {"Samsung SSD 980 PRO 1TB", 931, "NVMe"},
    {"WDC WD10EZEX-00BN5A0", 931, "SATA"},
    {"Seagate BarraCuda ST2000DM008", 1863, "SATA"}
};

const std::vector<std::string> CLEAN_SERVICES = {
    "dps", "pcasvc", "sysmain", "eventlog", "dnscache", "dusmsvc", "cdpsvc", "diagtrack"
};

struct Identity {
    std::string PcName;
    std::string Username;
    std::string MachineId;

    std::vector<VolumeInfo> Volumes;
    std::vector<DiskInfo> Disks;

    int CountPrefetch;
    int CountBam;
    int CountDns;

    bool IsCleanMode;

    void LoadReal() {
        IsCleanMode = true;

        char buf[256]; DWORD len = 256;
        if (GetComputerNameA(buf, &len)) PcName = buf; else PcName = "UNKNOWN";
        len = 256;
        if (GetUserNameA(buf, &len)) Username = buf; else Username = "User";
        MachineId = PcName + "_" + Username;

        Volumes.clear();
        DWORD drives = GetLogicalDrives();
        for (int i = 0; i < 26; i++) {
            if (drives & (1 << i)) {
                std::string root = std::string(1, 'A' + i) + ":\\";
                char volName[MAX_PATH] = { 0 };
                char fsName[MAX_PATH] = { 0 };
                DWORD serial = 0;
                if (GetVolumeInformationA(root.c_str(), volName, MAX_PATH, &serial, NULL, NULL, fsName, MAX_PATH)) {
                    std::stringstream ss; ss << std::hex << std::uppercase << serial;
                    Volumes.push_back({ std::string(1, 'A' + i) + ":", ss.str(), volName, fsName });
                }
            }
        }

        Disks.clear();
        for (int i = 0; i < 16; i++) {
            std::string path = "\\\\.\\PhysicalDrive" + std::to_string(i);
            HANDLE hDevice = CreateFileA(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (hDevice != INVALID_HANDLE_VALUE) {
                DiskInfo info = { "Generic Disk", "0000", "SATA", 0 };

                STORAGE_PROPERTY_QUERY query = { StorageDeviceProperty, PropertyStandardQuery };
                char buffer[1024]; DWORD bytesReturned = 0;
                if (DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, sizeof(buffer), &bytesReturned, NULL)) {
                    STORAGE_DEVICE_DESCRIPTOR* desc = (STORAGE_DEVICE_DESCRIPTOR*)buffer;
                    if (desc->ProductIdOffset) info.Model = Trim(std::string(buffer + desc->ProductIdOffset));
                    if (desc->SerialNumberOffset) info.Serial = Trim(std::string(buffer + desc->SerialNumberOffset));

                    if (desc->BusType == 17) info.Interface = "NVMe";
                    else if (desc->BusType == 7) info.Interface = "USB";
                    else if (desc->BusType == 8) info.Interface = "RAID";
                    else info.Interface = "SATA";
                }

                GET_LENGTH_INFORMATION sizeInfo;
                if (DeviceIoControl(hDevice, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &sizeInfo, sizeof(sizeInfo), &bytesReturned, NULL)) {
                    info.SizeGB = sizeInfo.Length.QuadPart / (1024 * 1024 * 1024);
                }
                Disks.push_back(info);
                CloseHandle(hDevice);
            }
        }

        CountPrefetch = RandomInt(150, 300);
        CountBam = RandomInt(100, 200);
        CountDns = RandomInt(50, 150);
    }

    void GenerateRandom() {
        IsCleanMode = false;

        PcName = "DESKTOP-" + RandomString(7);
        std::vector<std::string> users = { "Admin", "User", "Gaming", "Owner", "Default" };
        Username = users[RandomInt(0, (int)users.size() - 1)];
        MachineId = PcName + "_" + Username;

        Volumes.clear();
        Volumes.push_back({ "C:", RandomHex(8), "", "NTFS" });
        Volumes.push_back({ "D:", RandomHex(8), "Data", "NTFS" });

        Disks.clear();
        int idx = RandomInt(0, (int)DISK_DB.size() - 1);
        Disks.push_back({ DISK_DB[idx].Model, RandomString(15), DISK_DB[idx].Type, (long long)DISK_DB[idx].SizeGB });

        CountPrefetch = RandomInt(80, 150);
        CountBam = RandomInt(250, 450);
        CountDns = RandomInt(300, 600);
    }

    void SetCustom(std::string pc, std::string user) {
        GenerateRandom();
        PcName = pc;
        Username = user;
        MachineId = PcName + "_" + Username;
        IsCleanMode = true;
    }
};

Identity g_Id;

std::string GetTimestamp() {
    SYSTEMTIME st; GetSystemTime(&st);
    char buf[128];
    sprintf_s(buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03d0000Z", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return std::string(buf);
}

std::string GenerateHeartbeat() {
    std::stringstream json;
    json << R"({"machineId":")" << g_Id.MachineId << R"(","timestamp":")" << GetTimestamp() << R"(","status":"alive","hardware":{"volumes":[)";
    for (size_t i = 0; i < g_Id.Volumes.size(); i++) {
        auto& v = g_Id.Volumes[i];
        json << R"({"driveLetter":")" << v.DriveLetter << R"(","volumeSerial":")" << v.Serial << R"(","volumeName":")" << v.Name << R"(","fileSystem":")" << v.FileSystem << R"("})";
        if (i < g_Id.Volumes.size() - 1) json << ",";
    }
    json << R"(],"disks":[)";
    for (size_t i = 0; i < g_Id.Disks.size(); i++) {
        auto& d = g_Id.Disks[i];
        json << R"({"model":")" << Trim(d.Model) << R"(","serialNumber":")" << Trim(d.Serial) << R"(","interfaceType":")" << d.Interface << R"(","sizeGB":)" << d.SizeGB << "}";
        if (i < g_Id.Disks.size() - 1) json << ",";
    }
    json << "]}}";
    return json.str();
}

std::string GenerateResult() {
    std::stringstream svcJson; svcJson << "[";
    for (size_t i = 0; i < CLEAN_SERVICES.size(); i++) {
        svcJson << R"({"name":")" << CLEAN_SERVICES[i] << R"(","status":"Running"})";
        if (i < CLEAN_SERVICES.size() - 1) svcJson << ",";
    }
    svcJson << "]";

    std::string suspiciousExes = "[]";
    std::string suspiciousDomains = "[]";

    std::stringstream json;
    json << R"({
    "machineId": ")" << g_Id.MachineId << R"(",
    "timestamp": ")" << GetTimestamp() << R"(",
    "counts": {
        "prefetch": )" << g_Id.CountPrefetch << R"(,
        "bam": )" << g_Id.CountBam << R"(,
        "dns": )" << g_Id.CountDns << R"(
    },
    "suspiciousExes": )" << suspiciousExes << R"(,
    "suspiciousDomains": )" << suspiciousDomains << R"(,
    "services": )" << svcJson.str() << R"(
})";
    return json.str();
}

void SendPost(const std::wstring& path, const std::string& jsonData) {
    HINTERNET hSession = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return;

    HINTERNET hConnect = WinHttpConnect(hSession, SERVER_IP.c_str(), SERVER_PORT, 0);
    if (!hConnect) { 
        WinHttpCloseHandle(hSession); 
        return; 
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) { 
        WinHttpCloseHandle(hConnect); 
        WinHttpCloseHandle(hSession); 
        return; 
    }

    std::wstring headers = L"Content-Type: application/json; charset=utf-8";

    WinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)headers.length(), WINHTTP_ADDREQ_FLAG_ADD);

    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)jsonData.c_str(), (DWORD)jsonData.length(), (DWORD)jsonData.length(), 0);

    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
        if (bResults) {
            DWORD dwStatusCode = 0; DWORD dwSize = sizeof(dwStatusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
            if (dwStatusCode == 200) {
                std::cout << "Sending.." << std::endl;
            }
            else {
                std::cout << "Error: " << dwStatusCode << std::endl;
            }
        }
    }
    else {
        std::cout << "Error" << std::endl;
    }
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
}

int main() {
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "      JewAC Emulator - JavaThread       " << std::endl;
    std::cout << "----------------------------------------\n" << std::endl;
    std::cout << "1. Clean" << std::endl;
    std::cout << "2. Random" << std::endl;
    std::cout << "3. Custom" << std::endl;
    std::cout << "Select: ";

    int choice;
    std::cin >> choice;
    std::cin.ignore();

    if (choice == 1) {
        g_Id.LoadReal();
        g_Id.IsCleanMode = true;
    }
    else if (choice == 3) {
        std::string pc, user;
        std::cout << "PC Name: "; std::getline(std::cin, pc);
        std::cout << "Username: "; std::getline(std::cin, user);
        if (pc.empty()) pc = "DESKTOP-" + RandomString(7);
        if (user.empty()) user = "User";
        g_Id.SetCustom(pc, user);
    }
    else {
        g_Id.GenerateRandom();
    }

    while (true) {
        SendPost(L"/heartbeat", GenerateHeartbeat());

        int scanDelay = RandomInt(2000, 5000);
        std::this_thread::sleep_for(std::chrono::milliseconds(scanDelay));

        SendPost(L"/result", GenerateResult());

        std::cout << "Sleeping.." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
    return 0;
}