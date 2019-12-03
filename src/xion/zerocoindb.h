// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2019 The ION Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_ZEROCOINDB_H
#define PIVX_ZEROCOINDB_H

#include "dbwrapper.h"
#include "xion/zerocoin.h"
#include "libzerocoin/Coin.h"
#include "libzerocoin/CoinSpend.h"

/** Zerocoin database (zerocoin/) */
class CZerocoinDB : public CDBWrapper
{
public:
    CZerocoinDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CZerocoinDB(const CZerocoinDB&);
    void operator=(const CZerocoinDB&);

public:
    /** Write xION mints to the zerocoinDB in a batch */
    bool WriteCoinMintBatch(const std::vector<std::pair<libzerocoin::PublicCoin, uint256> >& mintInfo);
    bool ReadCoinMint(const CBigNum& bnPubcoin, uint256& txHash);
    bool ReadCoinMint(const uint256& hashPubcoin, uint256& hashTx);
    /** Write xION spends to the zerocoinDB in a batch */
    bool WriteCoinSpendBatch(const std::vector<std::pair<libzerocoin::CoinSpend, uint256> >& spendInfo);
    bool ReadCoinSpend(const CBigNum& bnSerial, uint256& txHash);
    bool ReadCoinSpend(const uint256& hashSerial, uint256 &txHash);
    bool EraseCoinMint(const CBigNum& bnPubcoin);
    bool EraseCoinSpend(const CBigNum& bnSerial);
    bool WipeCoins(std::string strType);
    bool WriteAccumulatorValue(const uint32_t& nChecksum, const CBigNum& bnValue);
    bool ReadAccumulatorValue(const uint32_t& nChecksum, CBigNum& bnValue);
    bool EraseAccumulatorValue(const uint32_t& nChecksum);
};

#endif //PIVX_ZEROCOINDB_H
