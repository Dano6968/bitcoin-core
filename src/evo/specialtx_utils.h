// Copyright (c) 2022 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or htts://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_SPECIALTX_UTILS_H
#define PIVX_SPECIALTX_UTILS_H

#include "bls/bls_wrapper.h"
#include "messagesigner.h"
#include "operationresult.h"
#include "primitives/transaction.h"

template<typename SpecialTxPayload>
OperationResult SignSpecialTxPayloadByHash(const CMutableTransaction& tx, SpecialTxPayload& payload, const CKey& key)
{
    payload.vchSig.clear();
    uint256 hash = ::SerializeHash(payload);
    return CHashSigner::SignHash(hash, key, payload.vchSig) ? OperationResult{true} :
            OperationResult{false, "failed to sign special tx payload"};
}

template<typename SpecialTxPayload>
OperationResult SignSpecialTxPayloadByHash(const CMutableTransaction& tx, SpecialTxPayload& payload, const CBLSSecretKey& key)
{
    payload.sig = key.Sign(::SerializeHash(payload));
    return payload.sig.IsValid() ? OperationResult{true} : OperationResult{false, "failed to sign special tx payload"};
}

template<typename SpecialTxPayload>
OperationResult SignSpecialTxPayloadByString(SpecialTxPayload& payload, const CKey& key)
{
    payload.vchSig.clear();
    std::string m = payload.MakeSignString();
    return CMessageSigner::SignMessage(m, payload.vchSig, key) ? OperationResult{true} :
            OperationResult{false, "failed to sign special tx payload"};
}

template<typename SpecialTxPayload>
static void UpdateSpecialTxInputsHash(const CMutableTransaction& tx, SpecialTxPayload& payload)
{
    payload.inputsHash = CalcTxInputsHash(tx);
}

#ifdef ENABLE_WALLET

class CWallet;
struct CMutableTransaction;

OperationResult FundSpecialTx(CWallet* pwallet, CMutableTransaction& tx);

template<typename SpecialTxPayload>
OperationResult FundSpecialTx(CWallet* pwallet, CMutableTransaction& tx, SpecialTxPayload& payload)
{
    SetTxPayload(tx, payload);
    auto res = FundSpecialTx(pwallet, tx);
    if (res) UpdateSpecialTxInputsHash(tx, payload);
    return res;
}

OperationResult SignAndSendSpecialTx(CWallet* pwallet, CMutableTransaction& tx);

template<typename SpecialTxPayload>
OperationResult SignAndSendSpecialTx(CWallet* const pwallet, CMutableTransaction& tx, const SpecialTxPayload& pl)
{
    SetTxPayload(tx, pl);
    return SignAndSendSpecialTx(pwallet, tx);
}

#endif // ENABLE_WALLET

#endif //PIVX_SPECIALTX_UTILS_H
