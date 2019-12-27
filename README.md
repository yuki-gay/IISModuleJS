<img src="https://i.imgur.com/JpPLWOC.png" />
A middleware for IIS (Internet Information Services) that allows you to harness the power and convenience of JavaScript for each request.

# 

# Dependencies
* [v8](https://github.com/v8/v8) (included precompiled library file in releases)
* [rpclib](https://github.com/rpclib/rpclib) (included precompiled library file in releases)
* [openssl](https://github.com/openssl/openssl) (included precompiled library file in releases)
* [httplib](https://github.com/yhirose/cpp-httplib) 
* [v8pp](https://github.com/pmed/v8pp) 
* [simdb](https://github.com/LiveAsynchronousVisualizedArchitecture/simdb)

# Getting Started
### Installation
1. Download *iismodulejs.64.dll* from the releases page.
2. Follow the instructions given [here](https://docs.microsoft.com/en-us/iis/develop/runtime-extensibility/develop-a-native-cc-module-for-iis#deploying-a-native-module) to install the dynamic-link library in IIS.
### Running Scripts
All scripts are executed from the `%PUBLIC%` directory. The module watches each script for changes and dynamically reloads a script if a change was found. 

Scripts be should named with their corresponding [application pool name](https://blogs.msdn.microsoft.com/rohithrajan/2017/10/08/quick-reference-iis-application-pool/). For example, the site `vldr.org` would likely have the application pool name `vldr_org` thus the script should be named `vldr_org.js`

You can load as many subsequent scripts as you want using the [load](#loadfilename-string--void) function.

# API
#### REQUEST_NOTIFICATION_STATUS: enum
The members of the REQUEST_NOTIFICATION_STATUS enumeration are used as return values from request-level notifications, and the members help to control process flow within the integrated request-processing pipeline.[⁺](https://docs.microsoft.com/en-us/iis/web-development-reference/native-code-api-reference/request-notification-status-enumeration#remarks)

* Use *RQ_NOTIFICATION_CONTINUE* if you want the request continue to other modules and the rest of the pipeline.
* Use *RQ_NOTIFICATION_FINISH_REQUEST* if you want the request to be handled only by yourself.
```javascript
enum REQUEST_NOTIFICATION_STATUS {
    // continue processing for notification
    RQ_NOTIFICATION_CONTINUE = 0,
    
    // suspend processing for notification (DO NOT USE FOR INTERNAL USE ONLY!)
    RQ_NOTIFICATION_PENDING = 1, 
    
    // finish request processing
    RQ_NOTIFICATION_FINISH_REQUEST = 2 
}
```

#### register(callback: (Function(Response, Request): REQUEST_NOTIFICATION_STATUS)): void
Registers a given function as a callback which will be called for every request.

Your callback function will be provided a [Response](#response-object) object, and a [Request](#request-object) object respectively. The callback function can also be an asynchronous function but keep in mind that this will yield far **worse** performance than using a synchronous function.

```javascript
function callback(response, request) 
{
    return RQ_NOTIFICATION_CONTINUE;
}

register(callback);
```

#### load(fileName: String, ...): void
Loads a script using *fileName* as the name of the JavaScript file.

```javascript
load("test.js");
```

#### print(msg: String, ...): void
Prints *msg* using OutputDebugString. You can observe the print out using a debugger or [DebugView](https://docs.microsoft.com/en-us/sysinternals/downloads/debugview).
```javascript
print("test message");
```

### Request: Object

#### getHost(): String
Returns the host section of the url for the request.

```javascript
register((response, request) => 
{
    // If the full url is "http://127.0.0.1/this/is/a/absolute/path?this=is&a=query&string"
    // then this will print out "127.0.0.1:80"
    print(
        request.getHost()
    );

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getQueryString(): String
Returns the query string for the request. 

```javascript
register((response, request) => 
{
    // If the full url is "http://127.0.0.1/this/is/a/absolute/path?this=is&a=query&string"
    // then this will print out "?this=is&a=query&string"
    print(
        request.getQueryString()
    );

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getFullUrl(): String
Returns the full url for the request. 

```javascript
register((response, request) => 
{
    // If the full url is "http://127.0.0.1/this/is/a/absolute/path?this=is&a=query&string"
    // then this will print out "http://127.0.0.1:80/this/is/a/absolute/path?this=is&a=query&string"
    print(
        request.getFullUrl()
    );

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getAbsPath(): String
Returns the absolute path for the request. 

```javascript
register((response, request) => 
{
    // If the full url is "http://127.0.0.1/this/is/a/absolute/path?this=is&a=query&string"
    // then this will print out "/this/is/a/absolute/path"
    print(
        request.getAbsPath()
    );

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getMethod(): String
Returns the HTTP method for the current request. Example: GET, POST, etc.
```javascript
register((response, request) => 
{
    // Prints out the HTTP method.
    print(
        request.getMethod()
    );

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getLocalAddress(): String
Returns the address of the local interface for the current request. This will return either a IPv4 or IPv6 address.
```javascript
register((response, request) => 
{
    // Prints out the local ip address.
    print(
        request.getLocalAddress()
    );

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getRemoteAddress(): String
Returns the address of the local interface for the current request. This will return either a IPv4 or IPv6 address.
This method can be used to get the connecting client's IP address.
```javascript
register((response, request) => 
{
    // Prints out the remote ip address.
    print(
        request.getRemoteAddress()
    );

    return RQ_NOTIFICATION_CONTINUE;
});
```

### Response: Object

#### write(body: String || Uint8Array, mimeType: String, contentEncoding: String {optional}): void
The **body** parameter gets written to the response. 
The **mimeType** parameter sets the [Content-Type](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Type) header with the given value.
The **contentEncoding** parameter sets the [Content-Encoding](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Encoding) header so you can provide compressed data through a response. 

The example below demonstrates various ways of writing *"hi"* as a response:
```javascript
register((response, request) => 
{
    // Writes a response with a String.
    response.write("hi", "text/html");
    
    // Writes a response with a Uint8Array.
    response.write(new Uint8Array([0x68, 0x69]), "text/html");
    
    // Writes a response with a Uint8Array and using the deflate content encoding.
    response.write(
        new Uint8Array([0x78, 0x9c, 0xcb, 0xc8, 0x04, 0x00, 0x01, 0x3b, 0x00, 0xd2]), 
        "text/html", 
        "deflate"
    );
    
    // You have to use RQ_NOTIFICATION_FINISH_REQUEST because 
    // we want the request to finish here, and not 
    // continue down the IIS pipeline; otherwise our written
    // response will be overwritten by other modules like the static file
    // module.
    return RQ_NOTIFICATION_FINISH_REQUEST;
});
```

#### setHeader(headerName: String, headerValue: String, shouldReplace: bool {optional}): bool
Sets or appends the value of a specified HTTP response header. 

The **headerName** parameter defines the name of the header, example: "Content-Type."
The **headerValue** parameter sets the value of the header, example: "text/html."
The **shouldReplace** paremeter determines whether to replace a preexisting headers value or to append to it. 

```javascript
register((response, request) => 
{
    // This will replace the 'Server' header with the value 'custom server value'
    response.setHeader('Server', 'custom server value');

    // This will append 'custom server value 2' to the 'Server' header.
    response.setHeader('Server', 'custom server value 2', false);
    
    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getHeader(headerName: String): String || null
Returns the value of a specified HTTP header. 
If the header doesn't exist this function will return *null* so make sure to check for it.

```javascript
register((response, request) => 
{
    // Gets the value of the server header.
    const serverHeaderValue = response.getHeader('Server');
    
    // Check if our header exists, in this case it will.
    if (serverHeaderValue)
        // Prints out "Microsoft-IIS/10.0" (depending on your server version)
        print(serverHeaderValue);
   
    return RQ_NOTIFICATION_CONTINUE;
});
```

#### clear(): void
Clears the response body. 
```javascript
register((response, request) => 
{
    response.clear();

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### clearHeaders(): void
Clears the response headers and sets headers to default values.
```javascript
register((response, request) => 
{
    response.clearHeaders();

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### closeConnection(): void
Closes the connection and sends a reset packet to the client.
```javascript
register((response, request) => 
{
    response.closeConnection();

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### setNeedDisconnect(): void
Resets the socket after the response is complete.
```javascript
register((response, request) => 
{
    response.setNeedDisconnect();

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getKernelCacheEnabled(): bool
Determines whether the kernel cache is enabled for the current response.
```javascript
register((response, request) => 
{
    response.getKernelCacheEnabled();

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### resetConnection(): void
Resets the socket connection immediately.
```javascript
register((response, request) => 
{
    response.resetConnection();

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### disableBuffering(): void
Disables response buffering for the current request.
```javascript
register((response, request) => 
{
    response.disableBuffering();

    return RQ_NOTIFICATION_CONTINUE;
});
```

#### getStatus(): Number
Retrieves the HTTP status for the response.

```javascript
register((response, request) => 
{
    const status = response.getStatus();
    
    return RQ_NOTIFICATION_CONTINUE;
});
```

#### redirect(url: String, resetStatusCode: bool, includeParameters: bool): bool
Redirects the client to a specified URL.

```javascript
register((response, request) => 
{
    response.redirect("/location", true /* resetStatusCode */, true /* includeParameters */);
    
    return RQ_NOTIFICATION_CONTINUE;
});
```

#### setErrorDescription(decription: String, shouldHtmlEncode: bool): bool
Specifies the custom error description.

```javascript
register((response, request) => 
{
    response.setErrorDescription("error <b>description</b>", true /* shouldHtmlEncode */);
    
    return RQ_NOTIFICATION_CONTINUE;
});
```

#### disableKernelCache(reason: Number): void
Disables the kernel cache for this response.

```javascript
register((response, request) => 
{
    const HANDLER_HTTPSYS_UNFRIENDLY = 9;
    
    response.setErrorDescription(HANDLER_HTTPSYS_UNFRIENDLY);
    
    return RQ_NOTIFICATION_CONTINUE;
});
```


#### deleteHeader(headerName: String): bool
Deletes an HTTP header from the request.

```javascript
register((response, request) => 
{
    response.deleteHeader('Server');

    return RQ_NOTIFICATION_CONTINUE;
});
```


