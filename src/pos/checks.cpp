// Copyright (c) 2019 The Ion developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "chainparams.h"
#include "checks.h"
#include "util.h"

#include "validation.h"
#include "xion/xionchain.h"
#include "xion/xionmodule.h"
#include "xion/zerocoindb.h"

bool IsBlockHashInChain(const uint256& hashBlock)
{
    if (hashBlock == uint256() || !mapBlockIndex.count(hashBlock))
        return false;

    return chainActive.Contains(mapBlockIndex[hashBlock]);
}

bool IsTransactionInChain(const uint256& txId, int& nHeightTx, CTransactionRef& tx)
{
    uint256 hashBlock;
    if (!GetTransaction(txId, tx, Params().GetConsensus(), hashBlock, true))
        return false;
    if (!IsBlockHashInChain(hashBlock))
        return false;

    nHeightTx = mapBlockIndex.at(hashBlock)->nHeight;
    return true;
}

bool IsTransactionInChain(const uint256& txId, int& nHeightTx)
{
    CTransactionRef tx;
    return IsTransactionInChain(txId, nHeightTx, tx);
}

bool CheckPublicCoinSpendEnforced(int blockHeight, bool isPublicSpend) {
    if (blockHeight >= Params().GetConsensus().nPublicZCSpends) { //nPublicZCSpends
        // reject old coin spend
        if (!isPublicSpend) {
            return error("%s: failed to add block with older zc spend version", __func__);
        }

    } else {
        if (isPublicSpend) {
            return error("%s: failed to add block, public spend enforcement not activated", __func__);
        }
    }
    return true;
}

bool isBlockBetweenFakeSerialAttackRange(int nHeight)
{
    if (Params().NetworkIDString() != CBaseChainParams::MAIN)
        return false;

    return nHeight <= Params().GetConsensus().nFakeSerialBlockheightEnd;
}

bool ContextualCheckZerocoinSpend(const CTransaction& tx, const libzerocoin::CoinSpend* spend, CBlockIndex* pindex, const uint256& hashBlock)
{
    if(!ContextualCheckZerocoinSpendNoSerialCheck(tx, spend, pindex, hashBlock)){
        return false;
    }

/*
    //Reject serial's that are already in the blockchain
    int nHeightTx = 0;
    if (IsSerialInBlockchain(spend->getCoinSerialNumber(), nHeightTx))
        return error("%s : zPIV spend with serial %s is already in block %d\n", __func__,
                     spend->getCoinSerialNumber().GetHex(), nHeightTx);

*/
    return true;
}

bool ContextualCheckZerocoinSpendNoSerialCheck(const CTransaction& tx, const libzerocoin::CoinSpend* spend, CBlockIndex* pindex, const uint256& hashBlock)
{
    //Check to see if the zPIV is properly signed
    if (pindex->nHeight >= Params().GetConsensus().nBlockZerocoinV2) {
        try {
            if (!spend->HasValidSignature())
                return error("%s: V2 zPIV spend does not have a valid signature\n", __func__);
        } catch (const libzerocoin::InvalidSerialException& e) {
            // Check if we are in the range of the attack
            if(!isBlockBetweenFakeSerialAttackRange(pindex->nHeight))
                return error("%s: Invalid serial detected, txid %s, in block %d\n", __func__, tx.GetHash().GetHex(), pindex->nHeight);
            else
                LogPrintf("%s: Invalid serial detected within range in block %d\n", __func__, pindex->nHeight);
        }

        libzerocoin::SpendType expectedType = libzerocoin::SpendType::SPEND;
        if (tx.IsCoinStake())
            expectedType = libzerocoin::SpendType::STAKE;
        if (spend->getSpendType() != expectedType) {
            return error("%s: trying to spend zPIV without the correct spend type. txid=%s\n", __func__,
                         tx.GetHash().GetHex());
        }
    }

    bool v1Serial = spend->getVersion() < libzerocoin::PrivateCoin::PUBKEY_VERSION;
    if (pindex->nHeight >= Params().GetConsensus().nPublicZCSpends) {
        //Reject V1 old serials.
        if (v1Serial) {
            return error("%s : zPIV v1 serial spend not spendable, serial %s, tx %s\n", __func__,
                         spend->getCoinSerialNumber().GetHex(), tx.GetHash().GetHex());
        }
    }

    //Reject serial's that are not in the acceptable value range
    if (!spend->HasValidSerial(Params().Zerocoin_Params(v1Serial)))  {
        // Up until this block our chain was not checking serials correctly..
        if (!isBlockBetweenFakeSerialAttackRange(pindex->nHeight))
            return error("%s : zPIV spend with serial %s from tx %s is not in valid range\n", __func__,
                     spend->getCoinSerialNumber().GetHex(), tx.GetHash().GetHex());
        else
            LogPrintf("%s:: HasValidSerial :: Invalid serial detected within range in block %d\n", __func__, pindex->nHeight);
    }


    return true;
}

bool ContextualCheckZerocoinMint(const libzerocoin::PublicCoin& coin, const CBlockIndex* pindex)
{
    if (pindex->nHeight >= Params().GetConsensus().nPublicZCSpends) {
        // Zerocoin MINTs have been disabled
        return error("%s: Mints disabled at height %d - unable to add pubcoin %s", __func__,
                pindex->nHeight, coin.getValue().GetHex().substr(0, 10));
    }
    if (pindex->nHeight >= Params().GetConsensus().nBlockZerocoinV2 && Params().NetworkIDString() != CBaseChainParams::TESTNET) {
        //See if this coin has already been added to the blockchain
        uint256 txid;
        int nHeight;
        if (zerocoinDB->ReadCoinMint(coin.getValue(), txid) && IsTransactionInChain(txid, nHeight))
            return error("%s: pubcoin %s was already accumulated in tx %s", __func__,
                         coin.getValue().GetHex().substr(0, 10),
                         txid.GetHex());
    }

    return true;
}

bool CheckZerocoinSpendTx(CBlockIndex *pindex, CValidationState& state, const CTransaction& tx,
        std::vector<uint256>& vSpendsInBlock,
        std::vector<std::pair<libzerocoin::CoinSpend, uint256> >& vSpends,
        std::vector<std::pair<libzerocoin::PublicCoin, uint256> >& vMints,
        CAmount& nValueIn) {
    int nHeightTx = 0;
    uint256 txid = tx.GetHash();
    vSpendsInBlock.emplace_back(txid);
    if (IsTransactionInChain(txid, nHeightTx)) {
        //when verifying blocks on init, the blocks are scanned without being disconnected - prevent that from causing an error
//                if (!fVerifyingBlocks || (fVerifyingBlocks && pindex->nHeight > nHeightTx))
        if (!IsInitialBlockDownload())
            return state.DoS(100, error("%s : txid %s already exists in block %d , trying to include it again in block %d", __func__,
                                        tx.GetHash().GetHex(), nHeightTx, pindex->nHeight),
                                REJECT_INVALID, "bad-txns-inputs-missingorspent");
    }
    //Check for double spending of serial #'s
    std::set<CBigNum> setSerials;
    for (const CTxIn& txIn : tx.vin) {
        bool isPublicSpend = txIn.IsZerocoinPublicSpend();
        bool isPrivZerocoinSpend = txIn.IsZerocoinSpend();
        if (!isPrivZerocoinSpend && !isPublicSpend)
            continue;

        // Check enforcement
        if (!CheckPublicCoinSpendEnforced(pindex->nHeight, isPublicSpend)){
            return false;
        }

        if (isPublicSpend) {
            libzerocoin::ZerocoinParams* params = Params().Zerocoin_Params(false);
            PublicCoinSpend publicSpend(params);
            if (!XIONModule::ParseZerocoinPublicSpend(txIn, tx, state, publicSpend)){
                LogPrintf("%s - Unable to parse zerocoin spend", __func__);
                return false;
            }
            nValueIn += publicSpend.getDenomination() * COIN;
            //queue for db write after the 'justcheck' section has concluded
            vSpends.emplace_back(std::make_pair(publicSpend, tx.GetHash()));
/*
            if (!ContextualCheckZerocoinSpend(tx, &publicSpend, pindex, hashBlock))
                return state.DoS(100, error("%s: failed to add block %s with invalid public zc spend", __func__, tx.GetHash().GetHex()), REJECT_INVALID);
*/
        } else {
            libzerocoin::CoinSpend spend = TxInToZerocoinSpend(txIn);
            nValueIn += spend.getDenomination() * COIN;
            //queue for db write after the 'justcheck' section has concluded
            vSpends.emplace_back(std::make_pair(spend, tx.GetHash()));
/*
            if (!ContextualCheckZerocoinSpend(tx, &spend, pindex, hashBlock))
                return state.DoS(100, error("%s: failed to add block %s with invalid zerocoinspend", __func__, tx.GetHash().GetHex()), REJECT_INVALID);
*/
        }
        // Set flag if input is a group token management address
    }
/*
    //Temporarily disable new token creation during management mode
    if (block.nTime > GetSporkValue(SPORK_10_TOKENGROUP_MAINTENANCE_MODE) && !IsInitialBlockDownload() && IsAnyOutputGroupedCreation(tx)) {
        if (IsAnyOutputGroupedCreation(tx, TokenGroupIdFlags::MGT_TOKEN)) {
            LogPrintf("%s: Management token creation during token group management mode\n", __func__);
        } else {
            return state.DoS(0, error("%s : new token creation is not possible during token group management mode",
                            __func__), REJECT_INVALID, "token-group-management");
        }
    }
*/
    // Check that xION mints are not already known
    if (tx.HasZerocoinMintOutputs()) {
        for (auto& out : tx.vout) {
            if (!out.IsZerocoinMint())
                continue;
            libzerocoin::PublicCoin coin(Params().Zerocoin_Params(false));
            if (!TxOutToPublicCoin(out, coin, state))
                return state.DoS(100, error("%s: failed final check of zerocoinmint for tx %s", __func__, tx.GetHash().GetHex()));

            if (!ContextualCheckZerocoinMint(coin, pindex))
                return state.DoS(100, error("%s: zerocoin mint failed contextual check", __func__));

            vMints.emplace_back(std::make_pair(coin, tx.GetHash()));
        }
    }
    return true;
}
