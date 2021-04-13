// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Copyright (c) 2019-2020 The Ion Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TOKEN_GROUP_RPC_H
#define TOKEN_GROUP_RPC_H

#include "chainparams.h"
#include "consensus/tokengroups.h"
#include "wallet/wallet.h"
#include <unordered_map>

// Number of satoshis we will put into a grouped output
static const CAmount GROUPED_SATOSHI_AMT = 1;

// Pass a group and a destination address (or CNoDestination) to get the balance of all outputs in the group
// or all outputs in that group and on that destination address.
CAmount GetGroupBalance(const CTokenGroupID &grpID, const CTxDestination &dest, const CWallet *wallet);

// Returns a mapping of groupID->balance
void GetAllGroupBalances(const CWallet *wallet, std::unordered_map<CTokenGroupID, CAmount> &balances);
void GetAllGroupBalancesAndAuthorities(const CWallet *wallet, std::unordered_map<CTokenGroupID, CAmount> &balances,
    std::unordered_map<CTokenGroupID, GroupAuthorityFlags> &authorities, const int nMinDepth = 0);
void ListAllGroupAuthorities(const CWallet *wallet, std::vector<COutput> &coins);
void ListGroupAuthorities(const CWallet *wallet, std::vector<COutput> &coins, const CTokenGroupID &grpID);
void GetGroupBalanceAndAuthorities(CAmount &balance, GroupAuthorityFlags &authorities, const CTokenGroupID &grpID,
    const CTxDestination &dest, const CWallet *wallet, const int nMinDepth = 0);

void GetGroupCoins(const CWallet *wallet, std::vector<COutput>& coins, CAmount& balance, const CTokenGroupID &grpID, const CTxDestination &dest = CNoDestination());
void GetGroupAuthority(const CWallet *wallet, std::vector<COutput>& coins, GroupAuthorityFlags flags, const CTokenGroupID &grpID, const CTxDestination &dest = CNoDestination());

//* Calculate a group ID based on the provided inputs.  Pass and empty script to opRetTokDesc if there is not
// going to be an OP_RETURN output in the transaction.
CTokenGroupID findGroupId(const COutPoint &input, CScript opRetTokDesc, TokenGroupIdFlags flags, uint64_t &nonce);

CAmount GroupCoinSelection(const std::vector<COutput> &coins, CAmount amt, std::vector<COutput> &chosenCoins);
bool RenewAuthority(const COutput &authority, std::vector<CRecipient> &outputs, CReserveKey &childAuthorityKey);

void ConstructTx(CWalletTx &wtxNew, const std::vector<COutput> &chosenCoins, const std::vector<CRecipient> &outputs,
    CAmount totalGroupedNeeded, CAmount totalXDMNeeded, CTokenGroupID grpID, CWallet *wallet);

void GroupMelt(CWalletTx &wtxNew, const CTokenGroupID &grpID, CAmount totalNeeded, CWallet *wallet);
void GroupSend(CWalletTx &wtxNew, const CTokenGroupID &grpID, const std::vector<CRecipient> &outputs,
    CAmount totalNeeded, CAmount totalXDMNeeded, CWallet *wallet);

CAmount GetXDMFeesPaid(const std::vector<CRecipient> outputs);
bool EnsureXDMFee(std::vector<CRecipient> &outputs, CAmount XDMFee);

#endif
