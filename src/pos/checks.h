// Copyright (c) 2019 The Ion developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ION_CHECKS_H
#define ION_CHECKS_H

#include "consensus/validation.h"
#include "libzerocoin/CoinSpend.h"
#include "primitives/transaction.h"

bool IsBlockHashInChain(const uint256& hashBlock);
bool IsTransactionInChain(const uint256& txId, int& nHeightTx, CTransactionRef& tx);
bool IsTransactionInChain(const uint256& txId, int& nHeightTx);

bool CheckPublicCoinSpendEnforced(int blockHeight, bool isPublicSpend);

bool ContextualCheckZerocoinSpend(const CTransaction& tx, const libzerocoin::CoinSpend* spend, CBlockIndex* pindex, const uint256& hashBlock);
bool ContextualCheckZerocoinSpendNoSerialCheck(const CTransaction& tx, const libzerocoin::CoinSpend* spend, CBlockIndex* pindex, const uint256& hashBlock);
bool ContextualCheckZerocoinMint(const libzerocoin::PublicCoin& coin, const CBlockIndex* pindex);

bool CheckZerocoinSpendTx(CBlockIndex *pindex, CValidationState& state, const CTransaction& tx, std::vector<uint256>& vSpendsInBlock, std::vector<std::pair<libzerocoin::CoinSpend, uint256> >& vSpends, std::vector<std::pair<libzerocoin::PublicCoin, uint256> >& vMints, CAmount& nValueIn);

#endif //ION_CHECKS_H
