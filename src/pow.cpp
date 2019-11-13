// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2018-2020 The Ion Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "uint256.h"
#include "versionbits.h"

#include <math.h>

const CBlockIndex*  GetHybridPrevIndex(const CBlockIndex* pindex, const bool fPos, const int nMinHeight) {
    if (!pindex || !pindex->pprev || pindex->pprev->nHeight < nMinHeight)
        return nullptr;

    if (((pindex->pprev->nVersion & BLOCKTYPEBITS_MASK) == BlockTypeBits::BLOCKTYPE_STAKING) == fPos) {
        return pindex->pprev;
    } else {
        return GetHybridPrevIndex(pindex->pprev, fPos, nMinHeight);
    }
}

unsigned int static DarkGravityWave(const CBlockIndex* pindexLast, const Consensus::Params& params) {
    /* current difficulty formula, dash - DarkGravity v3, written by Evan Duffield - evan@dash.org */
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    int64_t nPastBlocks = 24;

    const CBlockIndex* pindexLast = ((pindexLastIn->nVersion & BLOCKTYPEBITS_MASK) == BlockTypeBits::BLOCKTYPE_STAKING) ?
            GetHybridPrevIndex(pindexLastIn, false, params.POSPOWStartHeight) : pindexLastIn;

    // make sure we have at least (nPastBlocks + 1) blocks, otherwise just return hybridPowLimit
    if (!pindexLast || pindexLast->nHeight < params.POSPOWStartHeight + nPastBlocks) {
        return bnPowLimit.GetCompact();
    }

    const CBlockIndex *pindex = pindexLast;
    arith_uint256 bnPastTargetAvg;

    for (unsigned int nCountBlocks = 1; nCountBlocks <= nPastBlocks; nCountBlocks++) {
        arith_uint256 bnTarget = arith_uint256().SetCompact(pindex->nBits);
        if (nCountBlocks == 1) {
            bnPastTargetAvg = bnTarget;
        } else {
            // NOTE: that's not an average really...
            bnPastTargetAvg = (bnPastTargetAvg * nCountBlocks + bnTarget) / (nCountBlocks + 1);
        }

        if(nCountBlocks != nPastBlocks) {
            pindex = GetHybridPrevIndex(pindex, false, params.POSPOWStartHeight);
            if (!pindex || pindex->nHeight <= params.POSPOWStartHeight) {
                // If less than (nPastBlocks + 1) blocks, return minimum difficulty
                return bnPowLimit.GetCompact();
            }
        }
    }

    arith_uint256 bnNew(bnPastTargetAvg);

    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindex->GetBlockTime();
    int64_t nTargetTimespan = nPastBlocks * params.nHybridPowTargetSpacing;

    if (nActualTimespan < nTargetTimespan/4)
        nActualTimespan = nTargetTimespan/4;
    if (nActualTimespan > nTargetTimespan*4)
        nActualTimespan = nTargetTimespan*4;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

unsigned int static HybridPoSPIVXDifficulty(const CBlockIndex* pindexLastIn, const Consensus::Params& params)
{
    const CBlockIndex* pindexLast = ((pindexLastIn->nVersion & BLOCKTYPEBITS_MASK) == BlockTypeBits::BLOCKTYPE_STAKING) ?
        pindexLastIn : GetHybridPrevIndex(pindexLastIn, true, params.POSPOWStartHeight);

    // params.POSPOWStartHeight marks the first hybrid POS block. Start with a minimum difficulty block.
    if (pindexLast == nullptr || pindexLast->nHeight <= params.POSPOWStartHeight) {
        return UintToArith256(params.posLimit).GetCompact();
    }

    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    arith_uint256 bnTargetLimit = UintToArith256(params.posLimit);
    if (pindexLast->nHeight > params.POSStartHeight) {
        int64_t nTargetSpacing = params.nHybridPosTargetSpacing;
        int64_t nInterval = 40;
        int64_t nTargetTimespan = nTargetSpacing * nInterval;

        int64_t nActualSpacing = 0;
        const CBlockIndex* pindexPrev = GetHybridPrevIndex(pindexLast, true, params.POSPOWStartHeight);
        if (pindexPrev)
            nActualSpacing = pindexLast->GetBlockTime() - pindexPrev->GetBlockTime();

        if (nActualSpacing < 0)
            nActualSpacing = 1;

        // ppcoin: target change every block
        // ppcoin: retarget with exponential moving toward target spacing
        arith_uint256 bnNew;
        bnNew.SetCompact(pindexLast->nBits);

        bnNew *= ((nInterval - 1) * nTargetSpacing + nActualSpacing + nActualSpacing);
        bnNew /= ((nInterval + 1) * nTargetSpacing);

        if (bnNew <= 0 || bnNew > bnTargetLimit)
            bnNew = bnTargetLimit;

        return bnNew.GetCompact();
    }
}

unsigned int static GetNextWorkRequiredPivx(const CBlockIndex* pindexLast, const Consensus::Params& params, const bool fProofOfStake)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    /* current difficulty formula, ion - DarkGravity v3, written by Evan Duffield - evan@ionpay.io */
    const CBlockIndex* BlockLastSolved = pindexLast;
    const CBlockIndex* BlockReading = pindexLast;
    int64_t nActualTimespan = 0;
    int64_t LastBlockTime = 0;
    int64_t PastBlocksMin = 24;
    int64_t PastBlocksMax = 24;
    int64_t CountBlocks = 0;
    arith_uint256 PastDifficultyAverage;
    arith_uint256 PastDifficultyAveragePrev;

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || BlockLastSolved->nHeight < params.DGWDifficultyStartHeight + PastBlocksMin) {
        return UintToArith256(params.powLimit).GetCompact();
    }

    arith_uint256 bnTargetLimit = fProofOfStake ? UintToArith256(params.posLimit) : UintToArith256(params.powLimit);
    if (pindexLast->nHeight > params.POSStartHeight) {
        int64_t nTargetSpacing = 60;
        int64_t nTargetTimespan = 60 * 40;

        int64_t nActualSpacing = 0;
        if (pindexLast->nHeight != 0)
            nActualSpacing = pindexLast->GetBlockTime() - pindexLast->pprev->GetBlockTime();

        if (nActualSpacing < 0)
            nActualSpacing = 1;

        // ppcoin: target change every block
        // ppcoin: retarget with exponential moving toward target spacing
        arith_uint256 bnNew;
        bnNew.SetCompact(pindexLast->nBits);

        int64_t nInterval = nTargetTimespan / nTargetSpacing;
        bnNew *= ((nInterval - 1) * nTargetSpacing + nActualSpacing + nActualSpacing);
        bnNew /= ((nInterval + 1) * nTargetSpacing);

        if (bnNew <= 0 || bnNew > bnTargetLimit)
            bnNew = bnTargetLimit;

        return bnNew.GetCompact();
    } else if (Params().NetworkIDString() == CBaseChainParams::TESTNET && pindexLast->nHeight + 3 > params.POSStartHeight) {
        // Exception for current testnet; remove when starting a new testnet
        bnTargetLimit = UintToArith256(params.posLimit);

        int64_t nTargetSpacing = 60;
        int64_t nTargetTimespan = 60 * 40;

        int64_t nActualSpacing = 0;
        if (pindexLast->nHeight != 0)
            nActualSpacing = pindexLast->GetBlockTime() - pindexLast->pprev->GetBlockTime();

        if (nActualSpacing < 0)
            nActualSpacing = 1;

        // ppcoin: target change every block
        // ppcoin: retarget with exponential moving toward target spacing
        arith_uint256 bnNew;
        bnNew.SetCompact(pindexLast->nBits);

        int64_t nInterval = nTargetTimespan / nTargetSpacing;
        bnNew *= ((nInterval - 1) * nTargetSpacing + nActualSpacing + nActualSpacing);
        bnNew /= ((nInterval + 1) * nTargetSpacing);

        if (bnNew <= 0 || bnNew > bnTargetLimit)
            bnNew = bnTargetLimit;

        return bnNew.GetCompact();
    }

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) {
            break;
        }
        CountBlocks++;

        if (CountBlocks <= PastBlocksMin) {
            if (CountBlocks == 1) {
                PastDifficultyAverage.SetCompact(BlockReading->nBits);
            } else {
                PastDifficultyAverage = ((PastDifficultyAveragePrev * CountBlocks) + (arith_uint256().SetCompact(BlockReading->nBits))) / (CountBlocks + 1);
            }
            PastDifficultyAveragePrev = PastDifficultyAverage;
        }

        if (LastBlockTime > 0) {
            int64_t Diff = (LastBlockTime - BlockReading->GetBlockTime());
            nActualTimespan += Diff;
        }
        LastBlockTime = BlockReading->GetBlockTime();

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    arith_uint256 bnNew(PastDifficultyAverage);

    int64_t _nTargetTimespan = CountBlocks * params.nPosTargetSpacing;

    if (nActualTimespan < _nTargetTimespan / 3)
        nActualTimespan = _nTargetTimespan / 3;
    if (nActualTimespan > _nTargetTimespan * 3)
        nActualTimespan = _nTargetTimespan * 3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= _nTargetTimespan;

    if (bnNew > bnTargetLimit) {
        bnNew = bnTargetLimit;
    }

    return bnNew.GetCompact();
}

void avgRecentTimestamps(const CBlockIndex* pindexLast, int64_t *avgOf5, int64_t *avgOf7, int64_t *avgOf9, int64_t *avgOf17, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    assert(pblock != nullptr);
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);

    // this is only active on devnets
    if (pindexLast->nHeight < params.nMinimumDifficultyBlocks) {
        return bnPowLimit.GetCompact();
    }
    else
    { // genesis block or previous
    blocktime -= params.nPosTargetSpacing;
    }
    // for each block, add interval.
    if (blockoffset < 5) *avgOf5 += (oldblocktime - blocktime);
    if (blockoffset < 7) *avgOf7 += (oldblocktime - blocktime);
    if (blockoffset < 9) *avgOf9 += (oldblocktime - blocktime);
    *avgOf17 += (oldblocktime - blocktime);
  }
  // now we have the sums of the block intervals. Division gets us the averages.
  *avgOf5 /= 5;
  *avgOf7 /= 7;
  *avgOf9 /= 9;
  *avgOf17 /= 17;
}

    if (pindexLast->nHeight + 1 < params.nPowKGWHeight) {
        return GetNextWorkRequiredBTC(pindexLast, pblock, params);
    }

    // Note: GetNextWorkRequiredBTC has it's own special difficulty rule,
    // so we only apply this to post-BTC algos.
    if (params.fPowAllowMinDifficultyBlocks) {
        // recent block is more than 2 hours old
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + 2 * 60 * 60) {
            return bnPowLimit.GetCompact();
        }
        // recent block is more than 10 minutes old
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 4) {
            arith_uint256 bnNew = arith_uint256().SetCompact(pindexLast->nBits) * 10;
            if (bnNew > bnPowLimit) {
                return bnPowLimit.GetCompact();
            }
            return bnNew.GetCompact();
        }
    }

    if (pindexLast->nHeight + 1 < params.nPowDGWHeight) {
        return KimotoGravityWell(pindexLast, params);
    }

    return DarkGravityWave(pindexLast, params);
}

unsigned int static GetNextWorkRequiredOrig(const CBlockIndex* pindexLast, const Consensus::Params& params, const bool fProofOfStake)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    uint256 bnTargetLimit = fProofOfStake && Params().NetworkIDString() == CBaseChainParams::MAIN ?
                uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff") : params.powLimit;

    if (pindexLast == NULL)
        return UintToArith256(bnTargetLimit).GetCompact(); // genesis block
    const CBlockIndex* pindexPrev = pindexLast;
    while (pindexPrev && pindexPrev->pprev && (IsProofOfStakeHeight(pindexPrev->nHeight, params) != fProofOfStake))
        pindexPrev = pindexPrev->pprev;
    if (pindexPrev == NULL)
        return UintToArith256(bnTargetLimit).GetCompact(); // first block
    const CBlockIndex* pindexPrevPrev = pindexPrev->pprev;
    while (pindexPrevPrev && pindexPrevPrev->pprev && (IsProofOfStakeHeight(pindexPrevPrev->nHeight, params) != fProofOfStake))
        pindexPrevPrev = pindexPrevPrev->pprev;
    if (pindexPrevPrev == NULL)
        return UintToArith256(bnTargetLimit).GetCompact(); // second block

    int64_t nActualSpacing = pindexPrev->GetBlockTime() - pindexPrevPrev->GetBlockTime();

    if (nActualSpacing < 0) {
        nActualSpacing = 64;
    }
    else if (fProofOfStake && nActualSpacing > 64 * 10) {
         nActualSpacing = 64 * 10;
    }

    // target change every block
    // retarget with exponential moving toward target spacing
    // Includes fix for wrong retargeting difficulty by Mammix2
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexPrev->nBits);

    int64_t nInterval = fProofOfStake ? 10 : 10;
    bnNew *= ((nInterval - 1) * 64 + nActualSpacing + nActualSpacing);
    bnNew /= ((nInterval + 1) * 64);

    if (bnNew <= 0 || bnNew > UintToArith256(bnTargetLimit))
        bnNew = UintToArith256(bnTargetLimit);

    return bnNew.GetCompact();
}

bool IsProofOfStakeHeight(const int nHeight, const Consensus::Params& params) {
    bool fProofOfStake;
    if (nHeight >= params.POSStartHeight){
        fProofOfStake = true;
    } else if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (nHeight >= 455 && nHeight <= 479) {
            fProofOfStake = true;
        } else if (nHeight >= 481 && nHeight <= 489) {
            fProofOfStake = true;
        } else if (nHeight >= 492 && nHeight <= 492) {
            fProofOfStake = true;
        } else if (nHeight >= 501 && nHeight <= 501) {
            fProofOfStake = true;
        } else if (nHeight >= 691 && nHeight <= 691) {
            fProofOfStake = true;
        } else if (nHeight >= 702 && nHeight <= 703) {
            fProofOfStake = true;
        } else if (nHeight >= 721 && nHeight <= 721) {
            fProofOfStake = true;
        } else if (nHeight >= 806 && nHeight <= 811) {
            fProofOfStake = true;
        } else if (nHeight >= 876 && nHeight <= 876) {
            fProofOfStake = true;
        } else if (nHeight >= 889 && nHeight <= 889) {
            fProofOfStake = true;
        } else if (nHeight >= 907 && nHeight <= 907) {
            fProofOfStake = true;
        } else if (nHeight >= 913 && nHeight <= 914) {
            fProofOfStake = true;
        } else if (nHeight >= 916 && nHeight <= 929) {
            fProofOfStake = true;
        } else if (nHeight >= 931 && nHeight <= 931) {
            fProofOfStake = true;
        } else if (nHeight >= 933 && nHeight <= 942) {
            fProofOfStake = true;
        } else if (nHeight >= 945 && nHeight <= 947) {
            fProofOfStake = true;
        } else if (nHeight >= 949 && nHeight <= 960) {
            fProofOfStake = true;
        } else if (nHeight >= 962 && nHeight <= 962) {
            fProofOfStake = true;
        } else if (nHeight >= 969 && nHeight <= 969) {
            fProofOfStake = true;
        } else if (nHeight >= 991 && nHeight <= 991) {
            fProofOfStake = true;
        } else {
            fProofOfStake = false;
        };
    } else {
        fProofOfStake = false;
    }
    return fProofOfStake;
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params, const bool fHybridPow)
{
    const int nHeight = pindexLast->nHeight + 1;
    bool fProofOfStake = IsProofOfStakeHeight(nHeight, params);

    // this is only active on devnets
    if (pindexLast->nHeight < params.nMinimumDifficultyBlocks) {
        unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();
        return nProofOfWorkLimit;
    }

    // Most recent algo first
    if (nHeight >= params.POSPOWStartHeight) {
        if (fHybridPow) {
            return HybridPoWDarkGravityWave(pindexLast, params);
        } else {
            return HybridPoSPIVXDifficulty(pindexLast, params);
        }
    } else if (pindexLast->nHeight >= params.DGWDifficultyStartHeight) {
        return GetNextWorkRequiredPivx(pindexLast, params, fProofOfStake);
    }
    else if (pindexLast->nHeight >= params.MidasStartHeight) {
        return GetNextWorkRequiredMidas(pindexLast, params, fProofOfStake);
    }
    else {
        return GetNextWorkRequiredOrig(pindexLast, params, fProofOfStake);
    }
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
