#pragma once

#define _WINSOCKAPI_
#include <windows.h>
#include <sal.h>
#include <ws2tcpip.h>
#include <httpserv.h>
#include <vector>
#include <string>
#include <Shlobj.h>

#ifdef _DEBUG
#pragma comment(lib, "v8_monolith.64.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winmm.lib")
#else
#pragma comment(lib, "v8_monolith.release.64.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dbghelp.lib")
#endif

#include <cassert>
#include <libplatform/libplatform.h>
#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/module.hpp>
#include <thread>
#include <experimental/filesystem>

#define RETURN_NULL { args.GetReturnValue().Set(v8::Null(isolate));return; }
#define RETURN_THIS(value) args.GetReturnValue().Set(v8pp::to_v8(isolate, value)); return;

namespace v8_wrapper
{
	/*
	 * The delegate that handles deserialization.
	 */
	class DeserializerDelegate : public v8::ValueDeserializer::Delegate
	{
	public:
		explicit DeserializerDelegate(v8::Isolate * isolate) : _isolate(isolate) {}
	private:
		v8::Isolate * _isolate;
	};

	/**
	 * The delegate that handles serialization.
	 */
	class SerializerDelegate : public v8::ValueSerializer::Delegate
	{
	public:
		explicit SerializerDelegate(v8::Isolate * isolate) : _isolate(isolate) {}
		void ThrowDataCloneError(v8::Local<v8::String> message)
		{
			_isolate->ThrowException(v8::Exception::Error(message));
		}
	private:
		v8::Isolate * _isolate;
	};

	REQUEST_NOTIFICATION_STATUS begin_request(IHttpResponse * pHttpResponse, IHttpRequest * pHttpRequest);

	void start(std::wstring app_pool_name);
	void reset_engine();
	void load_and_watch();
	void initialize_objects();

	std::experimental::filesystem::path get_path(std::wstring script);
	void execute_file(const wchar_t * name);
	void report_exception(v8::TryCatch * try_catch);

	std::string sock_to_ip(PSOCKADDR address);
	bool execute_string(char * str, bool print_result, bool report_exceptions);
	const char* c_string(v8::String::Utf8Value& value);
	int __cdecl vs_printf(const char *format, ...);

	v8::Local<v8::Context> create_shell_context();
}