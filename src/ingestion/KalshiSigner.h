#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct evp_pkey_st;

namespace kparb {

enum class SignatureScheme : std::uint8_t {
    RsaPssSha256,
    Ed25519,
};

class KalshiSigner {
public:
    static KalshiSigner fromPem(std::string_view privateKeyPem);

    KalshiSigner(KalshiSigner&&) noexcept;
    KalshiSigner& operator=(KalshiSigner&&) noexcept;
    ~KalshiSigner();

    std::string signToBase64(std::string_view message) const;
    SignatureScheme scheme() const noexcept;

    std::vector<std::pair<std::string, std::string>> authenticationHeaders(const std::string& keyIdentifier,
        std::string_view method, std::string_view path, std::int64_t timestampMilliseconds) const;

private:
    struct KeyDeleter {
        void operator()(evp_pkey_st* key) const noexcept;
    };

    KalshiSigner(std::unique_ptr<evp_pkey_st, KeyDeleter> key, SignatureScheme signatureScheme) noexcept;

    std::unique_ptr<evp_pkey_st, KeyDeleter> privateKey;
    SignatureScheme signatureScheme;
};

}
