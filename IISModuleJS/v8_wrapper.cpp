#include "v8_wrapper.h"
#include <simdb/simdb.hpp>

namespace v8_wrapper
{
	v8::Global<v8::Object> global_http_response_object;
	v8::Global<v8::Object> global_http_request_object;
	v8::Global<v8::Function> function_begin_request;
	v8::Persistent<v8::Context> context;
	v8::Isolate * isolate = nullptr;

	IHttpResponse * p_http_response = nullptr;
	IHttpRequest * p_http_request = nullptr;

	std::wstring script_name;
	simdb db;
	
	void start(std::wstring app_pool_name)
	{
		// Start logging.
		start_logging(app_pool_name);

		// Setup our engine thread.
		std::thread engine_thread([app_pool_name] { 
			// Setup our db.
			db = simdb("test", 1024, 4096);
			db.flush();

			// Set our app pool name up.
			script_name = app_pool_name;
			script_name.append(L".js");

			// Initialize...
			std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
			
			v8::V8::InitializePlatform(platform.get());
			v8::V8::InitializeICU();
			v8::V8::Initialize();
#ifdef _DEBUG
			v8::V8::SetFlagsFromString("--always-opt --allow-natives-syntax --trace-opt");
#else
			v8::V8::SetFlagsFromString("--always-opt");
#endif
			// Setup create params...
			v8::Isolate::CreateParams create_params;

			create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();

			// Setup our isolate...
			isolate = v8::Isolate::New(create_params);

			// Reset our engine...
			reset_engine();

			//////////////////////////////////////////

			// Load our script and watch them.
			load_and_watch();
		});
		engine_thread.detach();
	}

	void start_logging(std::wstring app_pool_name)
	{
		//////////////////////////////////////////

		// Our log file pointer.
		FILE * log_file = nullptr;

		// Our counter.
		int count = 0;

		// Redirect stdout to our log file...
		do
		{
			// Setup our log file...
			auto log_file_path = get_path(app_pool_name + L"-log." + std::to_wstring(count) + L".txt");

			// Attempt to open the log file.
			log_file = _wfreopen(log_file_path.c_str(), L"w", stdout);

			// Increment our count.
			count++;

			// Sleep for a second.
			Sleep(1000);
		} while (!log_file);
	}

	void reset_engine()
	{
		// Setup lockers...
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);

		global_http_response_object.Reset();
		global_http_request_object.Reset();
		function_begin_request.Reset();

		// Reset our context...
		context.Reset(isolate, create_shell_context());

		// Initialize our objects...
		initialize_objects();
	}

	std::experimental::filesystem::path get_path(std::wstring script = std::wstring())
	{
		namespace fs = std::experimental::filesystem;

		//////////////////////////////////////////

		// String containing our documents path.
		PWSTR known_folder = nullptr;
		
		// Fetch our documents path.
		auto hr = SHGetKnownFolderPath(FOLDERID_Public, 0, (HANDLE)-1, &known_folder);

		// Check if our hr failed...
		if (FAILED(hr)) throw std::exception("unable to get known folder");

		//////////////////////////////////////////

		// Setup our path.
		fs::path path = known_folder;

		// Free our object.
		CoTaskMemFree(known_folder);

		//////////////////////////////////////////

		if (!script.empty())
		{
			// Create our script path.
			fs::path script_path = script;

			// Append our script's file name.
			path.append(script_path.filename());
		}

		/////////////////////////////////////////
		
		// Return path.
		return path;

		//////////////////////////////////////////
	}

	void load_and_watch()
	{
		// Get our working directory path to monitor for changes.
		auto working_directory = get_path();

		// Get our working directory path to monitor for changes.
		auto script_path = get_path(script_name);

		//////////////////////////////////////////

		// Read our file...
		execute_file(script_path.c_str());
		 
		//////////////////////////////////////////

		// Setup our last write change handle...
		HANDLE change_handle = FindFirstChangeNotificationW(working_directory.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);

		// Loop forever... 
		for (;;)
		{
			// Wait infinitely for our change handle...
			DWORD wait_handle = WaitForSingleObject(change_handle, INFINITE);

			if (wait_handle == WAIT_OBJECT_0)
			{
				// Inform user...
				printf("Resetting and reloading \"%ws\" script...\n", script_name.c_str());

				// Reset our engine...
				reset_engine();

				// Read our file again...
				execute_file(script_path.c_str());

				// Find the next change notification...
				FindNextChangeNotification(change_handle);
			}
			else
			{
				break;
			}
		}
	}

	v8::Local<v8::Context> create_shell_context()
	{
		// Setup isolate locker...
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);

		// Setup our global module.
		v8pp::module global(isolate);

		// print(msg: String, ...): void
		global.set("print", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			for (int i = 0; i < args.Length(); i++)
			{
				// Get the string provided by the user.
				auto string = v8pp::from_v8<const char*>(args.GetIsolate(), args[i]);

				// Check if first item.
				if (i != 0) printf(" ");

				// Print contents provided.
				printf("%s", string);
			}

			// Print a newline.
			printf("\n");
		});

		// load(fileName: String, ...): void
		global.set("load", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			for (int i = 0; i < args.Length(); i++)
			{
				// Get the name of the file provided by the user.
				auto name = v8pp::from_v8<std::wstring>(args.GetIsolate(), args[i]);

				// Get the path with our file name.
				auto path = get_path(name);

				// Execute the file in v8.
				execute_file(path.c_str());
			}
		});

		// registerBeginRequest(callback: (Function(Response, Request): REQUEST_NOTIFICATION_STATUS)): void
		global.set("registerBeginRequest", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			if (args.Length() < 1) throw std::exception("invalid function signature");
			if (!args[0]->IsFunction()) throw std::exception("invalid first parameter, must be a function");

			function_begin_request.Reset(isolate, v8::Local<v8::Function>::Cast(args[0]));
		});

		////////////////////////////////////////

		// ipc Property
		v8pp::module ipc_module(isolate);

		// ipc.set(key: String, value: Object): bool
		ipc_module.set("set", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			// Check if atleast one argument was provided.
			if (args.Length() < 2) throw std::exception("invalid function signature for ipc.put");

			// Check if our first parameter is a string.
			if (!args[0]->IsString()) throw std::exception("invalid first parameter, must be a string for ipc.put");

			/////////////////////////////////////////////

			// Stringify our second object.
			auto key = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);

			// Stringify our second object.
			auto value = v8::JSON::Stringify(args.GetIsolate()->GetCurrentContext(), args[1]);

			// Check if we were successful in stringifying.
			if (value.IsEmpty()) throw std::exception("invalid second parameter for ipc.put");

			/////////////////////////////////////////////

			// Our value string.
			auto value_string = v8pp::from_v8<std::string>(args.GetIsolate(), value.ToLocalChecked());

			// Attempt to get our key.
			auto result = db.put(key, value_string);

			// Check if our result was successful.
			if (!result) throw std::exception("unable to put value for ipc.put");
		});

		// ipc.get(key: String): Object || null
		ipc_module.set("get", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
			// Need this to return our custom object.
			v8::EscapableHandleScope escapable_handle_scope(args.GetIsolate());
			
			// Check if atleast one argument was provided.
			if (args.Length() < 1) throw std::exception("invalid function signature for ipc.get");

			// Check if our first parameter is a string.
			if (!args[0]->IsString()) throw std::exception("invalid first parameter, must be a string for ipc.get");

			// Attempt to get our key. 
			auto key = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);

			// Our string containing our value.
			auto value = std::string();

			// Attempt to get our key.
			auto result = db.get(key, &value);

			// Check if the key exists.
			if (result)
			{
				// Get our value string.
				auto value_v8_string = v8pp::to_v8(args.GetIsolate(), value);

				// Parse our value string to an object
				auto value_v8_object = v8::JSON::Parse(args.GetIsolate()->GetCurrentContext(), value_v8_string);

				// Check if we were successful in the conversion.
				if (value_v8_object.IsEmpty()) throw std::exception("unable to parse object for ipc.get");

				// Return the object using an escapable handle.
				args.GetReturnValue().Set(escapable_handle_scope.Escape(value_v8_object.ToLocalChecked()));
				return;
			}

			RETURN_NULL
		});

		// ipc Object
		global.set_const("ipc", ipc_module);

		////////////////////////////////////////

		return v8::Context::New(isolate, NULL, global.obj_);
	}

	void initialize_objects()
	{
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));

		////////////////////////////
		// HttpResponse JS Object //
		////////////////////////////
		if (global_http_response_object.IsEmpty())
		{
			// Setup our module...
			v8pp::module module(isolate);

			// Setup our functions

			// clear(): void
			module.set("clear", []() {
				if (!p_http_response) throw std::exception("invalid p_http_response for clear");

				p_http_response->Clear();
			});		

			// clearHeaders(): void
			module.set("clearHeaders", []() {
				if (!p_http_response) throw std::exception("invalid p_http_response for clearHeaders");

				p_http_response->ClearHeaders();
			});

			// closeConnection(): void
			module.set("closeConnection", []() {
				if (!p_http_response) throw std::exception("invalid p_http_response for closeConnection");
					
				p_http_response->CloseConnection();
				
			});

			// setNeedDisconnect(): void
			module.set("setNeedDisconnect", []() {
				if (!p_http_response) throw std::exception("invalid p_http_response for setNeedDisconnect");
					
				p_http_response->SetNeedDisconnect();
			});

			// getKernelCacheEnabled(): bool
			module.set("getKernelCacheEnabled", []() {
				if (!p_http_response) throw std::exception("invalid p_http_response for getKernelCacheEnabled");

				return bool(p_http_response->GetKernelCacheEnabled());
			});	

			// resetConnection(): void
			module.set("resetConnection", []() {
				if (!p_http_response) throw std::exception("invalid p_http_response for resetConnection");
					
				p_http_response->ResetConnection();
			});

			// disableBuffering(): void
			module.set("disableBuffering", []() {
				if (!p_http_response) throw std::exception("invalid p_http_response for disableBuffering");
					
				p_http_response->DisableBuffering();
			});
				
			// getStatus(): Number
			module.set("getStatus", []() {
				// Check if our http response is set.
				if (!p_http_response) throw std::exception("invalid p_http_response for getStatus");

				// Our status code...
				USHORT status_code = 0;

				// Get our status code...
				p_http_response->GetStatus(&status_code);

				// Return our result.
				return status_code;
			});

			// redirect(url: String, resetStatusCode: bool, includeParameters: bool): bool
			module.set("redirect", [](std::string url, bool reset_status_code, bool include_parameters) {
				// Check if our http response is set.
				if (!p_http_response) throw std::exception("invalid p_http_response for redirect");

				// Set our error decription...
				auto hr  = p_http_response->Redirect(url.c_str(),
					reset_status_code,
					include_parameters);

				// Return our result...
				return SUCCEEDED(hr);
			});

			// setErrorDescription(decription: String, shouldHtmlEncode: bool): bool
			module.set("setErrorDescription", [](std::wstring description, bool should_html_encode) {
				// Check if our http response is set.
				if (!p_http_response) throw std::exception("invalid p_http_response for setErrorDescription");

				// Set our error decription...
				auto hr  = p_http_response->SetErrorDescription(description.c_str(), 
					description.length(), 
					should_html_encode);

				// Return our result...
				return SUCCEEDED(hr);
			});

			// disableKernelCache(reason: Number): void
			module.set("disableKernelCache", [](int reason) {
				// Check if our http response is set.
				if (!p_http_response) throw std::exception("invalid p_http_response for disableKernelCache");

				// Disable kernel caching...
				p_http_response->DisableKernelCache(reason);
			});

			// deleteHeader(headerName: String): bool
			module.set("deleteHeader", [](std::string header_name) {
				// Check if our http response is valid.
				if (!p_http_response) throw std::exception("invalid p_http_response for deleteHeader");

				// Attempt to get our header. 
				auto hr = p_http_response->DeleteHeader(header_name.c_str());

				// Check if our header value is valid...
				return SUCCEEDED(hr);
			});

			// getHeader(headerName: String): String || null
			module.set("getHeader", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our http response is set.
				if (!p_http_response) throw std::exception("invalid p_http_response for getHeader");
				
				// Check arguments.
				if (args.Length() < 1 || !args[0]->IsString()) throw std::exception("invalid signature for getHeader");

				// Get our header name...
				auto header_name = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);
				
				// Header value count.
				USHORT header_value_count = 0;

				// Attempt to get our header. 
				auto header_value = p_http_response->GetHeader(header_name.c_str(), &header_value_count);

				// Check if our header value is valid...
				if (header_value)
				{
					RETURN_THIS(std::string(header_value, header_value_count))
				}

				// Return null otherwise.
				RETURN_NULL
			});

			// write(body: String || Uint8Array, mimetype: String): bool
			module.set("write", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our http response is set.
				if (!p_http_response) throw std::exception("invalid p_http_response for write");

				// Check arguments.
				if (args.Length() < 2 || !args[1]->IsString()) throw std::exception("invalid signature for write");

				// Setup our length and body which will
				// be used to deliver our content.
				std::string body_string;
				v8::ArrayBuffer::Contents uint8_array_buffer;

				PVOID body = nullptr;
				size_t size = 0; 

				// Check if a string was provided.
				if (args[0]->IsString())
				{
					body_string = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);

					body = (PVOID)body_string.data();
					size = body_string.length() * sizeof(char);
				}
				// Or if an array was provided.
				else if (args[0]->IsUint8Array())
				{
					uint8_array_buffer = args[0].As<v8::Uint8Array>()->Buffer()->GetContents();

					body = (PVOID)uint8_array_buffer.Data();
					size = uint8_array_buffer.ByteLength() * sizeof(unsigned char);
				}
				else throw std::exception("invalid first argument type for write");

				// Check if we have too many bytes...
				if (size > 65535) throw std::exception("too many bytes in body for write");

				// Get our mimetype.
				auto mime_type = v8pp::from_v8<std::string>(args.GetIsolate(), args[1]);

				// Create an array of data chunks.
				HTTP_DATA_CHUNK data_chunk[1];
				DWORD cb_sent;

				// Clear and set our header...
				p_http_response->SetHeader(HttpHeaderContentType, mime_type.c_str(), (USHORT)mime_type.length(), TRUE);

				// Set the chunk to a chunk in memory.
				data_chunk[0].DataChunkType = HttpDataChunkFromMemory;
				data_chunk[0].FromMemory.pBuffer = (PVOID)body;
				data_chunk[0].FromMemory.BufferLength = (USHORT)size;

				// Insert the data chunks into the response.
				auto hr = p_http_response->WriteEntityChunks(data_chunk, 1, FALSE, FALSE, &cb_sent);
				 
				// Check if we succeeded.
				return SUCCEEDED(hr);
			});

			// setHeader(headerName: String, headerValue: String, shouldReplace: bool): bool
			module.set("setHeader", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our http response is set.
				if (!p_http_response) throw std::exception("invalid p_http_response for setHeader");

				// Check arguments.
				if (args.Length() < 2
					|| !args[0]->IsString() 
					|| !args[1]->IsString()) 
					throw std::exception("invalid signature for setHeader");

				// Get our header name...
				auto header_name = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);
				auto header_value = v8pp::from_v8<std::string>(args.GetIsolate(), args[1]);
				auto should_replace = args.Length() >= 3 ? v8pp::from_v8<bool>(args.GetIsolate(), args[2], true) : true;

				// Attempt to get our header. 
				auto hr = p_http_response->SetHeader( header_name.c_str(), 
					header_value.c_str(), 
					header_value.length(), 
					should_replace);  

				RETURN_THIS(SUCCEEDED(hr))
			});

			// Reset our pointer...
			global_http_response_object.Reset(isolate, module.new_instance());
		}

		////////////////////////////////////////////////

		///////////////////////////
		// HttpRequest JS Object //
		///////////////////////////
		if (global_http_request_object.IsEmpty())
		{
			// Setup our module...
			v8pp::module module(isolate);

			// Setup our functions

			// Gets the method
			// getMethod(): String
			module.set("getMethod", []() {
				if (!p_http_request) throw std::exception("invalid p_http_request for getMethod");

				return std::string(p_http_request->GetHttpMethod());
			});

			// getAbsPath(): String
			module.set("getAbsPath", []() {
				if (!p_http_request) throw std::exception("invalid p_http_request for getMethod");

				return std::wstring(p_http_request->GetRawHttpRequest()->CookedUrl.pAbsPath, 
					p_http_request->GetRawHttpRequest()->CookedUrl.AbsPathLength / sizeof(wchar_t));
			});

			// getFullUrl(): String
			module.set("getFullUrl", []() {
				if (!p_http_request) throw std::exception("invalid p_http_request for getFullUrl");

				return std::wstring(p_http_request->GetRawHttpRequest()->CookedUrl.pFullUrl, 
					p_http_request->GetRawHttpRequest()->CookedUrl.FullUrlLength / sizeof(wchar_t));
			});

			// getQueryString(): String
			module.set("getQueryString", []() {
				if (!p_http_request) throw std::exception("invalid p_http_request for getQueryString");

				return std::wstring(p_http_request->GetRawHttpRequest()->CookedUrl.pQueryString, 
					p_http_request->GetRawHttpRequest()->CookedUrl.QueryStringLength / sizeof(wchar_t));
			});

			// getHost(): String
			module.set("getHost", []() {
				if (!p_http_request) throw std::exception("invalid p_http_request for getHost");

				return std::wstring(p_http_request->GetRawHttpRequest()->CookedUrl.pHost, 
					p_http_request->GetRawHttpRequest()->CookedUrl.HostLength / sizeof(wchar_t));
			});

			// getLocalAddress(): String
			module.set("getLocalAddress", []() {
				// Check if our pointer is valid...
				if (!p_http_request) throw std::exception("invalid p_http_request for getLocalAddress");
				
				return sock_to_ip(p_http_request->GetLocalAddress());
			});

			// getRemoteAddress(): String
			module.set("getRemoteAddress", []() {
				// Check if our pointer is valid...
				if (!p_http_request) throw std::exception("invalid p_http_request for getRemoteAddress");
				
				return sock_to_ip(p_http_request->GetRemoteAddress());
			});

			// setHeader(headerName: String): String || null
			module.set("getHeader", [](v8::FunctionCallbackInfo<v8::Value> const& args) {
				// Check if our http response is set.
				if (!p_http_request) throw std::exception("invalid p_http_request for getHeader");

				// Check arguments.
				if (args.Length() < 1 || !args[0]->IsString()) throw std::exception("invalid signature for getHeader");

				// Get our header name...
				auto header_name = v8pp::from_v8<std::string>(args.GetIsolate(), args[0]);

				// Header value count.
				USHORT header_value_count = 0;

				// Attempt to get our header. 
				auto header_value = p_http_request->GetHeader(header_name.c_str(), &header_value_count);

				// Check if our header value is valid...
				if (header_value)
				{
					RETURN_THIS(std::string(header_value, header_value_count))
				}

				// Return null otherwise.
				RETURN_NULL
			});

			// Reset our pointer...
			global_http_request_object.Reset(isolate, module.new_instance());
		}
	}

	REQUEST_NOTIFICATION_STATUS begin_request(IHttpResponse * pHttpResponse, IHttpRequest * pHttpRequest)
	{
		// Check if our pointers are null...
		if (!isolate || !pHttpResponse || !pHttpRequest) return RQ_NOTIFICATION_CONTINUE;

		// Check if our function is empty...
		if (function_begin_request.IsEmpty()) return RQ_NOTIFICATION_CONTINUE;

		////////////////////////////////////////////////

		// Setup our lockers, isolate scope, and handle scope...
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));
		
		////////////////////////////////////////////////

		// Get our object...
		auto http_response_object = global_http_response_object.Get(isolate);
		auto http_request_object = global_http_request_object.Get(isolate);

		// Setup our arguments...
		v8::Local<v8::Value> argv[2] = { http_response_object, http_request_object };

		// Setup local function...
		auto local_function = function_begin_request.Get(isolate);

		// Update our global values...
		p_http_request = pHttpRequest;
		p_http_response = pHttpResponse;

		// Attempt to get our registered create move callback and call it...
		auto result = local_function->Call(isolate->GetCurrentContext(), v8::Null(isolate), 2, argv);

		// Reset our pointers...
		p_http_request = nullptr;
		p_http_response = nullptr; 

		// Check if our function returned anything...
		if (result.IsEmpty()) return RQ_NOTIFICATION_CONTINUE;

		// Our returned value...
		auto returned_value = v8pp::from_v8<int>(isolate, result.ToLocalChecked(), 0);

		// Cast our value to a request notification...
		return REQUEST_NOTIFICATION_STATUS(returned_value);
	}

	std::string sock_to_ip(PSOCKADDR address)
	{
		if (!address) throw std::exception("invalid address for sock_to_ip");

		if (address->sa_family == AF_INET)
		{
			// Setup our ip address variable...
			char ip_address[INET_ADDRSTRLEN] = { 0 };
			auto socket = (sockaddr_in*)address;

			InetNtopA(socket->sin_family, &socket->sin_addr, ip_address, sizeof(ip_address));

			return std::string(ip_address);
		}
		else if (address->sa_family == AF_INET6)
		{
			// Setup our ip address variable...
			char ip_address[INET6_ADDRSTRLEN] = { 0 };
			auto socket = (sockaddr_in6*)address;

			InetNtopA(socket->sin6_family, &socket->sin6_addr, ip_address, sizeof(ip_address));

			return std::string(ip_address);
		}
		else
			throw std::exception("invalid family for sock_to_ip");
	}

	bool execute_string(char * str, bool print_result, bool report_exceptions)
	{
		// Setup context...
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));

		// Enter the execution environment before evaluating any code.
		v8::Local<v8::String> name(v8::String::NewFromUtf8(isolate, "(shell)", v8::NewStringType::kNormal).ToLocalChecked());

		// Setup other related...
		v8::TryCatch try_catch(isolate);
		v8::ScriptOrigin origin(name);
		v8::Local<v8::Context> context(isolate->GetCurrentContext());
		v8::Local<v8::Script> script;

		// Setup our source...
		auto source = v8::String::NewFromUtf8(isolate, str, v8::NewStringType::kNormal).ToLocalChecked();

		// Compile.
		if (!v8::Script::Compile(context, source, &origin).ToLocal(&script))
		{
			// Print errors that happened during compilation.
			if (report_exceptions) report_exception(&try_catch);

			// Return false here...
			return false;
		}

		//////////////////////////////////////////////////

		v8::Local<v8::Value> result;

		if (!script->Run(context).ToLocal(&result))
		{
			assert(try_catch.HasCaught());

			// Print errors that happened during execution.
			if (report_exceptions) report_exception(&try_catch);

			return true;
		}

		//////////////////////////////////////////////////

		assert(!try_catch.HasCaught());
		if (print_result && !result->IsUndefined()) {
			// If all went well and the result wasn't undefined then print
			// the returned value.
			v8::String::Utf8Value str(isolate, result);
			const char* cstr = c_string(str);

			printf("%s\n", cstr);
		}

		return true;
	}

	void execute_file(const wchar_t * name)
	{
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));

		FILE* file = _wfopen(name, L"rb");
		if (file == NULL) return;

		fseek(file, 0, SEEK_END);
		size_t size = ftell(file);
		rewind(file);

		char* chars = new char[size + 1];
		chars[size] = '\0';
		for (size_t i = 0; i < size;) {
			i += fread(&chars[i], 1, size - i, file);
			if (ferror(file)) {
				fclose(file);
				return;
			}
		}
		fclose(file);

		if (!execute_string(chars, true, true)) {
			isolate->ThrowException(v8::String::NewFromUtf8(isolate, "Error executing file",
					v8::NewStringType::kNormal).ToLocalChecked());
		}

		delete[] chars;

		printf("Loaded %ws successfully...\n", name);
		fflush(stdout);
	}

	void report_exception(v8::TryCatch * try_catch)
	{
		// Setup context...
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Context::Scope context_scope(context.Get(isolate));

		v8::String::Utf8Value exception(isolate, try_catch->Exception());
		const char* exception_string = c_string(exception);
		v8::Local<v8::Message> message = try_catch->Message();

		if (message.IsEmpty())
		{
			printf("%s\n", exception_string);
		}
		else {
			v8::Local<v8::Context> context(isolate->GetCurrentContext());
			int linenum = message->GetLineNumber(context).FromJust();

			printf("%i: %s\n", linenum, exception_string);

			v8::String::Utf8Value sourceline(isolate, message->GetSourceLine(context).ToLocalChecked());
			const char* sourceline_string = c_string(sourceline);
			printf("%s\n", sourceline_string);

			int start = message->GetStartColumn(context).FromJust();
			for (int i = 0; i < start; i++) {
				printf(" ");
			}
			int end = message->GetEndColumn(context).FromJust();
			for (int i = start; i < end; i++) {
				printf("^");
			}
			printf("\n");
			v8::Local<v8::Value> stack_trace_string;
			if (try_catch->StackTrace(context).ToLocal(&stack_trace_string) &&
				stack_trace_string->IsString() &&
				v8::Local<v8::String>::Cast(stack_trace_string)->Length() > 0) {
				v8::String::Utf8Value stack_trace(isolate, stack_trace_string);
				const char* stack_trace_string = c_string(stack_trace);
				printf("%s\n", stack_trace_string);
			}
		}
	}

	const char* c_string(v8::String::Utf8Value& value)
	{
		return *value ? *value : "<string conversion failed>";
	}
}