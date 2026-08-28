#include <vosp/security.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {
class Checks {
  public:
    void expect(bool condition, std::string_view message) {
        if (!condition) {
            ++failures_;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    [[nodiscard]] int result() const noexcept {
        return failures_ == 0 ? 0 : 1;
    }

  private:
    int failures_ = 0;
};

struct TestAuthenticator {
    using TagType = std::array<std::byte, 4>;

    [[nodiscard]] vosp::security::Result<TagType>
    authenticate(std::span<const std::byte> input) const {
        TagType tag{};
        for (std::size_t index = 0; index < input.size(); ++index) {
            tag[index % tag.size()] ^= input[index];
        }
        return tag;
    }

    [[nodiscard]] vosp::security::OperationResult verify(std::span<const std::byte> input,
                                                         const TagType& tag) const {
        auto expected = authenticate(input);
        if (expected && vosp::security::constant_time_equal(*expected, tag)) {
            return {};
        }
        return std::unexpected{vosp::security::Error{
            vosp::security::ErrorCode::authentication_failed, "test tag mismatch"}};
    }
};

static_assert(vosp::contracts::MessageAuthenticator<TestAuthenticator, vosp::security::Model>);

enum class Permission : std::int8_t { read = 0, write = 1, administer = 2 };

void test_secure_erase_and_buffer(Checks& checks) {
    std::array bytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    vosp::security::secure_erase(bytes);
    checks.expect(std::ranges::all_of(bytes, [](std::byte value) { return value == std::byte{0}; }),
                  "secure_erase must overwrite every byte");

    const std::array secret{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
    auto buffer = vosp::security::SecureBuffer::copy_from(secret);
    checks.expect(buffer && buffer->size() == secret.size(),
                  "secure buffer must copy bounded data");
    checks.expect(buffer && vosp::security::constant_time_equal(buffer->bytes(), secret),
                  "secure buffer must preserve source bytes");
    if (buffer) {
        auto moved = std::move(*buffer);
        checks.expect(buffer->empty(), "moved-from secure buffer must not retain the secret");
        moved.clear();
        checks.expect(moved.empty(), "clear must erase and release the logical secret");
    }

    checks.expect(!vosp::security::SecureBuffer::copy_from({}), "empty secret must be rejected");
    checks.expect(!vosp::security::SecureBuffer::copy_from(secret, 2),
                  "secret larger than policy must be rejected");
    checks.expect(!vosp::security::SecureBuffer::copy_from(
                      secret, vosp::security::SecureBuffer::hard_maximum_size + 1U),
                  "policy above hard maximum must be rejected");
}

void test_constant_time_comparison(Checks& checks) {
    const std::array first{std::byte{1}, std::byte{2}, std::byte{3}};
    const std::array equal = first;
    const std::array different{std::byte{1}, std::byte{2}, std::byte{4}};
    const std::array short_value{std::byte{1}, std::byte{2}};
    checks.expect(vosp::security::constant_time_equal(first, equal),
                  "equal tags must compare equal");
    checks.expect(!vosp::security::constant_time_equal(first, different),
                  "different tags must compare unequal");
    checks.expect(!vosp::security::constant_time_equal(first, short_value),
                  "different public lengths must compare unequal");
}

void test_permissions(Checks& checks) {
    vosp::security::PermissionSet<Permission, 3> permissions;
    checks.expect(permissions.empty(), "permission set must start empty");
    checks.expect(permissions.grant(Permission::read), "valid permission must be granted");
    checks.expect(permissions.grant(Permission::administer),
                  "second valid permission must be granted");
    checks.expect(permissions.allows(Permission::read) && !permissions.allows(Permission::write),
                  "permission lookup must use independent bits");
    checks.expect(!permissions.grant(static_cast<Permission>(-1)),
                  "negative permission must be rejected");
    checks.expect(!permissions.grant(static_cast<Permission>(3)),
                  "out-of-range permission must be rejected");
    checks.expect(permissions.revoke(Permission::read) && !permissions.allows(Permission::read),
                  "revocation must clear one bit");
    permissions.clear();
    checks.expect(permissions.empty(), "clear must remove every permission");
}

void test_authentication_extensions(Checks& checks) {
    const std::array input{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    TestAuthenticator authenticator;
    auto tag = authenticator.authenticate(input);
    checks.expect(tag && authenticator.verify(input, *tag),
                  "compatible authenticator must verify its own tag");
    auto extension = vosp::security::authentication_extension(*tag);
    checks.expect(extension.has_value(), "authentication tag must create a VSP1 extension");
    auto view = vosp::security::authentication_tag(*extension);
    checks.expect(view && vosp::security::constant_time_equal(*view, *tag),
                  "authentication extension must preserve opaque tag bytes");

    checks.expect(!vosp::security::authentication_extension({}),
                  "empty authentication tag must be rejected");
    checks.expect(!vosp::security::authentication_extension(*tag, 2),
                  "authentication tag limit must be enforced");
    const vosp::protocol::Extension wrong{vosp::protocol::ExtensionId::checksum,
                                          std::vector<std::byte>{std::byte{1}}};
    checks.expect(!vosp::security::authentication_tag(wrong),
                  "non-authentication extension must be rejected");
}
} // namespace

int main() {
    Checks checks;
    test_secure_erase_and_buffer(checks);
    test_constant_time_comparison(checks);
    test_permissions(checks);
    test_authentication_extensions(checks);
    return checks.result();
}
