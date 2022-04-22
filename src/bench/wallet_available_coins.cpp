// Copyright (c) 2012-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <chainparams.h>
#include <consensus/merkle.h>
#include <node/context.h>
#include <test/util/setup_common.h>
#include <test/util/wallet.h>
#include <validation.h>
#include <wallet/spend.h>
#include <wallet/wallet.h>

using wallet::CWallet;
using wallet::CreateMockWalletDatabase;
using wallet::DBErrors;
using wallet::WALLET_FLAG_DESCRIPTORS;

struct TipBlock
{
    uint256 prev_block_hash;
    int64_t prev_block_time;
    int tip_height;
};

TipBlock getTip(const CChainParams& params, const node::NodeContext& context)
{
    auto tip = context.chainman->ActiveTip();
    return (tip) ? TipBlock{tip->GetBlockHash(), tip->GetBlockTime(), tip->nHeight} :
           TipBlock{params.GenesisBlock().GetHash(), params.GenesisBlock().GetBlockTime(), 0};
}

void generateFakeBlock(const CChainParams& params,
                       const node::NodeContext& context,
                       CWallet& wallet,
                       const CScript& coinbase_out_script,
                       std::vector<CTransactionRef> txs = {})
{
    TipBlock tip{getTip(params, context)};

    // Create block
    CBlock block;
    CMutableTransaction coinbase_tx;
    coinbase_tx.vin.resize(1);
    coinbase_tx.vin[0].prevout.SetNull();
    coinbase_tx.vout.resize(1);
    coinbase_tx.vout[0].scriptPubKey = coinbase_out_script;
    coinbase_tx.vout[0].nValue = 50 * COIN;
    coinbase_tx.vin[0].scriptSig = CScript() << ++tip.tip_height << OP_0;
    block.vtx = {MakeTransactionRef(std::move(coinbase_tx))};
    block.vtx.insert(block.vtx.end(), txs.begin(), txs.end());

    block.nVersion = VERSIONBITS_LAST_OLD_BLOCK_VERSION;
    block.hashPrevBlock = tip.prev_block_hash;
    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.nTime = ++tip.prev_block_time;
    block.nBits = params.GenesisBlock().nBits;
    block.nNonce = 0;

    {
        LOCK(::cs_main);
        // Add it to the index
        CBlockIndex *pindex{context.chainman->m_blockman.AddToBlockIndex(block, context.chainman->m_best_header)};
        // add it to the chain
        context.chainman->ActiveChain().SetTip(pindex);
    }

    // notify wallet
    wallet.blockConnected(block, tip.tip_height);
}

static void WalletAvailableCoins(benchmark::Bench& bench, const std::vector<OutputType>& output_type)
{
    const auto test_setup = MakeNoLogFileContext<const TestingSetup>();

    CWallet wallet{test_setup->m_node.chain.get(), "", gArgs, CreateMockWalletDatabase()};
    {
        LOCK(wallet.cs_wallet);
        if (std::any_of(output_type.begin(), output_type.end(), [](OutputType type){return type > OutputType::LEGACY;})) {
            wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
            wallet.SetupDescriptorScriptPubKeyMans();
        }
        if (std::any_of(output_type.begin(), output_type.end(), [](OutputType type){return type == OutputType::LEGACY;})) {
            wallet.SetupLegacyScriptPubKeyMan();
        }
        if (wallet.LoadWallet() != DBErrors::LOAD_OK) assert(false);
    }

    // Generate destinations
    std::vector<CScript> dest_wallet;
    for (auto type : output_type) {
        dest_wallet.emplace_back(GetScriptForDestination(getnewaddress(wallet, type)));
    }

    // Generate first block
    const auto& params = Params();
    unsigned int chain_size = 1000;
    for (unsigned int i = 0; i < chain_size / dest_wallet.size(); ++i) {
        for (auto dest : dest_wallet) {
            generateFakeBlock(params, test_setup->m_node, wallet, dest);
        }
    }

    auto bal = wallet::GetAvailableBalance(wallet); // Cache
    assert(bal == 50 * COIN * (chain_size - COINBASE_MATURITY));

    bench.epochIterations(5).run([&] {
        LOCK(wallet.cs_wallet);
        auto res_coins = wallet::AvailableCoins(wallet);

        assert(res_coins.coins.size() == (chain_size - COINBASE_MATURITY));
    });
}

static void WalletAvailableCoinsOnlyBech32M(benchmark::Bench& bench) { WalletAvailableCoins(bench, {OutputType::BECH32M}); }
static void WalletAvailableCoinsOnlyBech32(benchmark::Bench& bench) { WalletAvailableCoins(bench, {OutputType::BECH32}); }
static void WalletAvailableCoinsOnlyP2SH_SEGWIT(benchmark::Bench& bench) { WalletAvailableCoins(bench, {OutputType::P2SH_SEGWIT}); }
static void WalletAvailableCoinsOnlyLegacy(benchmark::Bench& bench) { WalletAvailableCoins(bench, {OutputType::LEGACY}); }
static void WalletAvailableCoinsMulti(benchmark::Bench& bench) { WalletAvailableCoins(bench, {OutputType::LEGACY, OutputType::BECH32M, OutputType::BECH32, OutputType::P2SH_SEGWIT}); }

BENCHMARK(WalletAvailableCoinsOnlyBech32M);
BENCHMARK(WalletAvailableCoinsOnlyBech32);
BENCHMARK(WalletAvailableCoinsOnlyP2SH_SEGWIT);
BENCHMARK(WalletAvailableCoinsOnlyLegacy);
BENCHMARK(WalletAvailableCoinsMulti);
