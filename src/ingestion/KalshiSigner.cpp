#include "ingestion/KalshiSigner.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <stdexcept>
#include <vector>

namespace kparb {

namespace {

std::string lastOpenSslError()
{
    const unsigned long code = ERR_get_error();
    if (code == 0) {
        return "unknown OpenSSL error";
    }
    char buffer[256];
    ERR_error_string_n(code, buffer, sizeof(buffer));
    return buffer;
}

struct DigestContextDeleter {
    void operator()(EVP_MD_CTX* context) const noexcept
    {
        EVP_MD_CTX_free(context);
    }
};

std::string encodeBase64(const std::vector<unsigned char>& bytes)
{
    std::string encoded(4 * ((bytes.size() + 2) / 3), '\0');
    const int length = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()), bytes.data(), static_cast<int>(bytes.size()));
    encoded.resize(static_cast<std::size_t>(length));
    return encoded;
}

}

void KalshiSigner::KeyDeleter::operator()(evp_pkey_st* key) const noexcept
{
    EVP_PKEY_free(key);
}

KalshiSigner::KalshiSigner(std::unique_ptr<evp_pkey_st, KeyDeleter> key, SignatureScheme scheme) noexcept
    : privateKey(std::move(key))
    , signatureScheme(scheme)
{
}

KalshiSigner::KalshiSigner(KalshiSigner&&) noexcept = default;
KalshiSigner& KalshiSigner::operator=(KalshiSigner&&) noexcept = default;
KalshiSigner::~KalshiSigner() = default;

KalshiSigner KalshiSigner::fromPem(std::string_view privateKeyPem)
{
    std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_mem_buf(privateKeyPem.data(), static_cast<int>(privateKeyPem.size())), &BIO_free);
    if (!input) {
        throw std::runtime_error("Unable to allocate key buffer");
    }
    std::unique_ptr<evp_pkey_st, KeyDeleter> key(PEM_read_bio_PrivateKey(input.get(), nullptr, nullptr, nullptr));
    if (!key) {
        throw std::runtime_error("Unable to read Kalshi private key: " + lastOpenSslError());
    }
    const int keyType = EVP_PKEY_get_base_id(key.get());
    if (keyType == EVP_PKEY_RSA || keyType == EVP_PKEY_RSA_PSS) {
        return KalshiSigner(std::move(key), SignatureScheme::RsaPssSha256);
    }
    if (keyType == EVP_PKEY_ED25519) {
        return KalshiSigner(std::move(key), SignatureScheme::Ed25519);
    }
    throw std::runtime_error("Unsupported Kalshi private key type");
}

std::string KalshiSigner::signToBase64(std::string_view message) const
{
    std::unique_ptr<EVP_MD_CTX, DigestContextDeleter> context(EVP_MD_CTX_new());
    if (!context) {
        throw std::runtime_error("Unable to allocate signing context");
    }
    const auto* messageBytes = reinterpret_cast<const unsigned char*>(message.data());
    std::size_t signatureLength = 0;

    if (signatureScheme == SignatureScheme::RsaPssSha256) {
        EVP_PKEY_CTX* keyContext = nullptr;
        if (EVP_DigestSignInit(context.get(), &keyContext, EVP_sha256(), nullptr, privateKey.get()) != 1
            || EVP_PKEY_CTX_set_rsa_padding(keyContext, RSA_PKCS1_PSS_PADDING) != 1
            || EVP_PKEY_CTX_set_rsa_pss_saltlen(keyContext, RSA_PSS_SALTLEN_DIGEST) != 1) {
            throw std::runtime_error("Unable to initialise RSA-PSS signing: " + lastOpenSslError());
        }
    } else if (EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, privateKey.get()) != 1) {
        throw std::runtime_error("Unable to initialise Ed25519 signing: " + lastOpenSslError());
    }

    if (EVP_DigestSign(context.get(), nullptr, &signatureLength, messageBytes, message.size()) != 1) {
        throw std::runtime_error("Unable to size signature: " + lastOpenSslError());
    }
    std::vector<unsigned char> signature(signatureLength);
    if (EVP_DigestSign(context.get(), signature.data(), &signatureLength, messageBytes, message.size()) != 1) {
        throw std::runtime_error("Unable to sign message: " + lastOpenSslError());
    }
    signature.resize(signatureLength);
    return encodeBase64(signature);
}

SignatureScheme KalshiSigner::scheme() const noexcept
{
    return signatureScheme;
}

std::vector<std::pair<std::string, std::string>> KalshiSigner::authenticationHeaders(const std::string& keyIdentifier,
    std::string_view method, std::string_view path, std::int64_t timestampMilliseconds) const
{
    const std::string timestamp = std::to_string(timestampMilliseconds);
    std::string message = timestamp;
    message.append(method);
    message.append(path);
    return {
        {"KALSHI-ACCESS-KEY", keyIdentifier},
        {"KALSHI-ACCESS-SIGNATURE", signToBase64(message)},
        {"KALSHI-ACCESS-TIMESTAMP", timestamp},
    };
}

}
