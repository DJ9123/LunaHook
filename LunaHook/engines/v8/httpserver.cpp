// https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/Win7Samples/netds/http/HttpV2Server/main.c
/*++
 Copyright (c) 2002 - 2002 Microsoft Corporation.  All Rights Reserved.

 THIS CODE AND INFORMATION IS PROVIDED "AS-IS" WITHOUT WARRANTY OF
 ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
 PARTICULAR PURPOSE.

 THIS CODE IS NOT SUPPORTED BY MICROSOFT.

--*/

#define SECURITY_WIN32
#include <http.h>
#include <sspi.h>
#include <strsafe.h>
#define NUM_SCHEMES 2
#define MAX_USERNAME_LENGTH 100
#pragma warning(disable : 4127) // condition expression is constant

//
// Macros.
//
#define INITIALIZE_HTTP_RESPONSE(resp, status, reason) \
    do                                                 \
    {                                                  \
        RtlZeroMemory((resp), sizeof(*(resp)));        \
        (resp)->StatusCode = (status);                 \
        (resp)->pReason = (reason);                    \
        (resp)->ReasonLength = (USHORT)strlen(reason); \
    } while (FALSE)

#define ADD_KNOWN_HEADER(Response, HeaderId, RawValue)                      \
    do                                                                      \
    {                                                                       \
        (Response).Headers.KnownHeaders[(HeaderId)].pRawValue = (RawValue); \
        (Response).Headers.KnownHeaders[(HeaderId)].RawValueLength =        \
            (USHORT)strlen(RawValue);                                       \
    } while (FALSE)

#define ALLOC_MEM(cb) HeapAlloc(GetProcessHeap(), 0, (cb))
#define FREE_MEM(ptr) HeapFree(GetProcessHeap(), 0, (ptr))

//
// Prototypes.
//
DWORD
DoReceiveRequests(
    HANDLE hReqQueue);

DWORD
SendHttpResponse(
    IN HANDLE hReqQueue,
    IN PHTTP_REQUEST pRequest);

/***************************************************************************++

Routine Description:
    main routine.

Arguments:
    argc - # of command line arguments.
    argv - Arguments.

Return Value:
    Success/Failure.

--***************************************************************************/

int cleanuphttp(HANDLE hReqQueue, HTTP_SERVER_SESSION_ID ssID, HTTP_URL_GROUP_ID urlGroupId)
{
    ULONG retCode;
    //
    // Call HttpRemoveUrl for all the URLs that we added.
    // HTTP_URL_FLAG_REMOVE_ALL flag allows us to remove
    // all the URLs registered on URL Group at once
    //
    if (!HTTP_IS_NULL_ID(&urlGroupId))
    {

        retCode = HttpRemoveUrlFromUrlGroup(urlGroupId,
                                            NULL,
                                            HTTP_URL_FLAG_REMOVE_ALL);
    }

    //
    // Close the Url Group
    //

    if (!HTTP_IS_NULL_ID(&urlGroupId))
    {
        retCode = HttpCloseUrlGroup(urlGroupId);
    }

    //
    // Close the serversession
    //

    if (!HTTP_IS_NULL_ID(&urlGroupId))
    {
        retCode = HttpCloseServerSession(ssID);
    }

    //
    // Close the Request Queue handle.
    //

    if (hReqQueue)
    {
        retCode = HttpCloseRequestQueue(hReqQueue);
    }

    //
    // Call HttpTerminate.
    //
    HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
    return retCode;
}
auto makeserveronce(int port)
{
    ULONG retCode;

    HANDLE hReqQueue = NULL;
    HTTP_SERVER_SESSION_ID ssID = HTTP_NULL_ID;
    HTTP_URL_GROUP_ID urlGroupId = HTTP_NULL_ID;
    HTTPAPI_VERSION HttpApiVersion = HTTPAPI_VERSION_2;
    HTTP_BINDING_INFO BindingProperty;
    HTTP_TIMEOUT_LIMIT_INFO CGTimeout;

    auto url = std::wstring(L"http://127.0.0.1:") + std::to_wstring(port) + L"/fuck";
    //
    // Initialize HTTP APIs.
    //

    retCode = HttpInitialize(
        HttpApiVersion,
        HTTP_INITIALIZE_SERVER, // Flags
        NULL                    // Reserved
    );

    if (retCode != NO_ERROR)
    {
        return std::tuple{false, hReqQueue, ssID, urlGroupId};
    }

    //
    // Create a server session handle
    //

    retCode = HttpCreateServerSession(HttpApiVersion,
                                      &ssID,
                                      0);

    if (retCode != NO_ERROR)
    {
        return std::tuple{false, hReqQueue, ssID, urlGroupId};
    }

    //
    // Create UrlGroup handle
    //

    retCode = HttpCreateUrlGroup(ssID,
                                 &urlGroupId,
                                 0);

    if (retCode != NO_ERROR)
    {
        return std::tuple{false, hReqQueue, ssID, urlGroupId};
    }

    //
    // Create a request queue handle
    //

    retCode = HttpCreateRequestQueue(HttpApiVersion,
                                     (std::wstring(L"LUNA_INTERNAL_HTTP_QUEUE") + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(rand())).c_str(),
                                     NULL,
                                     0,
                                     &hReqQueue);
    if (retCode != NO_ERROR)
    {
        return std::tuple{false, hReqQueue, ssID, urlGroupId};
    }

    BindingProperty.Flags.Present = 1; // Specifies that the property is present on UrlGroup
    BindingProperty.RequestQueueHandle = hReqQueue;

    //
    // Bind the request queue to UrlGroup
    //

    retCode = HttpSetUrlGroupProperty(urlGroupId,
                                      HttpServerBindingProperty,
                                      &BindingProperty,
                                      sizeof(BindingProperty));

    if (retCode != NO_ERROR)
    {
        return std::tuple{false, hReqQueue, ssID, urlGroupId};
    }

    //
    // Set EntityBody Timeout property on UrlGroup
    //

    ZeroMemory(&CGTimeout, sizeof(HTTP_TIMEOUT_LIMIT_INFO));

    CGTimeout.Flags.Present = 1; // Specifies that the property is present on UrlGroup
    CGTimeout.EntityBody = 50;   // The timeout is in secs

    retCode = HttpSetUrlGroupProperty(urlGroupId,
                                      HttpServerTimeoutsProperty,
                                      &CGTimeout,
                                      sizeof(HTTP_TIMEOUT_LIMIT_INFO));

    if (retCode != NO_ERROR)
    {
        return std::tuple{false, hReqQueue, ssID, urlGroupId};
    }

    //
    // Add the URLs on URL Group
    // The command line arguments represent URIs that we want to listen on.
    // We will call HttpAddUrlToUrlGroup for each of these URIs.
    //
    // The URI is a fully qualified URI and MUST include the terminating '/'
    //

    retCode = HttpAddUrlToUrlGroup(urlGroupId,
                                   url.c_str(),
                                   0,
                                   0);

    if (retCode != NO_ERROR)
    {
        return std::tuple{false, hReqQueue, ssID, urlGroupId};
    }
    return std::tuple{true, hReqQueue, ssID, urlGroupId};
}
int GetRandomAvailablePort()
{
    static int xx = 9000 + GetCurrentProcessId() % 20000;
    return xx++;

    // WSADATA wsaData;
    // int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    // if (result != 0)
    // {
    //     return 0;
    // }

    // // 创建一个 TCP 套接字
    // SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    // if (sock == INVALID_SOCKET)
    // {
    //     WSACleanup();
    //     return 0;
    // }

    // // 绑定到随机端口
    // sockaddr_in addr;
    // addr.sin_family = AF_INET;
    // addr.sin_addr.s_addr = INADDR_ANY;
    // addr.sin_port = 0; // 0 表示让系统自动选择一个可用端口

    // result = bind(sock, (SOCKADDR *)&addr, sizeof(addr));
    // if (result == SOCKET_ERROR)
    // {
    //     closesocket(sock);
    //     WSACleanup();
    //     return 0;
    // }

    // // 获取实际绑定的端口号
    // int addrLen = sizeof(addr);
    // result = getsockname(sock, (SOCKADDR *)&addr, &addrLen);
    // if (result == SOCKET_ERROR)
    // {
    //     closesocket(sock);
    //     WSACleanup();
    //     return 0;
    // }

    // // 关闭套接字
    // closesocket(sock);
    // WSACleanup();

    // // 返回实际绑定的端口号
    // return ntohs(addr.sin_port);
}

int makehttpgetserverinternal()
{
    while (1)
    {
        auto port = GetRandomAvailablePort();
        auto [succ, hReqQueue, ssID, urlGroupId] = makeserveronce(port);
        if (!succ)
        {
            cleanuphttp(hReqQueue, ssID, urlGroupId);
            continue;
        }
        std::thread([=]()
                    {
                // Loop while receiving requests
                DoReceiveRequests(hReqQueue);
                cleanuphttp(hReqQueue, ssID, urlGroupId); })
            .detach();
        return port;
    }
}
// int main()
// {
//     wprintf(L"%d", makehttpgetserverinternal());
//     Sleep(999999);
// }
/***************************************************************************++

Routine Description:
    The routine to receive a request. This routine calls the corresponding
    routine to deal with the response.

Arguments:
    hReqQueue - Handle to the request queue.

Return Value:
    Success/Failure.

--***************************************************************************/

DWORD
DoReceiveRequests(
    IN HANDLE hReqQueue)
{
    ULONG result;
    HTTP_REQUEST_ID requestId;
    DWORD bytesRead;
    PHTTP_REQUEST pRequest;
    PCHAR pRequestBuffer;
    ULONG RequestBufferLength;

    //
    // Allocate a 2K buffer. Should be good for most requests, we'll grow
    // this if required. We also need space for a HTTP_REQUEST structure.
    //
    RequestBufferLength = sizeof(HTTP_REQUEST) + 2048;
    pRequestBuffer = (PCHAR)ALLOC_MEM(RequestBufferLength);

    if (pRequestBuffer == NULL)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    pRequest = (PHTTP_REQUEST)pRequestBuffer;

    //
    // Wait for a new request -- This is indicated by a NULL request ID.
    //

    HTTP_SET_NULL_ID(&requestId);

    for (;;)
    {
        RtlZeroMemory(pRequest, RequestBufferLength);

        result = HttpReceiveHttpRequest(
            hReqQueue,           // Req Queue
            requestId,           // Req ID
            0,                   // Flags
            pRequest,            // HTTP request buffer
            RequestBufferLength, // req buffer length
            &bytesRead,          // bytes received
            NULL                 // LPOVERLAPPED
        );

        if (NO_ERROR == result)
        {
            //
            // Worked!
            //
            // switch (pRequest->Verb)
            // {
            // case HttpVerbGET:
            result = SendHttpResponse(
                hReqQueue,
                pRequest);

            // case HttpVerbPOST:
            // default:

            // if (result != NO_ERROR)
            // {
            //     break;
            // }

            //
            // Reset the Request ID so that we pick up the next request.
            //
            HTTP_SET_NULL_ID(&requestId);
        }
        else if (result == ERROR_MORE_DATA)
        {
            //
            // The input buffer was too small to hold the request headers
            // We have to allocate more buffer & call the API again.
            //
            // When we call the API again, we want to pick up the request
            // that just failed. This is done by passing a RequestID.
            //
            // This RequestID is picked from the old buffer.
            //
            requestId = pRequest->RequestId;

            //
            // Free the old buffer and allocate a new one.
            //
            RequestBufferLength = bytesRead;
            FREE_MEM(pRequestBuffer);
            pRequestBuffer = (PCHAR)ALLOC_MEM(RequestBufferLength);

            if (pRequestBuffer == NULL)
            {
                result = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }

            pRequest = (PHTTP_REQUEST)pRequestBuffer;
        }
        else if (ERROR_CONNECTION_INVALID == result &&
                 !HTTP_IS_NULL_ID(&requestId))
        {
            // The TCP connection got torn down by the peer when we were
            // trying to pick up a request with more buffer. We'll just move
            // onto the next request.

            HTTP_SET_NULL_ID(&requestId);
        }
        else
        {
            break;
        }

    } // for(;;)

    if (pRequestBuffer)
    {
        FREE_MEM(pRequestBuffer);
    }

    return result;
}

/***************************************************************************++

Routine Description:
    The routine sends a HTTP response.

Arguments:
    hReqQueue     - Handle to the request queue.
    pRequest      - The parsed HTTP request.
    StatusCode    - Response Status Code.
    pReason       - Response reason phrase.
    pEntityString - Response entity body.

Return Value:
    Success/Failure.

--***************************************************************************/

std::string urlDecode(const std::string &encoded)
{
    std::string decoded;
    for (size_t i = 0; i < encoded.size(); i++)
    {
        if (encoded[i] == '%')
        {
            char ch = std::stoi(encoded.substr(i + 1, 2), 0, 16);
            decoded += ch;
            i = i + 2;
        }
        else if (encoded[i] == '+')
        {
            decoded += ' ';
        }
        else
        {
            decoded += encoded[i];
        }
    }
    return decoded;
}

#pragma optimize("", off)
const wchar_t *LUNA_CONTENTBYPASS(const wchar_t *_)
{
    return _;
}
#pragma optimize("", on)

DWORD
SendHttpResponse(
    IN HANDLE hReqQueue,
    IN PHTTP_REQUEST pRequest)
{
    HTTP_RESPONSE response;
    HTTP_DATA_CHUNK dataChunk;
    DWORD result;
    DWORD bytesSent;
    USHORT StatusCode = 200;
    PSTR pReason = "OK";

    //
    // Initialize the HTTP response structure.
    //
    INITIALIZE_HTTP_RESPONSE(&response, StatusCode, pReason);

    //
    // Add a known header.
    //
    ADD_KNOWN_HEADER(response, HttpHeaderContentType, "text/html");
    std::string url(pRequest->pRawUrl, pRequest->RawUrlLength);
    auto fnd = url.find('?');

    if (fnd != url.npos)
    {
        url = url.substr(fnd + 1);
        url = urlDecode(url);
        url = WideStringToString(LUNA_CONTENTBYPASS(StringToWideString(url).c_str()));
        //
        // Add an entity chunk
        //
        dataChunk.DataChunkType = HttpDataChunkFromMemory;
        dataChunk.FromMemory.pBuffer = (PVOID)url.c_str();
        dataChunk.FromMemory.BufferLength = (ULONG)url.size();

        response.EntityChunkCount = 1;
        response.pEntityChunks = &dataChunk;
    }

    //
    // Since we are sending all the entity body in one call, we don't have
    // to specify the Content-Length.
    //

    result = HttpSendHttpResponse(
        hReqQueue,           // ReqQueueHandle
        pRequest->RequestId, // Request ID
        0,                   // Flags
        &response,           // HTTP response
        NULL,                // pReserved1
        &bytesSent,          // bytes sent   (OPTIONAL)
        NULL,                // pReserved2   (must be NULL)
        0,                   // Reserved3    (must be 0)
        NULL,                // LPOVERLAPPED (OPTIONAL)
        NULL                 // pReserved4   (must be NULL)
    );

    return result;
}