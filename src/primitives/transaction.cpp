// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2018-2020 The Ion Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/transaction.h>

#include <hash.h>
#include <tinyformat.h>
#include <utilstrencodings.h>

std::string COutPoint::ToString() const
{
    return strprintf("COutPoint(%s, %u)", hash.ToString()/*.substr(0,10)*/, n);
}

std::string COutPoint::ToStringShort() const
{
    return strprintf("%s-%u", hash.ToString().substr(0,64), n);
}

CTxIn::CTxIn(COutPoint prevoutIn, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = prevoutIn;
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

CTxIn::CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = COutPoint(hashPrevTx, nOut);
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

std::string CTxIn::ToString() const
{
    std::string str;
    str += "CTxIn(";
    str += prevout.ToString();
    if (prevout.IsNull())
        str += strprintf(", coinbase %s", HexStr(scriptSig));
    else
        str += strprintf(", scriptSig=%s", HexStr(scriptSig).substr(0, 24));
    if (nSequence != SEQUENCE_FINAL)
        str += strprintf(", nSequence=%u", nSequence);
    str += ")";
    return str;
}

CTxOut::CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn, int nRoundsIn)
{
    nValue = nValueIn;
    scriptPubKey = scriptPubKeyIn;
    nRounds = nRoundsIn;
}

bool CTxOut::IsZerocoinMint() const
{
    return scriptPubKey.IsZerocoinMint();
}

std::string CTxOut::ToString() const
{
    return strprintf("CTxOut(nValue=%d.%08d, scriptPubKey=%s)", nValue / COIN, nValue % COIN, HexStr(scriptPubKey).substr(0, 30));
}

CMutableTransaction::CMutableTransaction() : nVersion(CTransaction::CURRENT_VERSION), nType(TRANSACTION_NORMAL), nLockTime(0) {}
CMutableTransaction::CMutableTransaction(const CTransaction& tx) : vin(tx.vin), vout(tx.vout), nVersion(tx.nVersion), nType(tx.nType), nLockTime(tx.nLockTime), vExtraPayload(tx.vExtraPayload) {}

uint256 CMutableTransaction::GetHash() const
{
    return SerializeHash(*this);
}

std::string CMutableTransaction::ToString() const
{
    std::string str;
    str += strprintf("CMutableTransaction(hash=%s, ver=%d, type=%d, nTime=%u, vin.size=%u, vout.size=%u, nLockTime=%u, vExtraPayload.size=%d)\n",
        GetHash().ToString().substr(0,10),
        nVersion,
        nType,
        nTime,
        vin.size(),
        vout.size(),
        nLockTime,
        vExtraPayload.size());
    for (unsigned int i = 0; i < vin.size(); i++)
        str += "    " + vin[i].ToString() + "\n";
    for (unsigned int i = 0; i < vout.size(); i++)
        str += "    " + vout[i].ToString() + "\n";
    return str;
}

uint256 CTransaction::ComputeHash() const
{
    return SerializeHash(*this);
}

/* For backward compatibility, the hash is initialized to 0. TODO: remove the need for this default constructor entirely. */
CTransaction::CTransaction() : vin(), vout(), nVersion(CTransaction::CURRENT_VERSION), nType(TRANSACTION_NORMAL), nLockTime(0), hash() {}
CTransaction::CTransaction(const CMutableTransaction &tx) : vin(tx.vin), vout(tx.vout), nVersion(tx.nVersion), nType(tx.nType), nLockTime(tx.nLockTime), vExtraPayload(tx.vExtraPayload), hash(ComputeHash()) {}
CTransaction::CTransaction(CMutableTransaction &&tx) : vin(std::move(tx.vin)), vout(std::move(tx.vout)), nVersion(tx.nVersion), nType(tx.nType), nLockTime(tx.nLockTime), vExtraPayload(tx.vExtraPayload), hash(ComputeHash()) {}

CAmount CTransaction::GetValueOut() const
{
    CAmount nValueOut = 0;
    for (const auto& tx_out : vout) {
        nValueOut += tx_out.nValue;
        if (!MoneyRange(tx_out.nValue) || !MoneyRange(nValueOut))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }
    return nValueOut;
}

unsigned int CTransaction::GetTotalSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

std::string CTransaction::ToString() const
{
    std::string str;
    str += strprintf("CTransaction(hash=%s, ver=%d, type=%d, nTime=%u, vin.size=%u, vout.size=%u, nLockTime=%u, vExtraPayload.size=%d)\n",
        GetHash().ToString().substr(0,10),
        nVersion,
        nType,
        nTime,
        vin.size(),
        vout.size(),
        nLockTime,
        vExtraPayload.size());
    for (const auto& tx_in : vin)
        str += "    " + tx_in.ToString() + "\n";
    for (const auto& tx_out : vout)
        str += "    " + tx_out.ToString() + "\n";
    return str;
}

CAmount CTransaction::GetZerocoinSpent() const
{
    CAmount nValueOut = 0;
    for (const CTxIn& txin : vin) {
        if(!txin.IsZerocoinSpend())
            continue;

        nValueOut += txin.nSequence * COIN;
    }

    return nValueOut;
}

bool CTransaction::HasZerocoinSpendInputs() const
{
    for (const CTxIn& txin: vin) {
        if (txin.IsZerocoinSpend() || txin.IsZerocoinPublicSpend())
            return true;
    }
    return false;
}

bool CTransaction::HasZerocoinMintOutputs() const
{
    for(const CTxOut& txout : vout) {
        if (txout.IsZerocoinMint())
            return true;
    }
    return false;
}

bool CTransaction::HasZerocoinPublicSpendInputs() const
{
    // The wallet only allows publicSpend inputs in the same tx and not a combination between ion and xion
    for(const CTxIn& txin : vin) {
        if (txin.IsZerocoinPublicSpend())
            return true;
    }
    return false;
}

bool CTransaction::IsCoinStake() const
{
    if (vin.empty())
        return false;

    // ppcoin: the coin stake transaction is marked with the first output empty
    bool fAllowNull = vin[0].IsZerocoinSpend();
    if (vin[0].prevout.IsNull() && !fAllowNull)
        return false;

    return (vout.size() >= 2 && vout[0].IsEmpty());
}

bool CTransaction::IsGenerated() const
{
    return IsCoinBase() || IsCoinStake();
}

bool CTxIn::IsZerocoinSpend() const
{
    return prevout.hash == uint256() && scriptSig.IsZerocoinSpend();
}

bool CTxIn::IsZerocoinPublicSpend() const
{
    return scriptSig.IsZerocoinPublicSpend();
}
