// Copyright (c) 2018-2019 The Bitcoin Unlimited developers
// Copyright (c) 2019-2020 The Ion Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "core_io.h"
#include "dstencode.h"
#include "ionaddrenc.h"
#include "rpc/protocol.h"
#include "rpc/server.h"
#include "script/tokengroup.h"
#include "tokens/tokengroupmanager.h"
#include "tokens/tokengroupwallet.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validation.h"
#include "wallet/coincontrol.h"
#include "wallet/rpcwallet.h"

#include <boost/lexical_cast.hpp>

extern void WalletTxToJSON(const CWalletTx &wtx, UniValue &entry);

static GroupAuthorityFlags ParseAuthorityParams(const JSONRPCRequest& request, unsigned int &curparam)
{
    GroupAuthorityFlags flags = GroupAuthorityFlags::CTRL | GroupAuthorityFlags::CCHILD;
    while (1)
    {
        std::string sflag;
        std::string p = request.params[curparam].get_str();
        std::transform(p.begin(), p.end(), std::back_inserter(sflag), ::tolower);
        if (sflag == "mint")
            flags |= GroupAuthorityFlags::MINT;
        else if (sflag == "melt")
            flags |= GroupAuthorityFlags::MELT;
        else if (sflag == "nochild")
            flags &= ~GroupAuthorityFlags::CCHILD;
        else if (sflag == "child")
            flags |= GroupAuthorityFlags::CCHILD;
        else if (sflag == "rescript")
            flags |= GroupAuthorityFlags::RESCRIPT;
        else if (sflag == "subgroup")
            flags |= GroupAuthorityFlags::SUBGROUP;
        else if (sflag == "configure")
            flags |= GroupAuthorityFlags::CONFIGURE;
        else if (sflag == "all")
            flags |= GroupAuthorityFlags::ALL;
        else
            break; // If param didn't match, then return because we've left the list of flags
        curparam++;
        if (curparam >= request.params.size())
            break;
    }
    return flags;
}

// extracts a common RPC call parameter pattern.  Returns curparam.
static unsigned int ParseGroupAddrValue(const JSONRPCRequest& request,
    unsigned int curparam,
    CTokenGroupID &grpID,
    std::vector<CRecipient> &outputs,
    CAmount &totalValue,
    bool groupedOutputs)
{
    grpID = GetTokenGroup(request.params[curparam].get_str());
    if (!grpID.isUserGroup())
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: No group specified");
    }
    CTokenGroupCreation tgCreation;
    if (!tokenGroupManager->GetTokenGroupCreation(grpID, tgCreation)) {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: Token group configuration transaction not found. Has it confirmed?");
    }

    outputs.reserve(request.params.size() / 2);
    curparam++;
    totalValue = 0;
    while (curparam + 1 < request.params.size())
    {
        CTxDestination dst = DecodeDestination(request.params[curparam].get_str(), Params());
        if (dst == CTxDestination(CNoDestination()))
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: destination address");
        }
        CAmount amount = tokenGroupManager->AmountFromTokenValue(request.params[curparam + 1], grpID);
        if (amount <= 0)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid parameter: amount");
        CScript script;
        CRecipient recipient;
        if (groupedOutputs)
        {
            script = GetScriptForDestination(dst, grpID, amount);
            recipient = {script, GROUPED_SATOSHI_AMT, false};
        }
        else
        {
            script = GetScriptForDestination(dst, NoGroup, 0);
            recipient = {script, amount, false};
        }

        totalValue += amount;
        outputs.push_back(recipient);
        curparam += 2;
    }
    return curparam;
}

std::vector<std::vector<unsigned char> > ParseGroupDescParams(const JSONRPCRequest& request, unsigned int &curparam, bool &confirmed)
{
    std::vector<std::vector<unsigned char> > ret;
    std::string strCurparamValue;

    confirmed = false;

    std::string tickerStr = request.params[curparam].get_str();
    if (tickerStr.size() > 10) {
        std::string strError = strprintf("Ticker %s has too many characters (10 max)", tickerStr);
        throw JSONRPCError(RPC_INVALID_PARAMS, strError);
    }
    ret.push_back(std::vector<unsigned char>(tickerStr.begin(), tickerStr.end()));

    curparam++;
    if (curparam >= request.params.size())
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Missing parameter: token name");
    }
    std::string name = request.params[curparam].get_str();
    if (name.size() > 30) {
        std::string strError = strprintf("Name %s has too many characters (30 max)", name);
        throw JSONRPCError(RPC_INVALID_PARAMS, strError);
    }
    ret.push_back(std::vector<unsigned char>(name.begin(), name.end()));

    curparam++;
    // we will accept just ticker and name
    if (curparam >= request.params.size())
    {
        ret.push_back(std::vector<unsigned char>());
        ret.push_back(std::vector<unsigned char>());
        ret.push_back(std::vector<unsigned char>());
        return ret;
    }
    strCurparamValue = request.params[curparam].get_str();
    if (strCurparamValue == "true") {
        confirmed = true;
        return ret;
    } else if (strCurparamValue == "false") {
        return ret;
    }

    int32_t decimalPosition;
    if (!ParseInt32(strCurparamValue, &decimalPosition) || decimalPosition > 16 || decimalPosition < 0) {
        std::string strError = strprintf("Parameter %d is invalid - valid values are between 0 and 16", decimalPosition);
        throw JSONRPCError(RPC_INVALID_PARAMS, strError);
    }
    ret.push_back(SerializeAmount(decimalPosition));

    curparam++;
    // we will accept just ticker, name and decimal position
    if (curparam >= request.params.size())
    {
        ret.push_back(std::vector<unsigned char>());
        ret.push_back(std::vector<unsigned char>());
        return ret;
    }
    strCurparamValue = request.params[curparam].get_str();
    if (strCurparamValue == "true") {
        confirmed = true;
        return ret;
    } else if (strCurparamValue == "false") {
        return ret;
    }

    std::string url = strCurparamValue;
    if (name.size() > 98) {
        std::string strError = strprintf("URL %s has too many characters (98 max)", name);
        throw JSONRPCError(RPC_INVALID_PARAMS, strError);
    }
    ret.push_back(std::vector<unsigned char>(url.begin(), url.end()));

    curparam++;
    if (curparam >= request.params.size())
    {
        // If you have a URL to the TDD, you need to have a hash or the token creator
        // could change the document without holders knowing about it.
        throw JSONRPCError(RPC_INVALID_PARAMS, "Missing parameter: token description document hash");
    }

    std::string hexDocHash = request.params[curparam].get_str();
    uint256 docHash;
    docHash.SetHex(hexDocHash);
    ret.push_back(std::vector<unsigned char>(docHash.begin(), docHash.end()));

    curparam++;
    if (curparam >= request.params.size())
    {
        return ret;
    }
    if (request.params[curparam].get_str() == "true") {
        confirmed = true;
        return ret;
    }
    return ret;
}

CScript BuildTokenDescScript(const std::vector<std::vector<unsigned char> > &desc)
{
    CScript ret;
    std::vector<unsigned char> data;
    uint32_t OpRetGroupId = 88888888;
    ret << OP_RETURN << OpRetGroupId;
    for (auto &d : desc)
    {
        ret << d;
    }
    return ret;
}

static void MaybePushAddress(UniValue &entry, const CTxDestination &dest)
{
    if (IsValidDestination(dest))
    {
        entry.push_back(Pair("address", EncodeDestination(dest)));
    }
}

static void AcentryToJSON(const CAccountingEntry &acentry, const std::string &strAccount, UniValue &ret)
{
    bool fAllAccounts = (strAccount == std::string("*"));

    if (fAllAccounts || acentry.strAccount == strAccount)
    {
        UniValue entry(UniValue::VOBJ);
        entry.push_back(Pair("account", acentry.strAccount));
        entry.push_back(Pair("category", "move"));
        entry.push_back(Pair("time", acentry.nTime));
        entry.push_back(Pair("amount", UniValue(acentry.nCreditDebit)));
        entry.push_back(Pair("otheraccount", acentry.strOtherAccount));
        entry.push_back(Pair("comment", acentry.strComment));
        ret.push_back(entry);
    }
}

void GetGroupedTransactions(CWallet * const pwallet,
    const CWalletTx &wtx,
    const std::string &strAccount,
    int nMinDepth,
    bool fLong,
    UniValue &ret,
    const CAmount nFee,
    const std::string &strSentAccount,
    const std::list<CGroupedOutputEntry> &listReceived,
    const std::list<CGroupedOutputEntry> &listSent)
{
    bool fAllAccounts = (strAccount == std::string("*"));
    bool involvesWatchonly = wtx.IsFromMe(ISMINE_WATCH_ONLY);

    // Sent
    if ((!listSent.empty() || nFee != 0) && (fAllAccounts || strAccount == strSentAccount))
    {
        for (const CGroupedOutputEntry &s : listSent)
        {
            const CTokenGroupInfo tokenGroupInfo(s.grp, s.grpAmount);

            UniValue entry(UniValue::VOBJ);
            if (involvesWatchonly || (::IsMine(*pwallet, s.destination) & ISMINE_WATCH_ONLY))
                entry.push_back(Pair("involvesWatchonly", true));
            entry.push_back(Pair("account", strSentAccount));
            MaybePushAddress(entry, s.destination);
            entry.push_back(Pair("category", "send"));
            entry.push_back(Pair("groupID", EncodeTokenGroup(tokenGroupInfo.associatedGroup)));
            if (tokenGroupInfo.isAuthority()){
                entry.push_back(Pair("tokenType", "authority"));
                entry.push_back(Pair("tokenAuthorities", EncodeGroupAuthority(tokenGroupInfo.controllingGroupFlags())));
            } else {
                entry.push_back(Pair("tokenType", "amount"));
                entry.push_back(Pair("tokenValue", tokenGroupManager->TokenValueFromAmount(-tokenGroupInfo.getAmount(), tokenGroupInfo.associatedGroup)));
                entry.push_back(Pair("tokenValueSat", tokenGroupManager->TokenValueFromAmount(-tokenGroupInfo.getAmount(), tokenGroupInfo.associatedGroup)));
            }
            if (pwallet->mapAddressBook.count(s.destination))
                entry.push_back(Pair("label", pwallet->mapAddressBook[s.destination].name));
            entry.push_back(Pair("vout", s.vout));
            entry.push_back(Pair("fee", ValueFromAmount(-nFee)));
            if (fLong)
                WalletTxToJSON(wtx, entry);
            ret.push_back(entry);
        }
    }

    // Received
    if (listReceived.size() > 0 && wtx.GetDepthInMainChain() >= nMinDepth)
    {
        for (const CGroupedOutputEntry &r : listReceived)
        {
            std::string account;
            if (pwallet->mapAddressBook.count(r.destination))
                account = pwallet->mapAddressBook[r.destination].name;
            if (fAllAccounts || (account == strAccount))
            {
            const CTokenGroupInfo tokenGroupInfo(r.grp, r.grpAmount);
                UniValue entry(UniValue::VOBJ);
                if (involvesWatchonly || (::IsMine(*pwallet, r.destination) & ISMINE_WATCH_ONLY))
                    entry.push_back(Pair("involvesWatchonly", true));
                entry.push_back(Pair("account", account));
                MaybePushAddress(entry, r.destination);
                if (wtx.IsCoinBase())
                {
                    if (wtx.GetDepthInMainChain() < 1)
                        entry.push_back(Pair("category", "orphan"));
                    else if (wtx.GetBlocksToMaturity() > 0)
                        entry.push_back(Pair("category", "immature"));
                    else
                        entry.push_back(Pair("category", "generate"));
                }
                else
                {
                    entry.push_back(Pair("category", "receive"));
                }
                entry.push_back(Pair("groupID", EncodeTokenGroup(tokenGroupInfo.associatedGroup)));
                if (tokenGroupInfo.isAuthority()){
                    entry.push_back(Pair("tokenType", "authority"));
                    entry.push_back(Pair("tokenAuthorities", EncodeGroupAuthority(tokenGroupInfo.controllingGroupFlags())));
                } else {
                    entry.push_back(Pair("tokenType", "amount"));
                    entry.push_back(Pair("tokenValue", tokenGroupManager->TokenValueFromAmount(tokenGroupInfo.getAmount(), tokenGroupInfo.associatedGroup)));
                    entry.push_back(Pair("tokenValueSat", tokenGroupInfo.getAmount()));
                }
                if (pwallet->mapAddressBook.count(r.destination))
                    entry.push_back(Pair("label", account));
                entry.push_back(Pair("vout", r.vout));
                if (fLong)
                    WalletTxToJSON(wtx, entry);
                ret.push_back(entry);
            }
        }
    }
}

void ListGroupedTransactions(CWallet * const pwallet,
    const CTokenGroupID &grp,
    const CWalletTx &wtx,
    const std::string &strAccount,
    int nMinDepth,
    bool fLong,
    UniValue &ret,
    const isminefilter &filter)
{
    CAmount nFee;
    std::string strSentAccount;
    std::list<CGroupedOutputEntry> listReceived;
    std::list<CGroupedOutputEntry> listSent;

    wtx.GetGroupAmounts(listReceived, listSent, nFee, strSentAccount, filter, [&grp](const CTokenGroupInfo& txgrp){
        return grp == txgrp.associatedGroup;
    });
    GetGroupedTransactions(pwallet, wtx, strAccount, nMinDepth, fLong, ret, nFee, strSentAccount, listReceived, listSent);
}

void ListAllGroupedTransactions(CWallet * const pwallet,
    const CWalletTx &wtx,
    const std::string &strAccount,
    int nMinDepth,
    bool fLong,
    UniValue &ret,
    const isminefilter &filter)
{
    CAmount nFee;
    std::string strSentAccount;
    std::list<CGroupedOutputEntry> listReceived;
    std::list<CGroupedOutputEntry> listSent;

    wtx.GetGroupAmounts(listReceived, listSent, nFee, strSentAccount, filter, [](const CTokenGroupInfo& txgrp){
        return true;
    });
    GetGroupedTransactions(pwallet, wtx, strAccount, nMinDepth, fLong, ret, nFee, strSentAccount, listReceived, listSent);
}

extern UniValue gettokenbalance(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp)
        throw std::runtime_error(
            "gettokenbalance ( \"groupid\" ) ( \"address\" )\n"
            "\nIf groupID is not specified, returns all tokens with a balance (including token authorities).\n"
            "If a groupID is specified, returns the balance of the specified token group.\n"
            "\nArguments:\n"
            "1. \"groupid\" (string, optional) the token group identifier to filter\n"
            "2. \"address\" (string, optional) the ION address to filter\n"
            "3. \"minconf\" (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
            "\n"
            "\nExamples:\n" +
            HelpExampleCli("gettokenbalance", "groupid ionrt1zwm0kzlyptdmwy3849fd6z5epesnjkruqlwlv02u7y6ymf75nk4qs6u85re") +
            "\n"
        );

    if (request.params.size() > 3)
    {
        throw std::runtime_error("Invalid number of argument to token balance");
    }

    unsigned int curparam = 0;

    if (request.params.size() <= curparam) // no group specified, show them all
    {
        std::unordered_map<CTokenGroupID, CAmount> balances;
        std::unordered_map<CTokenGroupID, GroupAuthorityFlags> authorities;
        GetAllGroupBalancesAndAuthorities(pwallet, balances, authorities, 1);
        UniValue ret(UniValue::VARR);
        for (const auto &item : balances)
        {
            CTokenGroupID grpID = item.first;
            UniValue retobj(UniValue::VOBJ);
            retobj.push_back(Pair("groupID", EncodeTokenGroup(grpID)));

            CTokenGroupCreation tgCreation;
            if (grpID.isSubgroup()) {
                CTokenGroupID parentgrp = grpID.parentGroup();
                std::vector<unsigned char> subgroupData = grpID.GetSubGroupData();
                tokenGroupManager->GetTokenGroupCreation(parentgrp, tgCreation);
                retobj.push_back(Pair("parentGroupID", EncodeTokenGroup(parentgrp)));
                retobj.push_back(Pair("subgroupData", std::string(subgroupData.begin(), subgroupData.end())));
            } else {
                tokenGroupManager->GetTokenGroupCreation(grpID, tgCreation);
            }
            retobj.push_back(Pair("ticker", tgCreation.tokenGroupDescription.strTicker));
            retobj.push_back(Pair("name", tgCreation.tokenGroupDescription.strName));

            retobj.push_back(Pair("balance", tokenGroupManager->TokenValueFromAmount(item.second, item.first)));
            if (hasCapability(authorities[item.first], GroupAuthorityFlags::CTRL))
                retobj.push_back(Pair("authorities", EncodeGroupAuthority(authorities[item.first])));

            ret.push_back(retobj);
        }
        return ret;
    } else {
        CTokenGroupID grpID = GetTokenGroup(request.params[curparam].get_str());
        if (!grpID.isUserGroup())
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter 1: No group specified");
        }

        curparam++;
        CTxDestination dst;
        if (request.params.size() > curparam)
        {
            dst = DecodeDestination(request.params[curparam].get_str(), Params());
        }
        curparam++;
        int nMinDepth = 0;
        if (request.params.size() > curparam)
        {
            nMinDepth = request.params[curparam].get_int();
        }
        CAmount balance;
        GroupAuthorityFlags authorities;
        GetGroupBalanceAndAuthorities(balance, authorities, grpID, dst, pwallet, nMinDepth);
        UniValue retobj(UniValue::VOBJ);
        retobj.push_back(Pair("groupID", EncodeTokenGroup(grpID)));
        retobj.push_back(Pair("balance", tokenGroupManager->TokenValueFromAmount(balance, grpID)));
        if (hasCapability(authorities, GroupAuthorityFlags::CTRL))
            retobj.push_back(Pair("authorities", EncodeGroupAuthority(authorities)));
        return retobj;
    }
}

extern UniValue listtokentransactions(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 4)
        throw std::runtime_error(
            "listtokentransactions (\"groupid\" count skip include_watchonly)\n"
            "\nReturns transactions for given groupid.\n"
            "\nArguments:\n"
            "1. \"groupid\"         (string, required) the token group identifier.\n"
            "2. count             (numeric, optional, default=10) The number of transactions to return\n"
            "3. skip              (numeric, optional, default=0) The number of transactions to skip\n"
            "4. include_watchonly (bool, optional, default=false) Include transactions to watch-only addresses (see 'importaddress')\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"account\":\"accountname\",(string) DEPRECATED. The account name associated with the "
            "transaction. \n"
            "    \"address\":\"ION address\",(string) The ION address of the transaction. Not present for \n"
            "    \"category\": \"category\", (string) The token category\n"
            "    \"groupID\": \"groupid\",   (string) The groupID of the token.\n"
            "    \"tokenType\": \"type\",    (string) Type of token\n" 
            "    \"tokenValue\": xxx,      (numeric) The token value for the specified transaction\n"
            "    \"tokenValueSat\": n,     (numeric) The token value in satoshis\n"
            "    \"vout\": n,              (numeric) the vout value\n"
            "    \"fee\": x.xxx,           (numeric) The amount of the fee in ION. "
            "This is negative and only available for the \n"
            "                                         'send' category of transactions.\n"
            "    \"confirmations\": n,     (numeric) The number of confirmations for the transaction. Available for "
            "'send' and \n"
            "                                         'receive' category of transactions. Negative confirmations "
            "indicate the\n"
            "                                         transaction conflicts with the block chain\n"
            "    \"instantlock\": n,       (boolean)\n"
            "    \"instantlock_internal\": n, (boolean)\n"
            "    \"chainlock\": n,         (boolean)\n"
            "    \"blockhash\": \"hashvalue\", (string) The block hash containing the transaction. Available for "
            "'send' and 'receive'\n"
            "                                          category of transactions.\n"
            "    \"blockindex\": n,        (numeric) The index of the transaction in the block that includes it. "
            "Available for 'send' and 'receive'\n"
            "                                          category of transactions.\n"
            "    \"blocktime\": xxx,       (numeric) The block time in seconds since epoch (1 Jan 1970 GMT).\n"
            "    \"txid\": \"transactionid\",(string) The transaction id. Available for 'send' and 'receive' category "
            "of transactions.\n"
            "    \"walletconflicts\":      (array)\n"
            "  }\n"
            "]\n"


            "\nExamples:\n"
            "\nList transactions in a token group\n" +
            HelpExampleCli("listtokentransactions", "groupid") + "\nList transactions for given groupid.\n"
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    unsigned int curparam = 0;

    std::string strAccount = "*";
    bool fAllGroups = true;
    CTokenGroupID grpID;

    if (request.params.size() > curparam)
    {
        std::string strGrpID = request.params[curparam].get_str();
        fAllGroups = (strGrpID == std::string("*"));

        if (!fAllGroups) {
            grpID = GetTokenGroup(strGrpID);
            if (!grpID.isUserGroup())
            {
                throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: No group specified");
            }
        }
    }

    curparam++;
    int nCount = 10;
    if (request.params.size() > curparam)
        nCount = request.params[curparam].get_int();

    curparam++;
    int nFrom = 0;
    if (request.params.size() > curparam)
        nFrom = request.params[curparam].get_int();

    curparam++;
    isminefilter filter = ISMINE_SPENDABLE;
    if (request.params.size() > curparam)
        if (request.params[curparam].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    if (nCount < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
    if (nFrom < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative from");

    UniValue ret(UniValue::VARR);

    const CWallet::TxItems &txOrdered = pwallet->wtxOrdered;

    // iterate backwards until we have nCount items to return:
    for (CWallet::TxItems::const_reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
    {
        CWalletTx *const pwtx = (*it).second.first;
        if (pwtx != 0) {
            if (fAllGroups) {
                ListAllGroupedTransactions(pwallet, *pwtx, strAccount, 0, true, ret, filter);
            } else {
                ListGroupedTransactions(pwallet, grpID, *pwtx, strAccount, 0, true, ret, filter);
            }
        }
        CAccountingEntry *const pacentry = (*it).second.second;
        if (pacentry != 0)
            AcentryToJSON(*pacentry, strAccount, ret);

        if ((int)ret.size() >= (nCount + nFrom))
            break;
    }
    // ret is newest to oldest

    if (nFrom > (int)ret.size())
        nFrom = ret.size();
    if ((nFrom + nCount) > (int)ret.size())
        nCount = ret.size() - nFrom;

    std::vector<UniValue> arrTmp = ret.getValues();

    std::vector<UniValue>::iterator first = arrTmp.begin();
    std::advance(first, nFrom);
    std::vector<UniValue>::iterator last = arrTmp.begin();
    std::advance(last, nFrom + nCount);

    if (last != arrTmp.end())
        arrTmp.erase(last, arrTmp.end());
    if (first != arrTmp.begin())
        arrTmp.erase(arrTmp.begin(), first);

    std::reverse(arrTmp.begin(), arrTmp.end()); // Return oldest to newest

    ret.clear();
    ret.setArray();
    ret.push_backV(arrTmp);

    return ret;
}

extern UniValue listtokenssinceblock(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
            "listtokenssinceblock \"groupid\" ( \"blockhash\" target-confirmations includeWatchonly )\n"
            "\nGet all transactions in blocks since block [blockhash], or all transactions if omitted\n"
            "\nArguments:\n"
            "1. \"groupid\"              (string, required) List transactions containing this group only\n"
            "2. \"blockhash\"            (string, optional) The block hash to list transactions since\n"
            "3. target-confirmations:  (numeric, optional) The confirmations required, must be 1 or more\n"
            "4. includeWatchonly:      (bool, optional, default=false) Include transactions to watchonly addresses "
            "(see 'importaddress')"
            "\nResult:\n"
            "{\n"
            "  \"transactions\": [\n"
            "    \"account\":\"accountname\",       (string) DEPRECATED. The account name associated with the "
            "transaction. Will be \"\" for the default account.\n"
            "    \"address\":\"ION address\",    (string) The ION address of the transaction. Not present for "
            "move transactions (category = move).\n"
            "    \"category\":\"send|receive\",     (string) The transaction category. 'send' has negative amounts, "
            "'receive' has positive amounts.\n"
            "    \"amount\": x.xxx,          (numeric) The amount in "
            "ION. This is negative for the 'send' category, and for the 'move' category for moves \n"
                            "                                          outbound. It is positive for the 'receive' "
                            "category, and for the 'move' category for inbound funds.\n"
                            "    \"vout\" : n,               (numeric) the vout value\n"
                            "    \"fee\": x.xxx,             (numeric) The amount of the fee in "
            "ION"
            ". This is negative and only available for the 'send' category of transactions.\n"
            "    \"confirmations\": n,       (numeric) The number of confirmations for the transaction. Available for "
            "'send' and 'receive' category of transactions.\n"
            "    \"blockhash\": \"hashvalue\",     (string) The block hash containing the transaction. Available for "
            "'send' and 'receive' category of transactions.\n"
            "    \"blockindex\": n,          (numeric) The index of the transaction in the block that includes it. "
            "Available for 'send' and 'receive' category of transactions.\n"
            "    \"blocktime\": xxx,         (numeric) The block time in seconds since epoch (1 Jan 1970 GMT).\n"
            "    \"txid\": \"transactionid\",  (string) The transaction id. Available for 'send' and 'receive' "
            "category of transactions.\n"
            "    \"time\": xxx,              (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT).\n"
            "    \"timereceived\": xxx,      (numeric) The time received in seconds since epoch (Jan 1 1970 GMT). "
            "Available for 'send' and 'receive' category of transactions.\n"
            "    \"abandoned\": xxx,         (bool) 'true' if the transaction has been abandoned (inputs are "
            "respendable). Only available for the 'send' category of transactions.\n"
            "    \"comment\": \"...\",       (string) If a comment is associated with the transaction.\n"
            "    \"label\" : \"label\"       (string) A comment for the address/transaction, if any\n"
            "    \"to\": \"...\",            (string) If a comment to is associated with the transaction.\n"
            "  ],\n"
            "  \"lastblock\": \"lastblockhash\"     (string) The hash of the last block\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("listtokenssinceblock", "") +
            HelpExampleCli("listtokenssinceblock", "\"ionrt1zwm0kzlyptdmwy3849fd6z5epesnjkruqlwlv02u7y6ymf75nk4qs6u85re\" \"36507bf934ffeb556b4140a8d57750954ad4c3c3cd8abad3b8a7fd293ae6e93b\" 6") +
            HelpExampleRpc(
                "listtokenssinceblock", "\"36507bf934ffeb556b4140a8d57750954ad4c3c3cd8abad3b8a7fd293ae6e93b\", 6"));

    LOCK2(cs_main, pwallet->cs_wallet);

    unsigned int curparam = 0;

    CBlockIndex *pindex = NULL;
    int target_confirms = 1;
    isminefilter filter = ISMINE_SPENDABLE;

    if (request.params.size() <= curparam)
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: No group specified");
    }
    CTokenGroupID grpID = GetTokenGroup(request.params[curparam].get_str());
    if (!grpID.isUserGroup())
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: No group specified");
    }

    curparam++;
    if (request.params.size() > curparam)
    {
        uint256 blockId;

        blockId.SetHex(request.params[curparam].get_str());
        BlockMap::iterator it = mapBlockIndex.find(blockId);
        if (it != mapBlockIndex.end())
            pindex = it->second;
    }

    curparam++;
    if (request.params.size() > curparam)
    {
        target_confirms = boost::lexical_cast<unsigned int>(request.params[curparam].get_str());

        if (target_confirms < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter");
    }

    curparam++;
    if (request.params.size() > curparam)
        if (InterpretBool(request.params[curparam].get_str()))
            filter = filter | ISMINE_WATCH_ONLY;

    int depth = pindex ? (1 + chainActive.Height() - pindex->nHeight) : -1;

    UniValue transactions(UniValue::VARR);

    for (std::map<uint256, CWalletTx>::iterator it = pwallet->mapWallet.begin(); it != pwallet->mapWallet.end();
         it++)
    {
        CWalletTx tx = (*it).second;

        if (depth == -1 || tx.GetDepthInMainChain() < depth)
            ListGroupedTransactions(pwallet, grpID, tx, "*", 0, true, transactions, filter);
    }

    CBlockIndex *pblockLast = chainActive[chainActive.Height() + 1 - target_confirms];
    uint256 lastblock = pblockLast ? pblockLast->GetBlockHash() : uint256();

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("transactions", transactions));
    ret.push_back(Pair("lastblock", lastblock.GetHex()));

    return ret;
}

extern UniValue sendtoken(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
            "sendtoken \"groupid\" \"address\" amount ( \"address\" amount ) ( .. ) \n"
            "\nSends token to a given address. Specify multiple addresses and amounts for multiple recipients.\n"
            "\n"
            "1. \"groupid\"     (string, required) the group identifier\n"
            "2. \"address\"     (string, required) the destination address\n"
            "3. \"amount\"      (numeric, required) the amount of tokens to send\n"
        );

    EnsureWalletIsUnlocked(pwallet);

    CTokenGroupID grpID;
    CAmount totalTokensNeeded = 0;
    unsigned int curparam = 0;
    std::vector<CRecipient> outputs;
    curparam = ParseGroupAddrValue(request, curparam, grpID, outputs, totalTokensNeeded, true);

    if (outputs.empty())
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "No destination address or payment amount");
    }
    if (curparam != request.params.size())
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Improper number of parameters, did you forget the payment amount?");
    }

    // Optionally, add XDM fee
    CAmount XDMFeeNeeded = 0;
    if (tokenGroupManager->MatchesDarkMatter(grpID)) {
        tokenGroupManager->GetXDMFee(chainActive.Tip(), XDMFeeNeeded);
    }

    // Ensure enough XDM fees are paid
    EnsureXDMFee(outputs, XDMFeeNeeded);

    CWalletTx wtx;
    GroupSend(wtx, grpID, outputs, totalTokensNeeded, XDMFeeNeeded, pwallet);
    return wtx.GetHash().GetHex();
}

extern UniValue configuretokendryrun(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    unsigned int curparam = 0;
    bool confirmed = false;

    COutput coin(nullptr, 0, 0, false, false, false);

    {
        std::vector<COutput> coins;
        CAmount lowest = MAX_MONEY;
        pwallet->FilterCoins(coins, [&lowest](const CWalletTx *tx, const CTxOut *out) {
            CTokenGroupInfo tg(out->scriptPubKey);
            // although its possible to spend a grouped input to produce
            // a single mint group, I won't allow it to make the tx construction easier.
            if ((tg.associatedGroup == NoGroup) && (out->nValue < lowest))
            {
                lowest = out->nValue;
                return true;
            }
            return false;
        });

        if (0 == coins.size())
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "No coins available in the wallet");
        }
        coin = coins[coins.size() - 1];
    }

    uint64_t grpNonce = 0;

    std::vector<COutput> chosenCoins;
    chosenCoins.push_back(coin);

    std::vector<CRecipient> outputs;

    CReserveKey authKeyReservation(pwallet);
    CTxDestination authDest;
    CScript opretScript;
    if (curparam >= request.params.size())
    {
        CPubKey authKey;
        authKeyReservation.GetReservedKey(authKey, true);
        authDest = authKey.GetID();
    }
    else
    {
        authDest = DecodeDestination(request.params[curparam].get_str(), Params());
        if (authDest == CTxDestination(CNoDestination()))
        {
            auto desc = ParseGroupDescParams(request, curparam, confirmed);
            if (desc.size()) // Add an op_return if there's a token desc doc
            {
                opretScript = BuildTokenDescScript(desc);
                outputs.push_back(CRecipient{opretScript, 0, false});
            }
            CPubKey authKey;
            authKeyReservation.GetReservedKey(authKey, true);
            authDest = authKey.GetID();
        }
    }
    curparam++;

    CTokenGroupID grpID = findGroupId(coin.GetOutPoint(), opretScript, TokenGroupIdFlags::NONE, grpNonce);

    CScript script = GetScriptForDestination(authDest, grpID, (CAmount)GroupAuthorityFlags::ALL | grpNonce);
    CRecipient recipient = {script, GROUPED_SATOSHI_AMT, false};
    outputs.push_back(recipient);

    std::string strError;
    std::vector<COutput> coins;

    // When minting a regular (non-management) token, an XDM fee is needed
    // Note that XDM itself is also a management token
    // Add XDM output to fee address and to change address
    CAmount XDMFeeNeeded = 0;
    CAmount totalXDMAvailable = 0;
    if (!grpID.hasFlag(TokenGroupIdFlags::MGT_TOKEN))
    {
        tokenGroupManager->GetXDMFee(chainActive.Tip(), XDMFeeNeeded);
        XDMFeeNeeded *= 5;

        // Ensure enough XDM fees are paid
        EnsureXDMFee(outputs, XDMFeeNeeded);

        // Add XDM inputs
        if (XDMFeeNeeded > 0) {
            CTokenGroupID XDMGrpID = tokenGroupManager->GetDarkMatterID();
            pwallet->FilterCoins(coins, [XDMGrpID, &totalXDMAvailable](const CWalletTx *tx, const CTxOut *out) {
                CTokenGroupInfo tg(out->scriptPubKey);
                if ((XDMGrpID == tg.associatedGroup) && !tg.isAuthority())
                {
                    totalXDMAvailable += tg.quantity;
                    return true;
                }
                return false;
            });
        }

        if (totalXDMAvailable < XDMFeeNeeded)
        {
            strError = strprintf("Not enough XDM in the wallet.  Need %d more.", tokenGroupManager->TokenValueFromAmount(XDMFeeNeeded - totalXDMAvailable, grpID));
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strError);
        }

        // Get a near but greater quantity
        totalXDMAvailable = GroupCoinSelection(coins, XDMFeeNeeded, chosenCoins);
    }

    UniValue ret(UniValue::VOBJ);

/*
    UniValue retChosenCoins(UniValue::VARR);
    for (auto coin : chosenCoins) {
        retChosenCoins.push_back(coin.ToString());
    }
    ret.push_back(Pair("chosen_coins", retChosenCoins));
    UniValue retOutputs(UniValue::VOBJ);
    for (auto output : outputs) {
        retOutputs.push_back(Pair(output.scriptPubKey.ToString(), output.nAmount));
    }
    ret.push_back(Pair("outputs", retOutputs));
*/

    if (tokenGroupManager->ManagementTokensCreated(chainActive.Height())) {
        ret.push_back(Pair("xdm_available", tokenGroupManager->TokenValueFromAmount(totalXDMAvailable, tokenGroupManager->GetDarkMatterID())));
        ret.push_back(Pair("xdm_needed", tokenGroupManager->TokenValueFromAmount(XDMFeeNeeded, tokenGroupManager->GetDarkMatterID())));
    }
    ret.push_back(Pair("groupID", EncodeTokenGroup(grpID)));

    CTokenGroupInfo tokenGroupInfo(opretScript);
    CTokenGroupDescription tokenGroupDescription(opretScript);
    CTokenGroupStatus tokenGroupStatus;
    CTransaction dummyTransaction;
    CTokenGroupCreation tokenGroupCreation(MakeTransactionRef(dummyTransaction), uint256(), tokenGroupInfo, tokenGroupDescription, tokenGroupStatus);
    tokenGroupCreation.ValidateDescription();

    ret.push_back(Pair("ticker", tokenGroupCreation.tokenGroupDescription.strTicker));
    ret.push_back(Pair("name", tokenGroupCreation.tokenGroupDescription.strName));
    ret.push_back(Pair("decimalpos", tokenGroupCreation.tokenGroupDescription.nDecimalPos));
    ret.push_back(Pair("documenturl", tokenGroupCreation.tokenGroupDescription.strDocumentUrl));
    ret.push_back(Pair("documenthash", tokenGroupCreation.tokenGroupDescription.documentHash.ToString()));
    ret.push_back(Pair("status", tokenGroupCreation.status.messages));

    return ret;
}

extern UniValue configuretoken(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

     if (request.fHelp || request.params.size() < 5)
        throw std::runtime_error(
            "configuretoken \"ticker\" \"name\" decimalpos \"description_url\" description_hash ( confirm_send ) \n"
            "\n"
            "Configures a new token type.\n"
            "\nArguments:\n"
            "1. \"ticker\"              (string, required) the token ticker\n"
            "2. \"name\"                (string, required) the token name\n"
            "3. \"decimalpos\"          (numeric, required, default=8) the number of decimals after the decimal separator\n"
            "4. \"description_url\"     (string, required) the URL of the token's description document\n"
            "5. \"description_hash\"    (hex, required) the hash of the token description document\n"
            "6. \"confirm_send\"        (boolean, optional, default=false) the configuration transaction will be sent\n"
            "\n"
            "\nExamples:\n" +
            HelpExampleCli("configuretoken", "\"GRAV\" \"GravityToken\" 6 \"https://raw.githubusercontent.com/ioncoincore/ATP-descriptions/master/ION-mainnet-GRAV.json\" 4f92d91db24bb0b8ca24a2ec86c4b012ccdc4b2e9d659c2079f5cc358413a765 true") +
            "\n"
        );

    if (request.params.size() < 6 || request.params[5].get_str() != "true") {
        return configuretokendryrun(request);
    }

    EnsureWalletIsUnlocked(pwallet);

    LOCK2(cs_main, pwallet->cs_wallet);

    unsigned int curparam = 0;
    bool confirmed = false;

    COutput coin(nullptr, 0, 0, false, false, false);

    {
        std::vector<COutput> coins;
        CAmount lowest = MAX_MONEY;
        pwallet->FilterCoins(coins, [&lowest](const CWalletTx *tx, const CTxOut *out) {
            CTokenGroupInfo tg(out->scriptPubKey);
            // although its possible to spend a grouped input to produce
            // a single mint group, I won't allow it to make the tx construction easier.
            if ((tg.associatedGroup == NoGroup) && (out->nValue < lowest))
            {
                lowest = out->nValue;
                return true;
            }
            return false;
        });

        if (0 == coins.size())
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "No coins available in the wallet");
        }
        coin = coins[coins.size() - 1];
    }

    uint64_t grpNonce = 0;

    std::vector<COutput> chosenCoins;
    chosenCoins.push_back(coin);

    std::vector<CRecipient> outputs;

    CReserveKey authKeyReservation(pwallet);
    CTxDestination authDest;
    CScript opretScript;

    authDest = DecodeDestination(request.params[curparam].get_str(), Params());
    if (authDest == CTxDestination(CNoDestination()))
    {
        auto desc = ParseGroupDescParams(request, curparam, confirmed);
        if (desc.size()) // Add an op_return if there's a token desc doc
        {
            opretScript = BuildTokenDescScript(desc);
            outputs.push_back(CRecipient{opretScript, 0, false});
        }
        CPubKey authKey;
        authKeyReservation.GetReservedKey(authKey, true);
        authDest = authKey.GetID();
    }

    CTokenGroupID grpID = findGroupId(coin.GetOutPoint(), opretScript, TokenGroupIdFlags::NONE, grpNonce);

    CScript script = GetScriptForDestination(authDest, grpID, (CAmount)GroupAuthorityFlags::ALL | grpNonce);
    CRecipient recipient = {script, GROUPED_SATOSHI_AMT, false};
    outputs.push_back(recipient);

    std::string strError;
    std::vector<COutput> coins;

    // When minting a regular (non-management) token, an XDM fee is needed
    // Note that XDM itself is also a management token
    // Add XDM output to fee address and to change address
    CAmount XDMFeeNeeded = 0;
    CAmount totalXDMAvailable = 0;
    if (!grpID.hasFlag(TokenGroupIdFlags::MGT_TOKEN))
    {
        tokenGroupManager->GetXDMFee(chainActive.Tip(), XDMFeeNeeded);
        XDMFeeNeeded *= 5;

        // Ensure enough XDM fees are paid
        EnsureXDMFee(outputs, XDMFeeNeeded);

        // Add XDM inputs
        if (XDMFeeNeeded > 0) {
            CTokenGroupID XDMGrpID = tokenGroupManager->GetDarkMatterID();
            pwallet->FilterCoins(coins, [XDMGrpID, &totalXDMAvailable](const CWalletTx *tx, const CTxOut *out) {
                CTokenGroupInfo tg(out->scriptPubKey);
                if ((XDMGrpID == tg.associatedGroup) && !tg.isAuthority())
                {
                    totalXDMAvailable += tg.quantity;
                    return true;
                }
                return false;
            });
        }

        if (totalXDMAvailable < XDMFeeNeeded)
        {
            strError = strprintf("Not enough XDM in the wallet.  Need %d more.", tokenGroupManager->TokenValueFromAmount(XDMFeeNeeded - totalXDMAvailable, grpID));
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strError);
        }

        // Get a near but greater quantity
        totalXDMAvailable = GroupCoinSelection(coins, XDMFeeNeeded, chosenCoins);
    }

    CWalletTx wtx;
    ConstructTx(wtx, chosenCoins, outputs, 0, XDMFeeNeeded, grpID, pwallet);
    authKeyReservation.KeepKey();
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("groupID", EncodeTokenGroup(grpID)));
    ret.push_back(Pair("transaction", wtx.GetHash().GetHex()));
    return ret;
}

extern UniValue configuremanagementtoken(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

     if (request.fHelp || request.params.size() < 5)
        throw std::runtime_error(
            "configuremanagementtoken \"ticker\" \"name\" decimalpos \"description_url\" description_hash ( confirm_send ) \n"
            "\n"
            "Configures a new management token type. Currelty the only management tokens are MAGIC, XDM and ATOM.\n"
            "\nArguments:\n"
            "1. \"ticker\"              (string, required) the token ticker\n"
            "2. \"name\"                (string, required) the token name\n"
            "3. \"decimalpos\"          (numeric, required) the number of decimals after the decimal separator\n"
            "4. \"description_url\"     (string, required) the URL of the token's description document\n"
            "5. \"description_hash\"    (hex) the hash of the token description document\n"
            "6. \"confirm_send\"        (boolean, optional, default=false) the configuration transaction will be sent\n"
            "\n"
            "\nExamples:\n" +
            HelpExampleCli("configuremanagementtoken", "\"MAGIC\" \"MagicToken\" 4 \"https://raw.githubusercontent.com/ioncoincore/ATP-descriptions/master/ION-testnet-MAGIC.json\" 4f92d91db24bb0b8ca24a2ec86c4b012ccdc4b2e9d659c2079f5cc358413a765 true") +
            "\n"
        );

    EnsureWalletIsUnlocked(pwallet);

    LOCK2(cs_main, pwallet->cs_wallet);
    unsigned int curparam = 0;
    bool confirmed = false;

    CReserveKey authKeyReservation(pwallet);
    CTxDestination authDest;
    CScript opretScript;
    std::vector<CRecipient> outputs;

    auto desc = ParseGroupDescParams(request, curparam, confirmed);
    if (desc.size()) // Add an op_return if there's a token desc doc
    {
        opretScript = BuildTokenDescScript(desc);
        outputs.push_back(CRecipient{opretScript, 0, false});
    }
    CPubKey authKey;
    authKeyReservation.GetReservedKey(authKey, true);
    authDest = authKey.GetID();

    COutput coin(nullptr, 0, 0, false, false, false);
    // If the MagicToken exists: spend a magic token output
    // Otherwise: spend an ION output from the token management address
    if (tokenGroupManager->MagicTokensCreated()){
        CTokenGroupID magicID = tokenGroupManager->GetMagicID();

        std::vector<COutput> coins;
        CAmount lowest = MAX_MONEY;
        pwallet->FilterCoins(coins, [&lowest, magicID](const CWalletTx *tx, const CTxOut *out) {
            CTokenGroupInfo tg(out->scriptPubKey);
            // although its possible to spend a grouped input to produce
            // a single mint group, I won't allow it to make the tx construction easier.

            if (tg.associatedGroup == magicID && !tg.isAuthority())
            {
                CTxDestination address;
                if (ExtractDestination(out->scriptPubKey, address)) {
                    if ((tg.quantity < lowest))
                    {
                        lowest = tg.quantity;
                        return true;
                    }
                }
            }
            return false;
        });

        if (0 == coins.size())
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Input tx is not available for spending");
        }

        coin = coins[coins.size() - 1];

        // Add magic change
        CTxDestination address;
        ExtractDestination(coin.GetScriptPubKey(), address);
        CTokenGroupInfo tgMagicInfo(coin.GetScriptPubKey());
        CScript script = GetScriptForDestination(address, magicID, tgMagicInfo.getAmount());
        CRecipient recipient = {script, GROUPED_SATOSHI_AMT, false};
        outputs.push_back(recipient);
    } else {
        CTxDestination dest = DecodeDestination(Params().GetConsensus().strTokenManagementKey);

        std::vector<COutput> coins;
        CAmount lowest = MAX_MONEY;
        pwallet->FilterCoins(coins, [&lowest, dest](const CWalletTx *tx, const CTxOut *out) {
            CTokenGroupInfo tg(out->scriptPubKey);
            // although its possible to spend a grouped input to produce
            // a single mint group, I won't allow it to make the tx construction easier.

            if ((tg.associatedGroup == NoGroup))
            {
                CTxDestination address;
                txnouttype whichType;
                if (ExtractDestinationAndType(out->scriptPubKey, address, whichType))
                {
                    if (address == dest){
                        if ((out->nValue < lowest))
                        {
                            lowest = out->nValue;
                            return true;
                        }
                    }
                }
            }
            return false;
        });

        if (0 == coins.size())
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Input tx is not available for spending");
        }

        coin = coins[coins.size() - 1];
    }
    if (coin.tx == nullptr)
        throw JSONRPCError(RPC_INVALID_PARAMS, "Management Group Token key is not available");

    uint64_t grpNonce = 0;
    CTokenGroupID grpID = findGroupId(coin.GetOutPoint(), opretScript, TokenGroupIdFlags::MGT_TOKEN, grpNonce);

    std::vector<COutput> chosenCoins;
    chosenCoins.push_back(coin);

    CScript script = GetScriptForDestination(authDest, grpID, (CAmount)GroupAuthorityFlags::ALL | grpNonce);
    CRecipient recipient = {script, GROUPED_SATOSHI_AMT, false};
    outputs.push_back(recipient);

    UniValue ret(UniValue::VOBJ);
    if (confirmed) {
        CWalletTx wtx;
        ConstructTx(wtx, chosenCoins, outputs, 0, 0, grpID, pwallet);
        authKeyReservation.KeepKey();
        ret.push_back(Pair("groupID", EncodeTokenGroup(grpID)));
        ret.push_back(Pair("transaction", wtx.GetHash().GetHex()));
    }
    return ret;
}

extern UniValue createtokenauthorities(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
            "createtokenauthorities \"groupid\" \"ionaddress\" authoritylist \n"
            "\nCreates new authorities and sends them to the specified address.\n"
            "\nArguments:\n"
            "1. \"groupid\"     (string, required) the group identifier\n"
            "2. \"address\"     (string, required) the destination address\n"
            "3. \"quantity\"    (required) a list of token authorities to create, separated by spaces\n"
            "\n"
            "\nExamples:\n"
            "\nCreate a new authority that allows the reciepient to: 1) melt tokens, and 2) create new melt tokens:\n" +
            HelpExampleCli("createtokenauthorities", "\"ionrt1zwm0kzlyptdmwy3849fd6z5epesnjkruqlwlv02u7y6ymf75nk4qs6u85re\" \"g74Uz39YSNBB3DouQdH1UokcFT5qDWBMfa\" \"melt child\"") +
            "\n"
        );

    EnsureWalletIsUnlocked(pwallet);

    LOCK2(cs_main, pwallet->cs_wallet);
    unsigned int curparam = 0;
    std::vector<COutput> chosenCoins;
    std::vector<CRecipient> outputs;
    if (curparam >= request.params.size())
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Missing parameters");
    }

    CTokenGroupID grpID;
    GroupAuthorityFlags auth = GroupAuthorityFlags();
    // Get the group id from the command line
    grpID = GetTokenGroup(request.params[curparam].get_str());
    if (!grpID.isUserGroup())
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: No group specified");
    }

    // Get the destination address from the command line
    curparam++;
    CTxDestination dst = DecodeDestination(request.params[curparam].get_str(), Params());
    if (dst == CTxDestination(CNoDestination()))
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: destination address");
    }

    // Get what authority permissions the user wants from the command line
    curparam++;
    if (curparam < request.params.size()) // If flags are not specified, we assign all authorities
    {
        auth = ParseAuthorityParams(request, curparam);
        if (curparam < request.params.size())
        {
            std::string strError;
            strError = strprintf("Invalid parameter: flag %s", request.params[curparam].get_str());
            throw JSONRPCError(RPC_INVALID_PARAMS, strError);
        }
    } else {
        auth = GroupAuthorityFlags::ALL;
    }

    // Now find a compatible authority
    std::vector<COutput> coins;
    int nOptions = pwallet->FilterCoins(coins, [auth, grpID](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupInfo tg(out->scriptPubKey);
        if ((tg.associatedGroup == grpID) && tg.isAuthority() && tg.allowsRenew())
        {
            // does this authority have at least the needed bits set?
            if ((tg.controllingGroupFlags() & auth) == auth)
                return true;
        }
        return false;
    });

    // if its a subgroup look for a parent authority that will work
    if ((nOptions == 0) && (grpID.isSubgroup()))
    {
        // if its a subgroup look for a parent authority that will work
        nOptions = pwallet->FilterCoins(coins, [auth, grpID](const CWalletTx *tx, const CTxOut *out) {
            CTokenGroupInfo tg(out->scriptPubKey);
            if (tg.isAuthority() && tg.allowsRenew() && tg.allowsSubgroup() &&
                (tg.associatedGroup == grpID.parentGroup()))
            {
                if ((tg.controllingGroupFlags() & auth) == auth)
                    return true;
            }
            return false;
        });
    }

    if (nOptions == 0) // TODO: look for multiple authorities that can be combined to form the required bits
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "No authority exists that can grant the requested priviledges.");
    }
    else
    {
        // Just pick the first compatible authority.
        for (auto coin : coins)
        {
            chosenCoins.push_back(coin);
            break;
        }
    }

    CReserveKey renewAuthorityKey(pwallet);
    RenewAuthority(chosenCoins[0], outputs, renewAuthorityKey);

    { // Construct the new authority
        CScript script = GetScriptForDestination(dst, grpID, (CAmount)auth);
        CRecipient recipient = {script, GROUPED_SATOSHI_AMT, false};
        outputs.push_back(recipient);
    }

    CWalletTx wtx;
    ConstructTx(wtx, chosenCoins, outputs, 0, 0, grpID, pwallet);
    renewAuthorityKey.KeepKey();
    return wtx.GetHash().GetHex();
}

extern UniValue listtokenauthorities(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "listtokenauthorities ( \"groupid\" ) \n"
            "\nLists the available token authorities.\n"
            "\nArguments:\n"
            "1. \"groupid\"     (string, optional) the token group identifier\n"
            "\n"
            "\nExamples:\n"
            "\nList all available token authorities of group ionrt1zwm0kzlyptdmwy3849fd6z5epesnjkruqlwlv02u7y6ymf75nk4qs6u85re:\n" +
            HelpExampleCli("listtokenauthorities", "\"ionrt1zwm0kzlyptdmwy3849fd6z5epesnjkruqlwlv02u7y6ymf75nk4qs6u85re\" ") +
            "\n"
        );

    std::vector<COutput> coins;
    if (request.params.size() == 0) // no group specified, show them all
    {
        ListAllGroupAuthorities(pwallet, coins);
    } else {
        CTokenGroupID grpID = GetTokenGroup(request.params[0].get_str());
        if (!grpID.isUserGroup())
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter 1: No group specified");
        }
        ListGroupAuthorities(pwallet, coins, grpID);
    }
    UniValue ret(UniValue::VARR);
    for (const COutput &coin : coins)
    {
        CTokenGroupInfo tgInfo(coin.GetScriptPubKey());
        CTxDestination dest;
        ExtractDestination(coin.GetScriptPubKey(), dest);

        CTokenGroupCreation tgCreation;
        tokenGroupManager->GetTokenGroupCreation(tgInfo.associatedGroup, tgCreation);

        UniValue retobj(UniValue::VOBJ);
        retobj.push_back(Pair("groupID", EncodeTokenGroup(tgInfo.associatedGroup)));
        retobj.push_back(Pair("txid", coin.tx->GetHash().ToString()));
        retobj.push_back(Pair("vout", coin.i));
        retobj.push_back(Pair("ticker", tgCreation.tokenGroupDescription.strTicker));
        retobj.push_back(Pair("address", EncodeDestination(dest)));
        retobj.push_back(Pair("tokenAuthorities", EncodeGroupAuthority(tgInfo.controllingGroupFlags())));
        ret.push_back(retobj);
    }
    return ret;
}

extern UniValue droptokenauthorities(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
            "droptokenauthorities \"groupid\" \"transactionid\" outputnr [ authority1 ( authority2 ... ) ] \n"
            "\nDrops a token group's authorities.\n"
            "The authority to drop is specified by the txid:outnr of the UTXO that holds the authorities.\n"
            "\nArguments:\n"
            "1. \"groupid\"           (string, required) the group identifier\n"
            "2. \"transactionid\"     (string, required) transaction ID of the UTXO\n"
            "3. vout                (number, required) output number of the UTXO\n"
            "4. authority           (required) a list of token authorities to dro, separated by spaces\n"
            "\n"
            "\nExamples:\n"
            "\nDrop mint and melt authorities:\n" +
            HelpExampleCli("droptokenauthorities", "\"ionrt1zwm0kzlyptdmwy3849fd6z5epesnjkruqlwlv02u7y6ymf75nk4qs6u85re\" \"a018c9581b853e6387cf263fc14eeae07158e8e2ae47ce7434fcb87a3b75e7bf\" 1 \"mint\" \"melt\"") +
            "\n"
        );

    // Parameters:
    // - tokenGroupID
    // - tx ID of UTXU that needs to drop authorities
    // - vout value of UTXU that needs to drop authorities
    // - authority to remove
    // This function removes authority for a tokengroupID at a specific UTXO
    EnsureWalletIsUnlocked(pwallet);

    LOCK2(cs_main, pwallet->cs_wallet);
    unsigned int curparam = 0;
    std::vector<COutput> availableCoins;
    std::vector<COutput> chosenCoins;
    std::vector<CRecipient> outputs;
    if (curparam >= request.params.size())
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Missing parameters");
    }

    CTokenGroupID grpID;
    // Get the group id from the command line
    grpID = GetTokenGroup(request.params[curparam].get_str());
    if (!grpID.isUserGroup())
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: No group specified");
    }

    // Get the txid/voutnr from the command line
    curparam++;
    uint256 txid;
    txid.SetHex(request.params[curparam].get_str());
    // Note: IsHex("") is false
    if (txid == uint256()) {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: wrong txid");
    }

    curparam++;
    int32_t voutN;
    if (!ParseInt32(request.params[curparam].get_str(), &voutN) || voutN < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: wrong vout nr");
    }

    pwallet->AvailableCoins(availableCoins, true, nullptr, 0, MAX_MONEY, MAX_MONEY, 0, 0, 9999999, true);
    if (availableCoins.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: provided output is not available");
    }

    for (auto coin : availableCoins) {
        if (coin.tx->GetHash() == txid && coin.i == voutN) {
            chosenCoins.push_back(coin);
        }
    }
    if (chosenCoins.size() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: provided output is not available");
    }

    // Get what authority permissions the user wants from the command line
    curparam++;
    GroupAuthorityFlags authoritiesToDrop = GroupAuthorityFlags::NONE;
    if (curparam < request.params.size()) // If flags are not specified, we assign all authorities
    {
        while (1)
        {
            std::string sflag;
            std::string p = request.params[curparam].get_str();
            std::transform(p.begin(), p.end(), std::back_inserter(sflag), ::tolower);
            if (sflag == "mint")
                authoritiesToDrop |= GroupAuthorityFlags::MINT;
            else if (sflag == "melt")
                authoritiesToDrop |= GroupAuthorityFlags::MELT;
            else if (sflag == "child")
                authoritiesToDrop |= GroupAuthorityFlags::CCHILD;
            else if (sflag == "rescript")
                authoritiesToDrop |= GroupAuthorityFlags::RESCRIPT;
            else if (sflag == "subgroup")
                authoritiesToDrop |= GroupAuthorityFlags::SUBGROUP;
            else if (sflag == "configure")
                authoritiesToDrop |= GroupAuthorityFlags::CONFIGURE;
            else if (sflag == "all")
                authoritiesToDrop |= GroupAuthorityFlags::ALL;
            else
                break; // If param didn't match, then return because we've left the list of flags
            curparam++;
            if (curparam >= request.params.size())
                break;
        }
        if (curparam < request.params.size())
        {
            std::string strError;
            strError = strprintf("Invalid parameter: flag %s", request.params[curparam].get_str());
            throw JSONRPCError(RPC_INVALID_PARAMS, strError);
        }
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: need to specify which capabilities to drop");
    }

    CScript script = chosenCoins.at(0).GetScriptPubKey();
    CTokenGroupInfo tgInfo(script);
    CTxDestination dest;
    ExtractDestination(script, dest);
    std::string strAuthorities = EncodeGroupAuthority(tgInfo.controllingGroupFlags());

    GroupAuthorityFlags authoritiesToKeep = tgInfo.controllingGroupFlags() & ~authoritiesToDrop;

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("groupID", EncodeTokenGroup(tgInfo.associatedGroup)));
    ret.push_back(Pair("transaction", txid.GetHex()));
    ret.push_back(Pair("vout", voutN));
    ret.push_back(Pair("coin", chosenCoins.at(0).ToString()));
    ret.push_back(Pair("script", HexStr(script.begin(), script.end())));
    ret.push_back(Pair("destination", EncodeDestination(dest)));
    ret.push_back(Pair("authorities_former", strAuthorities));
    ret.push_back(Pair("authorities_new", EncodeGroupAuthority(authoritiesToKeep)));

    if ((authoritiesToKeep == GroupAuthorityFlags::CTRL) || (authoritiesToKeep == GroupAuthorityFlags::NONE) || !hasCapability(authoritiesToKeep, GroupAuthorityFlags::CTRL)) {
        ret.push_back(Pair("status", "Dropping all authorities"));
    } else {
        // Construct the new authority
        CScript script = GetScriptForDestination(dest, grpID, (CAmount)authoritiesToKeep);
        CRecipient recipient = {script, GROUPED_SATOSHI_AMT, false};
        outputs.push_back(recipient);
    }
    CWalletTx wtx;
    ConstructTx(wtx, chosenCoins, outputs, 0, 0, grpID, pwallet);
    return ret;
}

extern UniValue minttoken(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
            "minttoken \"groupid\" \"ionaddress\" quantity \n"
            "\nMint new tokens.\n"
            "\nArguments:\n"
            "1. \"groupID\"     (string, required) the group identifier\n"
            "2. \"address\"     (string, required) the destination address\n"
            "3. \"amount\"      (numeric, required) the amount of tokens desired\n"
            "\n"
            "\nExample:\n" +
            HelpExampleCli("minttoken", "ionrt1zwm0kzlyptdmwy3849fd6z5epesnjkruqlwlv02u7y6ymf75nk4qs6u85re gMngqs6eX1dUd8dKdwPqGJchc1S3e6b9Cx 40") +
            "\n"
        );

    EnsureWalletIsUnlocked(pwallet);

    LOCK(cs_main); // to maintain locking order
    LOCK(pwallet->cs_wallet); // because I am reserving UTXOs for use in a tx
    CTokenGroupID grpID;
    CAmount totalTokensNeeded = 0;
    unsigned int curparam = 0;
    std::vector<CRecipient> outputs;
    // Get data from the parameter line. this fills grpId and adds 1 output for the correct # of tokens
    curparam = ParseGroupAddrValue(request, curparam, grpID, outputs, totalTokensNeeded, true);

    if (outputs.empty())
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "No destination address or payment amount");
    }
    if (curparam != request.params.size())
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Improper number of parameters, did you forget the payment amount?");
    }

    CCoinControl coinControl;
    coinControl.fAllowOtherInputs = true; // Allow a normal ION input for change
    std::string strError;

    // Now find a mint authority
    std::vector<COutput> coins;
    int nOptions = pwallet->FilterCoins(coins, [grpID](const CWalletTx *tx, const CTxOut *out) {
        CTokenGroupInfo tg(out->scriptPubKey);
        if ((tg.associatedGroup == grpID) && tg.allowsMint())
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
        nOptions = pwallet->FilterCoins(coins, [grpID](const CWalletTx *tx, const CTxOut *out) {
            CTokenGroupInfo tg(out->scriptPubKey);
            if (tg.isAuthority() && tg.allowsRenew() && tg.allowsSubgroup() && tg.allowsMint() &&
                (tg.associatedGroup == grpID.parentGroup()))
            {
                return true;
            }
            return false;
        });
    }

    if (nOptions == 0)
    {
        strError = _("To mint coins, an authority output with mint capability is needed.");
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strError);
    }
    COutput authority(nullptr, 0, 0, false, false, false);

    // Just pick the first one for now.
    for (auto coin : coins)
    {
        authority = coin;
        break;
    }

    std::vector<COutput> chosenCoins;
    chosenCoins.push_back(authority);

    CReserveKey childAuthorityKey(pwallet);
    RenewAuthority(authority, outputs, childAuthorityKey);

    // When minting a regular (non-management) token, an XDM fee is needed
    // Note that XDM itself is also a management token
    // Add XDM output to fee address and to change address
    CAmount XDMFeeNeeded = 0;
    CAmount totalXDMAvailable = 0;
    if (!grpID.hasFlag(TokenGroupIdFlags::MGT_TOKEN))
    {
        tokenGroupManager->GetXDMFee(chainActive.Tip(), XDMFeeNeeded);
        XDMFeeNeeded *= 5;

        // Ensure enough XDM fees are paid
        EnsureXDMFee(outputs, XDMFeeNeeded);

        // Add XDM inputs
        if (XDMFeeNeeded > 0) {
            CTokenGroupID XDMGrpID = tokenGroupManager->GetDarkMatterID();
            pwallet->FilterCoins(coins, [XDMGrpID, &totalXDMAvailable](const CWalletTx *tx, const CTxOut *out) {
                CTokenGroupInfo tg(out->scriptPubKey);
                if ((XDMGrpID == tg.associatedGroup) && !tg.isAuthority())
                {
                    totalXDMAvailable += tg.quantity;
                    return true;
                }
                return false;
            });
        }

        if (totalXDMAvailable < XDMFeeNeeded)
        {
            strError = strprintf("Not enough XDM in the wallet.  Need %d more.", tokenGroupManager->TokenValueFromAmount(XDMFeeNeeded - totalXDMAvailable, grpID));
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strError);
        }

        // Get a near but greater quantity
        totalXDMAvailable = GroupCoinSelection(coins, XDMFeeNeeded, chosenCoins);
    }

    // I don't "need" tokens even though they are in the output because I'm minting, which is why
    // the token quantities are 0
    CWalletTx wtx;
    ConstructTx(wtx, chosenCoins, outputs, 0, XDMFeeNeeded, grpID, pwallet);
    childAuthorityKey.KeepKey();
    return wtx.GetHash().GetHex();
}

extern UniValue melttoken(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
            "melttoken \"groupid\" quantity \n"
            "\nMelts the specified amount of tokens.\n"
            "\nArguments:\n"
            "1. \"groupID\"     (string, required) the group identifier\n"
            "2. \"amount\"      (numeric, required) the amount of tokens desired\n"
            "\n"
            "\nExample:\n" +
            HelpExampleCli("melttoken", "ionrt1zwm0kzlyptdmwy3849fd6z5epesnjkruqlwlv02u7y6ymf75nk4qs6u85re 4.3") +
            "\n"
        );

    EnsureWalletIsUnlocked(pwallet);

    CTokenGroupID grpID;
    std::vector<CRecipient> outputs;

    unsigned int curparam = 0;

    grpID = GetTokenGroup(request.params[curparam].get_str());
    if (!grpID.isUserGroup())
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter: No group specified");
    }

    curparam++;
    CAmount totalNeeded = tokenGroupManager->AmountFromTokenValue(request.params[curparam], grpID);

    CWalletTx wtx;
    GroupMelt(wtx, grpID, totalNeeded, pwallet);
    return wtx.GetHash().GetHex();
}

UniValue listunspenttokens(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() > 6)
        throw std::runtime_error(
            "listunspenttokens ( minconf maxconf  [\"addresses\",...] [include_unsafe] [query_options])\n"
            "\nReturns array of unspent transaction outputs\n"
            "with between minconf and maxconf (inclusive) confirmations.\n"
            "Optionally filter to only include txouts paid to specified addresses.\n"
            "\nArguments:\n"
            "1. \"groupid\"      (string, optional) the token group identifier. Leave empty for all groups.\n"
            "2. minconf          (numeric, optional, default=1) The minimum confirmations to filter\n"
            "3. maxconf          (numeric, optional, default=9999999) The maximum confirmations to filter\n"
            "4. \"addresses\"      (string) A json array of ion addresses to filter\n"
            "    [\n"
            "      \"address\"     (string) ion address\n"
            "      ,...\n"
            "    ]\n"
            "5. include_unsafe (bool, optional, default=true) Include outputs that are not safe to spend\n"
            "                  See description of \"safe\" attribute below.\n"
            "6. query_options    (json, optional) JSON with query options\n"
            "    {\n"
            "      \"minimumAmount\"    (numeric or string, default=0) Minimum value of each UTXO in " + CURRENCY_UNIT + "\n"
            "      \"maximumAmount\"    (numeric or string, default=unlimited) Maximum value of each UTXO in " + CURRENCY_UNIT + "\n"
            "      \"maximumCount\"     (numeric or string, default=unlimited) Maximum number of UTXOs\n"
            "      \"minimumSumAmount\" (numeric or string, default=unlimited) Minimum sum value of all UTXOs in " + CURRENCY_UNIT + "\n"
            "    }\n"
            "\nResult\n"
            "[                   (array of json object)\n"
            "  {\n"
            "    \"txid\" : \"txid\",          (string) the transaction id \n"
            "    \"vout\" : n,               (numeric) the vout value\n"
            "    \"address\" : \"address\",    (string) the ion address\n"
            "    \"address\" : \"address\",    (string) the ion address\n"
            "    \"address\" : \"address\",    (string) the ion address\n"
            "    \"account\" : \"account\",    (string) DEPRECATED. The associated account, or \"\" for the default account\n"
            "    \"scriptPubKey\" : \"key\",   (string) the script key\n"
            "    \"amount\" : x.xxx,         (numeric) the transaction output amount in " + CURRENCY_UNIT + "\n"
            "    \"confirmations\" : n,      (numeric) The number of confirmations\n"
            "    \"redeemScript\" : n        (string) The redeemScript if scriptPubKey is P2SH\n"
            "    \"spendable\" : xxx,        (bool) Whether we have the private keys to spend this output\n"
            "    \"solvable\" : xxx,         (bool) Whether we know how to spend this output, ignoring the lack of keys\n"
            "    \"safe\" : xxx              (bool) Whether this output is considered safe to spend. Unconfirmed transactions\n"
            "                              from outside keys and unconfirmed replacement transactions are considered unsafe\n"
            "                              and are not eligible for spending by fundrawtransaction and sendtoaddress.\n"
            "    \"ps_rounds\" : n           (numeric) The number of PS rounds\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n"
            + HelpExampleCli("listunspenttokens", "")
            + HelpExampleCli("listunspenttokens", "\"tion1z0ysghq9vf3r38tpmfd87sf9s9fw6yl59ctnrd0jl905m39d8mfss3v0s8j\" 6 9999999 \"[\\\"idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6\\\",\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcg\\\"]\"")
            + HelpExampleRpc("listunspenttokens", "\"\" 6, 9999999 \"[\\\"idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6\\\",\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcg\\\"]\"")
            + HelpExampleCli("listunspenttokens", "\"tion1z0ysghq9vf3r38tpmfd87sf9s9fw6yl59ctnrd0jl905m39d8mfss3v0s8j\" 6 9999999 '[]' true '{ \"minimumAmount\": 0.005 }'")
            + HelpExampleRpc("listunspenttokens", "\"\"6, 9999999, [] , true, { \"minimumAmount\": 0.005 } ")
        );
    int curparam = 0;

    bool fIncludeGrouped = true;
    bool fFilterGrouped = false;
    CTokenGroupID filterGroupID;
    if (request.params.size() > curparam && !request.params[curparam].isNull()) {
        filterGroupID = GetTokenGroup(request.params[curparam].get_str());
        if (!filterGroupID.isUserGroup()) {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid parameter 1: No group specified");
        } else {
            fFilterGrouped = true;
        }
        if (filterGroupID == NoGroup) {
            fIncludeGrouped = false;
        }
    }
    curparam++;

    int nMinDepth = 1;
    if (request.params.size() > curparam && !request.params[curparam].isNull()) {
        RPCTypeCheckArgument(request.params[curparam], UniValue::VNUM);
        nMinDepth = request.params[curparam].get_int();
    }
    curparam++;

    int nMaxDepth = 9999999;
    if (request.params.size() > curparam && !request.params[curparam].isNull()) {
        RPCTypeCheckArgument(request.params[curparam], UniValue::VNUM);
        nMaxDepth = request.params[curparam].get_int();
    }
    curparam++;

    std::set<CBitcoinAddress> setAddress;
    if (request.params.size() > curparam && !request.params[curparam].isNull()) {
        RPCTypeCheckArgument(request.params[curparam], UniValue::VARR);
        UniValue inputs = request.params[curparam].get_array();
        for (unsigned int idx = 0; idx < inputs.size(); idx++) {
            const UniValue& input = inputs[idx];
            CBitcoinAddress address(input.get_str());
            if (!address.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Ion address: ")+input.get_str());
            if (setAddress.count(address))
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ")+input.get_str());
           setAddress.insert(address);
        }
    }
    curparam++;

    bool include_unsafe = true;
    if (request.params.size() > curparam && !request.params[curparam].isNull()) {
        RPCTypeCheckArgument(request.params[curparam], UniValue::VBOOL);
        include_unsafe = request.params[curparam].get_bool();
    }
    curparam++;

    CAmount nMinimumAmount = 0;
    CAmount nMaximumAmount = MAX_MONEY;
    CAmount nMinimumSumAmount = MAX_MONEY;
    uint64_t nMaximumCount = 0;

    if (!request.params[curparam].isNull()) {
        const UniValue& options = request.params[curparam].get_obj();

        if (options.exists("minimumAmount"))
            nMinimumAmount = AmountFromValue(options["minimumAmount"]);

        if (options.exists("maximumAmount"))
            nMaximumAmount = AmountFromValue(options["maximumAmount"]);

        if (options.exists("minimumSumAmount"))
            nMinimumSumAmount = AmountFromValue(options["minimumSumAmount"]);

        if (options.exists("maximumCount"))
            nMaximumCount = options["maximumCount"].get_int64();
    }
    curparam++;

    UniValue results(UniValue::VARR);
    std::vector<COutput> vecOutputs;
    assert(pwallet != nullptr);
    LOCK2(cs_main, pwallet->cs_wallet);

    pwallet->AvailableCoins(vecOutputs, !include_unsafe, nullptr, nMinimumAmount, nMaximumAmount, nMinimumSumAmount, nMaximumCount, nMinDepth, nMaxDepth, fIncludeGrouped);
    for (const COutput& out : vecOutputs) {
        CTxDestination address;
        const CScript& scriptPubKey = out.tx->tx->vout[out.i].scriptPubKey;

        CTokenGroupInfo grp(scriptPubKey);
        CTokenGroupCreation tgCreation;
        bool fValidGroup = !grp.invalid && grp.associatedGroup != NoGroup && !grp.isAuthority() && tokenGroupManager->GetTokenGroupCreation(grp.associatedGroup, tgCreation);

        if (fFilterGrouped && (!fValidGroup || grp.associatedGroup != filterGroupID))
            continue;

        bool fValidAddress = ExtractDestination(scriptPubKey, address);

        if (setAddress.size() && (!fValidAddress || !setAddress.count(address)))
            continue;

        UniValue entry(UniValue::VOBJ);
        entry.push_back(Pair("txid", out.tx->GetHash().GetHex()));
        entry.push_back(Pair("vout", out.i));

        if (fValidGroup) {
            entry.push_back(Pair("groupID", EncodeTokenGroup(grp.associatedGroup)));
            entry.push_back(Pair("ticker", tgCreation.tokenGroupDescription.strTicker));
            entry.push_back(Pair("tokenAmount", tokenGroupManager->TokenValueFromAmount(grp.getAmount(), tgCreation.tokenGroupInfo.associatedGroup)));
        }

        if (fValidAddress) {
            entry.push_back(Pair("address", CBitcoinAddress(address).ToString()));

            if (pwallet->mapAddressBook.count(address)) {
                entry.push_back(Pair("account", pwallet->mapAddressBook[address].name));
            }

            if (scriptPubKey.IsPayToScriptHash()) {
                const CScriptID& hash = boost::get<CScriptID>(address);
                CScript redeemScript;
                if (pwallet->GetCScript(hash, redeemScript)) {
                    entry.push_back(Pair("redeemScript", HexStr(redeemScript.begin(), redeemScript.end())));
                }
            }
        }

        entry.push_back(Pair("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end())));
        entry.push_back(Pair("amount", ValueFromAmount(out.tx->tx->vout[out.i].nValue)));
        entry.push_back(Pair("confirmations", out.nDepth));
        entry.push_back(Pair("spendable", out.fSpendable));
        entry.push_back(Pair("solvable", out.fSolvable));
        entry.push_back(Pair("safe", out.fSafe));
        entry.push_back(Pair("ps_rounds", pwallet->GetCappedOutpointPrivateSendRounds(COutPoint(out.tx->GetHash(), out.i))));
        results.push_back(entry);
    }

    return results;
}

static const CRPCCommand commands[] =
{ //  category              name                        actor (function)            okSafeMode
  //  --------------------- --------------------------  --------------------------  ----------
    { "tokens",             "listtokentransactions",    &listtokentransactions,     false, {}   },
    { "tokens",             "listtokenssinceblock",     &listtokenssinceblock,      false, {}   },
    { "tokens",             "listunspenttokens",        &listunspenttokens,         false, {"groupid","minconf","maxconf","addresses","include_unsafe","query_options"} },
    { "tokens",             "gettokenbalance",          &gettokenbalance,           false, {}   },
    { "tokens",             "sendtoken",                &sendtoken,                 false, {}   },
    { "tokens",             "configuretoken",           &configuretoken,            false, {}   },
    { "tokens",             "configuremanagementtoken", &configuremanagementtoken,  false, {}   },
    { "tokens",             "createtokenauthorities",   &createtokenauthorities,    false, {}   },
    { "tokens",             "listtokenauthorities",     &listtokenauthorities,      false, {}   },
    { "tokens",             "droptokenauthorities",     &droptokenauthorities,      false, {}   },
    { "tokens",             "minttoken",                &minttoken,                 false, {}   },
    { "tokens",             "melttoken",                &melttoken,                 false, {}   },
};

void RegisterTokenWalletRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
