#include <gtest/gtest.h>
#include "core/Order.h"

using namespace lob::core;

class OrderTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Setup code before each test
    }

    void TearDown() override
    {
        // Cleanup code after each test
    }
};

// ============================================================================
// Week 1 Tests
// ============================================================================

TEST_F(OrderTest, DefaultConstructor)
{
    Order order;
    EXPECT_EQ(order.order_id, 0);
    EXPECT_EQ(order.quantity, 0);
    EXPECT_EQ(order.status, OrderStatus::NEW);
}

TEST_F(OrderTest, ParameterizedConstructor)
{
    Order order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);

    EXPECT_EQ(order.order_id, 1);
    EXPECT_EQ(order.timestamp, 1000);
    EXPECT_EQ(order.price, 10000);
    EXPECT_EQ(order.quantity, 100);
    EXPECT_EQ(order.remaining_quantity, 100);
    EXPECT_EQ(order.side, Side::BUY);
    EXPECT_EQ(order.type, OrderType::LIMIT);
    EXPECT_EQ(order.status, OrderStatus::NEW);
}

TEST_F(OrderTest, IsFilledWhenNew)
{
    Order order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    EXPECT_FALSE(order.is_filled());
}

TEST_F(OrderTest, IsFilledWhenFullyFilled)
{
    Order order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    order.remaining_quantity = 0;
    EXPECT_TRUE(order.is_filled());
}

TEST_F(OrderTest, IsActive)
{
    Order order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    EXPECT_TRUE(order.is_active());

    order.status = OrderStatus::CANCELLED;
    EXPECT_FALSE(order.is_active());
}

TEST_F(OrderTest, PartialFill)
{
    Order order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    order.fill(30);

    EXPECT_EQ(order.remaining_quantity, 70);
    EXPECT_EQ(order.status, OrderStatus::PARTIAL);
    EXPECT_FALSE(order.is_filled());
}

TEST_F(OrderTest, CompleteFill)
{
    Order order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    order.fill(100);

    EXPECT_EQ(order.remaining_quantity, 0);
    EXPECT_EQ(order.status, OrderStatus::FILLED);
    EXPECT_TRUE(order.is_filled());
}

TEST_F(OrderTest, CancelOrder)
{
    Order order(1, 1000, 10000, 100, Side::BUY, OrderType::LIMIT);
    order.cancel();

    EXPECT_EQ(order.status, OrderStatus::CANCELLED);
    EXPECT_FALSE(order.is_active());
}

TEST_F(OrderTest, SizeOptimization)
{
    // Verify order fits in cache line (target: 64 bytes for cache efficiency)
    EXPECT_LE(sizeof(Order), 64) << "Order should fit in single cache line";
}
