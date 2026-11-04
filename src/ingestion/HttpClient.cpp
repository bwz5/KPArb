#include "ingestion/HttpClient.h"

#include <ixwebsocket/IXHttpClient.h>

#include <cstdlib>

namespace kparb {

namespace {

HttpResponse convertResponse(const ix::HttpResponsePtr& response)
{
    HttpResponse converted;
    if (!response) {
        converted.errorMessage = "No response";
        return converted;
    }
    converted.statusCode = response->statusCode;
    converted.body = response->body;
    converted.errorMessage = response->errorMsg;
    const auto retryAfter = response->headers.find("Retry-After");
    if (retryAfter != response->headers.end()) {
        converted.retryAfterSeconds = std::atoll(retryAfter->second.c_str());
    }
    return converted;
}

ix::HttpRequestArgsPtr createArguments(ix::HttpClient& client, const HttpHeaders& headers)
{
    auto arguments = client.createRequest();
    arguments->connectTimeout = 15;
    arguments->transferTimeout = 120;
    arguments->followRedirects = true;
    arguments->compress = true;
    arguments->extraHeaders["User-Agent"] = "kparb/0.1";
    arguments->extraHeaders["Accept"] = "application/json";
    for (const auto& [name, value] : headers) {
        arguments->extraHeaders[name] = value;
    }
    return arguments;
}

}

HttpClient::HttpClient()
    : client(std::make_unique<ix::HttpClient>(false))
{
}

HttpClient::~HttpClient() = default;

HttpResponse HttpClient::get(const std::string& url, const HttpHeaders& headers)
{
    return convertResponse(client->get(url, createArguments(*client, headers)));
}

HttpResponse HttpClient::post(const std::string& url, const std::string& body, const HttpHeaders& headers)
{
    auto arguments = createArguments(*client, headers);
    if (arguments->extraHeaders.find("Content-Type") == arguments->extraHeaders.end()) {
        arguments->extraHeaders["Content-Type"] = "application/json";
    }
    return convertResponse(client->post(url, body, arguments));
}

}
