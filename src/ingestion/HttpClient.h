#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ix {
class HttpClient;
}

namespace kparb {

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::string errorMessage;
    std::int64_t retryAfterSeconds = 0;

    bool isSuccess() const noexcept
    {
        return statusCode >= 200 && statusCode < 300;
    }
};

class HttpClient {
public:
    HttpClient();
    virtual ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    virtual HttpResponse get(const std::string& url, const HttpHeaders& headers = {});
    virtual HttpResponse post(const std::string& url, const std::string& body, const HttpHeaders& headers = {});

private:
    std::unique_ptr<ix::HttpClient> client;
};

}
