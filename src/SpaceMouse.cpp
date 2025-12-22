#if 0
#include "SpaceMouse.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <vector>
#include <map>
#include <iomanip>

#include <initguid.h>
#include <devpkey.h>
#include <cfgmgr32.h>

#include <setupapi.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

#ifndef QWORD
typedef unsigned long long QWORD;
#endif

// 3Dconnexion vendor ID
#define SPACEMOUSE_VENDOR_ID 0x256F

#define USAGE_PAGE_GENERIC_DESKTOP_CONTROL 0x01
#define USAGE_MULTIAXIS_CONTROLLER 0x08
#define USAGE_PAGE_BUTTON 0x09

// ------------------------------------------------------------
// Register for SpaceMouse raw input
// ------------------------------------------------------------
static Result<bool> RegisterSpaceMouseRawInput()
{
    logDebug("Registering for spacemouse input...");

    RAWINPUTDEVICE ridMotionController
    {
        .usUsagePage = USAGE_PAGE_GENERIC_DESKTOP_CONTROL,
        .usUsage = USAGE_MULTIAXIS_CONTROLLER
    };

    expect(RegisterRawInputDevices(&ridMotionController, 1, sizeof(ridMotionController)),
        "RegisterRawInputDevices failed: 0x{:x}", GetLastError());

    return true;
}

struct SpaceMouseDevice
{
    std::vector<void*> Handles;
};

Result<std::wstring> GetDeviceInstanceIdFromPath(std::wstring devicePath)
{
    ULONG bufSize = 0;
    DEVPROPTYPE type;

    // First call to determine the required buffer size
    auto result = CM_Get_Device_Interface_PropertyW(
        devicePath.c_str(),
        &DEVPKEY_Device_InstanceId,
        &type,
        nullptr,
        &bufSize,
        0
    );

    expect(CR_BUFFER_SMALL == result, "CM_Get_Device_Interface_PropertyW failed: 0x{:x}", GetLastError());

    // Allocate buffer with the required size
    std::vector<BYTE> buffer(bufSize);

    // Second call to retrieve the actual data
    result = CM_Get_Device_Interface_PropertyW(
        devicePath.c_str(),
        &DEVPKEY_Device_InstanceId,
        &type,
        buffer.data(),
        &bufSize,
        0
    );

    expect(CR_SUCCESS == result, "CM_Get_Device_Interface_PropertyW failed: 0x{:x}", GetLastError());

    auto instanceId = std::wstring(reinterpret_cast<wchar_t*>(buffer.data()));

    bufSize = 0;
    result = CM_Get_Device_Interface_PropertyW(
        devicePath.c_str(),
        &DEVPKEY_Device_ContainerId,
        &type,
        nullptr,
        &bufSize,
        0
    );

    expect(CR_BUFFER_SMALL == result, "CM_Get_Device_Interface_PropertyW failed: 0x{:x}", GetLastError());

    buffer.resize(bufSize);

    result = CM_Get_Device_Interface_PropertyW(
        devicePath.c_str(),
        &DEVPKEY_Device_ContainerId,
        &type,
        buffer.data(),
        &bufSize,
        0
    );

    expect(CR_SUCCESS == result, "CM_Get_Device_Interface_PropertyW failed: 0x{:x}", GetLastError());

    GUID* containerId = reinterpret_cast<GUID*>(buffer.data());

    return instanceId;
}

Result<GUID> GetDeviceContainerIdFromPath(std::wstring devicePath)
{
    ULONG bufSize = 0;
    DEVPROPTYPE type;

    auto result = CM_Get_Device_Interface_PropertyW(
        devicePath.c_str(),
        &DEVPKEY_Device_ContainerId,
        &type,
        nullptr,
        &bufSize,
        0
    );

    expect(CR_BUFFER_SMALL == result, "CM_Get_Device_Interface_PropertyW failed: 0x{:x}", GetLastError());

    std::vector<BYTE> buffer(bufSize);

    result = CM_Get_Device_Interface_PropertyW(
        devicePath.c_str(),
        &DEVPKEY_Device_ContainerId,
        &type,
        buffer.data(),
        &bufSize,
        0
    );

    expect(CR_SUCCESS == result, "CM_Get_Device_Interface_PropertyW failed: 0x{:x}", GetLastError());

    const GUID* containerId = reinterpret_cast<const GUID*>(buffer.data());

    return *containerId;
}

static auto GUIDCmp = [](const GUID& a, const GUID& b)
{
    return std::memcmp(&a, &b, sizeof(GUID));
};

using SpaceMouseDeviceCollection = std::map<GUID, SpaceMouseDevice, decltype(GUIDCmp)>;

// ------------------------------------------------------------
// Enumerate and print connected SpaceMouse devices
// ------------------------------------------------------------
static Result<void> EnumerateDevices(SpaceMouseDeviceCollection& devices)
{
    logDebug("Enumerating connected SpaceMouse devices...");

    devices.clear();

    UINT nDevices = 0;
    expect(GetRawInputDeviceList(NULL, &nDevices, sizeof(RAWINPUTDEVICELIST)) == 0,
        "GetRawInputDeviceList failed: 0x{:x}", GetLastError());

    if (0 == nDevices)
    {
        logDebug("No SpaceMouse devices found");
    }

    std::vector<RAWINPUTDEVICELIST> list(nDevices);
    nDevices = GetRawInputDeviceList(list.data(), &nDevices, sizeof(RAWINPUTDEVICELIST));
    expect(nDevices != (UINT)-1,
        "GetRawInputDeviceList failed: 0x{:x}", GetLastError());

    list.resize(nDevices);

    for (const auto& device : list)
    {
        RID_DEVICE_INFO info;
        UINT cbSize = info.cbSize = sizeof(info);

        if (GetRawInputDeviceInfoA(device.hDevice, RIDI_DEVICEINFO, &info, &cbSize) == (UINT)-1)
        {
            continue;
        }

        if (info.dwType != RIM_TYPEHID)
        {
            continue;
        }

        if (info.hid.dwVendorId != SPACEMOUSE_VENDOR_ID)
        {
            continue;
        }

        if (info.hid.usUsagePage != USAGE_PAGE_GENERIC_DESKTOP_CONTROL && info.hid.usUsagePage != USAGE_PAGE_BUTTON)
        {
            continue;
        }

        UINT pathLen = 0;
        expect(GetRawInputDeviceInfoW(device.hDevice, RIDI_DEVICENAME, NULL, &pathLen) != (UINT)-1,
            "GetRawInputDeviceInfoA failed: 0x{:x}", GetLastError());

        std::wstring path(pathLen, '\0');

        if (pathLen > 0)
        {
            expect(GetRawInputDeviceInfoW(device.hDevice, RIDI_DEVICENAME, path.data(), &pathLen) != (UINT)-1,
                "GetRawInputDeviceInfoA failed: 0x{:x}", GetLastError());
        }

        logDebug(L"Found SpaceMouse:\n  Handle:  {}\n  Vendor:  0x{:x}\n  Product: 0x{:x}\n  Path:    {}\n\n",
            device.hDevice, info.hid.dwVendorId, info.hid.dwProductId, path);

        auto containerIdResult = GetDeviceContainerIdFromPath(path);

        expect(containerIdResult, containerIdResult.error());

        auto key = containerIdResult.value();

        SpaceMouseDevice& smDevice = devices[key];

        smDevice.Handles.push_back(device.hDevice);
    }

    return {};
}

// ------------------------------------------------------------
// Poll all pending SpaceMouse input packets using GetRawInputBuffer()
// ------------------------------------------------------------
void SpaceMouse::Update()
{
    if (m_Handles.empty())
    {
        return;
    }

    UINT bufferSize = 0;

    if (GetRawInputBuffer(NULL, &bufferSize, sizeof(RAWINPUTHEADER)) != 0 || bufferSize == 0)
    {
        return;
    }

    std::vector<BYTE> buffer(bufferSize * 16);//Multiply by 16 to handle multiple messages.
    bufferSize = buffer.size();
    UINT numPackets = GetRawInputBuffer((PRAWINPUT)buffer.data(), &bufferSize, sizeof(RAWINPUTHEADER));
    if (numPackets == (UINT)-1)
    {
        logError("GetRawInputBuffer failed: 0x{:x}", GetLastError());
    }

    if (numPackets == 0)
    {
        return;
    }

    PRAWINPUT raw = (PRAWINPUT)buffer.data();

    for(UINT i = 0; i < numPackets; ++i, raw = NEXTRAWINPUTBLOCK(raw))
    {
        if (raw->header.dwType != RIM_TYPEHID)
        {
            continue;
        }

        const BYTE* data = raw->data.hid.bRawData;

        for (auto handle : m_Handles)
        {
            if (raw->header.hDevice != handle)
            {
                continue;
            }

            auto getShort = [](const BYTE* ptr) -> short
            {
                return (short)(ptr[0] | (ptr[1] << 8));
            };

            //Range of values for translation/rotation is -350 ... +350

            switch (data[0]) // Report ID
            {
            case 1: // Translation
            case 2: // Rotation
                if (raw->data.hid.dwSizeHid >= 13)
                {
                    tx = getShort(&data[1]);
                    ty = getShort(&data[3]);
                    tz = getShort(&data[5]);
                    rx = getShort(&data[7]);
                    ry = getShort(&data[9]);
                    rz = getShort(&data[11]);
                    logTrace("SpaceMouse Movement: tx {}, ty {}, tz {}, rx {}, ry {}, rz {}", tx, ty, tz, rx, ry, rz);
                }
                break;

            case 3: // Buttons
                if (raw->data.hid.dwSizeHid >= 13)
                {
                    BYTE buttons = data[1];
                    bool left = (buttons & 0x01);
                    bool right = (buttons & 0x02);
                    logTrace("SpaceMouse Buttons: left {}, right {}", left, right);
                }
                break;

            default:
                logError("Unexpected value {} in HID device packet", data[0]);
                break;
            }
        }
    }
}

Result<SpaceMouse*>
SpaceMouse::Create()
{
    auto registerResult = RegisterSpaceMouseRawInput();
    expect(registerResult, registerResult.error());

    SpaceMouseDeviceCollection devices(GUIDCmp);

    auto enumerateResult = EnumerateDevices(devices);
    expect(enumerateResult, enumerateResult.error());

    expect(!devices.empty(), "No spacemouse devices found");

    const auto& chosenDevice = devices.begin()->second;

    SpaceMouse* spaceMouse = new SpaceMouse(chosenDevice.Handles);

    expectv(spaceMouse, "Error allocating SpaceMouse");

    return spaceMouse;
}

SpaceMouse*
SpaceMouse::CreateDummy()
{
    return new SpaceMouse();
}

void
SpaceMouse::Destroy(SpaceMouse* spacemouse)
{
    delete spacemouse;
}

#endif