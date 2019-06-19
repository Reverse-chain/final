// Copyright (c) 2019 The Zcoin Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <hdmint/hdmint.h>
#include <primitives/zerocoin.h>
#include "hdmint/tracker.h"
#include "util.h"
#include "sync.h"
#include "txdb.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "hdmint/wallet.h"
#include "libzerocoin/Zerocoin.h"
#include "main.h"
#include "zerocoin_v3.h"

using namespace std;
using namespace sigma;

CHDMintTracker::CHDMintTracker(std::string strWalletFile)
{
    this->strWalletFile = strWalletFile;
    mapSerialHashes.clear();
    mapPendingSpends.clear();
    fInitialized = false;
}

CHDMintTracker::~CHDMintTracker()
{
    mapSerialHashes.clear();
    mapPendingSpends.clear();
}

void CHDMintTracker::Init()
{
    //Load all CZerocoinEntries and CHDMints from the database
    if (!fInitialized) {
        ListMints(false, false);
        fInitialized = true;
    }
}

bool CHDMintTracker::Archive(CMintMeta& meta)
{
    uint256 hashPubcoin = sigma::GetPubCoinValueHash(meta.pubCoinValue);

    if (HasSerialHash(meta.hashSerial))
        mapSerialHashes.at(meta.hashSerial).isArchived = true;

    CWalletDB walletdb(strWalletFile);
    CSigmaEntry zerocoin;
    // if (walletdb.ReadZerocoinEntry(meta.pubCoinValue, zerocoin)) {
    //     if (!CWalletDB(strWalletFile).ArchiveMintOrphan(zerocoin))
    //         return error("%s: failed to archive zerocoinmint", __func__);
    // } else {
    //     //failed to read mint from DB, try reading deterministic
    //     CHDMint dMint;
    //     if (!walletdb.ReadHDMint(hashPubcoin, dMint))
    //         return error("%s: could not find pubcoinhash %s in db", __func__, hashPubcoin.GetHex());
    //     if (!walletdb.ArchiveDeterministicOrphan(dMint))
    //         return error("%s: failed to archive deterministic ophaned mint", __func__);
    // }

    LogPrintf("%s: archived pubcoinhash %s\n", __func__, hashPubcoin.GetHex());
    return true;
}

bool CHDMintTracker::UnArchive(const uint256& hashPubcoin, bool isDeterministic)
{
    CWalletDB walletdb(strWalletFile);
    if (isDeterministic) {
        CHDMint dMint;
        if (!walletdb.UnarchiveHDMint(hashPubcoin, dMint))
            return error("%s: failed to unarchive deterministic mint", __func__);
        Add(dMint, false);
    } else {
        CSigmaEntry zerocoin;
        if (!walletdb.UnarchiveZerocoinMint(hashPubcoin, zerocoin))
            return error("%s: failed to unarchivezerocoin mint", __func__);
        Add(zerocoin, false);
    }

    LogPrintf("%s: unarchived %s\n", __func__, hashPubcoin.GetHex());
    return true;
}

bool CHDMintTracker::Get(const uint256 &hashSerial, CMintMeta& mMeta)
{
    auto it = mapSerialHashes.find(hashSerial);
    if(it == mapSerialHashes.end())
        return false;

    mMeta = mapSerialHashes.at(hashSerial);
    return true;
}

CMintMeta CHDMintTracker::GetMetaFromPubcoin(const uint256& hashPubcoin)
{
    for (auto it : mapSerialHashes) {
        CMintMeta meta = it.second;
        if (sigma::GetPubCoinValueHash(meta.pubCoinValue) == hashPubcoin)
            return meta;
    }

    return CMintMeta();
}

std::vector<uint256> CHDMintTracker::GetSerialHashes()
{
    vector<uint256> vHashes;
    for (auto it : mapSerialHashes) {
        if (it.second.isArchived)
            continue;

        vHashes.emplace_back(it.first);
    }


    return vHashes;
}

CAmount CHDMintTracker::GetBalance(bool fConfirmedOnly, bool fUnconfirmedOnly) const
{
    CAmount nTotal = 0;
    //! zerocoin specific fields

    std::map<sigma::CoinDenomination, unsigned int> myZerocoinSupply;
    std::vector<sigma::CoinDenomination> denominations;
    GetAllDenoms(denominations);
    BOOST_FOREACH(sigma::CoinDenomination denomination, denominations){
        myZerocoinSupply.insert(make_pair(denomination, 0));
    }

    {
        //LOCK(cs_pivtracker);
        // Get Unused coins
        for (auto& it : mapSerialHashes) {
            CMintMeta meta = it.second;
            if (meta.isUsed || meta.isArchived)
                continue;
            bool fConfirmed = ((meta.nHeight < chainActive.Height() - ZC_MINT_CONFIRMATIONS) && !(meta.nHeight == 0));
            if (fConfirmedOnly && !fConfirmed)
                continue;
            if (fUnconfirmedOnly && fConfirmed)
                continue;
            int64_t nValue;
            sigma::DenominationToInteger(meta.denom, nValue);
            nTotal += nValue;
            myZerocoinSupply.at(meta.denom)++;
        }
    }

    if (nTotal < 0 ) nTotal = 0; // Sanity never hurts

    return nTotal;
}

CAmount CHDMintTracker::GetUnconfirmedBalance() const
{
    return GetBalance(false, true);
}

std::list<CMintMeta> CHDMintTracker::GetMints(bool fConfirmedOnly, bool fInactive) const
{
    std::list<CMintMeta> vMints;
    for (auto& it : mapSerialHashes) {
        CMintMeta mint = it.second;
        if ((mint.isArchived || mint.isUsed) && fInactive)
            continue;
        bool fConfirmed = (mint.nHeight < chainActive.Height() - ZC_MINT_CONFIRMATIONS);
        if (fConfirmedOnly && !fConfirmed)
            continue;
        vMints.push_back(mint);
    }
    return vMints;
}

//Does a mint in the tracker have this txid
bool CHDMintTracker::HasMintTx(const uint256& txid)
{
    for (auto it : mapSerialHashes) {
        if (it.second.txid == txid)
            return true;
    }

    return false;
}

bool CHDMintTracker::HasPubcoin(const GroupElement &pubcoin) const
{
    // Check if this mint's pubcoin value belongs to our mapSerialHashes (which includes hashpubcoin values)
    uint256 hash = sigma::GetPubCoinValueHash(pubcoin);
    return HasPubcoinHash(hash);
}

bool CHDMintTracker::HasPubcoinHash(const uint256& hashPubcoin) const
{
    for (auto it : mapSerialHashes) {
        CMintMeta meta = it.second;
        if (sigma::GetPubCoinValueHash(meta.pubCoinValue) == hashPubcoin)
            return true;
    }
    return false;
}

bool CHDMintTracker::HasSerial(const Scalar& bnSerial) const
{
    uint256 hash = GetSerialHash(bnSerial);
    return HasSerialHash(hash);
}

bool CHDMintTracker::HasSerialHash(const uint256& hashSerial) const
{
    auto it = mapSerialHashes.find(hashSerial);
    return it != mapSerialHashes.end();
}

bool CHDMintTracker::UpdateZerocoinEntry(const CSigmaEntry& zerocoin)
{
    if (!HasSerial(zerocoin.serialNumber))
        return error("%s: zerocoin %s is not known", __func__, zerocoin.value.GetHex());

    uint256 hashSerial = GetSerialHash(zerocoin.serialNumber);

    //Update the meta object
    CMintMeta meta;
    Get(hashSerial, meta);
    meta.isUsed = zerocoin.IsUsed;
    meta.denom = zerocoin.get_denomination();
    meta.nHeight = zerocoin.nHeight;
    mapSerialHashes.at(hashSerial) = meta;

    //Write to db
    return CWalletDB(strWalletFile).WriteZerocoinEntry(zerocoin);
}

bool CHDMintTracker::UpdateState(const CMintMeta& meta)
{
    uint256 hashPubcoin = sigma::GetPubCoinValueHash(meta.pubCoinValue);
    CWalletDB walletdb(strWalletFile);

    if (meta.isDeterministic) {
        CHDMint dMint;
        if (!walletdb.ReadHDMint(hashPubcoin, dMint)) {
            // Check archive just in case
            if (!meta.isArchived)
                return error("%s: failed to read deterministic mint from database", __func__);

            // Unarchive this mint since it is being requested and updated
            if (!walletdb.UnarchiveHDMint(hashPubcoin, dMint))
                return error("%s: failed to unarchive deterministic mint from database", __func__);
        }

        // get coin id & height
        int height, id;
        if(meta.nHeight<0 || meta.nId <= 0){
            std::tie(height, id) = sigma::CSigmaState::GetState()->GetMintedCoinHeightAndId(sigma::PublicCoin(dMint.GetPubcoinValue(), dMint.GetDenomination()));
        }
        else{
            height = meta.nHeight;
            id = meta.nId;
        }

        dMint.SetHeight(height);
        dMint.SetId(id);
        dMint.SetUsed(meta.isUsed);
        dMint.SetDenomination(meta.denom);

        if (!walletdb.WriteHDMint(dMint))
            return error("%s: failed to update deterministic mint when writing to db", __func__);
    } else {
        CSigmaEntry zerocoin;
        if (!walletdb.ReadZerocoinEntry(meta.pubCoinValue, zerocoin))
            return error("%s: failed to read mint from database", __func__);

        zerocoin.nHeight = meta.nHeight;
        zerocoin.id = meta.nId;
        zerocoin.IsUsed = meta.isUsed;
        zerocoin.set_denomination(meta.denom);

        if (!walletdb.WriteZerocoinEntry(zerocoin))
            return error("%s: failed to write mint to database", __func__);
    }

    mapSerialHashes[meta.hashSerial] = meta;

    return true;
}

void CHDMintTracker::Add(const CHDMint& dMint, bool isNew, bool isArchived, CHDMintWallet* zerocoinWallet)
{
    bool iszerocoinWalletInitialized = (NULL != zerocoinWallet);
    CMintMeta meta;
    meta.pubCoinValue = dMint.GetPubcoinValue();
    meta.nHeight = dMint.GetHeight();
    meta.nId = dMint.GetId();
    meta.txid = dMint.GetTxHash();
    meta.isUsed = dMint.IsUsed();
    meta.hashSerial = dMint.GetSerialHash();
    meta.denom = dMint.GetDenomination();
    meta.isArchived = isArchived;
    meta.isDeterministic = true;
    if (! iszerocoinWalletInitialized)
        zerocoinWallet = new CHDMintWallet(strWalletFile);
    meta.isSeedCorrect = true;
    if (! iszerocoinWalletInitialized)
        delete zerocoinWallet;
    mapSerialHashes[meta.hashSerial] = meta;

    if (isNew)
        CWalletDB(strWalletFile).WriteHDMint(dMint);
}

void CHDMintTracker::Add(const CSigmaEntry& zerocoin, bool isNew, bool isArchived)
{
    CMintMeta meta;
    meta.pubCoinValue = zerocoin.value;
    meta.nHeight = zerocoin.nHeight;
    meta.nId = zerocoin.id;
    //meta.txid = zerocoin.GetTxHash();
    meta.isUsed = zerocoin.IsUsed;
    meta.hashSerial = GetSerialHash(zerocoin.serialNumber);
    meta.denom = zerocoin.get_denomination();
    meta.isArchived = isArchived;
    meta.isDeterministic = false;
    meta.isSeedCorrect = true;
    mapSerialHashes[meta.hashSerial] = meta;

    if (isNew)
        CWalletDB(strWalletFile).WriteZerocoinEntry(zerocoin);
}

void CHDMintTracker::SetPubcoinUsed(const uint256& hashPubcoin, const uint256& txid)
{
    if (!HasPubcoinHash(hashPubcoin))
        return;
    CMintMeta meta = GetMetaFromPubcoin(hashPubcoin);
    meta.isUsed = true;
    mapPendingSpends.insert(make_pair(meta.hashSerial, txid));
    UpdateState(meta);
}

void CHDMintTracker::SetPubcoinNotUsed(const uint256& hashPubcoin)
{
    if (!HasPubcoinHash(hashPubcoin))
        return;
    CMintMeta meta = GetMetaFromPubcoin(hashPubcoin);
    meta.isUsed = false;

    if (mapPendingSpends.count(meta.hashSerial))
        mapPendingSpends.erase(meta.hashSerial);

    UpdateState(meta);
}

void CHDMintTracker::RemovePending(const uint256& txid)
{
    uint256 hashSerial;
    for (auto it : mapPendingSpends) {
        if (it.second == txid) {
            hashSerial = it.first;
            break;
        }
    }
    if (UintToArith256(hashSerial) > 0)
        mapPendingSpends.erase(hashSerial);
}

bool CHDMintTracker::UpdateStatusInternal(const std::set<uint256>& setMempool, CMintMeta& mint)
{
    uint256 hashPubcoin = sigma::GetPubCoinValueHash(mint.pubCoinValue);
    //! Check whether this mint has been spent and is considered 'pending' or 'confirmed'
    // If there is not a record of the block height, then look it up and assign it
    COutPoint outPoint;
    sigma::PublicCoin pubCoin(mint.pubCoinValue, mint.denom);
    bool isMintInChain = GetOutPoint(outPoint, pubCoin);
    const uint256& txidMint = outPoint.hash;

    //See if there is internal record of spending this mint (note this is memory only, would reset on restart)
    bool isPendingSpend = static_cast<bool>(mapPendingSpends.count(mint.hashSerial));

    // See if there is a blockchain record of spending this mint
    CSigmaState *sigmaState = sigma::CSigmaState::GetState();
    Scalar bnSerial;
    bool isConfirmedSpend = sigmaState->IsUsedCoinSerialHash(bnSerial, mint.hashSerial);

    // Double check the mempool for pending spend
    if (isPendingSpend) {
        uint256 txidPendingSpend = mapPendingSpends.at(mint.hashSerial);
        if (!setMempool.count(txidPendingSpend) || isConfirmedSpend) {
            RemovePending(txidPendingSpend);
            isPendingSpend = false;
            LogPrintf("%s : Pending txid %s removed because not in mempool\n", __func__, txidPendingSpend.GetHex());
        }
    }

    bool isUsed = isPendingSpend || isConfirmedSpend;

    if ((mint.nHeight==-1) || (mint.nId<=0) || !isMintInChain || isUsed != mint.isUsed) {
        CTransaction tx;
        uint256 hashBlock;

        // Txid will be marked 0 if there is no knowledge of the final tx hash yet
        if (mint.txid.IsNull()) {
            if (!isMintInChain) {
                LogPrintf("%s : Failed to find mint in zerocoinDB %s\n", __func__, hashPubcoin.GetHex().substr(0, 6));
                return true;
            }
            mint.txid = txidMint;
        }

        if (setMempool.count(mint.txid))
            return true;

        // Check the transaction associated with this mint
        if (!IsInitialBlockDownload() && !GetTransaction(mint.txid, tx, ::Params().GetConsensus(), hashBlock, true)) {
            LogPrintf("%s : Failed to find tx for mint txid=%s\n", __func__, mint.txid.GetHex());
            mint.isArchived = true;
            Archive(mint);
            return true;
        }

        bool isUpdated = false;

        // An orphan tx if hashblock is in mapBlockIndex but not in chain active
        if (mapBlockIndex.count(hashBlock)){
            if(!chainActive.Contains(mapBlockIndex.at(hashBlock))) {
                LogPrintf("%s : Found orphaned mint txid=%s\n", __func__, mint.txid.GetHex());
                mint.isUsed = false;
                mint.nHeight = 0;

                return true;
            }else if((mint.nHeight==-1) || (mint.nId<=0)){ // assign nHeight if not present
                sigma::PublicCoin pubcoin(mint.pubCoinValue, mint.denom);
                auto MintedCoinHeightAndId = sigmaState->GetMintedCoinHeightAndId(pubcoin);
                mint.nHeight = MintedCoinHeightAndId.first;
                mint.nId = MintedCoinHeightAndId.second;
                LogPrintf("%s : Set mint %s nHeight to %d\n", __func__, hashPubcoin.GetHex(), mint.nHeight);
                LogPrintf("%s : Set mint %s nId to %d\n", __func__, hashPubcoin.GetHex(), mint.nId);
                isUpdated = true;
            }
        }

        // Check that the mint has correct used status
        if (mint.isUsed != isUsed) {
            LogPrintf("%s : Set mint %s isUsed to %d\n", __func__, hashPubcoin.GetHex(), isUsed);    
            mint.isUsed = isUsed;
            isUpdated = true;
        }

        if(isUpdated) return true;
    }

    return false;
}

bool CHDMintTracker::MintMetaToZerocoinEntries(std::list <CSigmaEntry>& entries, std::list<CMintMeta> listMints) const {
    CSigmaEntry entry;
    for (const CMintMeta& mint : listMints) {
        if (pwalletMain->GetMint(mint.hashSerial, entry))
            entries.push_back(entry);
    }
    return true;
}

bool CHDMintTracker::UpdateMints(std::set<uint256> serialHashes, bool fReset, bool fUpdateStatus, bool fStatus){
    // if list is populated, only update mints with these serials
    bool fSelection = serialHashes.size()>0;
    // Only allow updates for one or the other
    if(fReset && fUpdateStatus)
        return false;

    std::list<CSigmaEntry> listMintsDB;
    CWalletDB walletdb(strWalletFile);
    walletdb.ListSigmaPubCoin(listMintsDB);
    for (auto& mint : listMintsDB){
        if(fReset){
            mint.nHeight = -1;
            mint.IsUsed = false;
        }
        else if(fUpdateStatus){
            mint.IsUsed = fStatus;
        }

        if((!fSelection) ||
            (fSelection && (serialHashes.find(GetSerialHash(mint.serialNumber)) != serialHashes.end()))){
            Add(mint);
        }
    }
    std::list<CHDMint> listDeterministicDB = walletdb.ListHDMints();

    CHDMintWallet* zerocoinWallet = new CHDMintWallet(strWalletFile);
    for (auto& dMint : listDeterministicDB) {
        if(fReset){
            dMint.SetHeight(-1);
            dMint.SetUsed(false);
        }
        else if(fUpdateStatus){
            dMint.SetUsed(fStatus);
        }

        if((!fSelection) ||
            (fSelection && (serialHashes.find(dMint.GetSerialHash()) != serialHashes.end()))){ 
            Add(dMint, true, false, zerocoinWallet);
        }
    }
    delete zerocoinWallet;

    return true;
}

list<CSigmaEntry> CHDMintTracker::MintsAsZerocoinEntries(){
    list <CSigmaEntry> listPubcoin;
    CWalletDB walletdb(strWalletFile);
    std::vector<CMintMeta> vecMists = ListMints();
    list<CMintMeta> listMints(vecMists.begin(), vecMists.end());
    for (const CMintMeta& mint : listMints) {
        CSigmaEntry entry;
        pwalletMain->GetMint(mint.hashSerial, entry);
        listPubcoin.push_back(entry);
    }
    return listPubcoin;
}

std::vector<CMintMeta> CHDMintTracker::ListMints(bool fUnusedOnly, bool fMatureOnly, bool fUpdateStatus, bool fWrongSeed)
{
    std::vector<CMintMeta> setMints;
    CWalletDB walletdb(strWalletFile);
    if (fUpdateStatus) {
        std::list<CSigmaEntry> listMintsDB;
        walletdb.ListSigmaPubCoin(listMintsDB);
        for (auto& mint : listMintsDB){
            Add(mint);
        }
        LogPrint("zero", "%s: added %d zerocoinmints from DB\n", __func__, listMintsDB.size());

        std::list<CHDMint> listDeterministicDB = walletdb.ListHDMints();

        CHDMintWallet* zerocoinWallet = new CHDMintWallet(strWalletFile);
        for (auto& dMint : listDeterministicDB) {
            Add(dMint, false, false, zerocoinWallet);
        }
        delete zerocoinWallet;
        LogPrint("zero", "%s: added %d hdmint from DB\n", __func__, listDeterministicDB.size());
    }

    std::vector<CMintMeta> vOverWrite;
    std::set<uint256> setMempool;
    {
        LOCK(mempool.cs);
        mempool.getTransactions(setMempool);
    }
    for (auto& it : mapSerialHashes) {
        CMintMeta mint = it.second;

        //This is only intended for unarchived coins
        if (mint.isArchived)
            continue;

        // Update the metadata of the mints if requested
        if (fUpdateStatus && UpdateStatusInternal(setMempool, mint)) {
            if (mint.isArchived)
                continue;

            // Mint was updated, queue for overwrite
            vOverWrite.emplace_back(mint);
        }

        if (fUnusedOnly && mint.isUsed)
            continue;

        if (fMatureOnly) {
            // Not confirmed
            if (!mint.nHeight || !(mint.nHeight + (ZC_MINT_CONFIRMATIONS-1) <= chainActive.Height()))
                continue;
        }

        if (!fWrongSeed && !mint.isSeedCorrect)
            continue;

        setMints.push_back(mint);
    }

    //overwrite any updates
    for (CMintMeta& meta : vOverWrite)
        UpdateState(meta);

    return setMints;
}

void CHDMintTracker::Clear()
{
    mapSerialHashes.clear();
}
