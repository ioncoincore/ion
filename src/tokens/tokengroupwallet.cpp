// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Copyright (c) 2019-2020 The Ion Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tokens/tokengroupwallet.h"

#include "consensus/tokengroups.h"
#include "consensus/validation.h"
#include "dstencode.h"
#include "net.h"
#include "rpc/protocol.h"
#include "script/tokengroup.h"
#include "tokens/tokengroupmanager.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validation.h" // for cs_main
#include "wallet/wallet.h"

// allow this many times fee overpayment, rather than make a change output
#define FEE_FUDGE 2

// Approximate size of signature in a script -- used for guessing fees
const unsigned int TX_SIG_SCRIPT_LEN = 72;

/* Grouped transactions look like this:

GP2PKH:

OP_DATA(group identifier)
OP_DATA(SerializeAmount(amount))
OP_GROUP
OP_DROP
OP_DUP
OP_HASH160
OP_DATA(pubkeyhash)
OP_EQUALVERIFY
OP_CHECKSIG

GP2SH:

OP_DATA(group identifier)
OP_DATA(CompactSize(amount))
OP_GROUP
OP_DROP
OP_HASH160 [20-byte-hash-value] OP_EQUAL

FUTURE: GP2SH version 2:

OP_DATA(group identifier)
OP_DATA(CompactSize(amount))
OP_GROUP
OP_DROP
OP_HASH256 [32-byte-hash-value] OP_EQUAL
*/

void GetAllGroupBalances(const CWallet *wallet, std::unordered_map<CTokenGroupID, CAmount> &balances)
{
    std::vector<COutput> coins;
    wallet->FilterCoins(coins, [&balances](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupInfo tg(out->scriptPubKey);
        if ((tg.associatedGroup != NoGroup) && !tg.isAuthority()) // must be sitting in any group address
        {
            if (tg.quantity > std::numeric_limits<CAmount>::max() - balances[tg.associatedGroup])
                balances[tg.associatedGroup] = std::numeric_limits<CAmount>::max();
            else
                balances[tg.associatedGroup] += tg.quantity;
        }
        return false; // I don't want to actually filter anything
    });
}

void GetAllGroupBalancesAndAuthorities(const CWallet *wallet, std::unordered_map<CTokenGroupID, CAmount> &balances, std::unordered_map<CTokenGroupID, GroupAuthorityFlags> &authorities, const int nMinDepth)
{
    std::vector<COutput> coins;
    wallet->FilterCoins(coins, [&balances, &authorities](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupInfo tg(out->scriptPubKey);
        if ((tg.associatedGroup != NoGroup)) {
            authorities[tg.associatedGroup] |= tg.controllingGroupFlags();
            if (!tg.isAuthority()) {
                if (tg.quantity > std::numeric_limits<CAmount>::max() - balances[tg.associatedGroup])
                    balances[tg.associatedGroup] = std::numeric_limits<CAmount>::max();
                else
                    balances[tg.associatedGroup] += tg.quantity;
            } else {
                balances[tg.associatedGroup] += 0;
            }
        }
        return false; // I don't want to actually filter anything
    }, nMinDepth);
}

void ListAllGroupAuthorities(const CWallet *wallet, std::vector<COutput> &coins) {
    wallet->FilterCoins(coins, [](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupInfo tg(out->scriptPubKey);
        if (tg.isAuthority()) {
            return true;
        } else {
            return false;
        }
    });
}

void ListGroupAuthorities(const CWallet *wallet, std::vector<COutput> &coins, const CTokenGroupID &grpID) {
    wallet->FilterCoins(coins, [grpID](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupInfo tg(out->scriptPubKey);
        if (tg.isAuthority() && tg.associatedGroup == grpID) {
            return true;
        } else {
            return false;
        }
    });
}

CAmount GetGroupBalance(const CTokenGroupID &grpID, const CTxDestination &dest, const CWallet *wallet)
{
    std::vector<COutput> coins;
    CAmount balance = 0;
    wallet->FilterCoins(coins, [grpID, dest, &balance](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupInfo tg(out->scriptPubKey);
        if ((grpID == tg.associatedGroup) && !tg.isAuthority()) // must be sitting in group address
        {
            bool useit = dest == CTxDestination(CNoDestination());
            if (!useit)
            {
                CTxDestination address;
                txnouttype whichType;
                if (ExtractDestinationAndType(out->scriptPubKey, address, whichType))
                {
                    if (address == dest)
                        useit = true;
                }
            }
            if (useit)
            {
                if (tg.quantity > std::numeric_limits<CAmount>::max() - balance)
                    balance = std::numeric_limits<CAmount>::max();
                else
                    balance += tg.quantity;
            }
        }
        return false;
    });
    return balance;
}

void GetGroupBalanceAndAuthorities(CAmount &balance, GroupAuthorityFlags &authorities, const CTokenGroupID &grpID, const CTxDestination &dest, const CWallet *wallet, const int nMinDepth)
{
    std::vector<COutput> coins;
    balance = 0;
    authorities = GroupAuthorityFlags::NONE;
    wallet->FilterCoins(coins, [grpID, dest, &balance, &authorities](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupInfo tg(out->scriptPubKey);
        if ((grpID == tg.associatedGroup)) // must be sitting in group address
        {
            bool useit = dest == CTxDestination(CNoDestination());
            if (!useit)
            {
                CTxDestination address;
                txnouttype whichType;
                if (ExtractDestinationAndType(out->scriptPubKey, address, whichType))
                {
                    if (address == dest)
                        useit = true;
                }
            }
            if (useit)
            {
                authorities |= tg.controllingGroupFlags();
                if (!tg.isAuthority()) {
                    if (tg.quantity > std::numeric_limits<CAmount>::max() - balance)
                        balance = std::numeric_limits<CAmount>::max();
                    else
                        balance += tg.quantity;
                } else {
                    balance += 0;
                }
            }
        }
        return false;
    }, nMinDepth);
}

void GetGroupCoins(const CWallet *wallet, std::vector<COutput>& coins, CAmount& balance, const CTokenGroupID &grpID, const CTxDestination &dest) {
    wallet->FilterCoins(coins, [dest, grpID, &balance](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupInfo tg(out->scriptPubKey);
        if ((grpID == tg.associatedGroup) && !tg.isAuthority()) {
            bool useit = dest == CTxDestination(CNoDestination());
            if (!useit) {
                CTxDestination address;
                txnouttype whichType;
                if (ExtractDestinationAndType(out->scriptPubKey, address, whichType)) {
                    if (address == dest)
                        useit = true;
                }
            }
            if (useit) {
                if (tg.quantity > std::numeric_limits<CAmount>::max() - balance) {
                    balance = std::numeric_limits<CAmount>::max();
                } else {
                    balance += tg.quantity;
                }
                return true;
            }
        }
        return false;
    });
}

void GetGroupAuthority(const CWallet *wallet, std::vector<COutput>& coins, GroupAuthorityFlags flags, const CTokenGroupID &grpID, const CTxDestination &dest) {
    // For now: return only the first matching coin (in coins[0])
    // Todo:
    // - Find the coin with the minimum amount of authorities
    // - If needed, combine coins to provide the requested authorities
    wallet->FilterCoins(coins, [flags, dest, grpID](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupInfo tg(out->scriptPubKey);
        if ((grpID == tg.associatedGroup) && tg.isAuthority() && hasCapability(tg.controllingGroupFlags(), flags)) {
            bool useit = dest == CTxDestination(CNoDestination());
            if (!useit) {
                CTxDestination address;
                txnouttype whichType;
                if (ExtractDestinationAndType(out->scriptPubKey, address, whichType)) {
                    if (address == dest)
                        useit = true;
                }
            }
            if (useit) {
                return true;
            }
        }
        return false;
    });
}

bool NearestGreaterCoin(const std::vector<COutput> &coins, CAmount amt, COutput &chosenCoin)
{
    bool ret = false;
    CAmount curBest = std::numeric_limits<CAmount>::max();

    for (const auto &coin : coins)
    {
        CAmount camt = coin.GetValue();
        if ((camt > amt) && (camt < curBest))
        {
            curBest = camt;
            chosenCoin = coin;
            ret = true;
        }
    }

    return ret;
}


CAmount CoinSelection(const std::vector<COutput> &coins, CAmount amt, std::vector<COutput> &chosenCoins)
{
    // simple algorithm grabs until amount exceeded
    CAmount cur = 0;

    for (const auto &coin : coins)
    {
        chosenCoins.push_back(coin);
        cur += coin.GetValue();
        if (cur >= amt)
            break;
    }
    return cur;
}

CAmount GroupCoinSelection(const std::vector<COutput> &coins, CAmount amt, std::vector<COutput> &chosenCoins)
{
    // simple algorithm grabs until amount exceeded
    CAmount cur = 0;

    for (const auto &coin : coins)
    {
        chosenCoins.push_back(coin);
        CTokenGroupInfo tg(coin.tx->tx->vout[coin.i].scriptPubKey);
        cur += tg.quantity;
        if (cur >= amt)
            break;
    }
    return cur;
}

bool RenewAuthority(const COutput &authority, std::vector<CRecipient> &outputs, CReserveKey &childAuthorityKey)
{
    // The melting authority is consumed.  A wallet can decide to create a child authority or not.
    // In this simple wallet, we will always create a new melting authority if we spend a renewable
    // (CCHILD is set) one.
    CTokenGroupInfo tg(authority.GetScriptPubKey());

    if (tg.allowsRenew())
    {
        // Get a new address from the wallet to put the new mint authority in.
        CPubKey pubkey;
        childAuthorityKey.GetReservedKey(pubkey, true);
        CTxDestination authDest = pubkey.GetID();
        CScript script = GetScriptForDestination(authDest, tg.associatedGroup, (CAmount)(tg.controllingGroupFlags() & GroupAuthorityFlags::ALL_BITS));
        CRecipient recipient = {script, GROUPED_SATOSHI_AMT, false};
        outputs.push_back(recipient);
    }

    return true;
}

void ConstructTx(CWalletTx &wtxNew, const std::vector<COutput> &chosenCoins, const std::vector<CRecipient> &outputs,
    CAmount totalGroupedNeeded, CAmount totalXDMNeeded, CTokenGroupID grpID, CWallet *wallet)
{
    CAmount totalGroupedAvailable = 0;
    CAmount totalXDMAvailable = 0;
    CTokenGroupID XDMGrpID;

    CMutableTransaction tx;
    CReserveKey groupChangeKeyReservation(wallet);

    const bool XDMCreated = tokenGroupManager->DarkMatterTokensCreated();
    if (XDMCreated) {
        XDMGrpID = tokenGroupManager->GetDarkMatterID();
    }

    {
        // Add group outputs based on the passed recipient data to the tx.
        for (const CRecipient &recipient : outputs)
        {
            CTxOut txout(recipient.nAmount, recipient.scriptPubKey);
            tx.vout.push_back(txout);
        }

        for (const auto &coin : chosenCoins)
        {
            CTxIn txin(coin.GetOutPoint());
            tx.vin.push_back(txin);

            // Gather data on the provided inputs, and add them to the tx.
            CTokenGroupInfo tg(coin.GetScriptPubKey());
            if (!tg.isInvalid() && tg.associatedGroup != NoGroup && !tg.isAuthority())
            {
                if (tg.associatedGroup == grpID){
                    totalGroupedAvailable += tg.quantity;
                } else if (XDMCreated && tg.associatedGroup == XDMGrpID) {
                    totalXDMAvailable += tg.quantity;
                }
            }
        }

        if (totalGroupedAvailable > totalGroupedNeeded) // need to make a group change output
        {
            CPubKey newKey;
            if (!groupChangeKeyReservation.GetReservedKey(newKey, true))
                throw JSONRPCError(
                    RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

            CTxOut txout(GROUPED_SATOSHI_AMT,
                GetScriptForDestination(newKey.GetID(), grpID, totalGroupedAvailable - totalGroupedNeeded));
            tx.vout.push_back(txout);
        }

        if (totalXDMAvailable > totalXDMNeeded) // need to make a group change output
        {
            CPubKey newKey;

            if (!groupChangeKeyReservation.GetReservedKey(newKey, true))
                throw JSONRPCError(
                    RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

            CTxOut txout(GROUPED_SATOSHI_AMT,
                GetScriptForDestination(newKey.GetID(), tokenGroupManager->GetDarkMatterID(), totalXDMAvailable - totalXDMNeeded));
            tx.vout.push_back(txout);
        }

        // Now add ION fee
        CAmount fee;
        int nChangePosRet = -1;
        std::string strError;
        bool lockUnspents;
        std::set<int> setSubtractFeeFromOutputs;
        CCoinControl coinControl;

        if (!wallet->FundTransaction(tx, fee, nChangePosRet, strError, lockUnspents, setSubtractFeeFromOutputs, coinControl)) {
            throw JSONRPCError(RPC_WALLET_ERROR, strError);
        }

        if (!wallet->SignTransaction(tx))
        {
            throw JSONRPCError(RPC_WALLET_ERROR, "Signing transaction failed");
        }
    }

    wtxNew.BindWallet(wallet);
    wtxNew.fFromMe = true;
    wtxNew.SetTx(MakeTransactionRef(std::move(tx)));

    for (auto vout : wtxNew.tx->vout) {
        CTokenGroupInfo tgInfo(vout.scriptPubKey);
        if (!tgInfo.isInvalid()) {
            CTokenGroupCreation tgCreation;
            tokenGroupManager->GetTokenGroupCreation(tgInfo.associatedGroup, tgCreation);
            LogPrint(BCLog::TOKEN, "%s - name[%s] amount[%d]\n", __func__, tgCreation.tokenGroupDescription.strName, tgInfo.quantity);
        }
    }

    // I'll manage my own keys because I have multiple.  Passing a valid key down breaks layering.
    CReserveKey dummy(wallet);
    CValidationState state;
    if (!wallet->CommitTransaction(wtxNew, dummy, g_connman.get(), state))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the "
                                             "coins in your wallet were already spent, such as if you used a copy of "
                                             "wallet.dat and coins were spent in the copy but not marked as spent "
                                             "here.");

    groupChangeKeyReservation.KeepKey();
}


void GroupMelt(CWalletTx &wtxNew, const CTokenGroupID &grpID, CAmount totalNeeded, CWallet *wallet)
{
    std::string strError;
    std::vector<CRecipient> outputs; // Melt has no outputs (except change)
    CAmount totalAvailable = 0;
    LOCK2(cs_main, wallet->cs_wallet);

    // Find melt authority
    std::vector<COutput> coins;

    int nOptions = wallet->FilterCoins(coins, [grpID](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupInfo tg(out->scriptPubKey);
        if ((tg.associatedGroup == grpID) && tg.allowsMelt())
        {
            return true;
        }
        return false;
    });

    // if its a subgroup look for a parent authority that will work
    // As an idiot-proofing step, we only allow parent authorities that can be renewed, but that is a
    // preference coded in this wallet, not a group token requirement.
    if ((nOptions == 0) && (grpID.isSubgroup()))
    {
        // if its a subgroup look for a parent authority that will work
        nOptions = wallet->FilterCoins(coins, [grpID](const CWalletTx *tx, const CTxOut *out) {
            CTokenGroupInfo tg(out->scriptPubKey);
            if (tg.isAuthority() && tg.allowsRenew() && tg.allowsSubgroup() && tg.allowsMelt() &&
                (tg.associatedGroup == grpID.parentGroup()))
            {
                return true;
            }
            return false;
        });
    }

    if (nOptions == 0)
    {
        strError = _("To melt coins, an authority output with melt capability is needed.");
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strError);
    }
    COutput authority(nullptr, 0, 0, false, false, false);
    // Just pick the first one for now.
    for (auto coin : coins)
    {
        authority = coin;
        break;
    }

    // Find meltable coins
    coins.clear();
    wallet->FilterCoins(coins, [grpID](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupInfo tg(out->scriptPubKey);
        // must be a grouped output sitting in group address
        return ((grpID == tg.associatedGroup) && !tg.isAuthority());
    });

    // Get a near but greater quantity
    std::vector<COutput> chosenCoins;
    totalAvailable = GroupCoinSelection(coins, totalNeeded, chosenCoins);

    if (totalAvailable < totalNeeded)
    {
        std::string strError;
        strError = strprintf("Not enough tokens in the wallet.  Need %d more.", tokenGroupManager->TokenValueFromAmount(totalNeeded - totalAvailable, grpID));
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strError);
    }

    chosenCoins.push_back(authority);

    CReserveKey childAuthorityKey(wallet);
    RenewAuthority(authority, outputs, childAuthorityKey);
    // by passing a fewer tokens available than are actually in the inputs, there is a surplus.
    // This surplus will be melted.
    ConstructTx(wtxNew, chosenCoins, outputs, totalNeeded, 0, grpID, wallet);
    childAuthorityKey.KeepKey();
}

void GroupSend(CWalletTx &wtxNew,
    const CTokenGroupID &grpID,
    const std::vector<CRecipient> &outputs,
    CAmount totalNeeded,
    CAmount totalXDMNeeded,
    CWallet *wallet)
{
    LOCK2(cs_main, wallet->cs_wallet);
    std::string strError;
    std::vector<COutput> coins;
    std::vector<COutput> chosenCoins;

    // Add XDM inputs
    // Increase tokens needed when sending XDM and select XDM coins otherwise
    CAmount totalXDMAvailable = 0;
    if (tokenGroupManager->MatchesDarkMatter(grpID)) {
        totalNeeded += totalXDMNeeded;
        totalXDMNeeded = 0;
    } else {
        if (totalXDMNeeded > 0) {
            CTokenGroupID XDMGrpID = tokenGroupManager->GetDarkMatterID();
            wallet->FilterCoins(coins, [XDMGrpID, &totalXDMAvailable](const CWalletTx *tx, const CTxOut *out) {
                CTokenGroupInfo tg(out->scriptPubKey);
                if ((XDMGrpID == tg.associatedGroup) && !tg.isAuthority())
                {
                    totalXDMAvailable += tg.quantity;
                    return true;
                }
                return false;
            });

            if (totalXDMAvailable < totalXDMNeeded)
            {
                strError = strprintf("Not enough XDM in the wallet.  Need %d more.", tokenGroupManager->TokenValueFromAmount(totalXDMNeeded - totalXDMAvailable, grpID));
                throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strError);
            }

            // Get a near but greater quantity
            totalXDMAvailable = GroupCoinSelection(coins, totalXDMNeeded, chosenCoins);
        }
    }

    CAmount totalAvailable = 0;
    wallet->FilterCoins(coins, [grpID, &totalAvailable](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupInfo tg(out->scriptPubKey);
        if ((grpID == tg.associatedGroup) && !tg.isAuthority())
        {
            totalAvailable += tg.quantity;
            return true;
        }
        return false;
    });

    if (totalAvailable < totalNeeded)
    {
        strError = strprintf("Not enough tokens in the wallet.  Need %d more.", tokenGroupManager->TokenValueFromAmount(totalNeeded - totalAvailable, grpID));
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strError);
    }

    // Get a near but greater quantity
    totalAvailable = GroupCoinSelection(coins, totalNeeded, chosenCoins);

    // Display outputs
    for (auto output : outputs) {
        CTokenGroupInfo tgInfo(output.scriptPubKey);
        if (!tgInfo.isInvalid()) {
            CTokenGroupCreation tgCreation;
            tokenGroupManager->GetTokenGroupCreation(tgInfo.associatedGroup, tgCreation);
            LogPrint(BCLog::TOKEN, "%s - name[%s] amount[%d]\n", __func__, tgCreation.tokenGroupDescription.strName, tgInfo.quantity);
        }
    }

    ConstructTx(wtxNew, chosenCoins, outputs, totalNeeded, totalXDMNeeded, grpID, wallet);
}

CTokenGroupID findGroupId(const COutPoint &input, CScript opRetTokDesc, TokenGroupIdFlags flags, uint64_t &nonce)
{
    CTokenGroupID ret;
    do
    {
        nonce += 1;
        CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
        // mask off any flags in the nonce
        nonce &= ~((uint64_t)GroupAuthorityFlags::ALL_BITS);
        hasher << input;

        if (opRetTokDesc.size())
        {
            std::vector<unsigned char> data(opRetTokDesc.begin(), opRetTokDesc.end());
            hasher << data;
        }
        hasher << nonce;
        ret = hasher.GetHash();
    } while (ret.bytes()[31] != (uint8_t)flags);
    return ret;
}

CAmount GetXDMFeesPaid(const std::vector<CRecipient> outputs) {
    CAmount XDMFeesPaid = 0;
    for (auto output : outputs) {
        CTxDestination payeeDest;
        if (ExtractDestination(output.scriptPubKey, payeeDest))
        {
            if (EncodeDestination(payeeDest) == Params().GetConsensus().strTokenManagementKey) {
                CTokenGroupInfo tgInfo(output.scriptPubKey);
                if (tokenGroupManager->MatchesDarkMatter(tgInfo.associatedGroup)) {
                    XDMFeesPaid += tgInfo.isAuthority() ? 0 : tgInfo.quantity;
                }
            }
        }
    }
    return XDMFeesPaid;
}

// Ensure that one of the recipients is an XDM fee payment
// If an output to the fee address already exists, it ensures that the output is at least XDMFee large
// Returns true if a new output is added and false if a current output is either increased or kept as-is
bool EnsureXDMFee(std::vector<CRecipient> &outputs, CAmount XDMFee) {
    if (!tokenGroupManager->DarkMatterTokensCreated()) return false;
    if (XDMFee <= 0) return false;
    CTxDestination payeeDest;
    for (auto &output : outputs) {
        if (ExtractDestination(output.scriptPubKey, payeeDest))
        {
            if (EncodeDestination(payeeDest) == Params().GetConsensus().strTokenManagementKey) {
                CTokenGroupInfo tgInfo(output.scriptPubKey);
                if (tokenGroupManager->MatchesDarkMatter(tgInfo.associatedGroup) && !tgInfo.isAuthority()) {
                    if (tgInfo.quantity < XDMFee) {
                        CScript script = GetScriptForDestination(payeeDest, tgInfo.associatedGroup, XDMFee);
                        CRecipient recipient = {script, GROUPED_SATOSHI_AMT, false};

                        output.scriptPubKey = script;
                        return false;
                    } else {
                        return false;
                    }
                }
            }
        }
    }
    CScript script = GetScriptForDestination(DecodeDestination(Params().GetConsensus().strTokenManagementKey), tokenGroupManager->GetDarkMatterID(), XDMFee);
    CRecipient recipient = {script, GROUPED_SATOSHI_AMT, false};
    outputs.push_back(recipient);

    return true;
}
