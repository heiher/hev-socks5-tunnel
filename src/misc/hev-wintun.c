/*
 ============================================================================
 Name        : hev-wintun.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2025 hev
 Description : Wintun utils
 ============================================================================
 */

#ifdef _WIN32

#include <wintun.h>
#include <netioapi.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "hev-logger.h"
#include "hev-wintun.h"

static WINTUN_CREATE_ADAPTER_FUNC *WintunCreateAdapter;
static WINTUN_CLOSE_ADAPTER_FUNC *WintunCloseAdapter;
static WINTUN_GET_ADAPTER_LUID_FUNC *WintunGetAdapterLUID;
static WINTUN_START_SESSION_FUNC *WintunStartSession;
static WINTUN_END_SESSION_FUNC *WintunEndSession;
static WINTUN_GET_READ_WAIT_EVENT_FUNC *WintunGetReadWaitEvent;
static WINTUN_RECEIVE_PACKET_FUNC *WintunReceivePacket;
static WINTUN_RELEASE_RECEIVE_PACKET_FUNC *WintunReleaseReceivePacket;
static WINTUN_ALLOCATE_SEND_PACKET_FUNC *WintunAllocateSendPacket;
static WINTUN_SEND_PACKET_FUNC *WintunSendPacket;

HevWinTun *
hev_wintun_open (void)
{
    HMODULE self;

    self = LoadLibraryExW (L"wintun.dll", NULL,
                           LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                               LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!self)
        return NULL;

#define X(Name) ((*(FARPROC *)&Name = GetProcAddress (self, #Name)) == NULL)
    if (X (WintunCreateAdapter) || X (WintunCloseAdapter) ||
        X (WintunGetAdapterLUID) || X (WintunStartSession) ||
        X (WintunEndSession) || X (WintunGetReadWaitEvent) ||
        X (WintunReceivePacket) || X (WintunReleaseReceivePacket) ||
        X (WintunAllocateSendPacket) || X (WintunSendPacket))
        goto free;
#undef X

    return self;

free:
    FreeLibrary (self);
    return NULL;
}

void
hev_wintun_close (HevWinTun *self)
{
    FreeLibrary (self);
}

static int
hev_wintun_parse_guid (const char *str, GUID *guid)
{
    unsigned int data1 = 0, data2 = 0, data3 = 0, data4[8] = { 0 };
    int i;

    if (!str || 36 != strlen (str))
        return -1;

    for (i = 0; i < 36; i++) {
        if (8 == i || 13 == i || 18 == i || 23 == i) {
            if ('-' != str[i])
                return -1;
        } else if (!isxdigit ((unsigned char)str[i])) {
            return -1;
        }
    }

    if (11 != sscanf (str, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &data1,
                      &data2, &data3, &data4[0], &data4[1], &data4[2],
                      &data4[3], &data4[4], &data4[5], &data4[6], &data4[7]))
        return -1;

    guid->Data1 = data1;
    guid->Data2 = (unsigned short)data2;
    guid->Data3 = (unsigned short)data3;

    for (i = 0; i < 8; i++)
        guid->Data4[i] = (unsigned char)data4[i];

    return 0;
}

HevWinTunAdapter *
hev_wintun_adapter_create (const char *name, const char *guid)
{
    const GUID *requested_guid = NULL;
    GUID adapter_guid;
    wchar_t tun[256];
    int len;

    len = MultiByteToWideChar (CP_UTF8, 0, name, -1, NULL, 0);
    if (len >= 256)
        return NULL;

    MultiByteToWideChar (CP_UTF8, 0, name, -1, tun, len);

    if (0 == hev_wintun_parse_guid (guid, &adapter_guid))
        requested_guid = &adapter_guid;
    else if (guid && guid[0])
        LOG_W ("wintun adapter guid (%s)", guid);

    return WintunCreateAdapter (tun, L"Socks5", requested_guid);
}

void
hev_wintun_adapter_close (HevWinTunAdapter *adapter)
{
    WintunCloseAdapter (adapter);
}

int
hev_wintun_adapter_get_index (HevWinTunAdapter *adapter)
{
    NET_LUID Luid;
    NET_IFINDEX index;

    WintunGetAdapterLUID (adapter, &Luid);

    if (ConvertInterfaceLuidToIndex (&Luid, &index) != NO_ERROR)
        return -1;

    return index;
}

int
hev_wintun_adapter_set_mtu (HevWinTunAdapter *adapter, int mtu)
{
    NET_LUID Luid;
    MIB_IFROW IfRow;

    WintunGetAdapterLUID (adapter, &Luid);

    if (ConvertInterfaceLuidToIndex (&Luid, &IfRow.dwIndex) != NO_ERROR)
        return -1;

    if (GetIfEntry (&IfRow) != NO_ERROR)
        return -1;

    IfRow.dwMtu = mtu;

    if (SetIfEntry (&IfRow) != NO_ERROR)
        return -1;

    return 0;
}

int
hev_wintun_adapter_set_ipv4 (HevWinTunAdapter *adapter, char addr[4],
                             unsigned int prefix)
{
    MIB_UNICASTIPADDRESS_ROW AddressRow;
    DWORD LastError;

    InitializeUnicastIpAddressEntry (&AddressRow);
    WintunGetAdapterLUID (adapter, &AddressRow.InterfaceLuid);

    AddressRow.Address.Ipv4.sin_family = AF_INET;
    memcpy (&AddressRow.Address.Ipv4.sin_addr, addr, 4);
    AddressRow.OnLinkPrefixLength = prefix;
    AddressRow.DadState = IpDadStatePreferred;

    LastError = CreateUnicastIpAddressEntry (&AddressRow);
    if (LastError != ERROR_SUCCESS && LastError != ERROR_OBJECT_ALREADY_EXISTS)
        return -1;

    return 0;
}

int
hev_wintun_adapter_set_ipv6 (HevWinTunAdapter *adapter, char addr[16],
                             unsigned int prefix)
{
    MIB_UNICASTIPADDRESS_ROW AddressRow;
    DWORD LastError;

    InitializeUnicastIpAddressEntry (&AddressRow);
    WintunGetAdapterLUID (adapter, &AddressRow.InterfaceLuid);

    AddressRow.Address.Ipv6.sin6_family = AF_INET6;
    memcpy (&AddressRow.Address.Ipv6.sin6_addr, addr, 16);
    AddressRow.OnLinkPrefixLength = prefix;
    AddressRow.DadState = IpDadStatePreferred;

    LastError = CreateUnicastIpAddressEntry (&AddressRow);
    if (LastError != ERROR_SUCCESS && LastError != ERROR_OBJECT_ALREADY_EXISTS)
        return -1;

    return 0;
}

HevWinTunSession *
hev_wintun_session_start (HevWinTunAdapter *adapter)
{
    return WintunStartSession (adapter, 0x400000);
}

void
hev_wintun_session_stop (HevWinTunSession *session)
{
    WintunEndSession (session);
}

void *
hev_wintun_session_get_read_wait_event (HevWinTunSession *session)
{
    return WintunGetReadWaitEvent (session);
}

void *
hev_wintun_session_receive (HevWinTunSession *session, int *size)
{
    return WintunReceivePacket (session, (DWORD *)size);
}

void
hev_wintun_session_release (HevWinTunSession *session, void *packet)
{
    WintunReleaseReceivePacket (session, packet);
}

void *
hev_wintun_session_allocate (HevWinTunSession *session, int size)
{
    return WintunAllocateSendPacket (session, size);
}

void
hev_wintun_session_send (HevWinTunSession *session, void *packet)
{
    WintunSendPacket (session, packet);
}

unsigned int
hev_wintun_get_last_error (void)
{
    return GetLastError ();
}

#endif /* _WIN32 */
