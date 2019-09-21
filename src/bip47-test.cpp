//
// Created by Top1s on 8/30/2019.
//
#include "bip47.h"
#include "wallet/wallet.h"
#include "wallet/walletexcept.h"
#include "wallet/sigmaspendbuilder.h"
#include "amount.h"
#include "base58.h"
#include "checkpoints.h"
#include "chain.h"
#include "coincontrol.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "key.h"
#include "keystore.h"
#include "main.h"
#include "zerocoin.h"
#include "sigma.h"
#include "../sigma/coinspend.h"
#include "../sigma/spend_metadata.h"
#include "../sigma/coin.h"
#include "../sigma/remint.h"
#include "../libzerocoin/SpendMetaData.h"
#include "net.h"
#include "policy/policy.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/sign.h"
#include "timedata.h"
#include "txmempool.h"
#include "util.h"
#include "ui_interface.h"
#include "utilmoneystr.h"
#include "validation.h"
#include "darksend.h"
#include "instantx.h"
#include "znode.h"
#include "znode-sync.h"
#include "random.h"
#include "init.h"
#include "hdmint/wallet.h"
#include "rpc/protocol.h"

#include "hdmint/tracker.h"

#include <assert.h>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

using namespace std;
#include <stdexcept>

extern void noui_connect();

#include <string>
#include <vector>
#include <string.h>
#include <iostream>


struct TestDerivation {
    std::string pub;
    std::string prv;
    unsigned int nChild;
};

struct TestVector {
    std::string strHexMaster;
    std::vector<TestDerivation> vDerive;

    TestVector(std::string strHexMasterIn) : strHexMaster(strHexMasterIn) {}

    TestVector& operator()(std::string pub, std::string prv, unsigned int nChild) {
        vDerive.push_back(TestDerivation());
        TestDerivation &der = vDerive.back();
        der.pub = pub;
        der.prv = prv;
        der.nChild = nChild;
        return *this;
    }
};

TestVector test1 =
        TestVector("000102030405060708090a0b0c0d0e0f")
                ("xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8",
                 "xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stbPy6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk33yuGBxrMPHi",
                 0x80000000)
                ("xpub68Gmy5EdvgibQVfPdqkBBCHxA5htiqg55crXYuXoQRKfDBFA1WEjWgP6LHhwBZeNK1VTsfTFUHCdrfp1bgwQ9xv5ski8PX9rL2dZXvgGDnw",
                 "xprv9uHRZZhk6KAJC1avXpDAp4MDc3sQKNxDiPvvkX8Br5ngLNv1TxvUxt4cV1rGL5hj6KCesnDYUhd7oWgT11eZG7XnxHrnYeSvkzY7d2bhkJ7",
                 1)
                ("xpub6ASuArnXKPbfEwhqN6e3mwBcDTgzisQN1wXN9BJcM47sSikHjJf3UFHKkNAWbWMiGj7Wf5uMash7SyYq527Hqck2AxYysAA7xmALppuCkwQ",
                 "xprv9wTYmMFdV23N2TdNG573QoEsfRrWKQgWeibmLntzniatZvR9BmLnvSxqu53Kw1UmYPxLgboyZQaXwTCg8MSY3H2EU4pWcQDnRnrVA1xe8fs",
                 0x80000002)
                ("xpub6D4BDPcP2GT577Vvch3R8wDkScZWzQzMMUm3PWbmWvVJrZwQY4VUNgqFJPMM3No2dFDFGTsxxpG5uJh7n7epu4trkrX7x7DogT5Uv6fcLW5",
                 "xprv9z4pot5VBttmtdRTWfWQmoH1taj2axGVzFqSb8C9xaxKymcFzXBDptWmT7FwuEzG3ryjH4ktypQSAewRiNMjANTtpgP4mLTj34bhnZX7UiM",
                 2)
                ("xpub6FHa3pjLCk84BayeJxFW2SP4XRrFd1JYnxeLeU8EqN3vDfZmbqBqaGJAyiLjTAwm6ZLRQUMv1ZACTj37sR62cfN7fe5JnJ7dh8zL4fiyLHV",
                 "xprvA2JDeKCSNNZky6uBCviVfJSKyQ1mDYahRjijr5idH2WwLsEd4Hsb2Tyh8RfQMuPh7f7RtyzTtdrbdqqsunu5Mm3wDvUAKRHSC34sJ7in334",
                 1000000000)
                ("xpub6H1LXWLaKsWFhvm6RVpEL9P4KfRZSW7abD2ttkWP3SSQvnyA8FSVqNTEcYFgJS2UaFcxupHiYkro49S8yGasTvXEYBVPamhGW6cFJodrTHy",
                 "xprvA41z7zogVVwxVSgdKUHDy1SKmdb533PjDz7J6N6mV6uS3ze1ai8FHa8kmHScGpWmj4WggLyQjgPie1rFSruoUihUZREPSL39UNdE3BBDu76",
                 0);

void init_test_config() {
    SoftSetBoolArg("-dandelion", false);
    ECC_Start();
    SetupEnvironment();
    SoftSetBoolArg("-dandelion", false);
    SetupNetworking();
    SoftSetBoolArg("-dandelion", false);
    fPrintToDebugLog = false; // don't want to write to debug.log file
    fCheckBlockIndex = true;
    SoftSetBoolArg("-dandelion", false);
    SelectParams(CBaseChainParams::MAIN);
    SoftSetBoolArg("-dandelion", false);
    noui_connect();
}


int main(int argc, char* argv[]) {

    printf("splited test\n");

    init_test_config();

//    std::vector<unsigned char> seed = ParseHex(test1.strHexMaster);
//    CExtKey key;
//    CExtPubKey pubkey;
//    key.SetMaster(&seed[0], seed.size());
//    pubkey = key.Neuter();
//    const TestDerivation &derive = test1.vDerive[0];
//
//    unsigned char data[74];
//    key.Encode(data);
//    pubkey.Encode(data);
//
//    // Test private key
//    CBitcoinExtKey b58key; b58key.SetKey(key);
//
//    printf("b58key = %s ,\n derive.prv = %s \n",
//            b58key.ToString().c_str(),
//            derive.prv.c_str());
//
//    CBitcoinExtKey b58keyDecodeCheck(derive.prv);
//    CExtKey checkKey = b58keyDecodeCheck.GetKey();
//    assert(checkKey == key);
//
//    printf("Test Passed\n");

//    std::string strHexMaster = "b7b8706d714d9166e66e7ed5b3c61048";
    std::string strHexMaster = "64dca76abc9c6f0cf3d212d248c380c4622c8f93b2c425ec6a5567fd5db57e10d3e6f94a2f6af4ac2edb8998072aad92098db73558c323777abf5bd1082d970a";
    std::vector<unsigned char> seed = ParseHex(strHexMaster);

    CExtKey masterKey;             //bip47 master key
    CExtKey purposeKey;            //key at m/47'
    CExtKey coinTypeKey;           //key at m/47'/<1/136>' (Testnet or Zcoin Coin Type respectively, according to SLIP-0047)
    CExtKey identityKey;           //key identity
    CExtKey childKey;              // index

    masterKey.SetMaster(&seed[0], seed.size());



    masterKey.Derive(purposeKey, BIP47_INDEX | BIP32_HARDENED_KEY_LIMIT);
    purposeKey.Derive(coinTypeKey, 0);
    coinTypeKey.Derive(identityKey, 0);



//    CExtKey key;
    unsigned char data[80];
    CExtPubKey pubkey;
    pubkey = identityKey.Neuter();
    identityKey.Encode(data);
    pubkey.Encode(data);

    bip47::byte alicepubkey[33];
    bip47::byte alicechian[32];
    std::copy(pubkey.pubkey.begin(), pubkey.pubkey.end(), alicepubkey);
    std::copy(pubkey.chaincode.begin(), pubkey.chaincode.end(), alicechian);

    bip47::PaymentCode alicePcode(alicepubkey, alicechian);
    printf("\n Payment code of alice \n%s\n", alicePcode.ToString().c_str());


    printf("Encoded Data\n");
    for(int i = 0; i < 80; i++) {
        printf("%u", data[i]);
    }
    printf("\n");

    CBitcoinExtKey b58key; b58key.SetKey(identityKey);

    printf("%s\n", b58key.ToString().c_str());

    CBitcoinExtPubKey b58pubkey; b58pubkey.SetKey(pubkey);

    printf("Pubkey value is %s\n",b58pubkey.ToString().c_str());

    std::string strPcode = "PM8TJTLJbPRGxSbc8EJi42Wrr6QbNSaSSVJ5Y3E4pbCYiTHUskHg13935Ubb7q8tx9GVbh2UuRnBc3WSyJHhUrw8KhprKnn9eDznYGieTzFcwQRya4GA";
    bip47::PaymentCode paymentCode(strPcode);

    CPubKey masterPubkey = paymentCode.getPubkey();
    printf("\n master pubkey size %d\n", masterPubkey.size());

    if (masterPubkey.IsValid()) {
        printf("\nmaster Pubkey is valid\n");
        CBitcoinAddress address(masterPubkey.GetID());
        std::cout << " Address from masterPubkey " << address.ToString() << std::endl;
    } else {
        printf("\nmaster Pubkey is not valid\n");
    }


    CExtPubKey extPubKey;
    paymentCode.valid();
    bip47::byte payloads[80] = {};
    paymentCode.get_payload(payloads);


    CBitcoinAddress newaddress("TN5pADYZMSDyKpcgicC1d2Z2adHdTgHRGG");
    CKeyID keyID;
    newaddress.GetKeyID(keyID);
    CPubKey vchPubKey;


//    "File named wallet.dat"
//    if (!pwalletMain->GetPubKey(keyID, vchPubKey)) {
//        printf("\n findout pubkey of address\n");
//    } else {
//        printf("\n cannot find out pubkey of address\n");
//    }

//    bip47::PaymentCode paymentCode1()



    /**
     * CKey CPubKey
     * CKeyMetadata
     * CBitcoinExtKey
     * CBitcoinExtPubKey
     * CExtKey
     * CExtPubKey
     */

//    SelectParams(CBaseChainParams::REGTEST);

//    string strAddress = "TPj1wZxMM7TRWeBdKWWMn34G6XNeobCq9K";
//    string privstr = "cSaagnTkEJymA6amdQ5kpdPTszfKzdjiXZmJm42qy7Fd4MnTwZeB";
//    CBitcoinSecret vchSecret;
//    vchSecret.SetString(privstr);
//    CKey key = vchSecret.GetKey();
//    printf("%s\n", key.IsValid() ? "true" : "false");
//
//    LOCK(pwalletMain->cs_wallet);
//    CPubKey pubKey = key.GetPubKey();


//    assert(key.VerifyPubKey(pubKey));


//    CBitcoinAddress addr(strAddress);
//    CKeyID keyId;
//    addr.GetKeyID(keyId);

//    pwalletMain->GetKey(hdChain.masterKeyID, key);

    return 0;
}