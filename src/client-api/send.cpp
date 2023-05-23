// Copyright (c) 2018 Tadhg Riordan Zcoin Developer
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "validation.h"
#include "send.h"
#include "client-api/server.h"
#include "util.h"
#include "init.h"
#include "net.h"
#include "wallet/wallet.h"
#include <client-api/wallet.h>
#include "client-api/protocol.h"
#include "client-api/bigint.h"
#include "rpc/server.h"
#include "univalue.h"
#include "wallet/coincontrol.h"
#include <fstream>

namespace fs = boost::filesystem;
using namespace boost::chrono;

std::map<std::string, int> nStates = {
        {"active",0},
        {"deleted",1},
        {"hidden",2},
        {"archived",3}
};

bool setPaymentRequest(UniValue paymentRequestUni){
    //write back UniValue
    fs::path const &path = CreatePaymentRequestFile();

    std::ofstream paymentRequestOut(path.string());

    paymentRequestOut << paymentRequestUni.write(4,0) << std::endl;

    return true;
}

bool getPaymentRequest(UniValue &paymentRequestUni, UniValue &paymentRequestData){
    fs::path const &path = CreatePaymentRequestFile();

    // get data as ifstream
    std::ifstream paymentRequestIn(path.string());

    // parse as std::string
    std::string paymentRequestStr((std::istreambuf_iterator<char>(paymentRequestIn)), std::istreambuf_iterator<char>());

    // finally as UniValue
    paymentRequestUni.read(paymentRequestStr);
    LogPrintf("paymentRequest: %s\n", paymentRequestUni.write());

    if(!paymentRequestUni["data"].isNull()){
        paymentRequestData = paymentRequestUni["data"];
    }
    
    return true;
}

bool setTxFee(const UniValue& feeperkb){
    CAmount nAmount = feeperkb.get_int64();

    payTxFee = CFeeRate(nAmount, 1000);

    return true;
}

UniValue getNewAddress()
{
    if (!EnsureWalletIsAvailable(pwalletMain, false))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (!pwalletMain->IsLocked())
        pwalletMain->TopUpKeyPool();

    // Generate a new key that is added to wallet
    CPubKey newKey;
    if (!pwalletMain->GetKeyFromPool(newKey))
        throw JSONAPIError(API_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    CKeyID keyID = newKey.GetID();

    pwalletMain->SetAddressBook(keyID, "", "receive");

    return CBitcoinAddress(keyID).ToString();
}

UniValue getNewSparkAddress()
{
    if (!EnsureWalletIsAvailable(pwalletMain, false))
        return NullUniValue;

    if (!pwalletMain || !pwalletMain->zwallet) {
        throw JSONRPCError(RPC_WALLET_ERROR, "lelantus mint/joinsplit is not allowed for legacy wallet");
    }
    spark::Address address = pwalletMain->sparkWallet->generateNewAddress();
    unsigned char network = spark::GetNetworkType();
    pwalletMain->SetSparkAddressBook(address.encode(network), "", "receive");

    return address.encode(network);
}

UniValue paymentrequestaddress(Type type, const UniValue& data, const UniValue& auth, bool fHelp){

    if (!EnsureWalletIsAvailable(pwalletMain, false))
        return NullUniValue;

    std::string addressType = find_value(data, "addressType").getValStr();
    UniValue ret(UniValue::VOBJ);
    std::string address = "";
    CWalletDB walletdb(pwalletMain->strWalletFile);

    if(addressType == "Spark") {
        if(!walletdb.ReadPaymentRequestSparkAddress(address)){
            address = getNewSparkAddress().get_str();
            walletdb.WritePaymentRequestSparkAddress(address);
        }
    } else if (addressType == "Transparent") {
        if(!walletdb.ReadPaymentRequestAddress(address)){
            address = getNewAddress().get_str();
            walletdb.WritePaymentRequestAddress(address);
        }
    } else {
        throw JSONAPIError(API_INVALID_PARAMETER, "Invalid addressType");
    }
    ret.push_back(Pair("address", address));
    return ret;
}

UniValue sendzcoin(Type type, const UniValue& data, const UniValue& auth, bool fHelp)
{   
    LOCK2(cs_main, pwalletMain->cs_wallet);

    CCoinControl cc;
    bool hasCoinControl = GetCoinControl(data, cc);

    switch(type){
        case Create: {
            UniValue sendTo(UniValue::VOBJ);
            bool fSubtractFeeFromAmount;
            payTxFee = CFeeRate(get_bigint(data["feePerKb"]));
            sendTo = find_value(data,"addresses").get_obj();
            fSubtractFeeFromAmount = find_value(data, "subtractFeeFromAmount").get_bool();

            int nMinDepth = 1;

            CWalletTx wtx;

            std::set<CBitcoinAddress> setAddress;
            std::vector<CRecipient> vecSend;

            CAmount totalAmount = 0;
            std::vector<std::string> keys = sendTo.getKeys();
            BOOST_FOREACH(const std::string& name_, keys)
            {
                
                UniValue entry(UniValue::VOBJ);
                try{
                    entry = find_value(sendTo, name_).get_obj();
                }catch (const std::exception& e){
                    throw JSONAPIError(API_WRONG_TYPE_CALLED, "wrong key passed/value type for method");
                }

                CBitcoinAddress address(name_);
                if (!address.IsValid())
                    throw JSONAPIError(API_INVALID_ADDRESS_OR_KEY, std::string("Invalid zcoin address: ")+name_);

                if (setAddress.count(address))
                    throw JSONAPIError(API_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ")+name_);
                setAddress.insert(address);

                CScript scriptPubKey = GetScriptForDestination(address.Get());
                CAmount nAmount = get_bigint(entry["amount"]);
                std::string label = find_value(entry, "label").get_str();
                if (nAmount <= 0)
                    throw JSONAPIError(API_TYPE_ERROR, "Invalid amount for send");
                totalAmount += nAmount;

                wtx.mapValue["label"] = label;

                CRecipient recipient = {scriptPubKey, nAmount, fSubtractFeeFromAmount};
                vecSend.push_back(recipient);
            }
            
            // Send
            CReserveKey keyChange(pwalletMain);
            CAmount nFeeRequired = 0;
            int nChangePosRet = -1;
            std::string strFailReason;
            bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired, nChangePosRet, strFailReason, hasCoinControl? (&cc):NULL);
            if (!fCreated)
                throw JSONAPIError(API_WALLET_ERROR, strFailReason);

            CValidationState state;
            if (!pwalletMain->CommitTransaction(wtx, keyChange, g_connman.get(), state))
                throw JSONAPIError(API_WALLET_ERROR, "Transaction commit failed");

            UniValue retval(UniValue::VOBJ);
            retval.push_back(Pair("txid",  wtx.GetHash().GetHex()));
            return retval;
        }
        default: {

        }
    }

    return NullUniValue;
}

UniValue txfee(Type type, const UniValue& data, const UniValue& auth, bool fHelp){
    // first set the tx fee per kb, then return the total fee with addresses.   
    if (!EnsureWalletIsAvailable(pwalletMain, fHelp))
        return NullUniValue;

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CCoinControl coinControl;
    bool hasCoinControl = GetCoinControl(data, coinControl);

    UniValue ret(UniValue::VOBJ);
    UniValue feePerKb;
    UniValue sendTo(UniValue::VOBJ);
    bool fSubtractFeeFromAmount;
    try{
        feePerKb = find_value(data, "feePerKb");
        sendTo = find_value(data, "addresses").get_obj();
        fSubtractFeeFromAmount = find_value(data, "subtractFeeFromAmount").get_bool();
    }catch (const std::exception& e){
        throw JSONAPIError(API_WRONG_TYPE_CALLED, "wrong key passed/value type for method");
    }
    setTxFee(feePerKb);

    CWalletTx wtx;
    wtx.strFromAccount = "";

    std::set<CBitcoinAddress> setAddress;
    std::vector<CRecipient> vecSend;

    CAmount totalAmount = 0;
    std::vector<std::string> keys = sendTo.getKeys();
    BOOST_FOREACH(const std::string& name_, keys)
    {
        CBitcoinAddress address(name_);
        if (!address.IsValid())
            throw JSONAPIError(API_INVALID_ADDRESS_OR_KEY, std::string("Invalid zcoin address: ")+name_);

        if (setAddress.count(address))
            throw JSONAPIError(API_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ")+name_);
        setAddress.insert(address);

        CScript scriptPubKey = GetScriptForDestination(address.Get());
        CAmount nAmount;
        try{
            nAmount = sendTo[name_].get_int64();
        }catch (const std::exception& e){
            throw JSONAPIError(API_WRONG_TYPE_CALLED, "wrong key passed/value type for method");
        }

        LogPrintf("nAmount getTransactionFee: %s\n", nAmount);
        if (nAmount <= 0)
            throw JSONAPIError(API_TYPE_ERROR, "Invalid amount for send");
        totalAmount += nAmount;

        CRecipient recipient = {scriptPubKey, nAmount, fSubtractFeeFromAmount};
        vecSend.push_back(recipient);
    }

    CReserveKey keyChange(pwalletMain);
    CAmount nFeeRequired = 0;
    int nChangePosRet = -1;
    std::string strFailReason;
    bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired, nChangePosRet, strFailReason, hasCoinControl ? &coinControl : NULL, false);
    if (!fCreated)
        throw JSONAPIError(API_WALLET_INSUFFICIENT_FUNDS, strFailReason);  
    
    ret.push_back(Pair("fee", nFeeRequired));
    return ret;
}

UniValue paymentrequest(Type type, const UniValue& data, const UniValue& auth, bool fHelp)
{
    if (!EnsureWalletIsAvailable(pwalletMain, false))
        return NullUniValue;

    UniValue paymentRequestUni(UniValue::VOBJ);
    UniValue paymentRequestData(UniValue::VOBJ);

    getPaymentRequest(paymentRequestUni, paymentRequestData);

    bool returnEntry = false;
    UniValue entry(UniValue::VOBJ);

    switch(type){
        case Initial: {
            return paymentRequestData;
            break;
        }
        case Create: {     

            milliseconds secs = duration_cast< milliseconds >(
                 system_clock::now().time_since_epoch()
            );
            UniValue createdAt = secs.count();

            std::string paymentRequestAddress;
            entry.push_back(Pair("createdAt", createdAt.get_int64()));
            entry.push_back(Pair("state", "active"));

            try{
                paymentRequestAddress = find_value(data, "address").get_str();
                entry.push_back(Pair("amount", find_value(data, "amount")));
                entry.push_back(Pair("address", paymentRequestAddress));
                entry.push_back(Pair("message", find_value(data, "message").get_str()));
                entry.push_back(Pair("label", find_value(data, "label").get_str()));
            }catch (const std::exception& e){
                throw JSONAPIError(API_WRONG_TYPE_CALLED, "wrong key passed/value type for method");
            }
            
            CWalletDB walletdb(pwalletMain->strWalletFile);
            std::string nextPaymentRequestAddress;
            if(!walletdb.ReadPaymentRequestAddress(nextPaymentRequestAddress))
                throw std::runtime_error("Could not retrieve wallet payment address.");

            if(nextPaymentRequestAddress != paymentRequestAddress)
                throw std::runtime_error("Payment request address passed does not match wallet.");

            if(!paymentRequestUni.replace("data", paymentRequestData)){
                throw std::runtime_error("Could not replace key/value pair.");
            }
            returnEntry = true;

            // remove payment request address
            if(!walletdb.ErasePaymentRequestAddress())
                throw std::runtime_error("Could not reset payment request address.");
                    
            break;
        }
        case Delete: {
            std::string id = find_value(data, "id").get_str();
            
            const UniValue addressObj = find_value(paymentRequestData, id);
            if(addressObj.isNull()){
                throw JSONAPIError(API_INVALID_PARAMETER, "Invalid data, id does not exist");
            }

            const UniValue addressStr = find_value(addressObj, "address");
            if(addressStr.isNull()){
                throw JSONAPIError(API_INVALID_PARAMETER, "Invalid data, address not found");
            }

            paymentRequestData.erase(addressStr);

            if(!paymentRequestUni.replace("data", paymentRequestData)){
                throw std::runtime_error("Could not replace key/value pair.");
            }
            return true;
            break;      
        }
        /*
          "Update" can be used to either:
            - Update an existing address and metadata associated with a payment request
            - Create a new entry for address and metadata that was NOT created through a payment request (eg. created with the Qt application).
        */
        case Update: {
            std::string id;
            std::vector<std::string> dataKeys;
            try{
                id = find_value(data, "id").get_str();
                dataKeys = data.getKeys();
            }catch (const std::exception& e){
                throw JSONAPIError(API_WRONG_TYPE_CALLED, "wrong key passed/value type for method");
            }

            entry = find_value(paymentRequestData, id);

            // If null, declare the object again.
             if(entry.isNull()){
                 entry.setObject();
                 entry.push_back(Pair("address", id));
             }

            for (std::vector<std::string>::iterator it = dataKeys.begin(); it != dataKeys.end(); it++){
                std::string key = (*it);
                UniValue value = find_value(data, key);
                if(!(key=="id")){
                    if(key=="state"){
                        // Only update state should it be a valid value
                        if(!(value.getType()==UniValue::VSTR) && !nStates.count(value.get_str()))
                          throw JSONAPIError(API_WRONG_TYPE_CALLED, "wrong key passed/value type for method");
                    }
                    entry.replace(key, value); //todo might have to specify type
                }
            }

            paymentRequestData.replace(id, entry);

            if(!paymentRequestUni.replace("data", paymentRequestData)){
                throw std::runtime_error("Could not replace key/value pair.");
            }
            returnEntry = true;
            break;
        }
        default: {

        }
    }

    setPaymentRequest(paymentRequestUni);

    if(returnEntry){
        return entry;
    }

    return true;
}


static const CAPICommand commands[] =
{ //  category              collection         actor (function)          authPort   authPassphrase   warmupOk
  //  --------------------- ------------       ----------------          -------- --------------   --------
    { "send",            "paymentRequest",         &paymentrequest,         true,      false,           false  },
    { "send",            "paymentRequestAddress",  &paymentrequestaddress,  true,      false,           false  },
    { "send",            "txFee",                  &txfee,                  true,      false,           false  },
    { "send",            "sendZcoin",              &sendzcoin,              true,      true,            false  }

};

void RegisterSendAPICommands(CAPITable &tableAPI)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableAPI.appendCommand(commands[vcidx].collection, &commands[vcidx]);
}
