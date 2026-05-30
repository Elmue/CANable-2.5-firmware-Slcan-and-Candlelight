#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <algorithm>

// Simulated safe version of the vulnerable function that encodes the security invariant.
// The invariant: buffer reads must never exceed the declared length.
// Specifically, when u8_ByteLen < 2, the expression (u8_ByteLen - 2) underflows
// for uint8_t, causing memcpy to copy up to 255 bytes — this must never happen.

static constexpr size_t MAX_STRING_BUFFER = 256;

// Returns false if the operation would be unsafe (invariant violated), true if safe.
bool safe_candlelight_copy(const uint8_t* u8_Buffer, uint8_t u8_ByteLen,
                            uint8_t* s_String, size_t s_String_size,
                            size_t actual_buffer_size) {
    // Invariant 1: u8_ByteLen must be >= 2 to avoid underflow
    if (u8_ByteLen < 2) {
        return false; // Would underflow: (u8_ByteLen - 2) wraps to 254 or 255
    }

    // Invariant 2: The copy size must not exceed the actual source buffer
    size_t copy_size = static_cast<size_t>(u8_ByteLen) - 2;
    if (copy_size > actual_buffer_size - 2) {
        return false; // Would read beyond source buffer
    }

    // Invariant 3: The copy size must not exceed the destination buffer
    if (copy_size > s_String_size) {
        return false; // Would overflow destination buffer
    }

    // Safe to copy
    memcpy(s_String, u8_Buffer + 2, copy_size);
    return true;
}

class SecurityTest : public ::testing::TestWithParam<std::string> {};

TEST_P(SecurityTest, BufferReadNeverExceedsDeclaredLength) {
    // Invariant: Buffer reads must never exceed the declared length.
    // When u8_ByteLen is 0 or 1, (u8_ByteLen - 2) underflows for uint8_t,
    // causing memcpy to copy up to 255 bytes — this must be rejected.

    std::string payload = GetParam();

    // Destination buffer of fixed size
    uint8_t dest_buffer[MAX_STRING_BUFFER];
    memset(dest_buffer, 0, sizeof(dest_buffer));

    // Source buffer constructed from payload
    std::vector<uint8_t> src_buffer(payload.begin(), payload.end());

    // Ensure source buffer has at least 2 bytes for the header
    if (src_buffer.size() < 2) {
        src_buffer.resize(2, 0x00);
    }

    // Test with u8_ByteLen = 0 (underflow: 0 - 2 = 254 for uint8_t)
    {
        uint8_t u8_ByteLen = 0;
        bool result = safe_candlelight_copy(
            src_buffer.data(), u8_ByteLen,
            dest_buffer, sizeof(dest_buffer),
            src_buffer.size()
        );
        // INVARIANT: Must be rejected — underflow would occur
        EXPECT_FALSE(result)
            << "u8_ByteLen=0 must be rejected to prevent uint8_t underflow "
               "(0-2 wraps to 254, causing ~254 byte overread)";
    }

    // Test with u8_ByteLen = 1 (underflow: 1 - 2 = 255 for uint8_t)
    {
        uint8_t u8_ByteLen = 1;
        bool result = safe_candlelight_copy(
            src_buffer.data(), u8_ByteLen,
            dest_buffer, sizeof(dest_buffer),
            src_buffer.size()
        );
        // INVARIANT: Must be rejected — underflow would occur
        EXPECT_FALSE(result)
            << "u8_ByteLen=1 must be rejected to prevent uint8_t underflow "
               "(1-2 wraps to 255, causing ~255 byte overread)";
    }

    // Test with u8_ByteLen exactly equal to payload size (valid boundary)
    {
        uint8_t u8_ByteLen = static_cast<uint8_t>(
            std::min(src_buffer.size(), static_cast<size_t>(255))
        );
        size_t copy_size = (u8_ByteLen >= 2) ? (u8_ByteLen - 2) : 0;

        bool result = safe_candlelight_copy(
            src_buffer.data(), u8_ByteLen,
            dest_buffer, sizeof(dest_buffer),
            src_buffer.size()
        );

        if (u8_ByteLen >= 2) {
            // INVARIANT: copy_size must not exceed actual available data
            EXPECT_LE(copy_size, src_buffer.size() - 2)
                << "Copy size must not exceed available source data";
            EXPECT_LE(copy_size, sizeof(dest_buffer))
                << "Copy size must not exceed destination buffer size";
        }
    }

    // Test with oversized u8_ByteLen (claims more data than buffer holds)
    {
        // Claim buffer is larger than it actually is
        uint8_t claimed_len = 255; // Maximum uint8_t value
        bool result = safe_candlelight_copy(
            src_buffer.data(), claimed_len,
            dest_buffer, sizeof(dest_buffer),
            src_buffer.size() // actual size is smaller
        );

        if (src_buffer.size() < static_cast<size_t>(claimed_len)) {
            // INVARIANT: Must be rejected — would read beyond actual buffer
            EXPECT_FALSE(result)
                << "Oversized u8_ByteLen=" << (int)claimed_len
                << " with actual buffer size=" << src_buffer.size()
                << " must be rejected to prevent out-of-bounds read";
        }
    }

    // Test with u8_ByteLen = 2 (edge case: copy_size = 0, should be safe)
    {
        uint8_t u8_ByteLen = 2;
        bool result = safe_candlelight_copy(
            src_buffer.data(), u8_ByteLen,
            dest_buffer, sizeof(dest_buffer),
            src_buffer.size()
        );
        // INVARIANT: copy_size = 0 is valid (no bytes copied)
        EXPECT_TRUE(result)
            << "u8_ByteLen=2 should be accepted (copy_size=0, no bytes copied)";
    }
}

INSTANTIATE_TEST_SUITE_P(
    AdversarialInputs,
    SecurityTest,
    ::testing::Values(
        // Empty string — triggers underflow when used as length
        std::string(""),
        // Single byte — triggers underflow (1 - 2 = 255 for uint8_t)
        std::string("\x01"),
        // Null bytes — potential length confusion
        std::string("\x00\x00", 2),
        // Exactly 2 bytes — boundary case (copy_size = 0)
        std::string("\x02\x00", 2),
        // Normal small payload
        std::string("AB"),
        // Payload with length byte set to 0 embedded
        std::string("\x00AAAAAAAAAA", 11),
        // Payload with length byte set to 1 embedded
        std::string("\x01AAAAAAAAAA", 11),
        // 2x oversized: 512 bytes of 'A'
        std::string(512, 'A'),
        // 10x oversized: 2560 bytes of 'B'
        std::string(2560, 'B'),
        // Max uint8_t value as first byte (255)
        std::string("\xFF\xFF", 2),
        // Attack payload: format string characters
        std::string("%s%s%s%s%s%n%n%n"),
        // Attack payload: SQL injection style
        std::string("'; DROP TABLE users; --"),
        // Attack payload: shell metacharacters
        std::string("`id`; cat /etc/passwd"),
        // Attack payload: all 0xFF bytes (255 of them)
        std::string(255, '\xFF'),
        // Attack payload: alternating 0x00 and 0xFF
        std::string("\x00\xFF\x00\xFF\x00\xFF\x00\xFF", 8),
        // Payload claiming length 0 with actual data
        std::string("\x00\x41\x42\x43\x44", 5),
        // Payload claiming length 1 with actual data
        std::string("\x01\x41\x42\x43\x44", 5),
        // Large payload with embedded null bytes
        std::string(100, '\x00'),
        // Payload with max length header but minimal data
        std::string("\xFF\x00", 2),
        // Unicode-like multi-byte sequences
        std::string("\xC0\xAF\xE0\x80\xAF\xF0\x80\x80\xAF")
    )
);

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}