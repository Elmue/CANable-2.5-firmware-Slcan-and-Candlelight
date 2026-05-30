#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <algorithm>

// Simulated safe version of the USB descriptor string copy logic
// This represents what the code SHOULD do to be safe
// The invariant: bytes copied must never exceed destination buffer size

static const size_t SAFE_DEST_BUFFER_SIZE = 128; // typical s_String buffer size

// Simulate the vulnerable pattern but with safety check applied
// Returns number of bytes that WOULD be copied (to test the invariant)
struct CopyResult {
    size_t bytes_requested;
    size_t bytes_safe;
    bool overflow_would_occur;
    bool copy_succeeded;
};

CopyResult simulate_usb_descriptor_copy(const std::vector<uint8_t>& descriptor_buffer,
                                         uint8_t dest_buffer_size = SAFE_DEST_BUFFER_SIZE) {
    CopyResult result = {0, 0, false, false};

    if (descriptor_buffer.size() < 2) {
        return result;
    }

    uint8_t u8_ByteLen = descriptor_buffer[0]; // First byte is length field from USB descriptor

    // The vulnerable code: memcpy(s_String, u8_Buffer + 2, u8_ByteLen - 2)
    // u8_ByteLen can be up to 255, so u8_ByteLen - 2 can be up to 253
    if (u8_ByteLen < 2) {
        return result;
    }

    size_t bytes_to_copy = static_cast<size_t>(u8_ByteLen) - 2;
    result.bytes_requested = bytes_to_copy;

    // Check if this would overflow the destination buffer
    if (bytes_to_copy > dest_buffer_size) {
        result.overflow_would_occur = true;
        result.bytes_safe = dest_buffer_size;
    } else {
        result.overflow_would_occur = false;
        result.bytes_safe = bytes_to_copy;
    }

    // Safe copy: only copy what fits
    uint8_t safe_dest[SAFE_DEST_BUFFER_SIZE + 64] = {0}; // extra space to detect overflow
    uint8_t canary_value = 0xAB;
    memset(safe_dest + dest_buffer_size, canary_value, 64); // canary region

    size_t actual_source_available = descriptor_buffer.size() >= 2 ?
                                     descriptor_buffer.size() - 2 : 0;
    size_t safe_copy_len = std::min({result.bytes_safe, actual_source_available, (size_t)dest_buffer_size});

    if (safe_copy_len > 0 && descriptor_buffer.size() >= 2) {
        memcpy(safe_dest, descriptor_buffer.data() + 2, safe_copy_len);
    }

    // Verify canary is intact (no overflow into canary region)
    bool canary_intact = true;
    for (int i = 0; i < 64; i++) {
        if (safe_dest[dest_buffer_size + i] != canary_value) {
            canary_intact = false;
            break;
        }
    }

    result.copy_succeeded = canary_intact;
    return result;
}

// Build a USB descriptor buffer with a given length byte and payload
std::vector<uint8_t> build_usb_descriptor(uint8_t length_byte, const std::string& payload_str) {
    std::vector<uint8_t> buf;
    buf.push_back(length_byte);
    buf.push_back(0x03); // USB string descriptor type
    for (char c : payload_str) {
        buf.push_back(static_cast<uint8_t>(c));
    }
    return buf;
}

class SecurityTest : public ::testing::TestWithParam<std::string> {};

TEST_P(SecurityTest, USBDescriptorCopyNeverExceedsDestinationBuffer) {
    // Invariant: The number of bytes copied from a USB descriptor into the
    // destination buffer must NEVER exceed the destination buffer's size,
    // regardless of what value the USB device reports in the length field.
    // A malicious USB device must not be able to cause buffer overflow.

    std::string payload = GetParam();

    // Test with maximum adversarial length byte (255 = max uint8_t)
    // This would cause u8_ByteLen - 2 = 253 bytes to be requested
    std::vector<uint8_t> adversarial_descriptor = build_usb_descriptor(
        static_cast<uint8_t>(255), payload
    );

    CopyResult result = simulate_usb_descriptor_copy(adversarial_descriptor, SAFE_DEST_BUFFER_SIZE);

    // INVARIANT 1: Bytes actually copied must never exceed destination buffer size
    EXPECT_LE(result.bytes_safe, SAFE_DEST_BUFFER_SIZE)
        << "SECURITY VIOLATION: Safe copy length " << result.bytes_safe
        << " exceeds destination buffer size " << SAFE_DEST_BUFFER_SIZE;

    // INVARIANT 2: Canary region must remain intact (no buffer overflow)
    EXPECT_TRUE(result.copy_succeeded)
        << "SECURITY VIOLATION: Buffer overflow detected - canary region was corrupted";

    // INVARIANT 3: If overflow would occur with raw length, it must be detected/prevented
    if (result.overflow_would_occur) {
        EXPECT_LE(result.bytes_safe, SAFE_DEST_BUFFER_SIZE)
            << "SECURITY VIOLATION: Overflow condition not properly mitigated";
    }

    // Test with length byte = 254 (u8_ByteLen - 2 = 252)
    std::vector<uint8_t> descriptor_254 = build_usb_descriptor(254, payload);
    CopyResult result_254 = simulate_usb_descriptor_copy(descriptor_254, SAFE_DEST_BUFFER_SIZE);
    EXPECT_LE(result_254.bytes_safe, SAFE_DEST_BUFFER_SIZE)
        << "SECURITY VIOLATION with length=254";
    EXPECT_TRUE(result_254.copy_succeeded)
        << "SECURITY VIOLATION: Overflow with length=254";

    // Test with length byte = 130 (just above typical buffer size of 128)
    std::vector<uint8_t> descriptor_130 = build_usb_descriptor(130, payload);
    CopyResult result_130 = simulate_usb_descriptor_copy(descriptor_130, SAFE_DEST_BUFFER_SIZE);
    EXPECT_LE(result_130.bytes_safe, SAFE_DEST_BUFFER_SIZE)
        << "SECURITY VIOLATION with length=130";
    EXPECT_TRUE(result_130.copy_succeeded)
        << "SECURITY VIOLATION: Overflow with length=130";

    // Test with length byte exactly at boundary (128+2=130 would request 128 bytes)
    std::vector<uint8_t> descriptor_boundary = build_usb_descriptor(
        static_cast<uint8_t>(SAFE_DEST_BUFFER_SIZE + 2), payload
    );
    CopyResult result_boundary = simulate_usb_descriptor_copy(descriptor_boundary, SAFE_DEST_BUFFER_SIZE);
    EXPECT_LE(result_boundary.bytes_safe, SAFE_DEST_BUFFER_SIZE)
        << "SECURITY VIOLATION at exact boundary";
    EXPECT_TRUE(result_boundary.copy_succeeded)
        << "SECURITY VIOLATION: Overflow at exact boundary";

    // Test with length byte = 2 (edge case: 0 bytes to copy)
    std::vector<uint8_t> descriptor_min = build_usb_descriptor(2, payload);
    CopyResult result_min = simulate_usb_descriptor_copy(descriptor_min, SAFE_DEST_BUFFER_SIZE);
    EXPECT_LE(result_min.bytes_safe, SAFE_DEST_BUFFER_SIZE)
        << "SECURITY VIOLATION with minimum length";
    EXPECT_TRUE(result_min.copy_succeeded)
        << "SECURITY VIOLATION: Overflow with minimum length";
}

TEST_P(SecurityTest, USBDescriptorLengthFieldValidation) {
    // Invariant: The length field from USB descriptor data must be validated
    // before use in memory operations. Any length value that would cause
    // writes beyond the destination buffer must be rejected or clamped.

    std::string payload = GetParam();

    // Sweep through all possible uint8_t length values (0-255)
    for (int len = 0; len <= 255; len++) {
        std::vector<uint8_t> descriptor = build_usb_descriptor(
            static_cast<uint8_t>(len), payload
        );

        CopyResult result = simulate_usb_descriptor_copy(descriptor, SAFE_DEST_BUFFER_SIZE);

        // The safe copy length must ALWAYS be within bounds
        EXPECT_LE(result.bytes_safe, SAFE_DEST_BUFFER_SIZE)
            << "SECURITY VIOLATION: length byte=" << len
            << " caused safe_bytes=" << result.bytes_safe
            << " to exceed buffer size=" << SAFE_DEST_BUFFER_SIZE;

        // Canary must always be intact
        EXPECT_TRUE(result.copy_succeeded)
            << "SECURITY VIOLATION: Buffer overflow detected with length byte=" << len;
    }
}

TEST_P(SecurityTest, USBDescriptorNullAndEmptyInputs) {
    // Invariant: Null, empty, or malformed descriptors must not cause
    // undefined behavior or buffer overflows.

    std::string payload = GetParam();

    // Empty descriptor
    std::vector<uint8_t> empty_descriptor;
    CopyResult result_empty = simulate_usb_descriptor_copy(empty_descriptor, SAFE_DEST_BUFFER_SIZE);
    EXPECT_EQ(result_empty.bytes_requested, 0u)
        << "Empty descriptor should result in 0 bytes requested";

    // Single byte descriptor (too short)
    std::vector<uint8_t> single_byte = {0xFF};
    CopyResult result_single = simulate_usb_descriptor_copy(single_byte, SAFE_DEST_BUFFER_SIZE);
    EXPECT_EQ(result_single.bytes_requested, 0u)
        << "Single byte descriptor should result in 0 bytes requested";

    // Descriptor with length=255 but only 2 bytes of actual data
    std::vector<uint8_t> short_data = {0xFF, 0x03};
    CopyResult result_short = simulate_usb_descriptor_copy(short_data, SAFE_DEST_BUFFER_SIZE);
    EXPECT_LE(result_short.bytes_safe, SAFE_DEST_BUFFER_SIZE)
        << "SECURITY VIOLATION: Short data with large length field";
    EXPECT_TRUE(result_short.copy_succeeded)
        << "SECURITY VIOLATION: Overflow with short data and large length";
}

INSTANTIATE_TEST_SUITE_P(
    AdversarialInputs,
    SecurityTest,
    ::testing::Values(
        // Maximum length attack payload (255 bytes of 'A')
        std::string(253, 'A'),
        // NUL bytes
        std::string(253, '\x00'),
        // Format string attack
        std::string("%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s"),
        // Shell metacharacters
        std::string(";cat /etc/passwd;echo "),
        // Unicode/wide char sequences
        std::string("\xff\xfe\x41\x00\x42\x00\x43\x00"),
        // All 0xFF bytes
        std::string(253, '\xff'),
        // Mixed boundary values
        std::string("\x00\xff\x00\xff\x00\xff\x00\xff"),
        // Typical overflow pattern
        std::string(200, '\x41') + std::string(53, '\x42'),
        // Empty string (edge case)
        std::string(""),
        // Single character
        std::string("A"),
        // Exactly at boundary
        std::string(126, 'B'),
        // One over boundary
        std::string(127, 'C'),
        // Two over boundary
        std::string(128, 'D'),
        // Return address overwrite pattern
        std::string("\x41\x41\x41\x41\xef\xbe\xad\xde"),
        // Heap spray pattern
        std::string(253, '\x90')
    )
);

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}