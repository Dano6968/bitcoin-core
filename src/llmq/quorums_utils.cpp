// Copyright (c) 2018-2019 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "llmq/quorums_utils.h"

#include "bls/bls_wrapper.h"
#include "chainparams.h"
#include "random.h"
#include "spork.h"

// todo: move me..
#include "evo/deterministicmns.h"
#include "tiertwo/masternode_meta_manager.h" // for g_mmetaman
#include "tiertwo/net_masternodes.h"
#include "validation.h"

namespace llmq
{

namespace utils
{

uint256 BuildCommitmentHash(Consensus::LLMQType llmqType, const uint256& blockHash, const std::vector<bool>& validMembers, const CBLSPublicKey& pubKey, const uint256& vvecHash)
{
    CHashWriter hw(SER_NETWORK, 0);
    hw << static_cast<uint8_t>(llmqType);
    hw << blockHash;
    hw << DYNBITSET(validMembers);
    hw << pubKey;
    hw << vvecHash;
    return hw.GetHash();
}

uint256 BuildSignHash(Consensus::LLMQType llmqType, const uint256& quorumHash, const uint256& id, const uint256& msgHash)
{
    CHashWriter h(SER_GETHASH, 0);
    h << static_cast<uint8_t>(llmqType);
    h << quorumHash;
    h << id;
    h << msgHash;
    return h.GetHash();
}

std::string ToHexStr(const std::vector<bool>& vBits)
{
    std::vector<uint8_t> vBytes((vBits.size() + 7) / 8);
    for (size_t i = 0; i < vBits.size(); i++) {
        vBytes[i / 8] |= vBits[i] << (i % 8);
    }
    return HexStr(vBytes);
}

uint256 DeterministicOutboundConnection(const uint256& proTxHash1, const uint256& proTxHash2)
{
    // We need to deterministically select who is going to initiate the connection. The naive way would be to simply
    // return the min(proTxHash1, proTxHash2), but this would create a bias towards MNs with a numerically low
    // hash. To fix this, we return the proTxHash that has the lowest value of:
    //   hash(min(proTxHash1, proTxHash2), max(proTxHash1, proTxHash2), proTxHashX)
    // where proTxHashX is the proTxHash to compare
    uint256 h1;
    uint256 h2;
    if (proTxHash1 < proTxHash2) {
        h1 = ::SerializeHash(std::make_tuple(proTxHash1, proTxHash2, proTxHash1));
        h2 = ::SerializeHash(std::make_tuple(proTxHash1, proTxHash2, proTxHash2));
    } else {
        h1 = ::SerializeHash(std::make_tuple(proTxHash2, proTxHash1, proTxHash1));
        h2 = ::SerializeHash(std::make_tuple(proTxHash2, proTxHash1, proTxHash2));
    }
    if (h1 < h2) {
        return proTxHash1;
    }
    return proTxHash2;
}

bool IsAllMembersConnectedEnabled(Consensus::LLMQType llmqType)
{
    return true; // EvalSpork(llmqType, sporkManager.GetSporkValue(SPORK_21_QUORUM_ALL_CONNECTED));
}

bool IsQuorumPoseEnabled(Consensus::LLMQType llmqType)
{
    return true; //EvalSpork(llmqType, sporkManager.GetSporkValue(SPORK_23_QUORUM_POSE));
}

std::set<uint256> GetQuorumRelayMembers(Consensus::LLMQType llmqType, const CBlockIndex *pindexQuorum, const uint256 &forMember, bool onlyOutbound)
{
    auto mns = deterministicMNManager->GetAllQuorumMembers(llmqType, pindexQuorum);
    std::set<uint256> result;

    auto calcOutbound = [&](size_t i, const uint256 proTxHash) {
        // Relay to nodes at indexes (i+2^k)%n, where
        //   k: 0..max(1, floor(log2(n-1))-1)
        //   n: size of the quorum/ring
        std::set<uint256> r;
        int gap = 1;
        int gap_max = (int)mns.size() - 1;
        int k = 0;
        while ((gap_max >>= 1) || k <= 1) {
            size_t idx = (i + gap) % mns.size();
            auto& otherDmn = mns[idx];
            if (otherDmn->proTxHash == proTxHash) {
                continue;
            }
            r.emplace(otherDmn->proTxHash);
            gap <<= 1;
            k++;
        }
        return r;
    };

    for (size_t i = 0; i < mns.size(); i++) {
        auto& dmn = mns[i];
        if (dmn->proTxHash == forMember) {
            auto r = calcOutbound(i, dmn->proTxHash);
            result.insert(r.begin(), r.end());
        } else if (!onlyOutbound) {
            auto r = calcOutbound(i, dmn->proTxHash);
            if (r.count(forMember)) {
                result.emplace(dmn->proTxHash);
            }
        }
    }

    return result;
}

std::set<uint256> GetQuorumConnections(Consensus::LLMQType llmqType, const CBlockIndex* pindexQuorum, const uint256& forMember, bool onlyOutbound)
{
    if (IsAllMembersConnectedEnabled(llmqType)) {
        auto mns = deterministicMNManager->GetAllQuorumMembers(llmqType, pindexQuorum);
        std::set<uint256> result;

        for (auto& dmn : mns) {
            if (dmn->proTxHash == forMember) {
                continue;
            }
            // Determine which of the two MNs (forMember vs dmn) should initiate the outbound connection and which
            // one should wait for the inbound connection. We do this in a deterministic way, so that even when we
            // end up with both connecting to each other, we know which one to disconnect
            uint256 deterministicOutbound = DeterministicOutboundConnection(forMember, dmn->proTxHash);
            if (!onlyOutbound || deterministicOutbound == dmn->proTxHash) {
                result.emplace(dmn->proTxHash);
            }
        }
        return result;
    } else {
        return GetQuorumRelayMembers(llmqType, pindexQuorum, forMember, onlyOutbound);
    }
}

std::set<size_t> CalcDeterministicWatchConnections(Consensus::LLMQType llmqType, const CBlockIndex* pindexQuorum, size_t memberCount, size_t connectionCount)
{
    static uint256 qwatchConnectionSeed;
    static std::atomic<bool> qwatchConnectionSeedGenerated{false};
    static RecursiveMutex qwatchConnectionSeedCs;
    if (!qwatchConnectionSeedGenerated) {
        LOCK(qwatchConnectionSeedCs);
        if (!qwatchConnectionSeedGenerated) {
            qwatchConnectionSeed = GetRandHash();
            qwatchConnectionSeedGenerated = true;
        }
    }

    std::set<size_t> result;
    uint256 rnd = qwatchConnectionSeed;
    for (size_t i = 0; i < connectionCount; i++) {
        rnd = ::SerializeHash(std::make_pair(rnd, std::make_pair(static_cast<uint8_t>(llmqType), pindexQuorum->GetBlockHash())));
        result.emplace(rnd.GetUint64(0) % memberCount);
    }
    return result;
}

bool EnsureQuorumConnections(Consensus::LLMQType llmqType, const CBlockIndex* pindexQuorum, const uint256& myProTxHash)
{
    auto members = deterministicMNManager->GetAllQuorumMembers(llmqType, pindexQuorum);
    bool isMember = std::find_if(members.begin(), members.end(), [&](const CDeterministicMNCPtr& dmn) { return dmn->proTxHash == myProTxHash; }) != members.end();

    if (!isMember) { // && !CLLMQUtils::IsWatchQuorumsEnabled()) {
        return false;
    }

    std::set<uint256> connections;
    std::set<uint256> relayMembers;
    if (isMember) {
        connections = GetQuorumConnections(llmqType, pindexQuorum, myProTxHash, true);
        relayMembers = GetQuorumRelayMembers(llmqType, pindexQuorum, myProTxHash, true);
    } else {
        auto cindexes = CalcDeterministicWatchConnections(llmqType, pindexQuorum, members.size(), 1);
        for (auto idx : cindexes) {
            connections.emplace(members[idx]->proTxHash);
        }
        relayMembers = connections;
    }
    if (!connections.empty()) {
        auto connman = g_connman->GetTierTwoConnMan();
        if (!connman->hasQuorumNodes(llmqType, pindexQuorum->GetBlockHash()) && LogAcceptCategory(BCLog::LLMQ)) {
            auto mnList = deterministicMNManager->GetListAtChainTip();
            std::string debugMsg = strprintf("CLLMQUtils::%s -- adding masternodes quorum connections for quorum %s:\n", __func__, pindexQuorum->GetBlockHash().ToString());
            for (auto& c : connections) {
                auto dmn = mnList.GetValidMN(c);
                if (!dmn) {
                    debugMsg += strprintf("  %s (not in valid MN set anymore)\n", c.ToString());
                } else {
                    debugMsg += strprintf("  %s (%s)\n", c.ToString(), dmn->pdmnState->addr.ToString());
                }
            }
            LogPrint(BCLog::LLMQ, debugMsg.c_str()); /* Continued */
        }
        connman->setQuorumNodes(llmqType, pindexQuorum->GetBlockHash(), connections);
    }
    if (!relayMembers.empty()) {
        auto connman = g_connman->GetTierTwoConnMan();
        connman->setMasternodeQuorumRelayMembers(llmqType, pindexQuorum->GetBlockHash(), relayMembers);
    }
    return true;
}

void AddQuorumProbeConnections(Consensus::LLMQType llmqType, const CBlockIndex *pindexQuorum, const uint256 &myProTxHash)
{
    if (!IsQuorumPoseEnabled(llmqType)) {
        return;
    }

    auto members = deterministicMNManager->GetAllQuorumMembers(llmqType, pindexQuorum);
    auto curTime = GetAdjustedTime();

    std::set<uint256> probeConnections;
    for (auto& dmn : members) {
        if (dmn->proTxHash == myProTxHash) {
            continue;
        }
        auto lastOutbound = g_mmetaman.GetMetaInfo(dmn->proTxHash)->GetLastOutboundSuccess();
        // re-probe after 50 minutes so that the "good connection" check in the DKG doesn't fail just because we're on
        // the brink of timeout
        if (curTime - lastOutbound > 50 * 60) {
            probeConnections.emplace(dmn->proTxHash);
        }
    }

    if (!probeConnections.empty()) {
        if (LogAcceptCategory(BCLog::LLMQ)) {
            auto mnList = deterministicMNManager->GetListAtChainTip();
            std::string debugMsg = strprintf("CLLMQUtils::%s -- adding masternodes probes for quorum %s:\n", __func__, pindexQuorum->GetBlockHash().ToString());
            for (auto& c : probeConnections) {
                auto dmn = mnList.GetValidMN(c);
                if (!dmn) {
                    debugMsg += strprintf("  %s (not in valid MN set anymore)\n", c.ToString());
                } else {
                    debugMsg += strprintf("  %s (%s)\n", c.ToString(), dmn->pdmnState->addr.ToString());
                }
            }
            LogPrint(BCLog::LLMQ, debugMsg.c_str()); /* Continued */
        }
        g_connman->GetTierTwoConnMan()->addPendingProbeConnections(probeConnections);
    }
}

} // namespace llmq::utils

} // namespace llmq
