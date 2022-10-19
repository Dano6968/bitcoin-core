// Copyright (c) 2011-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <chainparamsbase.h>
#include <test/util/setup_common.h>

#include <consensus/merkle.h>
#include <key_io.h>
#include <test/util/mining.h>
#include <test/util/wallet.h>

#include <rpc/client.h>
#include <rpc/server.h>
#include <validation.h>

struct TipBlock
{
    uint256 prev_block_hash;
    int64_t prev_block_time;
    int tip_height;
};

TipBlock getTip(const CChainParams& params, const node::NodeContext& context)
{
    auto tip = WITH_LOCK(::cs_main, return context.chainman->ActiveTip());
    return (tip) ? TipBlock{tip->GetBlockHash(), tip->GetBlockTime(), tip->nHeight} :
           TipBlock{params.GenesisBlock().GetHash(), params.GenesisBlock().GetBlockTime(), 0};
}

uint256 generateFakeBlock(const CChainParams& params,
                       const node::NodeContext& context,
                       const CScript& coinbase_out_script)
{
    TipBlock tip{getTip(params, context)};

    // Create block
    CBlock block;
    CMutableTransaction coinbase_tx;
    coinbase_tx.vin.resize(1);
    coinbase_tx.vin[0].prevout.SetNull();
    coinbase_tx.vout.resize(2);
    coinbase_tx.vout[0].scriptPubKey = coinbase_out_script;
    coinbase_tx.vout[0].nValue = 49 * COIN;
    coinbase_tx.vin[0].scriptSig = CScript() << ++tip.tip_height << OP_0;
    coinbase_tx.vout[1].scriptPubKey = coinbase_out_script; // extra output
    coinbase_tx.vout[1].nValue = 1 * COIN;
    block.vtx = {MakeTransactionRef(std::move(coinbase_tx))};

    block.nVersion = VERSIONBITS_LAST_OLD_BLOCK_VERSION;
    block.hashPrevBlock = tip.prev_block_hash;
    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.nTime = ++tip.prev_block_time;
    block.nBits = params.GenesisBlock().nBits;
    block.nNonce = 0;

    LOCK(::cs_main);
    // Add it to the index
    CBlockIndex* pindex{context.chainman->m_blockman.AddToBlockIndex(block, context.chainman->m_best_header)};
    // add it to the chain
    context.chainman->ActiveChain().SetTip(*pindex);
    return pindex->GetBlockHash();
}

UniValue CallRPC(const std::string& args, const node::NodeContext& m_node)
{
    std::vector<std::string> vArgs{SplitString(args, ' ')};
    std::string strMethod = vArgs[0];
    vArgs.erase(vArgs.begin());
    JSONRPCRequest request;
    request.context = &m_node;
    request.strMethod = strMethod;
    request.params = RPCConvertValues(strMethod, vArgs);
    if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
    try {
        UniValue result = tableRPC.execute(request);
        return result;
    } catch (const UniValue& objError) {
        throw std::runtime_error(find_value(objError, "message").get_str());
    }
}

static void RpcScanBlocks(benchmark::Bench& bench)
{
    const auto testing_setup = MakeNoLogFileContext<const TestingSetup>();
    // Generate 10k blocks
    const std::string& addr = "mkS4HXoTYWRTescLGaUTGbtTTYX5EjJyEE";
    std::vector<std::string> blocks;

    CScript dest = GetScriptForDestination(DecodeDestination(addr));
    CScript dummy = CScript() << OP_TRUE;

    // Generate chain; each coinbase will have two outputs to fill-up the wallet
    const auto& params = Params();
    unsigned int chain_size = 5000;
    for (unsigned int i = 0; i < chain_size; ++i) {
        if (i % 2500) {
            blocks.emplace_back(generateFakeBlock(params, testing_setup->m_node, dest).GetHex());
        } else {
            generateFakeBlock(params, testing_setup->m_node, dummy);
        }
    }

    // TODO: tengo que hacer TODOS los block filters.. en la función de generateFakeBlock y guardarlos en db...

    bench.run([&] {
        // add things to bench
        UniValue ret = CallRPC("scanblocks start \"addr("+addr+")\" " + std::to_string(chain_size), testing_setup->m_node);
        UniValue arr = ret["relevant_blocks"].get_array();
        assert(arr.size() == 2);
        for (size_t i=0; i<arr.size(); i++) {
            assert(blocks[i] == arr[i].get_str());
        }
    });
}

BENCHMARK(RpcScanBlocks);
