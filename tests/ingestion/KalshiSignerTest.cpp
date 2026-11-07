#include "TestConstants.h"
#include "ingestion/KalshiSigner.h"

#include <gtest/gtest.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace kparb {
namespace {

using namespace test_constants::signer_test;

struct KeyDeleter {
    void operator()(EVP_PKEY* key) const noexcept
    {
        EVP_PKEY_free(key);
    }
};

using KeyPointer = std::unique_ptr<EVP_PKEY, KeyDeleter>;

KeyPointer generateKey(const char* algorithm)
{
    if (std::string(algorithm) == "RSA") {
        return KeyPointer(EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", static_cast<size_t>(rsaKeyBits)));
    }
    return KeyPointer(EVP_PKEY_Q_keygen(nullptr, nullptr, algorithm));
}

std::string privateKeyPem(EVP_PKEY* key)
{
    std::unique_ptr<BIO, decltype(&BIO_free)> output(BIO_new(BIO_s_mem()), &BIO_free);
    PEM_write_bio_PrivateKey(output.get(), key, nullptr, nullptr, 0, nullptr, nullptr);
    char* data = nullptr;
    const long length = BIO_get_mem_data(output.get(), &data);
    return std::string(data, static_cast<std::size_t>(length));
}

std::vector<unsigned char> decodeBase64(const std::string& text)
{
    std::vector<unsigned char> decoded(text.size());
    const int length = EVP_DecodeBlock(decoded.data(), reinterpret_cast<const unsigned char*>(text.data()), static_cast<int>(text.size()));
    std::size_t padding = 0;
    if (!text.empty() && text.back() == '=') {
        ++padding;
        if (text.size() > 1 && text[text.size() - base64PaddingCharacters] == '=') {
            ++padding;
        }
    }
    decoded.resize(static_cast<std::size_t>(length) - padding);
    return decoded;
}

bool verify(EVP_PKEY* key, const std::string& message, const std::vector<unsigned char>& signature, bool isRsa)
{
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    EVP_PKEY_CTX* keyContext = nullptr;
    if (isRsa) {
        EVP_DigestVerifyInit(context.get(), &keyContext, EVP_sha256(), nullptr, key);
        EVP_PKEY_CTX_set_rsa_padding(keyContext, RSA_PKCS1_PSS_PADDING);
        EVP_PKEY_CTX_set_rsa_pss_saltlen(keyContext, RSA_PSS_SALTLEN_DIGEST);
    } else {
        EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key);
    }
    return EVP_DigestVerify(context.get(), signature.data(), signature.size(),
               reinterpret_cast<const unsigned char*>(message.data()), message.size())
        == 1;
}

TEST(KalshiSignerTest, RsaKeysProduceVerifiablePssSignatures)
{
    KeyPointer key = generateKey("RSA");
    ASSERT_TRUE(key);
    const KalshiSigner signer = KalshiSigner::fromPem(privateKeyPem(key.get()));
    EXPECT_EQ(signer.scheme(), SignatureScheme::RsaPssSha256);
    const std::string message = std::to_string(requestTimestampMilliseconds) + "GET/trade-api/ws/v2";
    EXPECT_TRUE(verify(key.get(), message, decodeBase64(signer.signToBase64(message)), true));
}

TEST(KalshiSignerTest, Ed25519KeysProduceVerifiableSignatures)
{
    KeyPointer key = generateKey("ED25519");
    ASSERT_TRUE(key);
    const KalshiSigner signer = KalshiSigner::fromPem(privateKeyPem(key.get()));
    EXPECT_EQ(signer.scheme(), SignatureScheme::Ed25519);
    const std::string message = std::to_string(requestTimestampMilliseconds) + "GET/trade-api/ws/v2";
    const auto signature = decodeBase64(signer.signToBase64(message));
    EXPECT_EQ(signature.size(), ed25519SignatureBytes);
    EXPECT_TRUE(verify(key.get(), message, signature, false));
}

TEST(KalshiSignerTest, AuthenticationHeadersSignTimestampMethodAndPath)
{
    KeyPointer key = generateKey("ED25519");
    const KalshiSigner signer = KalshiSigner::fromPem(privateKeyPem(key.get()));
    const auto headers = signer.authenticationHeaders("key-id", "GET", "/trade-api/ws/v2", requestTimestampMilliseconds);
    const std::string timestamp = std::to_string(requestTimestampMilliseconds);
    ASSERT_EQ(headers.size(), authenticationHeaderCount);
    EXPECT_EQ(headers[keyHeaderIndex].first, "KALSHI-ACCESS-KEY");
    EXPECT_EQ(headers[keyHeaderIndex].second, "key-id");
    EXPECT_EQ(headers[timestampHeaderIndex].second, timestamp);
    EXPECT_TRUE(verify(key.get(), timestamp + "GET/trade-api/ws/v2", decodeBase64(headers[signatureHeaderIndex].second), false));
}

TEST(KalshiSignerTest, RejectsGarbageKeys)
{
    EXPECT_THROW(KalshiSigner::fromPem("not a key"), std::runtime_error);
}

}
}
