// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_OPERATIONRESULT_H
#define BITCOIN_OPERATIONRESULT_H

#include <optional>
#include <util/translation.h>

/*
 * 'BResult' is a generic class useful for wrapping a return object
 * (in case of success) or propagating the error cause.
*/
template<class T>
class BResult {
private:
    std::optional<T> m_obj_res{std::nullopt};
    std::optional<bilingual_str> m_error{std::nullopt};
public:
    BResult() : m_error(Untranslated("")) {}
    BResult(const T& _obj) : m_obj_res(_obj) {}
    BResult(const bilingual_str& error) : m_error(error) {}

    /* In case of failure, the error cause */
    bilingual_str GetError() const { return (m_error ? *m_error : bilingual_str()); }
    /* In case of success, the result object */
    const std::optional<T>& GetObj() const { return m_obj_res; }
    /* Whether the function succeeded or not */
    bool GetRes() const { return m_error == std::nullopt; }

    explicit operator bool() const { return GetRes(); }
};

#endif // BITCOIN_OPERATIONRESULT_H
