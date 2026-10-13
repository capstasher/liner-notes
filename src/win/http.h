#pragma once
#include <string>

namespace ln {
struct HttpResponse {
    int status = 0;            // 0 = transport failure
    std::string body;          // raw bytes (UTF-8 for our APIs)
    std::string error;
    int retryAfterSec = 0;
};
HttpResponse httpGet(const std::string& url, const std::string& userAgent, int timeoutMs = 20000);
}
