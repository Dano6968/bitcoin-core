#include "test/test_pivx.h"

#include "uint256.h"
#include "utilstrencodings.h"

#include <sodium.h>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(libsodium_consensus_tests, TestingSetup)

void TestLibsodiumEd25519SignatureVerification(
        const std::string &scope,
        const std::string &msg,
        std::vector<unsigned char> pubkey,
        std::vector<unsigned char> sig)
{
    BOOST_CHECK_EQUAL(
            crypto_sign_verify_detached(
                    sig.data(),
                    (const unsigned char*)msg.data(), msg.size(),
                    pubkey.data()),
            0);
}

BOOST_AUTO_TEST_CASE(LibsodiumPubkeyValidation)
{
    // libsodium <= 1.0.15 accepts valid signatures for a non-zero pubkey with
    // small order; this is currently part of our consensus rules.
    // libsodium >= 1.0.16 rejects all pubkeys with small order.
    //
    // These test vectors were generated by finding pairs of points (A, P) both
    // in the eight-torsion subgroup such that R = B + P and R = [1] B - [k] A
    // (where SHA512(bytes(R) || bytes(A) || message) represents k in
    // little-endian order, as in Ed25519).
    TestLibsodiumEd25519SignatureVerification(
    "Test vector 1",
    "zcash ed25519 libsodium compatibility",
    ParseHex("0100000000000000000000000000000000000000000000000000000000000000"),
    ParseHex("58666666666666666666666666666666666666666666666666666666666666660100000000000000000000000000000000000000000000000000000000000000"));
    TestLibsodiumEd25519SignatureVerification(
    "Test vector 2",
    "zcash ed25519 libsodium compatibility",
    ParseHex("0000000000000000000000000000000000000000000000000000000000000080"),
    ParseHex("58666666666666666666666666666666666666666666666666666666666666660100000000000000000000000000000000000000000000000000000000000000"));
    TestLibsodiumEd25519SignatureVerification(
    "Test vector 3",
    "zcash ed25519 libsodium compatibility",
    ParseHex("26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc85"),
    ParseHex("da99e28ba529cdde35a25fba9059e78ecaee239f99755b9b1aa4f65df00803e20100000000000000000000000000000000000000000000000000000000000000"));
    TestLibsodiumEd25519SignatureVerification(
    "Test vector 4",
    "zcash ed25519 libsodium compatibility",
    ParseHex("c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a"),
    ParseHex("95999999999999999999999999999999999999999999999999999999999999990100000000000000000000000000000000000000000000000000000000000000"));
    TestLibsodiumEd25519SignatureVerification(
    "Test vector 5",
    "zcash ed25519 libsodium compatibility",
    ParseHex("26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc85"),
    ParseHex("13661d745ad63221ca5da0456fa618713511dc60668aa464e55b09a20ff7fc1d0100000000000000000000000000000000000000000000000000000000000000"));

    // libsodium <= 1.0.15 contains a blocklist of small-order points that R is
    // checked against. However, it does not contain all canonical small-order
    // points; in particular, it is missing the negative of one of the points.
    //
    // This test case is the only pair of points (A, R) both in the eight-torsion
    // subgroup, that satisfies R = [0] B - [k] A and also evades the blocklist.
    TestLibsodiumEd25519SignatureVerification(
    "Small order R that is not rejected by libsodium <= 1.0.15",
    "zcash ed25519 libsodium compatibility",
    ParseHex("c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a"),
    ParseHex("26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc850000000000000000000000000000000000000000000000000000000000000000"));
}

BOOST_AUTO_TEST_SUITE_END()