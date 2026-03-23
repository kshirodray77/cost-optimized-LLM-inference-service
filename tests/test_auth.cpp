#include <gtest/gtest.h>
#include "middleware/auth.h"
#include "config.h"

using namespace gateway;

TEST(AuthTest, CreateAndAuthenticate) {
    auto key = AuthMiddleware::instance().create_key("test-user", "pro");

    EXPECT_FALSE(key.key_id.empty());
    EXPECT_FALSE(key.key_hash.empty());
    EXPECT_EQ(key.owner, "test-user");
    EXPECT_EQ(key.tier, "pro");
    EXPECT_EQ(key.rate_limit_rpm, 120);

    // Authenticate with the raw key
    std::string raw_key = key.key_hash;
    auto result = AuthMiddleware::instance().authenticate("Bearer " + raw_key);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->owner, "test-user");
}

TEST(AuthTest, RejectInvalidKey) {
    auto result = AuthMiddleware::instance().authenticate("Bearer invalid-key-12345");
    EXPECT_FALSE(result.has_value());
}

TEST(AuthTest, RevokeKey) {
    auto key = AuthMiddleware::instance().create_key("revoke-test", "free");
    std::string raw = key.key_hash;

    // Should work before revoke
    EXPECT_TRUE(AuthMiddleware::instance().authenticate("Bearer " + raw).has_value());

    // Revoke
    bool ok = AuthMiddleware::instance().revoke_key(key.key_id);
    EXPECT_TRUE(ok);

    // Should fail after revoke
    EXPECT_FALSE(AuthMiddleware::instance().authenticate("Bearer " + raw).has_value());
}

TEST(AuthTest, TierLimits) {
    auto free_key = AuthMiddleware::instance().create_key("user1", "free");
    auto pro_key  = AuthMiddleware::instance().create_key("user2", "pro");
    auto team_key = AuthMiddleware::instance().create_key("user3", "team");

    EXPECT_EQ(free_key.rate_limit_rpm, 20);
    EXPECT_EQ(pro_key.rate_limit_rpm, 120);
    EXPECT_EQ(team_key.rate_limit_rpm, 600);
}

TEST(AuthTest, AdminCheck) {
    auto& cfg = Config::instance().get_mut();
    cfg.server.admin_key = "secret-admin-key";

    EXPECT_TRUE(AuthMiddleware::instance().is_admin("secret-admin-key"));
    EXPECT_FALSE(AuthMiddleware::instance().is_admin("wrong-key"));
    EXPECT_FALSE(AuthMiddleware::instance().is_admin(""));
}
