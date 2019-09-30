// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2019 The Ohmcoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "libzerocoin/Params.h"
#include "chainparams.h"
#include "libzerocoin/bignum.h"
#include "random.h"
//#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>
#include "crypto/scrypt.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <openssl/sha.h>
#include <boost/assign/list_of.hpp>
#include <limits>

static const int SCRYPT_SCRATCHPAD_SIZE = 131072 + 63;

void
PBKDF2_SHA256(const uint8_t *passwd, size_t passwdlen, const uint8_t *salt,
              size_t saltlen, uint64_t c, uint8_t *buf, size_t dkLen);

static inline uint32_t le32dec(const void *pp)
{
    const uint8_t *p = (uint8_t const *)pp;
    return ((uint32_t)(p[0]) + ((uint32_t)(p[1]) << 8) +
            ((uint32_t)(p[2]) << 16) + ((uint32_t)(p[3]) << 24));
}

static inline void le32enc(void *pp, uint32_t x)
{
    uint8_t *p = (uint8_t *)pp;
    p[0] = x & 0xff;
    p[1] = (x >> 8) & 0xff;
    p[2] = (x >> 16) & 0xff;
    p[3] = (x >> 24) & 0xff;
}


static inline uint32_t be32dec(const void *pp)
{
    const uint8_t *p = (uint8_t const *)pp;
    return ((uint32_t)(p[3]) + ((uint32_t)(p[2]) << 8) +
            ((uint32_t)(p[1]) << 16) + ((uint32_t)(p[0]) << 24));
}

static inline void be32enc(void *pp, uint32_t x)
{
    uint8_t *p = (uint8_t *)pp;
    p[3] = x & 0xff;
    p[2] = (x >> 8) & 0xff;
    p[1] = (x >> 16) & 0xff;
    p[0] = (x >> 24) & 0xff;
}

typedef struct HMAC_SHA256Context {
    SHA256_CTX ictx;
    SHA256_CTX octx;
} HMAC_SHA256_CTX;

/* Initialize an HMAC-SHA256 operation with the given key. */
static void
HMAC_SHA256_Init(HMAC_SHA256_CTX *ctx, const void *_K, size_t Klen)
{
    unsigned char pad[64];
    unsigned char khash[32];
    const unsigned char *K = (const unsigned char *)_K;
    size_t i;

    /* If Klen > 64, the key is really SHA256(K). */
    if (Klen > 64) {
        SHA256_Init(&ctx->ictx);
        SHA256_Update(&ctx->ictx, K, Klen);
        SHA256_Final(khash, &ctx->ictx);
        K = khash;
        Klen = 32;
    }

    /* Inner SHA256 operation is SHA256(K xor [block of 0x36] || data). */
    SHA256_Init(&ctx->ictx);
    memset(pad, 0x36, 64);
    for (i = 0; i < Klen; i++)
        pad[i] ^= K[i];
    SHA256_Update(&ctx->ictx, pad, 64);

    /* Outer SHA256 operation is SHA256(K xor [block of 0x5c] || hash). */
    SHA256_Init(&ctx->octx);
    memset(pad, 0x5c, 64);
    for (i = 0; i < Klen; i++)
        pad[i] ^= K[i];
    SHA256_Update(&ctx->octx, pad, 64);

    /* Clean the stack. */
    memset(khash, 0, 32);
}

/* Add bytes to the HMAC-SHA256 operation. */
static void
HMAC_SHA256_Update(HMAC_SHA256_CTX *ctx, const void *in, size_t len)
{
    /* Feed data to the inner SHA256 operation. */
    SHA256_Update(&ctx->ictx, in, len);
}

/* Finish an HMAC-SHA256 operation. */
static void
HMAC_SHA256_Final(unsigned char digest[32], HMAC_SHA256_CTX *ctx)
{
    unsigned char ihash[32];

    /* Finish the inner SHA256 operation. */
    SHA256_Final(ihash, &ctx->ictx);

    /* Feed the inner hash to the outer SHA256 operation. */
    SHA256_Update(&ctx->octx, ihash, 32);

    /* Finish the outer SHA256 operation. */
    SHA256_Final(digest, &ctx->octx);

    /* Clean the stack. */
    memset(ihash, 0, 32);
}

/**
 * PBKDF2_SHA256(passwd, passwdlen, salt, saltlen, c, buf, dkLen):
 * Compute PBKDF2(passwd, salt, c, dkLen) using HMAC-SHA256 as the PRF, and
 * write the output to buf.  The value dkLen must be at most 32 * (2^32 - 1).
 */
///////////////////////

#define ROTL(a, b) (((a) << (b)) | ((a) >> (32 - (b))))

static inline void xor_salsa8(uint32_t B[16], const uint32_t Bx[16])
{
    uint32_t x00,x01,x02,x03,x04,x05,x06,x07,x08,x09,x10,x11,x12,x13,x14,x15;
    int i;

    x00 = (B[ 0] ^= Bx[ 0]);
    x01 = (B[ 1] ^= Bx[ 1]);
    x02 = (B[ 2] ^= Bx[ 2]);
    x03 = (B[ 3] ^= Bx[ 3]);
    x04 = (B[ 4] ^= Bx[ 4]);
    x05 = (B[ 5] ^= Bx[ 5]);
    x06 = (B[ 6] ^= Bx[ 6]);
    x07 = (B[ 7] ^= Bx[ 7]);
    x08 = (B[ 8] ^= Bx[ 8]);
    x09 = (B[ 9] ^= Bx[ 9]);
    x10 = (B[10] ^= Bx[10]);
    x11 = (B[11] ^= Bx[11]);
    x12 = (B[12] ^= Bx[12]);
    x13 = (B[13] ^= Bx[13]);
    x14 = (B[14] ^= Bx[14]);
    x15 = (B[15] ^= Bx[15]);
    for (i = 0; i < 8; i += 2) {
        /* Operate on columns. */
        x04 ^= ROTL(x00 + x12,  7);  x09 ^= ROTL(x05 + x01,  7);
        x14 ^= ROTL(x10 + x06,  7);  x03 ^= ROTL(x15 + x11,  7);

        x08 ^= ROTL(x04 + x00,  9);  x13 ^= ROTL(x09 + x05,  9);
        x02 ^= ROTL(x14 + x10,  9);  x07 ^= ROTL(x03 + x15,  9);

        x12 ^= ROTL(x08 + x04, 13);  x01 ^= ROTL(x13 + x09, 13);
        x06 ^= ROTL(x02 + x14, 13);  x11 ^= ROTL(x07 + x03, 13);

        x00 ^= ROTL(x12 + x08, 18);  x05 ^= ROTL(x01 + x13, 18);
        x10 ^= ROTL(x06 + x02, 18);  x15 ^= ROTL(x11 + x07, 18);

        /* Operate on rows. */
        x01 ^= ROTL(x00 + x03,  7);  x06 ^= ROTL(x05 + x04,  7);
        x11 ^= ROTL(x10 + x09,  7);  x12 ^= ROTL(x15 + x14,  7);

        x02 ^= ROTL(x01 + x00,  9);  x07 ^= ROTL(x06 + x05,  9);
        x08 ^= ROTL(x11 + x10,  9);  x13 ^= ROTL(x12 + x15,  9);

        x03 ^= ROTL(x02 + x01, 13);  x04 ^= ROTL(x07 + x06, 13);
        x09 ^= ROTL(x08 + x11, 13);  x14 ^= ROTL(x13 + x12, 13);

        x00 ^= ROTL(x03 + x02, 18);  x05 ^= ROTL(x04 + x07, 18);
        x10 ^= ROTL(x09 + x08, 18);  x15 ^= ROTL(x14 + x13, 18);
    }
    B[ 0] += x00;
    B[ 1] += x01;
    B[ 2] += x02;
    B[ 3] += x03;
    B[ 4] += x04;
    B[ 5] += x05;
    B[ 6] += x06;
    B[ 7] += x07;
    B[ 8] += x08;
    B[ 9] += x09;
    B[10] += x10;
    B[11] += x11;
    B[12] += x12;
    B[13] += x13;
    B[14] += x14;
    B[15] += x15;
}

void scrypt_1024_1_1_256_sp_generic(const char *input, char *output, char *scratchpad)
{
    uint8_t B[128];
    uint32_t X[32];
    uint32_t *V;
    uint32_t i, j, k;

    V = (uint32_t *)(((uintptr_t)(scratchpad) + 63) & ~ (uintptr_t)(63));

    PBKDF2_SHA256((const uint8_t *)input, 80, (const uint8_t *)input, 80, 1, B, 128);

    for (k = 0; k < 32; k++)
        X[k] = le32dec(&B[4 * k]);

    for (i = 0; i < 1024; i++) {
        memcpy(&V[i * 32], X, 128);
        xor_salsa8(&X[0], &X[16]);
        xor_salsa8(&X[16], &X[0]);
    }
    for (i = 0; i < 1024; i++) {
        j = 32 * (X[16] & 1023);
        for (k = 0; k < 32; k++)
            X[k] ^= V[j + k];
        xor_salsa8(&X[0], &X[16]);
        xor_salsa8(&X[16], &X[0]);
    }

    for (k = 0; k < 32; k++)
        le32enc(&B[4 * k], X[k]);

    PBKDF2_SHA256((const uint8_t *)input, 80, B, 128, 1, (uint8_t *)output, 32);
}

#if defined(USE_SSE2)
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_AMD64) || (defined(MAC_OSX) && defined(__i386__))
/* Always SSE2 */
void scrypt_detect_sse2(unsigned int cpuid_edx)
{
    printf("scrypt: using scrypt-sse2 as built.\n");
}
#else
/* Detect SSE2 */
void (*scrypt_1024_1_1_256_sp)(const char *input, char *output, char *scratchpad);

void scrypt_detect_sse2(unsigned int cpuid_edx)
{
    if (cpuid_edx & 1<<26)
    {
        scrypt_1024_1_1_256_sp = &scrypt_1024_1_1_256_sp_sse2;
        printf("scrypt: using scrypt-sse2 as detected.\n");
    }
    else
    {
        scrypt_1024_1_1_256_sp = &scrypt_1024_1_1_256_sp_generic;
        printf("scrypt: using scrypt-generic, SSE2 unavailable.\n");
    }
}
#endif
#endif

void scrypt_1024_1_1_256(const char *input, char *output)
{
    char scratchpad[SCRYPT_SCRATCHPAD_SIZE];
#if defined(USE_SSE2)
    // Detection would work, but in cases where we KNOW it always has SSE2,
        // it is faster to use directly than to use a function pointer or conditional.
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_AMD64) || (defined(MAC_OSX) && defined(__i386__))
        // Always SSE2: x86_64 or Intel MacOS X
        scrypt_1024_1_1_256_sp_sse2(input, output, scratchpad);
#else
        // Detect SSE2: 32bit x86 Linux or Windows
        scrypt_1024_1_1_256_sp(input, output, scratchpad);
#endif
#else
    // Generic scrypt
    scrypt_1024_1_1_256_sp_generic(input, output, scratchpad);
#endif
}


using namespace std;
using namespace boost::assign;

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

#include "chainparamsseeds.h"

/**
 * Main network
 */

//! Convert the pnSeeds6 array into usable address objects.
static void convertSeed6(std::vector<CAddress>& vSeedsOut, const SeedSpec6* data, unsigned int count)
{
    // It'll only connect to one or two seed nodes because once it connects,
    // it'll get a pile of addresses with newer timestamps.
    // Seed nodes are given a random 'last seen time' of between one and two
    // weeks ago.
    const int64_t nOneWeek = 7 * 24 * 60 * 60;
    for (unsigned int i = 0; i < count; i++) {
        struct in6_addr ip;
        memcpy(&ip, data[i].addr, sizeof(ip));
        CAddress addr(CService(ip, data[i].port));
        addr.nTime = GetTime() - GetRand(nOneWeek) - nOneWeek;
        vSeedsOut.push_back(addr);
    }
}

//   What makes a good checkpoint block?
// + Is surrounded by blocks with reasonable timestamps
//   (no blocks before with a timestamp after, none after with
//    timestamp before)
// + Contains no strange transactions
//
// to get checkpoint use:
// getblockhash "block height"
// getblock "block hash"
static Checkpoints::MapCheckpoints mapCheckpoints =
    boost::assign::map_list_of
            (0, uint256("0xbf7fdb166c58ef349097c3964b433a9821983483307cf5fc71335fd7b380fe36"))
            (462146, uint256("0xb0ffff92a2933dd8bafba722d7c6ea6413840a863ba0ef22794561e4f944ff6f"))
            (618641, uint256("0x247f41f60b4fd8de2727d6745c544f6b399d6712c1cdc7f00a8a909b830bf015"))
            (831256, uint256("0x3913ccd8359e7d7f81df2fc2b5f68848b6aa3663f84c87899691eb0e43454eec"))
            (2848716, uint256("0x19dfab7fbb6dd6ff84fed0bda251632376d70054376da4469f5498dc3e575fb6"))
            (2967145, uint256("0x389fc6e4ee47eee73ffdc77727d3235be1095c23c071933fb7e343f7b6e544e2"))
            (2977932, uint256("0x9d17db9fd8034fd02cf24f85cd1e68b2a09553b422ac14f8ef83c5ec171af4d3"))
            (2991900, uint256("0xa13a332af480005d03bb4e72c32a84637d2b47e6de9689c52db1a03112ca5858"))
            (2996507, uint256("0xea11ad1f732071514b9c6e6daf74f70612053a77abf77a85502a731eb56770d7"));

static const Checkpoints::CCheckpointData data = {
    &mapCheckpoints,
    1605423256, // * UNIX timestamp of last checkpoint block
    6057936,    // * total number of transactions between genesis and last checkpoint
                //   (the tx=... number in the SetBestChain debug.log lines)
    500         // * estimated number of transactions per day after checkpoint
};

static Checkpoints::MapCheckpoints mapCheckpointsTestnet =
    boost::assign::map_list_of(0, uint256("0x001"));
static const Checkpoints::CCheckpointData dataTestnet = {
    &mapCheckpointsTestnet,
    1454124731,
    0,
    250};

static Checkpoints::MapCheckpoints mapCheckpointsRegtest =
    boost::assign::map_list_of(0, uint256("0x001"));
static const Checkpoints::CCheckpointData dataRegtest = {
    &mapCheckpointsRegtest,
    1454124731,
    0,
    100};

libzerocoin::ZerocoinParams* CChainParams::Zerocoin_Params() const
{
    assert(this);
    static CBigNum bnTrustedModulus;
    bnTrustedModulus.SetDec(zerocoinModulus);
    static libzerocoin::ZerocoinParams ZCParams = libzerocoin::ZerocoinParams(bnTrustedModulus);

    return &ZCParams;
}

libzerocoin::ZerocoinParams* CChainParams::OldZerocoin_Params() const
{
    assert(this);
    static CBigNum bnTrustedModulus;
    bnTrustedModulus.SetHex(zerocoinModulus);
    static libzerocoin::ZerocoinParams ZCParams = libzerocoin::ZerocoinParams(bnTrustedModulus);

    return &ZCParams;
}

bool CChainParams::HasStakeMinAgeOrDepth(const int contextHeight, const uint32_t contextTime,
                                         const int utxoFromBlockHeight, const uint32_t utxoFromBlockTime) const
{
    // before stake modifier V2, the age required was 3 * 60 * 60 (3 hour) / not required on regtest
    if (!IsStakeModifierV2(contextHeight))
        return (NetworkID() == CBaseChainParams::REGTEST || (utxoFromBlockTime + 10800 <= contextTime));

    // after stake modifier V2, we require the utxo to be nStakeMinDepth deep in the chain
    return (contextHeight - utxoFromBlockHeight >= nStakeMinDepth);
}


class CMainParams : public CChainParams
{
public:
    CMainParams()
    {
        networkID = CBaseChainParams::MAIN;
        strNetworkID = "main";

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 4-byte int at any alignment.
         */
        pchMessageStart[0] = 0x81;
        pchMessageStart[1] = 0xb4;
        pchMessageStart[2] = 0xed;
        pchMessageStart[3] = 0xa9;
        vAlertPubKey = ParseHex("0000076d3ba6ba6e7423fa5cbd6a89e0a9a5348f88d332b44a5cb1a8b7ed2c1eaa335fc8dc4f012cb8241cc0bdafd6ca70c5f5448916e4e6f511bcd746ed57dc50");
        nDefaultPort = 52020;
        bnProofOfWorkLimit = ~uint256(0) >> 20;
        nMaxReorganizationDepth = 100;
        nEnforceBlockUpgradeMajority = 2250;
        nRejectBlockOutdatedMajority = 2850;
        nToCheckBlockUpgradeMajority = 3000; // 24 hours (legacy)
        nMinerThreads = 0;
        /* Legacy Blocktime */
        nTargetTimespanLegacy = 1 * 60 * 40;  // OHMC: 40 Minutes
        nTargetSpacingLegacy = 1 * 30;        // OHMC: 30 Seconds
        /* New Blocktime */
        nTargetTimespan = 1 * 60 * 60 * 2;    // OHMC New: 120 Minutes
        nTargetSpacing = 1 * 60 * 4;          // OHMC New: 240 Seconds
        nStakeMinDepth = 600;
        nFutureTimeDriftPoW = 7200;
        nFutureTimeDriftPoS = 180;
        nMaturity = 4;
        nKarmanodeCountDrift = 20;
        nMaxMoneyOut = 30000000 * COIN;

        /** Height or Time Based Activations **/
        nLastPOWBlock = 1001;

        nModifierUpdateBlock = 615800;

        /**
         * Build the genesis block. Note that the output of the genesis coinbase cannot
         * be spent as it did not originally exist in the database.
         *
         * CBlock(hash=00000ffd590b14, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=e0028e, nTime=1390095618, nBits=1e0ffff0, nNonce=28917698, vtx=1)
         *   CTransaction(hash=e0028e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
         *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d01044c5957697265642030392f4a616e2f3230313420546865204772616e64204578706572696d656e7420476f6573204c6976653a204f76657273746f636b2e636f6d204973204e6f7720416363657074696e6720426974636f696e73)
         *     CTxOut(nValue=50.00000000, scriptPubKey=0xA9037BAC7050C479B121CF)
         *   vMerkleTree: e0028e
         */
        const char* pszTimestamp = "Royal Bank of Scotland to close 259 branches, cutting 680 jobs - Dec 21 2017";
        CMutableTransaction txNew;
        txNew.vin.resize(1);
        txNew.vout.resize(1);
        txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
        txNew.vout[0].nValue = 250 * COIN;
        txNew.vout[0].scriptPubKey = CScript() << ParseHex("04d10e83b2703ccf322f7dbd62dd5435ac7c10bd055814ce121ba32607d573b8810c02c0582aed05b4deb9c4b77b26d92428c61256cd42774babea0a073b2ed0c9") << OP_CHECKSIG;
        genesis.vtx.push_back(txNew);
        genesis.hashPrevBlock = 0;
        genesis.hashMerkleRoot = genesis.BuildMerkleTree();
        genesis.nVersion = 1;
        genesis.nTime = 1513867516;
        genesis.nBits = 0x1e0ffff0;;
        genesis.nNonce = 2266164;

        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0xbf7fdb166c58ef349097c3964b433a9821983483307cf5fc71335fd7b380fe36"));
        assert(genesis.hashMerkleRoot == uint256("0x48f5858d0f091dec3f5278144cdcca7c8926b091d5af5ea711539f29f1acaf8c"));

        // Dev seeders
        vSeeds.push_back(CDNSSeedData("dns1.ohmc.tips", "dns1.ohmc.tips"));     // Squid
        vSeeds.push_back(CDNSSeedData("dns2.ohmc.tips", "dns2.ohmc.tips"));     // Squid


        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 80);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 13);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 212);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x02)(0x2D)(0x25)(0x33).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x02)(0x21)(0x31)(0x2B).convert_to_container<std::vector<unsigned char> >();
        nExtCoinType = 444;

        bech32_hrp = "oh";

        convertSeed6(vFixedSeeds, pnSeed6_main, ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fAllowMinDifficultyBlocks = false;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fSkipProofOfWorkCheck = false;
        fTestnetToBeDeprecatedFieldRPC = false;
        fHeadersFirstSyncingActive = false;

        nPoolMaxTransactions = 3;
        strSporkKey = "04dcb6cbd18fdecce2aac1f795aa650a25749fb58eb5afc796655cce5c728a2eb38ec0ce85d67555ddde6530cd04e6fd1f7c5f818ba483ad6f098e402803225074";
        strObfuscationPoolDummyAddress = "D87q2gC9j6nNrnzCsg4aY6bHMLsT9nUhEw";

        nBlockStakeModifierlV2 = 99999999; // this will be set at a later date

        /** Zerocoin */
        zerocoinModulus = "25195908475657893494027183240048398571429282126204032027777137836043662020707595556264018525880784"
            "4069182906412495150821892985591491761845028084891200728449926873928072877767359714183472702618963750149718246911"
            "6507761337985909570009733045974880842840179742910064245869181719511874612151517265463228221686998754918242243363"
            "7259085141865462043576798423387184774447920739934236584823824281198163815010674810451660377306056201619676256133"
            "8441436038339044149526344321901146575444541784240209246165157233507787077498171257724679629263863563732899121548"
            "31438167899885040445364023527381951378636564391212010397122822120720357";

        nZerocoinStartHeight = 999999999;
        nZerocoinLastOldParams = 99999999; // Updated to defer zerocoin v2 for further testing.

        nMaxZerocoinSpendsPerTransaction = 7; // Assume about 20kb each
        nMinZerocoinMintFee = 1 * CENT; //high fee required for zerocoin mints
        nMintRequiredConfirmations = 20; //the maximum amount of confirmations until accumulated in 19
        nRequiredAccumulation = 1;
        nDefaultSecurityLevel = 100; //full security level for accumulators
        nZerocoinHeaderVersion = 99; //Block headers must be this version once zerocoin is active
        nBudgetFeeConfirmations = 3; // Number of confirmations for the finalization fee

        // Network upgrades
        consensus.vUpgrades[Consensus::BASE_NETWORK].nActivationHeight =
            Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        // Version 3.0.2 upgrade
        consensus.vUpgrades[Consensus::UPGRADE_V3_0_BLOCKTIME].nActivationHeight = 2977924;
        consensus.vUpgrades[Consensus::UPGRADE_V3_0_BLOCKTIME].nProtocolVersion = 71020;
        consensus.vUpgrades[Consensus::UPGRADE_V3_0_BLOCKREWARD].nActivationHeight = 2977924;
        consensus.vUpgrades[Consensus::UPGRADE_V3_0_BLOCKREWARD].nProtocolVersion = 71020;
        // Next version dummy
        consensus.vUpgrades[Consensus::UPGRADE_V3_1_DUMMY].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_V3_2_DUMMY].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;

        // Version 3.0.2 block activated at
        consensus.vUpgrades[Consensus::UPGRADE_V3_0_BLOCKTIME].hashActivationBlock =
            uint256S("0xce02f419dcac41e3a94c4284eee1d6525b1f5c0129a4242be8f01699df965157");
        consensus.vUpgrades[Consensus::UPGRADE_V3_0_BLOCKREWARD].hashActivationBlock =
            uint256S("0xce02f419dcac41e3a94c4284eee1d6525b1f5c0129a4242be8f01699df965157");

    }

    const Checkpoints::CCheckpointData& Checkpoints() const
    {
        return data;
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CMainParams
{
public:
    CTestNetParams()
    {
        networkID = CBaseChainParams::TESTNET;
        strNetworkID = "test";
        pchMessageStart[0] = 0x47;
        pchMessageStart[1] = 0x76;
        pchMessageStart[2] = 0x65;
        pchMessageStart[3] = 0xba;
        vAlertPubKey = ParseHex("04d7e13bc896eb07e2db2d7272f5ddfaedfb64b8ed4caa4d917d6e0781b59ca44f8b5d40995622008e40707b47687eebee11cbe3bbaf2348622cc271c7f0d0bd0a");
        nDefaultPort = 51515;
        nEnforceBlockUpgradeMajority = 51;
        nRejectBlockOutdatedMajority = 75;
        nToCheckBlockUpgradeMajority = 100;
        nMinerThreads = 0;
        /* Legacy Blocktime */
        nTargetTimespanLegacy = 1 * 60 * 40;  // OHMC: 40 Minutes
        nTargetSpacingLegacy = 1 * 22;        // OHMC: 22 Seconds
        /* New Blocktime */
        nTargetTimespan = 1 * 60 * 60 * 1;    // OHMC New: 60 Minutes
        nTargetSpacing = 1 * 60 * 2;          // OHMC New: 120 Seconds
        nMaturity = 15;
        nStakeMinDepth = 100;
        nKarmanodeCountDrift = 4;
        nModifierUpdateBlock = 51197; //approx Mon, 17 Apr 2017 04:00:00 GMT
        nMaxMoneyOut = 43199500 * COIN;
        nLastPOWBlock = 200;
        nZerocoinStartHeight = 999999999;

        nZerocoinLastOldParams = 50000;

        //! Modify the testnet genesis block so the timestamp is valid for a later start.
        genesis.nTime = 1597378077;
        genesis.nNonce = 8142020;

        hashGenesisBlock = genesis.GetHash();
        //assert(hashGenesisBlock == uint256("0xfab709a0c107fe7cf6b0d552c514ef3228f9e0f107cd3c9b2fcea96512342cd8"));

        vFixedSeeds.clear();
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 139); // Testnet ohmcoin addresses start with 'x' or 'y'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 19);  // Testnet ohmcoin script addresses start with '8' or '9'
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 239);     // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        // Testnet ohmcoin BIP32 pubkeys start with 'DRKV'
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x3a)(0x80)(0x61)(0xa0).convert_to_container<std::vector<unsigned char> >();
        // Testnet ohmcoin BIP32 prvkeys start with 'DRKP'
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x3a)(0x80)(0x58)(0x37).convert_to_container<std::vector<unsigned char> >();
        // Testnet phore BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        bech32_hrp = "tp";

        convertSeed6(vFixedSeeds, pnSeed6_test, ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fAllowMinDifficultyBlocks = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        nPoolMaxTransactions = 2;
        strSporkKey = "0491826db990788ac61af9f74556c07bbf049b36a2b6942a6df11011643c4076f0cb05b360de70822728dfb929bd3467336366ba7351c5772c504641b8310e4a91";
        strObfuscationPoolDummyAddress = "y57cqfGRkekRyDRNeJiLtYVEbvhXrNbmox";
        nBlockStakeModifierlV2 = 9999999; // this will be set at a later date
        nBudgetFeeConfirmations = 3; // Number of confirmations for the finalization fee. We have to make this very short
                                     // here because we only have a 8 block finalization window on testnet

         // Network upgrades
         consensus.vUpgrades[Consensus::BASE_NETWORK].nActivationHeight =
                 Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
         consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
                 Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
         consensus.vUpgrades[Consensus::UPGRADE_V3_0_BLOCKTIME].nActivationHeight = 33001;
         consensus.vUpgrades[Consensus::UPGRADE_V3_0_BLOCKREWARD].nActivationHeight = 33002;
         consensus.vUpgrades[Consensus::UPGRADE_V3_1_DUMMY].nActivationHeight =
                 Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
         consensus.vUpgrades[Consensus::UPGRADE_V3_2_DUMMY].nActivationHeight =
                 Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
    }
    const Checkpoints::CCheckpointData& Checkpoints() const
    {
        return dataTestnet;
    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CTestNetParams
{
public:
    CRegTestParams()
    {
        networkID = CBaseChainParams::REGTEST;
        strNetworkID = "regtest";
        strNetworkID = "regtest";
        pchMessageStart[0] = 0xa1;
        pchMessageStart[1] = 0xcf;
        pchMessageStart[2] = 0x7e;
        pchMessageStart[3] = 0xac;
        nEnforceBlockUpgradeMajority = 750;
        nRejectBlockOutdatedMajority = 950;
        nToCheckBlockUpgradeMajority = 1000;
        nMinerThreads = 1;
        /* Legacy Blocktime */
        nTargetTimespanLegacy = 1 * 60 * 60 * 24;   // OHMC: 24 Hours
        nTargetSpacingLegacy = 1 * 30;              // OHMC: 22 Seconds
        /* New Blocktime */
        nTargetTimespan = 1 * 60 * 60 * 2;          // OHMC New: 120 Minutes
        nTargetSpacing = 1 * 60 * 4;                // OHMC New: 240 Seconds
        bnProofOfWorkLimit = ~uint256(0) >> 1;
        genesis.nTime = 1454124731;
        genesis.nBits = 0x207fffff;
        genesis.nNonce = 12345;
        nMaturity = 0;
        nStakeMinDepth = 0;
        nLastPOWBlock = 999999999; // PoS complicates Regtest because of timing issues
        nZerocoinLastOldParams = 499;
        nZerocoinStartHeight = 100;

        nBlockStakeModifierlV2 = std::numeric_limits<int>::max(); // max integer value (never switch on regtest)

        hashGenesisBlock = genesis.GetHash();
        nDefaultPort = 51476;
        //assert(hashGenesisBlock == uint256("0x2b1a0f66712aad59ad283662d5b919415a25921ce89511d73019107e380485bf"));

        bech32_hrp = "ohmct";

        vFixedSeeds.clear(); //! Testnet mode doesn't have any fixed seeds.
        vSeeds.clear();      //! Testnet mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fAllowMinDifficultyBlocks = true;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;
        nRequiredAccumulation = 1;

        // {
        //     "PrivateKey": "923EhWh2bJHynX6d4Tqt2Q75bhTDCT1b4kff3qzDKDZHZ6pkQs7",
        //     "PublicKey": "04866dc02c998b7e1ab16fe14e0d86554595da90c36acb706a4d763b58ed0edb1f82c87e3ced065c5b299b26e12496956b9e5f9f19aa008b5c46229b15477c875a"
        // }
        strSporkKey = "04866dc02c998b7e1ab16fe14e0d86554595da90c36acb706a4d763b58ed0edb1f82c87e3ced065c5b299b26e12496956b9e5f9f19aa008b5c46229b15477c875a";

        // Network upgrades
        consensus.vUpgrades[Consensus::BASE_NETWORK].nActivationHeight =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
                Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_V3_0_BLOCKTIME].nActivationHeight = 33002;
        consensus.vUpgrades[Consensus::UPGRADE_V3_0_BLOCKREWARD].nActivationHeight = 33002;
        consensus.vUpgrades[Consensus::UPGRADE_V3_1_DUMMY].nActivationHeight =
                Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_V3_2_DUMMY].nActivationHeight =
                Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
    }

    const Checkpoints::CCheckpointData& Checkpoints() const
    {
        return dataRegtest;
    }

    void UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex idx, int nActivationHeight)
    {
        assert(idx > Consensus::BASE_NETWORK && idx < Consensus::MAX_NETWORK_UPGRADES);
        consensus.vUpgrades[idx].nActivationHeight = nActivationHeight;
    }
};
static CRegTestParams regTestParams;

/**
 * Unit test
 */
class CUnitTestParams : public CMainParams, public CModifiableParams
{
public:
    CUnitTestParams()
    {
        networkID = CBaseChainParams::UNITTEST;
        strNetworkID = "unittest";
        nDefaultPort = 51478;
        vFixedSeeds.clear(); //! Unit test mode doesn't have any fixed seeds.
        vSeeds.clear();      //! Unit test mode doesn't have any DNS seeds.

        nExtCoinType = 1;
        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fAllowMinDifficultyBlocks = false;
        fMineBlocksOnDemand = true;
    }

    const Checkpoints::CCheckpointData& Checkpoints() const
    {
        // UnitTest share the same checkpoints as MAIN
        return data;
    }

    //! Published setters to allow changing values in unit test cases
    virtual void setEnforceBlockUpgradeMajority(int anEnforceBlockUpgradeMajority) { nEnforceBlockUpgradeMajority = anEnforceBlockUpgradeMajority; }
    virtual void setRejectBlockOutdatedMajority(int anRejectBlockOutdatedMajority) { nRejectBlockOutdatedMajority = anRejectBlockOutdatedMajority; }
    virtual void setToCheckBlockUpgradeMajority(int anToCheckBlockUpgradeMajority) { nToCheckBlockUpgradeMajority = anToCheckBlockUpgradeMajority; }
    virtual void setDefaultConsistencyChecks(bool afDefaultConsistencyChecks) { fDefaultConsistencyChecks = afDefaultConsistencyChecks; }
    virtual void setAllowMinDifficultyBlocks(bool afAllowMinDifficultyBlocks) { fAllowMinDifficultyBlocks = afAllowMinDifficultyBlocks; }
    virtual void setSkipProofOfWorkCheck(bool afSkipProofOfWorkCheck) { fSkipProofOfWorkCheck = afSkipProofOfWorkCheck; }
};
static CUnitTestParams unitTestParams;


static CChainParams* pCurrentParams = 0;

CModifiableParams* ModifiableParams()
{
    assert(pCurrentParams);
    assert(pCurrentParams == &unitTestParams);
    return (CModifiableParams*)&unitTestParams;
}

const CChainParams& Params()
{
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams& Params(CBaseChainParams::Network network)
{
    switch (network) {
    case CBaseChainParams::MAIN:
        return mainParams;
    case CBaseChainParams::TESTNET:
        return testNetParams;
    case CBaseChainParams::REGTEST:
        return regTestParams;
    case CBaseChainParams::UNITTEST:
        return unitTestParams;
    default:
        assert(false && "Unimplemented network");
        return mainParams;
    }
}

void SelectParams(CBaseChainParams::Network network)
{
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}

bool SelectParamsFromCommandLine()
{
    CBaseChainParams::Network network = NetworkIdFromCommandLine();
    if (network == CBaseChainParams::MAX_NETWORK_TYPES)
        return false;

    SelectParams(network);
    return true;
}

void UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex idx, int nActivationHeight)
{
    regTestParams.UpdateNetworkUpgradeParameters(idx, nActivationHeight);
}
