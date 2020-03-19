// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <policy/feerate.h>
#include <test/test_dash.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(amount_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(MoneyRangeTest)
{
    BOOST_CHECK_EQUAL(MoneyRange(CAmount(-1)), false);
    BOOST_CHECK_EQUAL(MoneyRange(MAX_MONEY + CAmount(1)), false);
    BOOST_CHECK_EQUAL(MoneyRange(CAmount(1)), true);
}

BOOST_AUTO_TEST_CASE(GetFeeTest)
{
    CFeeRate feeRate, altFeeRate;

    feeRate = CFeeRate(0);
    // Must always return 0
    BOOST_CHECK_EQUAL(feeRate.GetFee(0), 0);
    BOOST_CHECK_EQUAL(feeRate.GetFee(1e5), 0);

    feeRate = CFeeRate(1000);
    // Must always just return the arg
    BOOST_CHECK_EQUAL(feeRate.GetFee(0), 0);
    BOOST_CHECK_EQUAL(feeRate.GetFee(1), 1);
    BOOST_CHECK_EQUAL(feeRate.GetFee(121), 121);
    BOOST_CHECK_EQUAL(feeRate.GetFee(999), 999);
    BOOST_CHECK_EQUAL(feeRate.GetFee(1e3), 1e3);
    BOOST_CHECK_EQUAL(feeRate.GetFee(9e3), 9e3);

    feeRate = CFeeRate(-1000);
    // Must always just return -1 * arg
    BOOST_CHECK_EQUAL(feeRate.GetFee(0), 0);
    BOOST_CHECK_EQUAL(feeRate.GetFee(1), -1);
    BOOST_CHECK_EQUAL(feeRate.GetFee(121), -121);
    BOOST_CHECK_EQUAL(feeRate.GetFee(999), -999);
    BOOST_CHECK_EQUAL(feeRate.GetFee(1e3), -1e3);
    BOOST_CHECK_EQUAL(feeRate.GetFee(9e3), -9e3);

    feeRate = CFeeRate(123);
    // Truncates the result, if not integer
    BOOST_CHECK_EQUAL(feeRate.GetFee(0), 0);
    BOOST_CHECK_EQUAL(feeRate.GetFee(8), 1); // Special case: returns 1 instead of 0
    BOOST_CHECK_EQUAL(feeRate.GetFee(9), 1);
    BOOST_CHECK_EQUAL(feeRate.GetFee(121), 14);
    BOOST_CHECK_EQUAL(feeRate.GetFee(122), 15);
    BOOST_CHECK_EQUAL(feeRate.GetFee(999), 122);
    BOOST_CHECK_EQUAL(feeRate.GetFee(1e3), 123);
    BOOST_CHECK_EQUAL(feeRate.GetFee(9e3), 1107);

    feeRate = CFeeRate(-123);
    // Truncates the result, if not integer
    BOOST_CHECK_EQUAL(feeRate.GetFee(0), 0);
    BOOST_CHECK_EQUAL(feeRate.GetFee(8), -1); // Special case: returns -1 instead of 0
    BOOST_CHECK_EQUAL(feeRate.GetFee(9), -1);

    // check alternate constructor
    feeRate = CFeeRate(1000);
    altFeeRate = CFeeRate(feeRate);
    BOOST_CHECK_EQUAL(feeRate.GetFee(100), altFeeRate.GetFee(100));

    // Check full constructor
    // default value
    BOOST_CHECK(CFeeRate(CAmount(-1), 1000) == CFeeRate(-1));
    BOOST_CHECK(CFeeRate(CAmount(0), 1000) == CFeeRate(0));
    BOOST_CHECK(CFeeRate(CAmount(1), 1000) == CFeeRate(1));
    // lost precision (can only resolve satoshis per kB)
    BOOST_CHECK(CFeeRate(CAmount(1), 1001) == CFeeRate(0));
    BOOST_CHECK(CFeeRate(CAmount(2), 1001) == CFeeRate(1));
    // some more integer checks
    BOOST_CHECK(CFeeRate(CAmount(26), 789) == CFeeRate(32));
    BOOST_CHECK(CFeeRate(CAmount(27), 789) == CFeeRate(34));
    // Maximum size in bytes, should not crash
    CFeeRate(MAX_MONEY, std::numeric_limits<size_t>::max() >> 1).GetFeePerK();
}

BOOST_AUTO_TEST_CASE(BinaryOperatorTest)
{
    CFeeRate a, b;
    a = CFeeRate(1);
    b = CFeeRate(2);
    BOOST_CHECK(a < b);
    BOOST_CHECK(b > a);
    BOOST_CHECK(a == a);
    BOOST_CHECK(a <= b);
    BOOST_CHECK(a <= a);
    BOOST_CHECK(b >= a);
    BOOST_CHECK(b >= b);
    // a should be 0.00000002 ION/kB now
    a += a;
    BOOST_CHECK(a == b);
}

BOOST_AUTO_TEST_CASE(ToStringTest)
{
    CFeeRate feeRate;
    feeRate = CFeeRate(1);
    BOOST_CHECK_EQUAL(feeRate.ToString(), "0.00000001 ION/kB");
}

BOOST_AUTO_TEST_CASE(RewardTest)
{
    uint256 grpData1;
    grpData1.SetHex("0006f0bfe02cb5baf8517503f3cc274f04f755837b00814574d33e9eb0f3b65a");
    CTokenGroupID tgID1(grpData1);

    uint256 grpData2;
    grpData2.SetHex("00bc4fd09e023474f0acdfe75cc2a3323899339d88e5eeef4b1fd64ec6ae9d6c");
    CTokenGroupID tgID2(grpData2);

    CReward reward1(CReward::RewardType::REWARD_COINBASE, 0.02 * COIN, tgID1, 200);

    CReward reward2a(CReward::RewardType::REWARD_COINBASE, 0.01 * COIN, tgID1, 200);
    CReward reward2b(CReward::RewardType::REWARD_COINBASE, 0.02 * COIN, tgID1, 200);
    CReward reward2c(CReward::RewardType::REWARD_COINBASE, 0.03 * COIN, tgID1, 200);

    CReward reward3a(CReward::RewardType::REWARD_COINBASE, 0.02 * COIN, tgID1, 100);
    CReward reward3b(CReward::RewardType::REWARD_COINBASE, 0.02 * COIN, tgID1, 200);
    CReward reward3c(CReward::RewardType::REWARD_COINBASE, 0.02 * COIN, tgID1, 300);

    CReward reward4a(CReward::RewardType::REWARD_COINBASE, 0.01 * COIN, tgID1, 100);
    CReward reward4b(CReward::RewardType::REWARD_COINBASE, 0.03 * COIN, tgID1, 100);
    CReward reward4c(CReward::RewardType::REWARD_COINBASE, 0.02 * COIN, tgID1, 100);
    reward4a.AddRewardAmounts(0, tgID2, 100);
    CReward reward1a = reward1;
    reward1a.AddRewardAmounts(0, tgID1, 100);
    reward4a.AddRewardAmounts(0.01 * COIN, tgID1, 200);
    reward1a.AddRewardAmounts(0.01 * COIN, tgID2, 301);
    reward4a.AddRewardAmounts(0.01 * COIN, tgID2, 200);
    reward4b.AddRewardAmounts(0, tgID2, 100);
    reward4c.AddRewardAmounts(0, tgID1, 100);

    BOOST_CHECK(reward1 > reward2a);
    BOOST_CHECK(reward1 == reward2b);
    BOOST_CHECK(reward1 < reward2c);

    BOOST_CHECK(reward1 > reward3a);
    BOOST_CHECK(reward1 == reward3b);
    BOOST_CHECK(reward1 < reward3c);

    BOOST_CHECK(reward1a <= reward4a);
    BOOST_CHECK(reward1 > reward4b);
    BOOST_CHECK(reward1 == reward4c);
}

BOOST_AUTO_TEST_SUITE_END()
