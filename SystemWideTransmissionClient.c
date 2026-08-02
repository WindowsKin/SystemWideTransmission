#include <windows.h>
#include <winioctl.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>
#include <io.h>
#include <fcntl.h>
#include <stdlib.h>
#include "sysbin.h"

#define SYM_LINK L"\\\\.\\SystemWideTransmission"
#define IOCTL_WIDE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x512, METHOD_BUFFERED, FILE_WRITE_DATA)

void ShowHelp() {
    wprintf(L"\nSystemWideTransmission v1.1\n");
    wprintf(L"Author : WindowsKin\n");
    wprintf(L"支持的系统 : Windows 8 ~ Windows 11 26H2 x64\n\n");
    wprintf(L"用法 :\n");
    wprintf(L"\t-speedup <ULONG>   加速 <ULONG> 倍\n");
    wprintf(L"\t-slowdown <ULONG>  减速 <ULONG> 倍\n");
    wprintf(L"\t-close             重置速度\n");
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    _setmode(_fileno(stdout), _O_U16TEXT);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == NULL) {
        return 1;
    }
    if (argc == 1) {
        ShowHelp();
        LocalFree(argv);
        return 1;
    }
    TransmissionMode Transmission;
    if (wcscmp(argv[1], L"-speedup") == 0 && argc == 3) {
        Transmission.Mode = TRUE;
        Transmission.WideTransmission = _wtoi(argv[2]);
    }
    else if (wcscmp(argv[1], L"-slowdown") == 0 && argc == 3) {
        Transmission.Mode = FALSE;
        Transmission.WideTransmission = _wtoi(argv[2]);
        if (Transmission.WideTransmission == 0) {
            wprintf(L"错误 : 减速值不能等于 0\n");
            LocalFree(argv);
            return 1;
        }
    }
    else if (wcscmp(argv[1], L"-close") == 0 && argc == 2) {
        Transmission.Mode = TRUE;
        Transmission.WideTransmission = 1;
        LocalFree(argv);
    }
    else {
        wprintf(L"未知参数: %s\n", argv[1]);
        ShowHelp();
        LocalFree(argv);
        return 1;
    }
    CreateStartSysFile:;
    HANDLE hDevice = CreateFileW(SYM_LINK, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        HANDLE hFile = CreateFileW(L"C:\\Windows\\Temp\\SystemWideTransmission.sys", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
        if(!hFile) {
            wprintf(L"错误 : 无法创建驱动\n");
            return 1;
        }
        LARGE_INTEGER FileSize;
        GetFileSizeEx(hFile, &FileSize);
        if(FileSize.QuadPart < sizeof(buffer)) {
            DWORD lpNumberOfBytesWritten;
            WriteFile(hFile, buffer, sizeof(buffer), &lpNumberOfBytesWritten, NULL);
        }
        CloseHandle(hFile);
        system("sc create SystemWideTransmission binPath= C:\\Windows\\Temp\\SystemWideTransmission.sys type= kernel start= demand");
        system("sc start SystemWideTransmission");
        system("sc delete SystemWideTransmission");
        hDevice = CreateFileW(SYM_LINK, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if(hDevice == INVALID_HANDLE_VALUE) {
            wprintf(L"错误 : 驱动无法加载\n");
            CloseHandle(hFile);
            return 1;
        }
    }
    if (DeviceIoControl(hDevice, IOCTL_WIDE, &Transmission, sizeof(Transmission), NULL, 0, NULL, NULL)) {
        wprintf(L"已设置：%s %d 倍\n", Transmission.Mode ? L"加速" : L"减速", Transmission.WideTransmission);
    }
    else {
        wprintf(L"DeviceIoControl 失败，错误码: %d\n", GetLastError());
        CloseHandle(hDevice);
        LocalFree(argv);
        return 1;
    }
    CloseHandle(hDevice);
    LocalFree(argv);
    return 0;
}
