// Copyright (c) 2018 Tadhg Riordan, Zcoin Developer
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ZCOIN_ZMQ_ZMQPUBLISHER_H
#define ZCOIN_ZMQ_ZMQPUBLISHER_H

#include "zmqabstract.h"
#include "univalue.h"
#include "evo/deterministicmns.h"
#include "client-api/server.h"
#include <boost/thread/thread.hpp>
#include <boost/chrono.hpp>
#include "validationinterface.h"

class CBlockIndex;

class CZMQAbstractPublisher : public CZMQAbstract
{
public:
    bool Initialize();
    void Shutdown();

    bool Execute();
    bool Publish();

    virtual void SetMethod() = 0;
    virtual void SetTopic() = 0;

protected:
    std::string method;
    UniValue request;
    UniValue publish;
    boost::thread* worker;

};

/* Special Instance of the CZMQAbstractPublisher class to handle threads. */
class CZMQThreadPublisher : public CZMQAbstractPublisher
{
public:
    static void* Thread(){
        RenameThread("CZMQAbstractPublisher");

        LogPrintf("CZMQAbstractPublisher Thread started.\n");
        const int STATUS_TIME_SECS = 1;
        const int MASTERNODE_TIME_SECS = 60;
        int counter = 0;
        while(true){
            boost::this_thread::sleep_for(boost::chrono::seconds(STATUS_TIME_SECS));
            GetMainSignals().NotifyAPIStatus();
            counter++;
            if(counter==MASTERNODE_TIME_SECS){
                GetMainSignals().NotifyMasternodeList();
                counter = 0;
            }
        }
    };
};

/* Event classes. Each one is a specific notifier in ValidationInterface.
   virtual to allow multiple inheritence by topic classes */
class CZMQBlockEvent : virtual public CZMQAbstractPublisher
{
    /* Data related to a new block (updatedblocktip)
    */
public:
    bool NotifyBlock(const CBlockIndex *pindex);
};


class CZMQTransactionEvent : virtual public CZMQAbstractPublisher
{
    /* Data related to a new transaction
    */
public:
    bool NotifyTransaction(const CTransaction& transaction);
    bool NotifyTransactionLock(const CTransaction& transaction);
};

class CZMQAPIStatusEvent : virtual public CZMQAbstractPublisher
{
    /* API Status notification
    */
public:
    bool NotifyAPIStatus();
};

class CZMQMasternodeListEvent : virtual public CZMQAbstractPublisher
{
    /* Masternode List notification
    */
public:
    bool NotifyMasternodeList();
};

class CZMQLockStatusEvent : virtual public CZMQAbstractPublisher
{
    /* Notification when a transaction is locked or unlocked */
public:
    bool NotifyTxoutLock(COutPoint outpoint, bool isLocked);
};

class CZMQSettingsEvent : virtual public CZMQAbstractPublisher
{
     /* Settings updated
    */
public:
    bool NotifySettingsUpdate(std::string update);
};

class CZMQMasternodeEvent : virtual public CZMQAbstractPublisher
{
    /* Data related to an updated Masternode
    */
public:
    bool NotifyMasternodeUpdate(CDeterministicMNCPtr masternode);
};

class CZMQMintStatusEvent : virtual public CZMQAbstractPublisher
{
    /* Data related to updated mint status
    */
public:
    bool NotifyMintStatusUpdate(std::string update);
};

/* Topics. inheriting from an event class implies publishing on that event.
   'method' string is the API method called in client-api/
*/
class CZMQBlockDataTopic : public CZMQBlockEvent
{
public:
    void SetTopic(){ topic = "address";}
    void SetMethod(){ method= "block";}
};

class CZMQTransactionTopic : public CZMQTransactionEvent
{
public:
    void SetTopic(){ topic = "transaction";}
    void SetMethod(){ method= "transaction";}
};

class CZMQSettingsTopic : public CZMQSettingsEvent
{
public:
    void SetTopic(){ topic = "settings";}
    void SetMethod(){ method= "readSettings";}
};

class CZMQAPIStatusTopic : public CZMQAPIStatusEvent
{
public:
    void SetTopic(){ topic = "apiStatus";}
    void SetMethod(){ method= "apiStatus";}
};

class CZMQMasternodeListTopic : public CZMQMasternodeListEvent
{
public:
    void SetTopic(){ topic = "masternodeList";}
    void SetMethod(){ method= "masternodeList";}
};

class CZMQLockStatusTopic : public CZMQLockStatusEvent
{
public:
    void SetTopic(){ topic = "lockStatus";}
    void SetMethod(){ method= "lockStatus";}
};

class CZMQMasternodeTopic : public CZMQMasternodeEvent
{
public:
    void SetTopic(){ topic = "masternode";}
    void SetMethod(){ method= "masternodeUpdate";}
};

#endif // ZCOIN_ZMQ_ZMQPUBLISHER_H
