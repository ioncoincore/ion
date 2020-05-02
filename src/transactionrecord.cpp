// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2018-2020 The Ion Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/transactionrecord.h>

#include <base58.h>
#include <consensus/consensus.h>
#include <validation.h>
#include <timedata.h>
#include <wallet/wallet.h>

#include <privatesend/privatesend.h>

#include <stdint.h>


/* Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction(const CWalletTx &wtx)
{
    // There are currently no cases where we hide transactions, but
    // we may want to use this in the future for things like RBF.
    return true;
}

bool DecomposeReward(const CWallet* wallet, const CWalletTx& wtx, const CTxDestination address, const bool fPos, std::vector<TransactionRecord> &records) {
    std::map<uint64_t, CTxOut> coinstakeRewards;
    std::map<uint64_t, CTxOut> coinbaseRewards;
    std::map<uint64_t, CTxOut> MNRewards;

    // Only a stake reward, so the default decompose function suffices
    if (wtx.tx->vout.size() < (fPos ? 3 : 2)) return false;

    const Consensus::Params& consensusParams = Params().GetConsensus();

    // When the block has not confirmed - or when not yet in hybrid mode, the default decompose function suffices
    const CBlockIndex* pindexWtx;
    const int nBlockDepth = wtx.GetDepthInMainChain(pindexWtx);
    if (nBlockDepth <= 0 || !pindexWtx) return false;
    const int nBlockHeight = pindexWtx->nHeight;
    if (nBlockHeight < consensusParams.POSPOWStartHeight) return false;

    CBlockReward blockReward(nBlockHeight, 0, fPos, consensusParams);

    if (fPos) {
        // When no coinstake value can be determined, the default decompose function suffices
        COutPoint prevout = wtx.tx->vin[0].prevout;
        uint256 hashBlock;
        CTransactionRef txIn;
        if (!GetTransaction(prevout.hash, txIn, consensusParams, hashBlock, true)) return false;
        if (txIn->vout.size() < prevout.n + 1) return false;

        CAmount nStakeInValue = txIn->vout[prevout.n].nValue;

        CAmount wtxOutValue = 0;
        for (auto txOut : wtx.tx->vout) {
            wtxOutValue += txOut.nValue;
        }
        CAmount nFees = (wtxOutValue - nStakeInValue) - blockReward.GetTotalRewards().IONAmount;
        blockReward.AddHybridFees(nFees / 0.8 + 1); // 20% of the fees were burned. Add 1 for slack, to avoid fetching the full block.

        CAmount nStakeValue = blockReward.GetCoinstakeReward().IONAmount;

        bool fAllCoinstakesFound = false;
        CAmount nStakeValueFound = 0;
        for (int i = 1; i < (int)wtx.tx->vout.size(); i++) {

            CTxOut curOut = wtx.tx->vout[i];
            if (!fAllCoinstakesFound) {
                if (nStakeValueFound + curOut.nValue <= nStakeInValue + nStakeValue) {
                    nStakeValueFound += curOut.nValue;
                    coinstakeRewards.insert(std::make_pair(i, curOut));
                } else {
                    MNRewards.insert(std::make_pair(i, curOut));
                    fAllCoinstakesFound = true;
                }
            } else {
                MNRewards.insert(std::make_pair(i, curOut));
            }
        }
    } else {
        CAmount nCoinbaseValue = blockReward.GetCoinbaseReward().IONAmount;

        bool fAllCoinbasesFound = false;
        CAmount nCoinbaseValueFound = 0;
        for (int i = 0; i < (int)wtx.tx->vout.size(); i++) {

            CTxOut curOut = wtx.tx->vout[i];
            if (!fAllCoinbasesFound) {
                if (nCoinbaseValueFound + curOut.nValue <= nCoinbaseValue) {
                    nCoinbaseValueFound += curOut.nValue;
                    coinbaseRewards.insert(std::make_pair(i, curOut));
                } else {
                    MNRewards.insert(std::make_pair(i, curOut));
                    fAllCoinbasesFound = true;
                }
            } else {
                MNRewards.insert(std::make_pair(i, curOut));
            }
        }
    }

    int64_t nTime = wtx.GetTxTime();
    const uint256 hash = wtx.GetHash();
    std::map<std::string, std::string> mapValue = wtx.mapValue;

    TransactionRecord stakeRecord(hash, nTime);
    if (coinstakeRewards.size() > 0) {
        if (isminetype mine = wallet->IsMine(wtx.tx->vout[1])){
            // Stake reward
            stakeRecord.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
            stakeRecord.type = TransactionRecord::StakeMint;
            stakeRecord.strAddress = CBitcoinAddress(address).ToString();
            stakeRecord.address.SetString(stakeRecord.strAddress);
            stakeRecord.txDest = stakeRecord.address.Get();
            for (auto stakeReward : coinstakeRewards) {
                stakeRecord.credit += wallet->GetCredit(stakeReward.second, ISMINE_ALL);
            }
            stakeRecord.credit -= wtx.GetDebit(ISMINE_ALL);
            records.push_back(stakeRecord);
        }
    }

    TransactionRecord coinbaseRecord(hash, nTime);
    if (coinbaseRewards.size() > 0) {
        if (isminetype mine = wallet->IsMine(wtx.tx->vout[0])){
            // Stake reward
            coinbaseRecord.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
            coinbaseRecord.type = TransactionRecord::Generated;
            coinbaseRecord.strAddress = CBitcoinAddress(address).ToString();
            coinbaseRecord.address.SetString(coinbaseRecord.strAddress);
            coinbaseRecord.txDest = coinbaseRecord.address.Get();
            for (auto coinbaseReward : coinbaseRewards) {
                coinbaseRecord.credit += wallet->GetCredit(coinbaseReward.second, ISMINE_ALL);
            }
            records.push_back(coinbaseRecord);
        }
    }

    TransactionRecord MNRecord(hash, nTime);
    if (MNRewards.size() > 0) {
        auto MNReward = MNRewards.begin();
        // Masternode reward
        if (isminetype mine = wallet->IsMine(MNReward->second)) {
            CTxDestination destMN;
            ExtractDestination(MNReward->second.scriptPubKey, destMN);
            MNRecord.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
            MNRecord.type = TransactionRecord::MNReward;
            MNRecord.strAddress = CBitcoinAddress(destMN).ToString();
            MNRecord.address.SetString(MNRecord.strAddress);
            MNRecord.txDest = MNRecord.address.Get();
            MNRecord.credit = MNReward->second.nValue;
            records.push_back(MNRecord);
        }
   }

    return true;
}

/*
 * Decompose CWallet transaction to model transaction records.
 */
std::vector<TransactionRecord> TransactionRecord::decomposeTransaction(const CWallet *wallet, const CWalletTx &wtx)
{
    std::vector<TransactionRecord> parts;
    int64_t nTime = wtx.GetTxTime();
    CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
    CAmount nNet = nCredit - nDebit;
    uint256 hash = wtx.GetHash();
    std::map<std::string, std::string> mapValue = wtx.mapValue;

    if (wtx.tx->IsCoinStake()) {
        TransactionRecord sub(hash, nTime);
        CTxDestination address;
        if (!ExtractDestination(wtx.tx->vout[1].scriptPubKey, address))
            return parts;

        if (DecomposeReward(wallet, wtx, address, true, parts)) {
            return parts;
        }

        if (isminetype mine = wallet->IsMine(wtx.tx->vout[1])) {
            // ION stake reward
            sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
            sub.type = TransactionRecord::StakeMint;
            sub.strAddress = CBitcoinAddress(address).ToString();
            sub.credit = nNet;
        } else {
            //Masternode reward
            CTxDestination destMN;
            int nIndexMN = wtx.tx->vout.size() - 1;
            if (ExtractDestination(wtx.tx->vout[nIndexMN].scriptPubKey, destMN) && IsMine(*wallet, destMN)) {
                isminetype mine = wallet->IsMine(wtx.tx->vout[nIndexMN]);
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                sub.type = TransactionRecord::MNReward;
                sub.strAddress = CBitcoinAddress(destMN).ToString();
                sub.credit = wtx.tx->vout[nIndexMN].nValue;
            }
        }

        sub.address.SetString(sub.strAddress);
        sub.txDest = sub.address.Get();
        parts.push_back(sub);
    } else if (nNet > 0 || wtx.IsCoinBase())
    {
        if (wtx.tx->IsCoinBase()) {
            TransactionRecord sub(hash, nTime);
            CTxDestination address;
            if (ExtractDestination(wtx.tx->vout[0].scriptPubKey, address) && DecomposeReward(wallet, wtx, address, false, parts)) {
                return parts;
            }
        }
        //
        // Credit
        //
        for(unsigned int i = 0; i < wtx.tx->vout.size(); i++)
        {
            const CTxOut& txout = wtx.tx->vout[i];
            isminetype mine = wallet->IsMine(txout);
            if(mine)
            {
                TransactionRecord sub(hash, nTime);
                CTxDestination address;
                sub.idx = i; // vout index
                sub.credit = txout.nValue;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*wallet, address))
                {
                    // Received by Ion Address
                    sub.type = TransactionRecord::RecvWithAddress;
                    sub.strAddress = EncodeDestination(address);
                    sub.txDest = address;
                }
                else
                {
                    // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                    sub.type = TransactionRecord::RecvFromOther;
                    sub.strAddress = mapValue["from"];
                    sub.txDest = DecodeDestination(sub.strAddress);
                }
                if (wtx.IsCoinBase())
                {
                    // Generated
                    sub.type = TransactionRecord::Generated;
                }

                parts.append(sub);
            }
        }
    }
    else
    {
        bool fAllFromMeDenom = true;
        int nFromMe = 0;
        bool involvesWatchAddress = false;
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        for (const CTxIn& txin : wtx.tx->vin)
        {
            if(wallet->IsMine(txin)) {
                fAllFromMeDenom = fAllFromMeDenom && wallet->IsDenominated(txin.prevout);
                nFromMe++;
            }
            isminetype mine = wallet->IsMine(txin);
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllFromMe > mine) fAllFromMe = mine;
        }

        isminetype fAllToMe = ISMINE_SPENDABLE;
        bool fAllToMeDenom = true;
        int nToMe = 0;
        for (const CTxOut& txout : wtx.tx->vout) {
            if(wallet->IsMine(txout)) {
                fAllToMeDenom = fAllToMeDenom && CPrivateSend::IsDenominatedAmount(txout.nValue);
                nToMe++;
            }
            isminetype mine = wallet->IsMine(txout);
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllToMe > mine) fAllToMe = mine;
        }

        if(fAllFromMeDenom && fAllToMeDenom && nFromMe * nToMe) {
            parts.push_back(TransactionRecord(hash, nTime, TransactionRecord::PrivateSendDenominate, "", -nDebit, nCredit));
            parts.back().involvesWatchAddress = false;   // maybe pass to TransactionRecord as constructor argument
        }
        else if (fAllFromMe && fAllToMe)
        {
            // Payment to self
            // TODO: this section still not accurate but covers most cases,
            // might need some additional work however

            TransactionRecord sub(hash, nTime);
            // Payment to self by default
            sub.type = TransactionRecord::SendToSelf;
            sub.strAddress = "";

            if(mapValue["DS"] == "1")
            {
                sub.type = TransactionRecord::PrivateSend;
                CTxDestination address;
                if (ExtractDestination(wtx.tx->vout[0].scriptPubKey, address))
                {
                    // Sent to Dash Address
                    sub.strAddress = EncodeDestination(address);
                    sub.txDest = address;
                }
                else
                {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.strAddress = mapValue["to"];
                    sub.txDest = DecodeDestination(sub.strAddress);
                }
            }
            else
            {
                sub.idx = parts.size();
                if(wtx.tx->vin.size() == 1 && wtx.tx->vout.size() == 1
                    && CPrivateSend::IsCollateralAmount(nDebit)
                    && CPrivateSend::IsCollateralAmount(nCredit)
                    && CPrivateSend::IsCollateralAmount(-nNet))
                {
                    sub.type = TransactionRecord::PrivateSendCollateralPayment;
                } else {
                    bool fMakeCollateral{false};
                    if (wtx.tx->vout.size() == 2) {
                        CAmount nMaxCollateralAmount = CPrivateSend::GetMaxCollateralAmount();
                        CAmount nPreMaxCollateralAmount = nMaxCollateralAmount  - CPrivateSend::GetCollateralAmount();
                        fMakeCollateral =
                            wtx.tx->vout[0].nValue == nMaxCollateralAmount ||
                            wtx.tx->vout[1].nValue == nMaxCollateralAmount ||
                            (wtx.tx->vout[0].nValue == nPreMaxCollateralAmount && CPrivateSend::IsCollateralAmount(wtx.tx->vout[1].nValue)) ||
                            (wtx.tx->vout[1].nValue == nPreMaxCollateralAmount && CPrivateSend::IsCollateralAmount(wtx.tx->vout[0].nValue));
                    }
                    if (fMakeCollateral) {
                        sub.type = TransactionRecord::PrivateSendMakeCollaterals;
                    } else {
                        for (const auto& txout : wtx.tx->vout) {
                            if (CPrivateSend::IsDenominatedAmount(txout.nValue)) {
                                sub.type = TransactionRecord::PrivateSendCreateDenominations;
                                break; // Done, it's definitely a tx creating mixing denoms, no need to look any further
                            }
                        }
                    }
                }
            }

            CAmount nChange = wtx.GetChange();

            sub.debit = -(nDebit - nChange);
            sub.credit = nCredit - nChange;
            parts.append(sub);
            parts.last().involvesWatchAddress = involvesWatchAddress;   // maybe pass to TransactionRecord as constructor argument
        }
        else if (fAllFromMe)
        {
            //
            // Debit
            //
            CAmount nTxFee = nDebit - wtx.tx->GetValueOut();

            bool fDone = false;
            if(wtx.tx->vin.size() == 1 && wtx.tx->vout.size() == 1
                && CPrivateSend::IsCollateralAmount(nDebit)
                && nCredit == 0 // OP_RETURN
                && CPrivateSend::IsCollateralAmount(-nNet))
            {
                TransactionRecord sub(hash, nTime);
                sub.idx = 0;
                sub.type = TransactionRecord::PrivateSendCollateralPayment;
                sub.debit = -nDebit;
                parts.push_back(sub);
                fDone = true;
            }

            for (unsigned int nOut = 0; nOut < wtx.tx->vout.size() && !fDone; nOut++)
            {
                const CTxOut& txout = wtx.tx->vout[nOut];
                TransactionRecord sub(hash, nTime);
                sub.idx = nOut;
                sub.involvesWatchAddress = involvesWatchAddress;

                if(wallet->IsMine(txout))
                {
                    // Ignore parts sent to self, as this is usually the change
                    // from a transaction sent back to our own address.
                    continue;
                }

                CTxDestination address;
                if (ExtractDestination(txout.scriptPubKey, address))
                {
                    // Sent to Ion Address
                    sub.type = TransactionRecord::SendToAddress;
                    sub.strAddress = EncodeDestination(address);
                    sub.txDest = address;
                }
                else
                {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.type = TransactionRecord::SendToOther;
                    sub.strAddress = mapValue["to"];
                    sub.txDest = DecodeDestination(sub.strAddress);
                }

                if(mapValue["DS"] == "1")
                {
                    sub.type = TransactionRecord::PrivateSend;
                }

                CAmount nValue = txout.nValue;
                /* Add fee to first output */
                if (nTxFee > 0)
                {
                    nValue += nTxFee;
                    nTxFee = 0;
                }
                sub.debit = -nValue;

                parts.append(sub);
            }
        }
        else
        {
            //
            // Mixed debit transaction, can't break down payees
            //
            parts.push_back(TransactionRecord(hash, nTime, TransactionRecord::Other, "", nNet, 0));
            parts.back().involvesWatchAddress = involvesWatchAddress;
        }
    }

    return parts;
}

void TransactionRecord::updateStatus(const CWalletTx &wtx, int chainLockHeight)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(wtx.GetWallet()->cs_wallet);
    // Determine transaction status

    // Find the block the tx is in
    CBlockIndex* pindex = nullptr;
    BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
    if (mi != mapBlockIndex.end())
        pindex = (*mi).second;

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        (pindex ? pindex->nHeight : std::numeric_limits<int>::max()),
        (wtx.IsCoinBase() ? 1 : 0),
        wtx.nTimeReceived,
        idx);
    status.countsForBalance = wtx.IsTrusted() && !(wtx.GetBlocksToMaturity() > 0);
    status.depth = wtx.GetDepthInMainChain();
    status.cur_num_blocks = chainActive.Height();
    status.cachedChainLockHeight = chainLockHeight;

    bool oldLockedByChainLocks = status.lockedByChainLocks;
    if (!status.lockedByChainLocks) {
        status.lockedByChainLocks = wtx.IsChainLocked();
    }

    auto addrBookIt = wtx.GetWallet()->mapAddressBook.find(this->txDest);
    if (addrBookIt == wtx.GetWallet()->mapAddressBook.end()) {
        status.label = "";
    } else {
        status.label = addrBookIt->second.name;
    }

    if (!CheckFinalTx(*wtx.tx))
    {
        if (wtx.tx->nLockTime < LOCKTIME_THRESHOLD)
        {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.tx->nLockTime - chainActive.Height();
        }
        else
        {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.tx->nLockTime;
        }
    }
    // For generated transactions, determine maturity
    else if(type == TransactionRecord::Generated || type == TransactionRecord::StakeMint || type == TransactionRecord::MNReward)
    {
        if (wtx.GetBlocksToMaturity() > 0)
        {
            status.status = TransactionStatus::Immature;

            if (wtx.IsInMainChain())
            {
                status.matures_in = wtx.GetBlocksToMaturity();

                // Check if the block was requested by anyone
                if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
                    status.status = TransactionStatus::MaturesWarning;
            }
            else
            {
                status.status = TransactionStatus::NotAccepted;
            }
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    else
    {
        // The IsLockedByInstantSend call is quite expensive, so we only do it when a state change is actually possible.
        if (status.lockedByChainLocks) {
            if (oldLockedByChainLocks != status.lockedByChainLocks) {
                status.lockedByInstantSend = wtx.IsLockedByInstantSend();
            } else {
                status.lockedByInstantSend = false;
            }
        } else if (!status.lockedByInstantSend) {
            status.lockedByInstantSend = wtx.IsLockedByInstantSend();
        }

        if (status.depth < 0)
        {
            status.status = TransactionStatus::Conflicted;
        }
        else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
        {
            status.status = TransactionStatus::Offline;
        }
        else if (status.depth == 0)
        {
            status.status = TransactionStatus::Unconfirmed;
            if (wtx.isAbandoned())
                status.status = TransactionStatus::Abandoned;
        }
        else if (status.depth < RecommendedNumConfirmations && !status.lockedByChainLocks)
        {
            status.status = TransactionStatus::Confirming;
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    status.needsUpdate = false;
}

bool TransactionRecord::statusUpdateNeeded(int chainLockHeight) const
{
    AssertLockHeld(cs_main);
    return status.cur_num_blocks != chainActive.Height() || status.needsUpdate
        || (!status.lockedByChainLocks && status.cachedChainLockHeight != chainLockHeight);
}

std::string TransactionRecord::getTxID() const
{
    return hash.ToString();
}

int TransactionRecord::getOutputIndex() const
{
    return idx;
}

std::string TransactionRecord::GetTransactionRecordType() const
{
    return GetTransactionRecordType(type);
}

std::string TransactionRecord::GetTransactionRecordType(Type type) const
{
    switch (type)
    {
        case Other: return "Other";
        case Generated: return "Generated";
        case StakeMint: return "StakeMint";
        case MNReward: return "MNReward";
        case SendToAddress: return "SendToAddress";
        case SendToOther: return "SendToOther";
        case RecvWithAddress: return "RecvWithAddress";
        case RecvFromOther: return "RecvFromOther";
        case SendToSelf: return "SendToSelf";
        case RecvWithPrivateSend: return "RecvWithPrivateSend";
        case PrivateSendDenominate: return "PrivateSendDenominate";
        case PrivateSendCollateralPayment: return "PrivateSendCollateralPayment";
        case PrivateSendMakeCollaterals: return "PrivateSendMakeCollaterals";
        case PrivateSendCreateDenominations: return "PrivateSendCreateDenominations";
        case PrivateSend: return "PrivateSend";
    }
    return NULL;
}

std::string TransactionRecord::GetTransactionStatus() const
{
    return GetTransactionStatus(status.status);
}
std::string TransactionRecord::GetTransactionStatus(TransactionStatus::Status status) const
{
    switch (status)
    {
        case TransactionStatus::Confirmed: return "Confirmed";           /**< Have 6 or more confirmations (normal tx) or fully mature (mined tx) **/
            /// Normal (sent/received) transactions
        case TransactionStatus::OpenUntilDate: return "OpenUntilDate";   /**< Transaction not yet final, waiting for date */
        case TransactionStatus::OpenUntilBlock: return "OpenUntilBlock"; /**< Transaction not yet final, waiting for block */
        case TransactionStatus::Offline: return "Offline";               /**< Not sent to any other nodes **/
        case TransactionStatus::Unconfirmed: return "Unconfirmed";       /**< Not yet mined into a block **/
        case TransactionStatus::Confirming: return "Confirmed";          /**< Confirmed, but waiting for the recommended number of confirmations **/
        case TransactionStatus::Conflicted: return "Conflicted";         /**< Conflicts with other transaction or mempool **/
            /// Generated (mined) transactions
        case TransactionStatus::Immature: return "Immature";             /**< Mined but waiting for maturity */
        case TransactionStatus::MaturesWarning: return "MaturesWarning"; /**< Transaction will likely not mature because no nodes have confirmed */
        case TransactionStatus::NotAccepted: return "NotAccepted";       /**< Mined but not accepted */
    }
    return NULL;
}
