// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BRESULT_H
#define BITCOIN_BRESULT_H

#include <optional>
#include <util/translation.h>
#include <variant>

/*
 * 'BResult' is a generic class useful for wrapping a return object
 * (in case of success) or propagating the error cause.
*/
template<class T>
class BResult {
private:
    std::variant<bilingual_str, T> m_variant;

public:
    BResult() : m_variant(Untranslated("")) {}
    BResult(const T& _obj) : m_variant(_obj) {}
    BResult(const bilingual_str& error) : m_variant(error) {}

    /* In case of success, the result object */
    const T* GetObj() const { return std::get_if<T>(&m_variant); }
    /* Whether the function succeeded or not */
    bool GetRes() const { return !std::holds_alternative<bilingual_str>(m_variant); }
    /* In case of failure, the error cause */
    bilingual_str GetError() const {
        auto error = std::get_if<bilingual_str>(&m_variant);
        return (error ? *error : bilingual_str());
    }

    explicit operator bool() const { return GetRes(); }
};

#endif // BITCOIN_BRESULT_H
