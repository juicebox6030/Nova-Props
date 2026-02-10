#ifndef PLATFORM_COMPAT_HTTP_SERVER_H
#define PLATFORM_COMPAT_HTTP_SERVER_H

#if defined(ESP32)
#include <WebServer.h>
using HttpServer = WebServer;
#elif defined(ESP8266)
#include <ESP8266WebServer.h>
using HttpServer = ESP8266WebServer;
#else
#error "Unsupported platform for HTTP server compatibility layer"
#endif

#endif
