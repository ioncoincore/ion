// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "uint256.h"

#include <math.h>

unsigned int static KimotoGravityWell(const CBlockIndex* pindexLast, const Consensus::Params& params) {
    const CBlockIndex *BlockLastSolved = pindexLast;
    const CBlockIndex *BlockReading = pindexLast;
    uint64_t PastBlocksMass = 0;
    int64_t PastRateActualSeconds = 0;
    int64_t PastRateTargetSeconds = 0;
    double PastRateAdjustmentRatio = double(1);
    arith_uint256 PastDifficultyAverage;
    arith_uint256 PastDifficultyAveragePrev;
    double EventHorizonDeviation;
    double EventHorizonDeviationFast;
    double EventHorizonDeviationSlow;

    uint64_t pastSecondsMin = params.nPowTargetTimespan * 0.025;
    uint64_t pastSecondsMax = params.nPowTargetTimespan * 7;
    uint64_t PastBlocksMin = pastSecondsMin / params.nPowTargetSpacing;
    uint64_t PastBlocksMax = pastSecondsMax / params.nPowTargetSpacing;

    if (BlockLastSolved == nullptr || BlockLastSolved->nHeight == 0 || (uint64_t)BlockLastSolved->nHeight < PastBlocksMin) { return UintToArith256(params.powLimit).GetCompact(); }

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) { break; }
        PastBlocksMass++;

        PastDifficultyAverage.SetCompact(BlockReading->nBits);
        if (i > 1) {
            // handle negative arith_uint256
            if(PastDifficultyAverage >= PastDifficultyAveragePrev)
                PastDifficultyAverage = ((PastDifficultyAverage - PastDifficultyAveragePrev) / i) + PastDifficultyAveragePrev;
            else
                PastDifficultyAverage = PastDifficultyAveragePrev - ((PastDifficultyAveragePrev - PastDifficultyAverage) / i);
        }
        PastDifficultyAveragePrev = PastDifficultyAverage;

        PastRateActualSeconds = BlockLastSolved->GetBlockTime() - BlockReading->GetBlockTime();
        PastRateTargetSeconds = params.nPowTargetSpacing * PastBlocksMass;
        PastRateAdjustmentRatio = double(1);
        if (PastRateActualSeconds < 0) { PastRateActualSeconds = 0; }
        if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
            PastRateAdjustmentRatio = double(PastRateTargetSeconds) / double(PastRateActualSeconds);
        }
        EventHorizonDeviation = 1 + (0.7084 * pow((double(PastBlocksMass)/double(28.2)), -1.228));
        EventHorizonDeviationFast = EventHorizonDeviation;
        EventHorizonDeviationSlow = 1 / EventHorizonDeviation;

        if (PastBlocksMass >= PastBlocksMin) {
                if ((PastRateAdjustmentRatio <= EventHorizonDeviationSlow) || (PastRateAdjustmentRatio >= EventHorizonDeviationFast))
                { assert(BlockReading); break; }
        }
        if (BlockReading->pprev == nullptr) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    arith_uint256 bnNew(PastDifficultyAverage);
    if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
        bnNew *= PastRateActualSeconds;
        bnNew /= PastRateTargetSeconds;
    }

    if (bnNew > UintToArith256(params.powLimit)) {
        bnNew = UintToArith256(params.powLimit);
    }

    return bnNew.GetCompact();
}

unsigned int static DarkGravityWave(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params, const bool fProofOfStake) {
    /* current difficulty formula, dash - DarkGravity v3, written by Evan Duffield - evan@dash.org */
    const arith_uint256 bnPowLimit = fProofOfStake ? UintToArith256(params.posLimit) : UintToArith256(params.powLimit);
    int64_t nPastBlocks = 24;

    // make sure we have at least (nPastBlocks + 1) blocks, otherwise just return powLimit
    if (!pindexLast || pindexLast->nHeight < nPastBlocks) {
        return bnPowLimit.GetCompact();
    }

    if (params.fPowAllowMinDifficultyBlocks) {
        // recent block is more than 2 hours old
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + 2 * 60 * 60) {
            return bnPowLimit.GetCompact();
        }
        // recent block is more than 10 minutes old
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 4) {
            arith_uint256 bnNew = arith_uint256().SetCompact(pindexLast->nBits) * 10;
            if (bnNew > bnPowLimit) {
                bnNew = bnPowLimit;
            }
            return bnNew.GetCompact();
        }
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
            assert(pindex->pprev); // should never fail
            pindex = pindex->pprev;
        }
    }

    arith_uint256 bnNew(bnPastTargetAvg);

    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindex->GetBlockTime();
    // NOTE: is this accurate? nActualTimespan counts it for (nPastBlocks - 1) blocks only...
    int64_t nTargetTimespan = nPastBlocks * params.nPowTargetSpacing;

    if (nActualTimespan < nTargetTimespan/3)
        nActualTimespan = nTargetTimespan/3;
    if (nActualTimespan > nTargetTimespan*3)
        nActualTimespan = nTargetTimespan*3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequiredBTC(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Only change once per interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 2.5 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 1 day worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

   return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int static GetNextWorkRequiredPivx(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params, const bool fProofOfStake)
{
    if (Params().NetworkIDString() == CBaseChainParams::REGTEST)
        return pindexLast->nBits;

    /* current difficulty formula, ion - DarkGravity v3, written by Evan Duffield - evan@dashpay.io */
    const CBlockIndex* BlockLastSolved = pindexLast;
    const CBlockIndex* BlockReading = pindexLast;
    int64_t nActualTimespan = 0;
    int64_t LastBlockTime = 0;
    int64_t PastBlocksMin = 24;
    int64_t PastBlocksMax = 24;
    int64_t CountBlocks = 0;
    arith_uint256 PastDifficultyAverage;
    arith_uint256 PastDifficultyAveragePrev;

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || BlockLastSolved->nHeight < params.DGWStartHeight + PastBlocksMin) {
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
  int blockoffset = 0;
  int64_t oldblocktime;
  int64_t blocktime;

  *avgOf5 = *avgOf7 = *avgOf9 = *avgOf17 = 0;
  if (pindexLast)
    blocktime = pindexLast->GetBlockTime();
  else blocktime = 0;

  for (blockoffset = 0; blockoffset < 17; blockoffset++)
  {
    oldblocktime = blocktime;
    if (pindexLast)
    {
      pindexLast = pindexLast->pprev;
      blocktime = pindexLast->GetBlockTime();
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

unsigned int static GetNextWorkRequiredMidas(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params, const bool fProofOfStake)
{
    int64_t avgOf5;
    int64_t avgOf9;
    int64_t avgOf7;
    int64_t avgOf17;
    int64_t toofast;
    int64_t tooslow;
    int64_t difficultyfactor = 10000;
    int64_t now;
    int64_t BlockHeightTime;

    int64_t nFastInterval = (params.nPosTargetSpacingMidas * 9 ) / 10; // seconds per block desired when far behind schedule
    int64_t nSlowInterval = (params.nPosTargetSpacingMidas * 11) / 10; // seconds per block desired when far ahead of schedule
    int64_t nIntervalDesired;

    uint256 bnTargetLimit = fProofOfStake ? params.posLimit : params.powLimit;
    unsigned int nTargetLimit = UintToArith256(bnTargetLimit).GetCompact();

    // Genesis Block
    if (pindexLast == NULL)
        return nTargetLimit;

    // Regulate block times so as to remain synchronized in the long run with the actual time.  The first step is to
    // calculate what interval we want to use as our regulatory goal.  It depends on how far ahead of (or behind)
    // schedule we are.  If we're more than an adjustment period ahead or behind, we use the maximum (nSlowInterval) or minimum
    // (nFastInterval) values; otherwise we calculate a weighted average somewhere in between them.  The closer we are
    // to being exactly on schedule the closer our selected interval will be to our nominal interval (TargetSpacing).

    now = pindexLast->GetBlockTime();
    BlockHeightTime = Params().GenesisBlock().nTime + pindexLast->nHeight * params.nPosTargetSpacingMidas;

    if (now < BlockHeightTime + params.nPosTargetTimespanMidas && now > BlockHeightTime )
    // ahead of schedule by less than one interval.
    nIntervalDesired = ((params.nPosTargetTimespanMidas - (now - BlockHeightTime)) * params.nPosTargetSpacingMidas +
                (now - BlockHeightTime) * nFastInterval) / params.nPosTargetSpacingMidas;
    else if (now + params.nPosTargetTimespanMidas > BlockHeightTime && now < BlockHeightTime)
    // behind schedule by less than one interval.
    nIntervalDesired = ((params.nPosTargetTimespanMidas - (BlockHeightTime - now)) * params.nPosTargetSpacingMidas +
                (BlockHeightTime - now) * nSlowInterval) / params.nPosTargetTimespanMidas;

    // ahead by more than one interval;
    else if (now < BlockHeightTime) nIntervalDesired = nSlowInterval;

    // behind by more than an interval.
    else  nIntervalDesired = nFastInterval;

    // find out what average intervals over last 5, 7, 9, and 17 blocks have been.
    avgRecentTimestamps(pindexLast, &avgOf5, &avgOf7, &avgOf9, &avgOf17, params);

    // check for emergency adjustments. These are to bring the diff up or down FAST when a burst miner or multipool
    // jumps on or off.  Once they kick in they can adjust difficulty very rapidly, and they can kick in very rapidly
    // after massive hash power jumps on or off.

    // Important note: This is a self-damping adjustment because 8/5 and 5/8 are closer to 1 than 3/2 and 2/3.  Do not
    // screw with the constants in a way that breaks this relationship.  Even though self-damping, it will usually
    // overshoot slightly. But normal adjustment will handle damping without getting back to emergency.
    toofast = (nIntervalDesired * 2) / 3;
    tooslow = (nIntervalDesired * 3) / 2;

    // both of these check the shortest interval to quickly stop when overshot.  Otherwise first is longer and second shorter.
    if (avgOf5 < toofast && avgOf9 < toofast && avgOf17 < toofast)
    {  //emergency adjustment, slow down (longer intervals because shorter blocks)
      difficultyfactor *= 8;
      difficultyfactor /= 5;
    }
    else if (avgOf5 > tooslow && avgOf7 > tooslow && avgOf9 > tooslow)
    {  //emergency adjustment, speed up (shorter intervals because longer blocks)
      difficultyfactor *= 5;
      difficultyfactor /= 8;
    }

    // If no emergency adjustment, check for normal adjustment.
    else if (((avgOf5 > nIntervalDesired || avgOf7 > nIntervalDesired) && avgOf9 > nIntervalDesired && avgOf17 > nIntervalDesired) ||
         ((avgOf5 < nIntervalDesired || avgOf7 < nIntervalDesired) && avgOf9 < nIntervalDesired && avgOf17 < nIntervalDesired))
    { // At least 3 averages too high or at least 3 too low, including the two longest. This will be executed 3/16 of
      // the time on the basis of random variation, even if the settings are perfect. It regulates one-sixth of the way
      // to the calculated point.
      difficultyfactor *= (6 * nIntervalDesired);
      difficultyfactor /= (avgOf17 +(5 * nIntervalDesired));
    }

    // limit to doubling or halving.  There are no conditions where this will make a difference unless there is an
    // unsuspected bug in the above code.
    if (difficultyfactor > 20000) difficultyfactor = 20000;
    if (difficultyfactor < 5000) difficultyfactor = 5000;

    arith_uint256 bnNew;
    arith_uint256 bnOld;

    bnOld.SetCompact(pindexLast->nBits);

    if (difficultyfactor == 10000) // no adjustment.
      return(bnOld.GetCompact());

    bnNew = bnOld / difficultyfactor;
    bnNew *= 10000;

    // Shouldn't this be bnTargetLimit ?
    if (bnNew > UintToArith256(bnTargetLimit))
      bnNew = UintToArith256(bnTargetLimit);

    return bnNew.GetCompact();

}

unsigned int static GetNextWorkRequiredOrig(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params, const bool fProofOfStake)
{
    uint256 bnTargetLimit = fProofOfStake ? uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff")
        : uint256S("000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

    if (pindexLast == NULL)
        return UintToArith256(bnTargetLimit).GetCompact(); // genesis block
    const CBlockIndex* pindexPrev = pindexLast;
    while (pindexPrev && pindexPrev->pprev && (pindexPrev->IsProofOfStake() != fProofOfStake))
        pindexPrev = pindexPrev->pprev;
    if (pindexPrev == NULL)
        return UintToArith256(bnTargetLimit).GetCompact(); // first block
    const CBlockIndex* pindexPrevPrev = pindexPrev->pprev;
    while (pindexPrevPrev && pindexPrevPrev->pprev && (pindexPrevPrev->IsProofOfStake() != fProofOfStake))
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

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    bool fProofOfStake;
    const int nHeight = pindexLast->nHeight + 1;
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

    // this is only active on devnets
    if (pindexLast->nHeight < params.nMinimumDifficultyBlocks) {
        unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();
        return nProofOfWorkLimit;
    }

    // Most recent algo first
    if (pindexLast->nHeight >= params.DGWStartHeight) {
        return GetNextWorkRequiredPivx(pindexLast, pblock, params, fProofOfStake);
    }
    else if (pindexLast->nHeight >= params.MidasStartHeight) {
        return GetNextWorkRequiredMidas(pindexLast, pblock, params, fProofOfStake);
    }
    else {
        return GetNextWorkRequiredOrig(pindexLast, pblock, params, fProofOfStake);
    }
}

// for DIFF_BTC only!
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
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
