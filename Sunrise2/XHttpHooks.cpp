#include "stdafx.h"
#include "Sunrise3.h"
#include "Utilities.h"
#include "XHttpHooks.h"

#include <string>

// Reimplementation of XHTTP to avoid SG issues with custom URLs.
// Im not even sure if this is needed - but I wrote it for the Xenia Kernel.

#ifndef HINTERNET
typedef PVOID HINTERNET;
#endif

#define XHTTP_FLAG_ASYNC                 0x10000000

#define XHTTP_ERROR_BASE                 12000
#define XHTTP_ERROR_INTERNAL_ERROR       (XHTTP_ERROR_BASE + 4)
#define XHTTP_ERROR_INCORRECT_HANDLE_TYPE (XHTTP_ERROR_BASE + 18)
#define XHTTP_ERROR_CONNECTION_ERROR     (XHTTP_ERROR_BASE + 30)
#define XHTTP_ERROR_HEADER_NOT_FOUND     (XHTTP_ERROR_BASE + 150)

#define XHTTP_QUERY_CONTENT_LENGTH       5
#define XHTTP_QUERY_VERSION              18
#define XHTTP_QUERY_STATUS_CODE          19
#define XHTTP_QUERY_STATUS_TEXT          20
#define XHTTP_QUERY_RAW_HEADERS          21
#define XHTTP_QUERY_RAW_HEADERS_CRLF     22
#define XHTTP_QUERY_STATUS_CODE_XBOX     0xFFFE
#define XHTTP_QUERY_CONTENT_LENGTH_XBOX  9
#define XHTTP_QUERY_ATTRIBUTE_MASK       0x0000FFFF
#define XHTTP_QUERY_FLAG_NUMBER          0x20000000

#define XHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE  0x00020000
#define XHTTP_CALLBACK_STATUS_READ_COMPLETE      0x00080000
#define XHTTP_CALLBACK_STATUS_WRITE_COMPLETE     0x00100000
#define XHTTP_CALLBACK_STATUS_REQUEST_ERROR      0x00200000
#define XHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE 0x00400000

#define XHTTP_API_RECEIVE_RESPONSE 1
#define XHTTP_API_READ_DATA        3
#define XHTTP_API_WRITE_DATA       4
#define XHTTP_API_SEND_REQUEST     5

#define X_ICU_DECODE 0x10000000

#define XHTTP_INTERNET_SCHEME_HTTP  1
#define XHTTP_INTERNET_SCHEME_HTTPS 2

#define XHTTP_MAX_HANDLES   64
#define XHTTP_MAX_HEADERS   64
#define XHTTP_RECV_CHUNK    4096

typedef VOID (NTAPI *XHTTP_STATUS_CALLBACK)(
	HINTERNET hInternet,
	DWORD_PTR dwContext,
	DWORD dwInternetStatus,
	LPVOID lpvStatusInformation,
	DWORD dwStatusInformationLength);

typedef struct _XHTTP_URL_COMPONENTS {
	DWORD dwStructSize;
	LPSTR lpszScheme;
	DWORD dwSchemeLength;
	DWORD nScheme;
	LPSTR lpszHostName;
	DWORD dwHostNameLength;
	WORD nPort;
	WORD pad;
	LPSTR lpszUserName;
	DWORD dwUserNameLength;
	LPSTR lpszPassword;
	DWORD dwPasswordLength;
	LPSTR lpszUrlPath;
	DWORD dwUrlPathLength;
	LPSTR lpszExtraInfo;
	DWORD dwExtraInfoLength;
} XHTTP_URL_COMPONENTS;

enum XHttpHandleType {
	kXHttpSession = 0,
	kXHttpConnection = 1,
	kXHttpRequest = 2
};

// POD only — static g_handles[] lives in BSS and is never C++-constructed.
// std::string members here previously caused heap corruption on clear()/append.
struct XHttpBuf {
	char* data;
	DWORD len;
	DWORD cap;
};

struct XHttpHandle {
	BOOL in_use;
	volatile LONG busy; // PerformXHttpRequest / worker holds this across I/O
	DWORD id;
	XHttpHandleType type;
	BOOL async;
	XNCALLER_TYPE xnc;

	char user_agent[128];

	DWORD session_handle;
	char host[256];
	WORD port;

	DWORD connection_handle;
	char verb[16];
	char path[512];
	char request_headers[XHTTP_MAX_HEADERS][256];
	int request_header_count;
	XHttpBuf request_body;
	DWORD context;

	XHTTP_STATUS_CALLBACK status_callback;

	BOOL performed;
	BOOL succeeded;
	DWORD status_code;
	char status_text[128];
	XHttpBuf response_headers;
	XHttpBuf response_body;
	DWORD read_offset;
};

struct XHttpCompletion {
	DWORD handle;
	DWORD context;
	XHTTP_STATUS_CALLBACK callback;
	DWORD status;
	LPVOID info_ptr;
	DWORD info_len;
	BOOL alloc_error;
	DWORD error_api;
	DWORD error_code;
	BOOL alloc_write_count;
	DWORD write_count;
};

struct XHttpReceiveWork {
	DWORD handle;
	DWORD context;
	XHTTP_STATUS_CALLBACK callback;
};

static XHttpHandle g_handles[XHTTP_MAX_HANDLES];
static DWORD g_next_handle = 0x50000000;

static XHttpCompletion g_pump_queue[32];
static int g_pump_count = 0;

static void BufFree(XHttpBuf* b)
{
	if (b->data) {
		free(b->data);
		b->data = NULL;
	}
	b->len = 0;
	b->cap = 0;
}

static BOOL BufAppend(XHttpBuf* b, const void* src, DWORD src_len)
{
	if (!src_len)
		return TRUE;
	// Reject wraps: len + src_len + 1 must fit in DWORD.
	if (src_len > 0x7FFFFFFF || b->len > 0x7FFFFFFF - src_len - 1)
		return FALSE;
	DWORD need = b->len + src_len + 1;
	if (need > b->cap) {
		DWORD cap = b->cap ? b->cap : 256;
		while (cap < need) {
			if (cap > 0x7FFFFFFF / 2)
				return FALSE;
			cap *= 2;
		}
		char* p = (char*)realloc(b->data, cap);
		if (!p)
			return FALSE;
		b->data = p;
		b->cap = cap;
	}
	memcpy(b->data + b->len, src, src_len);
	b->len += src_len;
	b->data[b->len] = 0;
	return TRUE;
}

static BOOL BufAssign(XHttpBuf* b, const void* src, DWORD src_len)
{
	BufFree(b);
	return BufAppend(b, src, src_len);
}

static XHttpHandle* LookupHandle(DWORD id)
{
	for (int i = 0; i < XHTTP_MAX_HANDLES; ++i) {
		if (g_handles[i].in_use && g_handles[i].id == id)
			return &g_handles[i];
	}
	return NULL;
}

static DWORD CreateHandle(XHttpHandleType type, XNCALLER_TYPE xnc)
{
	for (int i = 0; i < XHTTP_MAX_HANDLES; ++i) {
		if (!g_handles[i].in_use && g_handles[i].busy == 0) {
			XHttpHandle* h = &g_handles[i];
			// Slot may be dirty if a prior close raced a worker; free first.
			BufFree(&h->request_body);
			BufFree(&h->response_headers);
			BufFree(&h->response_body);
			memset(h, 0, sizeof(*h));
			h->in_use = TRUE;
			h->id = g_next_handle++;
			h->type = type;
			h->xnc = xnc;
			return h->id;
		}
	}
	return 0;
}

static BOOL CloseHandleId(DWORD id)
{
	for (int i = 0; i < XHTTP_MAX_HANDLES; ++i) {
		if (g_handles[i].in_use && g_handles[i].id == id) {
			// Don't free buffers while PerformXHttpRequest owns them.
			if (g_handles[i].busy) {
				Sunrise_Dbg("XHttpCloseHandle defer free id=%08X (busy)", id);
				g_handles[i].in_use = FALSE;
				return TRUE;
			}
			BufFree(&g_handles[i].request_body);
			BufFree(&g_handles[i].response_headers);
			BufFree(&g_handles[i].response_body);
			g_handles[i].in_use = FALSE;
			return TRUE;
		}
	}
	return FALSE;
}

static void SplitHeaderLines(const char* headers, DWORD length,
	char out[][256], int* out_count, int max_out)
{
	*out_count = 0;
	if (!headers || !length)
		return;

	DWORD start = 0;
	while (start < length && *out_count < max_out) {
		DWORD end = start;
		while (end + 1 < length && !(headers[end] == '\r' && headers[end + 1] == '\n'))
			++end;
		if (end > start) {
			DWORD len = end - start;
			if (len > 255)
				len = 255;
			memcpy(out[*out_count], headers + start, len);
			out[*out_count][len] = 0;
			(*out_count)++;
		}
		if (end + 1 < length && headers[end] == '\r')
			start = end + 2;
		else
			break;
	}
}

static BOOL FindHeaderValue(const char* raw, DWORD raw_len, const char* name, std::string* out_value)
{
	if (!raw)
		return FALSE;
	size_t start = 0;
	size_t name_len = strlen(name);
	while (start < raw_len) {
		size_t end = start;
		while (end + 1 < raw_len && !(raw[end] == '\r' && raw[end + 1] == '\n'))
			++end;
		if (end > start) {
			size_t line_len = end - start;
			size_t colon = (size_t)-1;
			for (size_t i = 0; i < line_len; ++i) {
				if (raw[start + i] == ':') {
					colon = i;
					break;
				}
			}
			if (colon != (size_t)-1 && colon == name_len &&
				_strnicmp(raw + start, name, (int)name_len) == 0) {
				size_t value_start = colon + 1;
				while (value_start < line_len && raw[start + value_start] == ' ')
					++value_start;
				out_value->assign(raw + start + value_start, line_len - value_start);
				return TRUE;
			}
		}
		if (end + 1 < raw_len && raw[end] == '\r')
			start = end + 2;
		else
			break;
	}
	return FALSE;
}

static BOOL ResolveRedirectHost(XNCALLER_TYPE xnc, IN_ADDR* out_addr)
{
	const char* host = BlamnetDomain ? BlamnetDomain : "127.0.0.1";
	DWORD ip = NetDll_inet_addr(host);
	if (ip != INADDR_NONE && ip != 0) {
		out_addr->S_un.S_addr = ip;
		return TRUE;
	}

	WSAEVENT event = WSACreateEvent();
	if (!event)
		return FALSE;

	XNDNS* dns = NULL;
	if (NetDll_XNetDnsLookup(xnc, host, event, &dns) != 0 || !dns) {
		WSACloseEvent(event);
		return FALSE;
	}

	WaitForSingleObject((HANDLE)event, 15000);
	BOOL ok = (dns->iStatus == 0 && dns->cina > 0);
	if (ok)
		*out_addr = dns->aina[0];

	WSACloseEvent(event);
	NetDll_XNetDnsRelease(xnc, dns);
	return ok;
}

static BOOL SockSendAll(XNCALLER_TYPE xnc, SOCKET s, const char* data, int len)
{
	int sent = 0;
	while (sent < len) {
		int n = NetDll_send(xnc, s, data + sent, len - sent, 0);
		if (n <= 0)
			return FALSE;
		sent += n;
	}
	return TRUE;
}

static BOOL SockRecvAppend(XNCALLER_TYPE xnc, SOCKET s, std::string* out)
{
	char buf[XHTTP_RECV_CHUNK];
	int n = NetDll_recv(xnc, s, buf, sizeof(buf), 0);
	if (n < 0)
		return FALSE;
	if (n == 0)
		return TRUE; // peer closed; caller checks size growth
	out->append(buf, n);
	return TRUE;
}

static void PerformXHttpRequest(XHttpHandle* request)
{
	if (request->performed)
		return;

	InterlockedIncrement(&request->busy);

	XHttpHandle* connection = LookupHandle(request->connection_handle);
	const char* host = (connection && connection->host[0]) ? connection->host : "";
	WORD host_port = connection ? connection->port : 0;
	XNCALLER_TYPE xnc = request->xnc;

	char path[512];
	if (!request->path[0] || request->path[0] != '/') {
		sprintf_s(path, "/%s", request->path);
	} else {
		strcpy_s(path, request->path);
	}

	const char* verb = request->verb[0] ? request->verb : "GET";

	IN_ADDR addr;
	memset(&addr, 0, sizeof(addr));
	if (!ResolveRedirectHost(xnc, &addr)) {
		Sunrise_Dbg("XHttp: DNS failed for %s", BlamnetDomain ? BlamnetDomain : "(null)");
		SetLastError(XHTTP_ERROR_CONNECTION_ERROR);
		request->performed = TRUE;
		request->succeeded = FALSE;
		InterlockedDecrement(&request->busy);
		return;
	}

	SOCKET sock = NetDll_socket(xnc, AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET) {
		SetLastError(XHTTP_ERROR_INTERNAL_ERROR);
		request->performed = TRUE;
		request->succeeded = FALSE;
		InterlockedDecrement(&request->busy);
		return;
	}

	BOOL insecure = TRUE;
	NetDll_setsockopt(xnc, sock, SOL_SOCKET, 0x5801, (char*)&insecure, sizeof(BOOL));

	WORD connect_port = host_port ? host_port : 80;

	sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(connect_port);
	sa.sin_addr = addr;

	if (NetDll_connect(xnc, sock, (sockaddr*)&sa, sizeof(sa)) != 0) {
		Sunrise_Dbg("XHttp: connect %s:%u failed",
			BlamnetDomain ? BlamnetDomain : "?", connect_port);
		NetDll_closesocket(xnc, sock);
		SetLastError(XHTTP_ERROR_CONNECTION_ERROR);
		request->performed = TRUE;
		request->succeeded = FALSE;
		InterlockedDecrement(&request->busy);
		return;
	}

	std::string req;
	req.reserve(1024 + request->request_body.len);
	req.append(verb);
	req.push_back(' ');
	req.append(path);
	req.append(" HTTP/1.1\r\n");

	char host_line[300];
	if (host[0]) {
		if (host_port && host_port != 80 && host_port != 443)
			sprintf_s(host_line, "Host: %s:%u\r\n", host, host_port);
		else
			sprintf_s(host_line, "Host: %s\r\n", host);
		req.append(host_line);
	}

	BOOL has_user_agent = FALSE;
	BOOL has_content_length = FALSE;
	BOOL has_connection = FALSE;
	for (int i = 0; i < request->request_header_count; ++i) {
		const char* h = request->request_headers[i];
		if (_strnicmp(h, "Host:", 5) == 0)
			continue;
		if (_strnicmp(h, "User-Agent:", 11) == 0)
			has_user_agent = TRUE;
		if (_strnicmp(h, "Content-Length:", 15) == 0)
			has_content_length = TRUE;
		if (_strnicmp(h, "Connection:", 11) == 0)
			has_connection = TRUE;
		req.append(h);
		req.append("\r\n");
	}

	XHttpHandle* session = connection ? LookupHandle(connection->session_handle) : NULL;
	if (!has_user_agent) {
		req.append("User-Agent: ");
		req.append(session && session->user_agent[0] ? session->user_agent : "sunrise");
		req.append("\r\n");
	}
	if (!has_content_length && request->request_body.len) {
		char cl[64];
		sprintf_s(cl, "Content-Length: %u\r\n", request->request_body.len);
		req.append(cl);
	}
	if (!has_connection)
		req.append("Connection: close\r\n");

	req.append("\r\n");
	if (request->request_body.len && request->request_body.data)
		req.append(request->request_body.data, request->request_body.len);

	Sunrise_Dbg("XHttp: %s %s (host: %s) -> %s:%u",
		verb, path, host, BlamnetDomain ? BlamnetDomain : "?", connect_port);

	if (!SockSendAll(xnc, sock, req.data(), (int)req.size())) {
		NetDll_closesocket(xnc, sock);
		SetLastError(XHTTP_ERROR_CONNECTION_ERROR);
		request->performed = TRUE;
		request->succeeded = FALSE;
		InterlockedDecrement(&request->busy);
		return;
	}

	std::string raw;
	raw.reserve(8192);
	for (;;) {
		size_t before = raw.size();
		if (!SockRecvAppend(xnc, sock, &raw)) {
			NetDll_closesocket(xnc, sock);
			SetLastError(XHTTP_ERROR_CONNECTION_ERROR);
			request->performed = TRUE;
			request->succeeded = FALSE;
			InterlockedDecrement(&request->busy);
			return;
		}
		if (raw.size() == before)
			break; // closed

		size_t hdr_end = raw.find("\r\n\r\n");
		if (hdr_end != std::string::npos) {
			std::string cl_str;
			DWORD content_length = 0;
			BOOL have_cl = FindHeaderValue(raw.c_str(), (DWORD)(hdr_end + 2),
				"Content-Length", &cl_str);
			if (have_cl)
				content_length = (DWORD)atoi(cl_str.c_str());

			size_t body_start = hdr_end + 4;
			size_t body_have = raw.size() - body_start;
			if (!have_cl) {
				// Keep reading until peer closes.
				continue;
			}
			if (body_have >= content_length)
				break;
		}
	}

	NetDll_closesocket(xnc, sock);

	size_t hdr_end = raw.find("\r\n\r\n");
	if (hdr_end == std::string::npos) {
		SetLastError(XHTTP_ERROR_CONNECTION_ERROR);
		request->performed = TRUE;
		request->succeeded = FALSE;
		InterlockedDecrement(&request->busy);
		return;
	}

	// Drop results if the title closed the handle mid-request.
	if (!request->in_use) {
		InterlockedDecrement(&request->busy);
		return;
	}

	BufAssign(&request->response_headers, raw.data(), (DWORD)(hdr_end + 2));
	BufAssign(&request->response_body, raw.data() + hdr_end + 4,
		(DWORD)(raw.size() - (hdr_end + 4)));

	// Status line: HTTP/1.x CODE reason
	request->status_code = 0;
	request->status_text[0] = 0;
	const char* hdrs = request->response_headers.data
		? request->response_headers.data : "";
	const char* line_end = strstr(hdrs, "\r\n");
	std::string status_line = line_end
		? std::string(hdrs, line_end - hdrs) : std::string(hdrs);
	size_t sp1 = status_line.find(' ');
	if (sp1 != std::string::npos) {
		size_t sp2 = status_line.find(' ', sp1 + 1);
		std::string code = (sp2 == std::string::npos)
			? status_line.substr(sp1 + 1)
			: status_line.substr(sp1 + 1, sp2 - sp1 - 1);
		request->status_code = (DWORD)atoi(code.c_str());
		if (sp2 != std::string::npos)
			strncpy_s(request->status_text, status_line.c_str() + sp2 + 1, _TRUNCATE);
	}

	Sunrise_Dbg("XHttp: %s %s -> status %u (%u body bytes)",
		verb, path, request->status_code, request->response_body.len);

	request->succeeded = TRUE;
	request->performed = TRUE;
	SetLastError(ERROR_SUCCESS);
	InterlockedDecrement(&request->busy);
}

static XHTTP_STATUS_CALLBACK ResolveStatusCallback(XHttpHandle* request)
{
	if (request->status_callback)
		return request->status_callback;
	XHttpHandle* connection = LookupHandle(request->connection_handle);
	if (connection) {
		if (connection->status_callback)
			return connection->status_callback;
		XHttpHandle* session = LookupHandle(connection->session_handle);
		if (session && session->status_callback)
			return session->status_callback;
	}
	return NULL;
}

static void ExecuteCompletion(const XHttpCompletion& c)
{
	if (!c.callback)
		return;

	if (c.alloc_error) {
		DWORD result[2];
		result[0] = c.error_api;
		result[1] = c.error_code;
		c.callback((HINTERNET)(ULONG_PTR)c.handle, c.context, c.status,
			result, sizeof(result));
		return;
	}

	if (c.alloc_write_count) {
		DWORD count = c.write_count;
		c.callback((HINTERNET)(ULONG_PTR)c.handle, c.context, c.status,
			&count, sizeof(count));
		return;
	}

	c.callback((HINTERNET)(ULONG_PTR)c.handle, c.context, c.status,
		c.info_ptr, c.info_len);
}

static void DeliverCompletion(const XHttpCompletion& completion)
{
	if (g_pump_count < 32)
		g_pump_queue[g_pump_count++] = completion;
}

static DWORD WINAPI XHttpReceiveWorker(LPVOID param)
{
	XHttpReceiveWork* work = (XHttpReceiveWork*)param;
	XHttpHandle* request = LookupHandle(work->handle);

	XHttpCompletion completion;
	memset(&completion, 0, sizeof(completion));
	completion.handle = work->handle;
	completion.context = work->context;
	completion.callback = work->callback;

	if (request) {
		PerformXHttpRequest(request);
		if (request->succeeded) {
			completion.status = XHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE;
		} else {
			completion.status = XHTTP_CALLBACK_STATUS_REQUEST_ERROR;
			completion.alloc_error = TRUE;
			completion.error_api = XHTTP_API_RECEIVE_RESPONSE;
			completion.error_code = XHTTP_ERROR_CONNECTION_ERROR;
		}
	} else {
		completion.status = XHTTP_CALLBACK_STATUS_REQUEST_ERROR;
		completion.alloc_error = TRUE;
		completion.error_api = XHTTP_API_RECEIVE_RESPONSE;
		completion.error_code = XHTTP_ERROR_INCORRECT_HANDLE_TYPE;
	}

	DeliverCompletion(completion);
	free(work);
	return 0;
}

static void StartReceiveWorker(DWORD handle, DWORD context, XHTTP_STATUS_CALLBACK callback)
{
	XHttpReceiveWork* work = (XHttpReceiveWork*)malloc(sizeof(XHttpReceiveWork));
	if (!work)
		return;
	work->handle = handle;
	work->context = context;
	work->callback = callback;

	HANDLE thread = NULL;
	DWORD tid = 0;
	ExCreateThread(&thread, 0, &tid, (PVOID)XapiThreadStartup,
		(LPTHREAD_START_ROUTINE)XHttpReceiveWorker, work, 0x2 | CREATE_SUSPENDED);
	if (thread) {
		XSetThreadProcessor(thread, 4);
		ResumeThread(thread);
	} else {
		free(work);
	}
}

// --- NetDll_XHttp* replacements ------------------------------------------------

static BOOL NTAPI Hook_XHttpStartup(XNCALLER_TYPE /*xnc*/, DWORD /*reserved*/, void* /*reserved_ptr*/)
{
	Sunrise_Dbg("XHttpStartup enter");
	Sunrise_Dbg("XHttpStartup leave -> TRUE");
	return TRUE;
}

static VOID NTAPI Hook_XHttpShutdown(XNCALLER_TYPE /*xnc*/)
{
	Sunrise_Dbg("XHttpShutdown enter");
	Sunrise_Dbg("XHttpShutdown leave");
}

static HINTERNET NTAPI Hook_XHttpOpen(
	XNCALLER_TYPE xnc,
	const CHAR* user_agent,
	DWORD /*access_type*/,
	const CHAR* /*proxy_name*/,
	const CHAR* /*proxy_bypass*/,
	DWORD flags)
{
	Sunrise_Dbg("XHttpOpen enter ua=%s flags=%08X",
		user_agent ? user_agent : "(null)", flags);

	DWORD id = CreateHandle(kXHttpSession, xnc);
	if (!id) {
		SetLastError(XHTTP_ERROR_INTERNAL_ERROR);
		Sunrise_Dbg("XHttpOpen leave -> NULL (oom)");
		return NULL;
	}
	XHttpHandle* session = LookupHandle(id);
	session->async = (flags & XHTTP_FLAG_ASYNC) != 0;
	if (user_agent)
		strncpy_s(session->user_agent, user_agent, _TRUNCATE);
	SetLastError(ERROR_SUCCESS);
	Sunrise_Dbg("XHttpOpen leave -> %08X async=%d", id, session->async);
	return (HINTERNET)(ULONG_PTR)id;
}

static BOOL NTAPI Hook_XHttpCloseHandle(XNCALLER_TYPE /*xnc*/, HINTERNET handle)
{
	DWORD id = (DWORD)(ULONG_PTR)handle;
	Sunrise_Dbg("XHttpCloseHandle enter %08X", id);
	if (!CloseHandleId(id)) {
		SetLastError(ERROR_INVALID_HANDLE);
		Sunrise_Dbg("XHttpCloseHandle leave -> FALSE");
		return FALSE;
	}
	SetLastError(ERROR_SUCCESS);
	Sunrise_Dbg("XHttpCloseHandle leave -> TRUE");
	return TRUE;
}

static HINTERNET NTAPI Hook_XHttpConnect(
	XNCALLER_TYPE xnc,
	HINTERNET hSession,
	const CHAR* serverName,
	WORD port,
	DWORD flags)
{
	DWORD session_id = (DWORD)(ULONG_PTR)hSession;
	Sunrise_Dbg("XHttpConnect enter sess=%08X %s:%u flags=%08X",
		session_id, serverName ? serverName : "(null)", port, flags);

	XHttpHandle* session = LookupHandle(session_id);
	if (!session || session->type != kXHttpSession) {
		SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
		Sunrise_Dbg("XHttpConnect leave -> NULL (bad session)");
		return NULL;
	}

	DWORD id = CreateHandle(kXHttpConnection, xnc);
	if (!id) {
		SetLastError(XHTTP_ERROR_INTERNAL_ERROR);
		Sunrise_Dbg("XHttpConnect leave -> NULL (oom)");
		return NULL;
	}

	XHttpHandle* connection = LookupHandle(id);
	connection->async = session->async;
	connection->session_handle = session_id;
	if (serverName)
		strncpy_s(connection->host, serverName, _TRUNCATE);
	connection->port = port;

	SetLastError(ERROR_SUCCESS);
	Sunrise_Dbg("XHttpConnect leave -> %08X (host %s -> %s:%u)",
		id, connection->host, BlamnetDomain ? BlamnetDomain : "?", connection->port);
	return (HINTERNET)(ULONG_PTR)id;
}

static HINTERNET NTAPI Hook_XHttpOpenRequest(
	XNCALLER_TYPE xnc,
	HINTERNET hConnect,
	const CHAR* verb,
	const CHAR* path,
	const CHAR* /*version*/,
	const CHAR* /*referrer*/,
	const CHAR* /*reserved*/,
	DWORD flag)
{
	DWORD connect_id = (DWORD)(ULONG_PTR)hConnect;
	Sunrise_Dbg("XHttpOpenRequest enter conn=%08X verb=%p path=%p flag=%08X",
		connect_id, verb, path, flag);

	XHttpHandle* connection = LookupHandle(connect_id);
	if (!connection || connection->type != kXHttpConnection) {
		SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
		Sunrise_Dbg("XHttpOpenRequest leave -> NULL (bad conn)");
		return NULL;
	}

	DWORD id = CreateHandle(kXHttpRequest, xnc);
	if (!id) {
		SetLastError(XHTTP_ERROR_INTERNAL_ERROR);
		Sunrise_Dbg("XHttpOpenRequest leave -> NULL (oom)");
		return NULL;
	}

	XHttpHandle* request = LookupHandle(id);
	request->async = connection->async;
	request->connection_handle = connect_id;
	strncpy_s(request->verb, verb ? verb : "GET", _TRUNCATE);
	strncpy_s(request->path, path ? path : "/", _TRUNCATE);

	SetLastError(ERROR_SUCCESS);
	Sunrise_Dbg("XHttpOpenRequest leave -> %08X", id);
	return (HINTERNET)(ULONG_PTR)id;
}

static DWORD_PTR NTAPI Hook_XHttpSetStatusCallback(
	XNCALLER_TYPE /*xnc*/,
	HINTERNET handle,
	XHTTP_STATUS_CALLBACK callback,
	DWORD flags,
	DWORD_PTR /*reserved*/)
{
	DWORD id = (DWORD)(ULONG_PTR)handle;
	Sunrise_Dbg("XHttpSetStatusCallback enter %08X cb=%p flags=%08X",
		id, callback, flags);

	XHttpHandle* h = LookupHandle(id);
	if (!h) {
		SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
		Sunrise_Dbg("XHttpSetStatusCallback leave -> -1");
		return (DWORD_PTR)-1;
	}
	XHTTP_STATUS_CALLBACK previous = h->status_callback;
	h->status_callback = callback;
	Sunrise_Dbg("XHttpSetStatusCallback leave -> prev=%p", previous);
	return (DWORD_PTR)previous;
}

static BOOL NTAPI Hook_XHttpSendRequest(
	XNCALLER_TYPE /*xnc*/,
	HINTERNET hRequest,
	const CHAR* headers,
	DWORD headers_length,
	const void* optional,
	DWORD optional_length,
	DWORD total_length,
	DWORD_PTR context)
{
	DWORD id = (DWORD)(ULONG_PTR)hRequest;
	Sunrise_Dbg("XHttpSendRequest enter %08X hdrLen=%u optLen=%u total=%u ctx=%08X",
		id, headers_length, optional_length, total_length, (DWORD)context);

	XHttpHandle* request = LookupHandle(id);
	if (!request || request->type != kXHttpRequest) {
		SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
		Sunrise_Dbg("XHttpSendRequest leave -> FALSE");
		return FALSE;
	}

	if (headers) {
		DWORD len = headers_length;
		if (len == (DWORD)-1)
			len = (DWORD)strlen(headers);
		SplitHeaderLines(headers, len, request->request_headers,
			&request->request_header_count, XHTTP_MAX_HEADERS);
	}

	if (optional && optional_length)
		BufAppend(&request->request_body, optional, optional_length);

	request->context = (DWORD)context;
	SetLastError(ERROR_SUCCESS);

	if (request->async) {
		XHttpCompletion completion;
		memset(&completion, 0, sizeof(completion));
		completion.handle = request->id;
		completion.context = (DWORD)context;
		completion.callback = ResolveStatusCallback(request);
		completion.status = XHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE;
		DeliverCompletion(completion);
	}

	Sunrise_Dbg("XHttpSendRequest leave -> TRUE async=%d", request->async);
	return TRUE;
}

static BOOL NTAPI Hook_XHttpWriteData(
	XNCALLER_TYPE /*xnc*/,
	HINTERNET hRequest,
	const void* buffer,
	DWORD bytes_to_write,
	DWORD* bytes_written)
{
	DWORD id = (DWORD)(ULONG_PTR)hRequest;
	Sunrise_Dbg("XHttpWriteData enter %08X len=%u", id, bytes_to_write);

	XHttpHandle* request = LookupHandle(id);
	if (!request || request->type != kXHttpRequest) {
		SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
		Sunrise_Dbg("XHttpWriteData leave -> FALSE");
		return FALSE;
	}

	if (buffer && bytes_to_write)
		BufAppend(&request->request_body, buffer, bytes_to_write);

	SetLastError(ERROR_SUCCESS);

	if (request->async) {
		XHttpCompletion completion;
		memset(&completion, 0, sizeof(completion));
		completion.handle = request->id;
		completion.context = request->context;
		completion.callback = ResolveStatusCallback(request);
		completion.status = XHTTP_CALLBACK_STATUS_WRITE_COMPLETE;
		completion.alloc_write_count = TRUE;
		completion.write_count = bytes_to_write;
		DeliverCompletion(completion);
		Sunrise_Dbg("XHttpWriteData leave -> TRUE (async)");
		return TRUE;
	}

	if (bytes_written)
		*bytes_written = bytes_to_write;
	Sunrise_Dbg("XHttpWriteData leave -> TRUE");
	return TRUE;
}

static BOOL NTAPI Hook_XHttpReceiveResponse(
	XNCALLER_TYPE /*xnc*/,
	HINTERNET hRequest,
	void* /*reserved*/)
{
	DWORD id = (DWORD)(ULONG_PTR)hRequest;
	Sunrise_Dbg("XHttpReceiveResponse enter %08X", id);

	XHttpHandle* request = LookupHandle(id);
	if (!request || request->type != kXHttpRequest) {
		SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
		Sunrise_Dbg("XHttpReceiveResponse leave -> FALSE");
		return FALSE;
	}

	if (request->async) {
		StartReceiveWorker(request->id, request->context, ResolveStatusCallback(request));
		SetLastError(ERROR_SUCCESS);
		Sunrise_Dbg("XHttpReceiveResponse leave -> TRUE (async queued)");
		return TRUE;
	}

	PerformXHttpRequest(request);
	if (!request->succeeded) {
		Sunrise_Dbg("XHttpReceiveResponse leave -> FALSE (failed)");
		return FALSE;
	}

	SetLastError(ERROR_SUCCESS);
	Sunrise_Dbg("XHttpReceiveResponse leave -> TRUE status=%u", request->status_code);
	return TRUE;
}

static BOOL NTAPI Hook_XHttpQueryHeaders(
	XNCALLER_TYPE /*xnc*/,
	HINTERNET hRequest,
	DWORD info_level,
	const CHAR* name,
	void* buffer,
	DWORD* buffer_length,
	DWORD* /*index*/)
{
	DWORD id = (DWORD)(ULONG_PTR)hRequest;
	Sunrise_Dbg("XHttpQueryHeaders enter %08X level=%08X name=%s buflen=%u",
		id, info_level, name ? name : "(null)",
		buffer_length ? *buffer_length : 0);

	XHttpHandle* request = LookupHandle(id);
	if (!request || request->type != kXHttpRequest) {
		SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
		Sunrise_Dbg("XHttpQueryHeaders leave -> FALSE");
		return FALSE;
	}

	PerformXHttpRequest(request);

	DWORD attribute = info_level & XHTTP_QUERY_ATTRIBUTE_MASK;
	BOOL want_number = (info_level & XHTTP_QUERY_FLAG_NUMBER) != 0;

	if (!buffer_length) {
		SetLastError(ERROR_INVALID_PARAMETER);
		Sunrise_Dbg("XHttpQueryHeaders leave -> FALSE (no buflen)");
		return FALSE;
	}

	DWORD buffer_size = *buffer_length;

	if (want_number) {
		DWORD value = 0;
		switch (attribute) {
		case XHTTP_QUERY_STATUS_CODE:
		case XHTTP_QUERY_STATUS_CODE_XBOX:
			value = request->status_code;
			break;
		case XHTTP_QUERY_CONTENT_LENGTH:
		case XHTTP_QUERY_CONTENT_LENGTH_XBOX:
			value = request->response_body.len;
			break;
		default:
			SetLastError(XHTTP_ERROR_HEADER_NOT_FOUND);
			Sunrise_Dbg("XHttpQueryHeaders leave -> FALSE (num attr)");
			return FALSE;
		}

		if (!buffer || buffer_size < sizeof(DWORD)) {
			*buffer_length = sizeof(DWORD);
			SetLastError(ERROR_INSUFFICIENT_BUFFER);
			Sunrise_Dbg("XHttpQueryHeaders leave -> FALSE (need %u)", sizeof(DWORD));
			return FALSE;
		}

		*(DWORD*)buffer = value;
		*buffer_length = sizeof(DWORD);
		SetLastError(ERROR_SUCCESS);
		Sunrise_Dbg("XHttpQueryHeaders leave -> TRUE num=%u", value);
		return TRUE;
	}

	std::string result;
	switch (attribute) {
	case XHTTP_QUERY_STATUS_CODE:
	case XHTTP_QUERY_STATUS_CODE_XBOX: {
		char tmp[16];
		sprintf_s(tmp, "%u", request->status_code);
		result = tmp;
		break;
	}
	case XHTTP_QUERY_STATUS_TEXT:
		result = request->status_text;
		break;
	case XHTTP_QUERY_CONTENT_LENGTH:
	case XHTTP_QUERY_CONTENT_LENGTH_XBOX: {
		char tmp[16];
		sprintf_s(tmp, "%u", request->response_body.len);
		result = tmp;
		break;
	}
	case XHTTP_QUERY_VERSION:
		result = "HTTP/1.1";
		break;
	case XHTTP_QUERY_RAW_HEADERS:
	case XHTTP_QUERY_RAW_HEADERS_CRLF:
		if (request->response_headers.data)
			result.assign(request->response_headers.data, request->response_headers.len);
		break;
	default:
		if (!name) {
			SetLastError(XHTTP_ERROR_HEADER_NOT_FOUND);
			Sunrise_Dbg("XHttpQueryHeaders leave -> FALSE (no name)");
			return FALSE;
		}
		if (!FindHeaderValue(request->response_headers.data, request->response_headers.len,
				name, &result)) {
			SetLastError(XHTTP_ERROR_HEADER_NOT_FOUND);
			Sunrise_Dbg("XHttpQueryHeaders leave -> FALSE (not found)");
			return FALSE;
		}
		break;
	}

	DWORD required = (DWORD)result.size() + 1;
	if (!buffer || buffer_size < required) {
		*buffer_length = (DWORD)result.size();
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		Sunrise_Dbg("XHttpQueryHeaders leave -> FALSE (need %u)", required);
		return FALSE;
	}

	memcpy(buffer, result.data(), result.size());
	((char*)buffer)[result.size()] = 0;
	*buffer_length = (DWORD)result.size();
	SetLastError(ERROR_SUCCESS);
	Sunrise_Dbg("XHttpQueryHeaders leave -> TRUE len=%u", *buffer_length);
	return TRUE;
}

static BOOL NTAPI Hook_XHttpReadData(
	XNCALLER_TYPE /*xnc*/,
	HINTERNET hRequest,
	void* buffer,
	DWORD bytes_to_read,
	DWORD* bytes_read)
{
	DWORD id = (DWORD)(ULONG_PTR)hRequest;
	Sunrise_Dbg("XHttpReadData enter %08X want=%u", id, bytes_to_read);

	XHttpHandle* request = LookupHandle(id);
	if (!request || request->type != kXHttpRequest) {
		SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
		Sunrise_Dbg("XHttpReadData leave -> FALSE");
		return FALSE;
	}

	PerformXHttpRequest(request);

	DWORD remaining = request->response_body.len > request->read_offset
		? request->response_body.len - request->read_offset : 0;
	DWORD to_copy = remaining < bytes_to_read ? remaining : bytes_to_read;

	if (to_copy && buffer && request->response_body.data) {
		memcpy(buffer, request->response_body.data + request->read_offset, to_copy);
		request->read_offset += to_copy;
	}

	if (request->async) {
		XHttpCompletion completion;
		memset(&completion, 0, sizeof(completion));
		completion.handle = request->id;
		completion.context = request->context;
		completion.callback = ResolveStatusCallback(request);
		completion.status = XHTTP_CALLBACK_STATUS_READ_COMPLETE;
		completion.info_ptr = buffer;
		completion.info_len = (DWORD)to_copy;
		DeliverCompletion(completion);
		SetLastError(ERROR_SUCCESS);
		Sunrise_Dbg("XHttpReadData leave -> TRUE (async) got=%u", (DWORD)to_copy);
		return TRUE;
	}

	if (bytes_read)
		*bytes_read = (DWORD)to_copy;
	SetLastError(ERROR_SUCCESS);
	Sunrise_Dbg("XHttpReadData leave -> TRUE got=%u", (DWORD)to_copy);
	return TRUE;
}

static BOOL NTAPI Hook_XHttpSetOption(
	XNCALLER_TYPE /*xnc*/,
	HINTERNET handle,
	DWORD option,
	void* /*buffer*/,
	DWORD buffer_length)
{
	Sunrise_Dbg("XHttpSetOption enter %08X opt=%u len=%u",
		(DWORD)(ULONG_PTR)handle, option, buffer_length);
	Sunrise_Dbg("XHttpSetOption leave -> TRUE");
	return TRUE;
}

static BOOL NTAPI Hook_XHttpQueryOption(
	XNCALLER_TYPE /*xnc*/,
	HINTERNET handle,
	DWORD option,
	void* /*buffer*/,
	DWORD* buffer_length)
{
	Sunrise_Dbg("XHttpQueryOption enter %08X opt=%u buflen=%u",
		(DWORD)(ULONG_PTR)handle, option,
		buffer_length ? *buffer_length : 0);
	Sunrise_Dbg("XHttpQueryOption leave -> TRUE");
	return TRUE;
}

static DWORD NTAPI Hook_XHttpDoWork(XNCALLER_TYPE /*xnc*/, HINTERNET handle, DWORD /*reserved*/)
{
	Sunrise_Dbg("XHttpDoWork enter %08X queue=%d",
		(DWORD)(ULONG_PTR)handle, g_pump_count);

	XHttpCompletion pending[32];
	int count = g_pump_count;
	if (count > 0) {
		memcpy(pending, g_pump_queue, count * sizeof(XHttpCompletion));
		g_pump_count = 0;
	}

	for (int i = 0; i < count; ++i)
		ExecuteCompletion(pending[i]);

	SetLastError(ERROR_SUCCESS);
	Sunrise_Dbg("XHttpDoWork leave -> 0 drained=%d", count);
	return 0;
}

static BOOL CopyUrlComponent(
	LPSTR* ppszOut,
	DWORD* pdwLength,
	const char* src,
	DWORD src_len,
	BOOL* insufficient)
{
	if (*ppszOut) {
		DWORD need = src_len + 1;
		if (!*pdwLength || *pdwLength < need) {
			*pdwLength = need;
			*insufficient = TRUE;
			return FALSE;
		}
		memcpy(*ppszOut, src, src_len);
		(*ppszOut)[src_len] = 0;
		*pdwLength = src_len;
	} else if (*pdwLength) {
		*ppszOut = (LPSTR)src;
		*pdwLength = src_len;
	} else {
		*pdwLength = src_len;
	}
	return TRUE;
}

// Title passes buffer capacities in dw*Length. If we skip a component, those
// capacities are left as "lengths" and Destiny's path join asserts
// (networking:http:handler: Error retrieving path from url).
static VOID ClearUrlComponent(LPSTR* ppszOut, DWORD* pdwLength)
{
	if (*ppszOut && *pdwLength)
		(*ppszOut)[0] = 0;
	*pdwLength = 0;
}

static BOOL NTAPI Hook_XHttpCrackUrl(
	XNCALLER_TYPE /*xnc*/,
	const CHAR* url,
	DWORD url_length,
	DWORD /*flags*/,
	XHTTP_URL_COMPONENTS* components)
{
	Sunrise_Dbg("XHttpCrackUrl enter url=%s len=%u",
		url ? url : "(null)", url_length);

	if (!url || !components || components->dwStructSize != sizeof(XHTTP_URL_COMPONENTS)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		Sunrise_Dbg("XHttpCrackUrl leave -> FALSE (bad args)");
		return FALSE;
	}

	DWORD len = url_length ? url_length : (DWORD)strlen(url);
	std::string u(url, len);

	// scheme://host[:port][/path][?query]
	size_t scheme_end = u.find("://");
	if (scheme_end == std::string::npos) {
		SetLastError(ERROR_INVALID_PARAMETER);
		Sunrise_Dbg("XHttpCrackUrl leave -> FALSE (no scheme)");
		return FALSE;
	}

	std::string scheme = u.substr(0, scheme_end);
	size_t auth_start = scheme_end + 3;
	size_t path_start = u.find('/', auth_start);
	size_t query_start = u.find('?', auth_start);
	size_t host_end = u.size();
	if (path_start != std::string::npos)
		host_end = path_start;
	if (query_start != std::string::npos && query_start < host_end)
		host_end = query_start;

	std::string hostport = u.substr(auth_start, host_end - auth_start);
	// skip user:pass@
	size_t at = hostport.find('@');
	if (at != std::string::npos)
		hostport = hostport.substr(at + 1);

	std::string host = hostport;
	WORD port = 0;
	size_t colon = hostport.rfind(':');
	if (colon != std::string::npos) {
		host = hostport.substr(0, colon);
		port = (WORD)atoi(hostport.c_str() + colon + 1);
	}

	std::string path = "/";
	std::string query;
	if (path_start != std::string::npos) {
		size_t path_end = (query_start != std::string::npos) ? query_start : u.size();
		path = u.substr(path_start, path_end - path_start);
		if (path.empty())
			path = "/";
	}
	if (query_start != std::string::npos)
		query = u.substr(query_start);

	BOOL insufficient = FALSE;

	if (_stricmp(scheme.c_str(), "http") == 0) {
		components->nScheme = XHTTP_INTERNET_SCHEME_HTTP;
		if (!port) port = 80;
	} else if (_stricmp(scheme.c_str(), "https") == 0) {
		components->nScheme = XHTTP_INTERNET_SCHEME_HTTPS;
		if (!port) port = 443;
	}
	components->nPort = port;

	CopyUrlComponent(&components->lpszScheme, &components->dwSchemeLength,
		url, (DWORD)scheme.size(), &insufficient);

	size_t host_off = auth_start;
	if (at != std::string::npos)
		host_off = auth_start + at + 1;
	CopyUrlComponent(&components->lpszHostName, &components->dwHostNameLength,
		url + host_off, (DWORD)host.size(), &insufficient);

	// Always write path/extra lengths — never leave caller buffer capacities.
	if (path_start != std::string::npos) {
		CopyUrlComponent(&components->lpszUrlPath, &components->dwUrlPathLength,
			url + path_start, (DWORD)path.size(), &insufficient);
	} else {
		CopyUrlComponent(&components->lpszUrlPath, &components->dwUrlPathLength,
			"/", 1, &insufficient);
	}

	if (!query.empty()) {
		CopyUrlComponent(&components->lpszExtraInfo, &components->dwExtraInfoLength,
			url + query_start, (DWORD)query.size(), &insufficient);
	} else {
		ClearUrlComponent(&components->lpszExtraInfo, &components->dwExtraInfoLength);
	}

	ClearUrlComponent(&components->lpszUserName, &components->dwUserNameLength);
	ClearUrlComponent(&components->lpszPassword, &components->dwPasswordLength);

	if (insufficient) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		Sunrise_Dbg("XHttpCrackUrl leave -> FALSE (buffer)");
		return FALSE;
	}

	SetLastError(ERROR_SUCCESS);
	Sunrise_Dbg("XHttpCrackUrl leave -> TRUE host=%s port=%u pathLen=%u extraLen=%u",
		host.c_str(), port,
		components->dwUrlPathLength, components->dwExtraInfoLength);
	return TRUE;
}

static void PatchXHttpImport(DWORD ordinal, DWORD dest)
{
	Sunrise_Dbg("XHttp patching ordinal %u...", ordinal);
	DWORD result = PatchModuleImport(
		(PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle,
		MODULE_XAM,
		ordinal,
		dest);
	Sunrise_Dbg("XHttp ordinal %u -> %s", ordinal, result == S_OK ? "ok" : "skip");
}

// This started failing with XHTTP hooks so I stubbed it based on Xenia Kernel
typedef struct _XAUTH_SETTINGS {
	DWORD SizeOfStruct;
	DWORD Flags;
	PVOID TitleBuffer; // present in Demonware's stack args even when size==8
} XAUTH_SETTINGS;

typedef struct _XAM_RELYING_PARTY_TOKEN {
	DWORD reserved;
	DWORD length;
	PBYTE token_data;
} XAM_RELYING_PARTY_TOKEN;

static PVOID g_xauthTitleBuffer = NULL;
static BOOL g_xauthStarted = FALSE;

static const char kMockXauthToken[] = "MOCK_XBOX_STS_TOKEN";

static void PatchXAuthImport(DWORD ordinal, DWORD dest)
{
	DWORD result = PatchModuleImport(
		(PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle,
		MODULE_XAM, ordinal, dest);
	Sunrise_Dbg("XAuth ordinal %u -> %s", ordinal, result == S_OK ? "ok" : "skip");
}

static HRESULT NTAPI XampXAuthStartupHook(XAUTH_SETTINGS* settings)
{
	if (!settings) {
		Sunrise_Dbg("XampXAuthStartup leave -> 80158404 (null)");
		return (HRESULT)0x80158404;
	}

	Sunrise_Dbg("XampXAuthStartup enter struct=%u flags=%08X buf=%p",
		settings->SizeOfStruct, settings->Flags, settings->TitleBuffer);

	// Demonware always places the allocated buffer at +8; accept size 8 or 12.
	if (settings->SizeOfStruct != 8 && settings->SizeOfStruct != 12) {
		Sunrise_Dbg("XampXAuthStartup leave -> 80158401 (bad size)");
		return (HRESULT)0x80158401;
	}

	g_xauthTitleBuffer = settings->TitleBuffer;
	g_xauthStarted = TRUE;
	Sunrise_Dbg("XampXAuthStartup leave -> S_OK");
	return S_OK;
}

static VOID NTAPI XampXAuthShutdownHook(PDWORD unk)
{
	Sunrise_Dbg("XampXAuthShutdown buf=%p", g_xauthTitleBuffer);
	g_xauthStarted = FALSE;
	// *unk == 0 => title will call GetTitleBuffer and free it.
	if (unk)
		*unk = g_xauthTitleBuffer ? 0 : 1;
}

static PVOID NTAPI XampXAuthGetTitleBufferHook(void)
{
	PVOID buf = g_xauthTitleBuffer;
	Sunrise_Dbg("XampXAuthGetTitleBuffer -> %p", buf);
	g_xauthTitleBuffer = NULL;
	return buf;
}

static BOOL NTAPI XampXAuthIsLocalSocketAllowedHook(void)
{
	return TRUE;
}

static DWORD NTAPI XamGetTokenHook(
	DWORD userIndex,
	const char* url,
	DWORD urlSize,
	XAM_RELYING_PARTY_TOKEN** tokenOut,
	PXOVERLAPPED overlapped)
{
	Sunrise_Dbg("XamGetToken enter user=%u url=%.*s started=%d",
		userIndex, urlSize, url ? url : "", g_xauthStarted);

	if (!tokenOut) {
		Sunrise_Dbg("XamGetToken leave -> INVALID_PARAMETER");
		return ERROR_INVALID_PARAMETER;
	}

	XAM_RELYING_PARTY_TOKEN* token = NULL;
	PBYTE tokenData = NULL;
	DWORD tokenLen = (DWORD)(sizeof(kMockXauthToken) - 1);

	if (XamAlloc(0, sizeof(XAM_RELYING_PARTY_TOKEN), (PVOID*)&token) != 0 || !token) {
		Sunrise_Dbg("XamGetToken leave -> OUTOFMEMORY (token)");
		return ERROR_OUTOFMEMORY;
	}
	if (XamAlloc(0, tokenLen + 1, (PVOID*)&tokenData) != 0 || !tokenData) {
		XamFree(token);
		Sunrise_Dbg("XamGetToken leave -> OUTOFMEMORY (data)");
		return ERROR_OUTOFMEMORY;
	}

	memset(token, 0, sizeof(*token));
	memcpy(tokenData, kMockXauthToken, tokenLen);
	tokenData[tokenLen] = 0;
	token->reserved = 0;
	token->length = tokenLen;
	token->token_data = tokenData;
	*tokenOut = token;

	if (overlapped) {
		overlapped->InternalLow = ERROR_SUCCESS;
		overlapped->InternalHigh = tokenLen;
		overlapped->InternalContext = (ULONG_PTR)GetCurrentThread();
		overlapped->dwExtendedError = ERROR_SUCCESS;
		if (overlapped->hEvent)
			SetEvent(overlapped->hEvent);
	}

	Sunrise_Dbg("XamGetToken leave -> SUCCESS len=%u", tokenLen);
	return ERROR_SUCCESS;
}

static VOID NTAPI XamFreeTokenHook(XAM_RELYING_PARTY_TOKEN* token)
{
	Sunrise_Dbg("XamFreeToken %p", token);
	if (!token)
		return;
	if (token->token_data)
		XamFree(token->token_data);
	XamFree(token);
}

static VOID SetupXAuthHooks()
{
	Sunrise_Dbg("SetupXAuthHooks enter");
	PatchXAuthImport(1212, (DWORD)XampXAuthStartupHook);       // XampXAuthStartup
	PatchXAuthImport(1213, (DWORD)XampXAuthShutdownHook);      // XampXAuthShutdown
	PatchXAuthImport(1214, (DWORD)XamGetTokenHook);            // XamGetToken
	PatchXAuthImport(1215, (DWORD)XamFreeTokenHook);           // XamFreeToken
	PatchXAuthImport(1262, (DWORD)XampXAuthGetTitleBufferHook); // XampXAuthGetTitleBuffer
	PatchXAuthImport(1440, (DWORD)XampXAuthIsLocalSocketAllowedHook); // optional
	Sunrise_Dbg("XAuth hooks installed");
}

VOID SetupXHttpHooks()
{
	Sunrise_Dbg("SetupXHttpHooks enter");

	SetupXAuthHooks(); 

	// Ordinals from xam NetDll_XHttp* export table.
	PatchXHttpImport(201, (DWORD)Hook_XHttpStartup);
	PatchXHttpImport(202, (DWORD)Hook_XHttpShutdown);
	PatchXHttpImport(203, (DWORD)Hook_XHttpOpen);
	PatchXHttpImport(204, (DWORD)Hook_XHttpCloseHandle);
	PatchXHttpImport(205, (DWORD)Hook_XHttpConnect);
	PatchXHttpImport(206, (DWORD)Hook_XHttpSetStatusCallback);
	PatchXHttpImport(207, (DWORD)Hook_XHttpOpenRequest);
	PatchXHttpImport(209, (DWORD)Hook_XHttpSendRequest);
	PatchXHttpImport(210, (DWORD)Hook_XHttpReceiveResponse);
	PatchXHttpImport(211, (DWORD)Hook_XHttpQueryHeaders);
	PatchXHttpImport(212, (DWORD)Hook_XHttpReadData);
	PatchXHttpImport(213, (DWORD)Hook_XHttpWriteData);
	PatchXHttpImport(214, (DWORD)Hook_XHttpQueryOption);
	PatchXHttpImport(215, (DWORD)Hook_XHttpSetOption);
	PatchXHttpImport(216, (DWORD)Hook_XHttpDoWork);
	PatchXHttpImport(220, (DWORD)Hook_XHttpCrackUrl);

	Sunrise_Dbg("XHTTP hooks installed -> %s", BlamnetDomain ? BlamnetDomain : "?");
}
