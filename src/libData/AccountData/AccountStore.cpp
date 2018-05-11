/**
* Copyright (c) 2018 Zilliqa 
* This source code is being disclosed to you solely for the purpose of your participation in 
* testing Zilliqa. You may view, compile and run the code for that purpose and pursuant to 
* the protocols and algorithms that are programmed into, and intended by, the code. You may 
* not do anything else with the code without express permission from Zilliqa Research Pte. Ltd., 
* including modifying or publishing the code (or any part of it), and developing or forming 
* another public or private blockchain network. This source code is provided ‘as is’ and no 
* warranties are given as to title or non-infringement, merchantability or fitness for purpose 
* and, to the extent permitted by law, all liability for your use of the code is disclaimed. 
* Some programs in this code are governed by the GNU General Public License v3.0 (available at 
* https://www.gnu.org/licenses/gpl-3.0.en.html) (‘GPLv3’). The programs that are governed by 
* GPLv3.0 are those programs that are located in the folders src/depends and tests/depends 
* and which include a reference to GPLv3 in their program files.
**/

#include <boost/filesystem.hpp>
#include <leveldb/db.h>

#include "AccountStore.h"
#include "Address.h"
#include "depends/common/RLP.h"
#include "libPersistence/BlockStorage.h"
#include "libPersistence/ContractStorage.h"
#include "libUtils/DataConversion.h"
#include "libUtils/Logger.h"

using namespace std;
using namespace boost::multiprecision;

AccountStore::AccountStore()
    : m_db("state")
{
    m_state = SecureTrieDB<Address, dev::OverlayDB>(&m_db);
}

AccountStore::~AccountStore()
{
    // boost::filesystem::remove_all("./state");
}

void AccountStore::Init()
{
    LOG_MARKER();
    m_addressToAccount.clear();
    ContractStorage::GetContractStorage().GetStateDB().ResetDB();
    m_db.ResetDB();
    m_state.init();
    prevRoot = m_state.root();
}

unsigned int AccountStore::Serialize(vector<unsigned char>& dst,
                                     unsigned int offset) const
{
    // [Total number of accounts (uint256_t)] [Addr 1] [Account 1] [Addr 2] [Account 2] .... [Addr n] [Account n]

    // LOG_MARKER();

    unsigned int size_needed = UINT256_SIZE;
    unsigned int size_remaining = dst.size() - offset;
    unsigned int totalSerializedSize = size_needed;

    if (size_remaining < size_needed)
    {
        dst.resize(size_needed + offset);
    }

    unsigned int curOffset = offset;

    // [Total number of accounts]
    LOG_GENERAL(
        INFO,
        "Debug: Total number of accounts to serialize: " << GetNumOfAccounts());
    uint256_t totalNumOfAccounts = GetNumOfAccounts();
    SetNumber<uint256_t>(dst, curOffset, totalNumOfAccounts, UINT256_SIZE);
    curOffset += UINT256_SIZE;

    vector<unsigned char> address_vec;
    // [Addr 1] [Account 1] [Addr 2] [Account 2] .... [Addr n] [Account n]
    for (auto entry : m_addressToAccount)
    {
        // Address
        address_vec = entry.first.asBytes();

        copy(address_vec.begin(), address_vec.end(), std::back_inserter(dst));
        curOffset += ACC_ADDR_SIZE;
        totalSerializedSize += ACC_ADDR_SIZE;

        // Account
        size_needed = entry.second.Serialize(dst, curOffset);
        curOffset += size_needed;
        totalSerializedSize += size_needed;
    }

    return totalSerializedSize;
}

int AccountStore::Deserialize(const vector<unsigned char>& src,
                              unsigned int offset)
{
    // [Total number of accounts] [Addr 1] [Account 1] [Addr 2] [Account 2] .... [Addr n] [Account n]
    // LOG_MARKER();

    try
    {
        unsigned int curOffset = offset;
        uint256_t totalNumOfAccounts
            = GetNumber<uint256_t>(src, curOffset, UINT256_SIZE);
        curOffset += UINT256_SIZE;

        Address address;
        Account account;
        unsigned int numberOfAccountDeserialze = 0;
        this->Init();
        while (numberOfAccountDeserialze < totalNumOfAccounts)
        {
            numberOfAccountDeserialze++;

            // Deserialize address
            copy(src.begin() + curOffset,
                 src.begin() + curOffset + ACC_ADDR_SIZE,
                 address.asArray().begin());
            curOffset += ACC_ADDR_SIZE;

            // Deserialize account
            // account.Deserialize(src, curOffset);
            if (account.Deserialize(src, curOffset) != 0)
            {
                LOG_GENERAL(WARNING, "We failed to init account.");
                return -1;
            }
            curOffset += ACCOUNT_SIZE;
            m_addressToAccount[address] = account;
            UpdateStateTrie(address, account);
            // MoveUpdatesToDisk();
        }
        PrintAccountState();
    }
    catch (const std::exception& e)
    {
        LOG_GENERAL(WARNING,
                    "Error with AccountStore::Deserialize." << ' ' << e.what());
        return -1;
    }
    return 0;
}

AccountStore& AccountStore::GetInstance()
{
    static AccountStore accountstore;
    return accountstore;
}

bool AccountStore::DoesAccountExist(const Address& address)
{
    LOG_MARKER();

    if (GetAccount(address) != nullptr)
    {
        return true;
    }

    return false;
}

void AccountStore::AddAccount(const Address& address, const Account& account)
{
    LOG_MARKER();

    if (!DoesAccountExist(address))
    {
        m_addressToAccount.insert(make_pair(address, account));
        // UpdateStateTrie(address, account);
    }
}

void AccountStore::AddAccount(const PubKey& pubKey, const Account& account)
{
    AddAccount(Account::GetAddressFromPublicKey(pubKey), account);
}

void AccountStore::UpdateAccounts(const uint64_t& blockNum,
                                  const Transaction& transaction)
{
    //LOG_MARKER();

    const PubKey& senderPubKey = transaction.GetSenderPubKey();
    const Address fromAddr = Account::GetAddressFromPublicKey(senderPubKey);
    Address toAddr = transaction.GetToAddr();
    const uint256_t& amount = transaction.GetAmount();

    if (transaction.GetCode().size() > 0 && toAddr == NullAddress)
    {
        // Create contract account
        Account* account = GetAccount(fromAddr);
        // TODO: remove this, temporary way to test transactions
        if (account == nullptr)
        {
            AddAccount(fromAddr, {10000000000, 0});
        }

        toAddr = Account::GetAddressForContract(
            fromAddr, m_addressToAccount[fromAddr].GetNonce());
        AddAccount(toAddr, {0, 0});
        m_addressToAccount[toAddr].SetCode(transaction.GetCode());
        // Store the immutable states
        m_addressToAccount[toAddr].InitContract(transaction.GetData());
    }

    if (transaction.GetData().size() > 0 && toAddr != NullAddress)
    {
        // TODO: Trigger a contract account
        // ParseJsonOutput(/*Call Interpreter To Get Json Output*/);
    }

    TransferBalance(fromAddr, toAddr, amount);
    IncreaseNonce(fromAddr);
}

void AccountStore::ParseJsonOutput(const Json::Value& _json)
{
    // the _json actually refers to the Array of Contracts,
    // one transaction can affect multiple contracts by one call
    for (auto j : _json)
    {
        if (!j.isMember("address") || !j.isMember("outputs")
            || !j.isMember("states"))
        {
            LOG_GENERAL(WARNING,
                        "The json output of this contract is corrupted");
            continue;
        }
        const Json::Value statesObj = j["states"];
        for (auto s : statesObj)
        {
            if (!s.isMember("vname") || !s.isMember("type")
                || !s.isMember("value"))
            {
                LOG_GENERAL(WARNING,
                            "Address: "
                                << j["address"].asString()
                                << ", The json output of states is corrupted");
                continue;
            }
            string vname = s["vname"].asString();
            string type = s["type"].asString();
            string value = s["value"].asString();
            m_addressToAccount[Address(j["address"].asString())].SetStorage(
                vname, type, value);
        }
    }
}

Account* AccountStore::GetAccount(const Address& address)
{
    //LOG_MARKER();

    auto it = m_addressToAccount.find(address);
    // LOG_GENERAL(INFO, (it != m_addressToAccount.end()));
    if (it != m_addressToAccount.end())
    {
        return &it->second;
    }

    string accountDataString = m_state.at(address);
    if (accountDataString.empty())
    {
        return nullptr;
    }

    dev::RLP accountDataRLP(accountDataString);
    if (accountDataRLP.itemCount() != 4)
    {
        LOG_GENERAL(WARNING, "Account data corrupted");
        return nullptr;
    }

    auto it2 = m_addressToAccount.emplace(
        std::piecewise_construct, std::forward_as_tuple(address),
        std::forward_as_tuple(
            accountDataRLP[0].toInt<boost::multiprecision::uint256_t>(),
            accountDataRLP[1].toInt<boost::multiprecision::uint256_t>()));

    // Code Hash
    if (accountDataRLP[3].toHash<dev::h256>() != dev::h256())
    {
        // Extract Code Content
        it2.first->second.SetCode(
            ContractStorage::GetContractStorage().GetContractCode(address));
        if (accountDataRLP[3].toHash<dev::h256>()
            != it2.first->second.GetCodeHash())
        {
            LOG_GENERAL(WARNING, "Account Code Content doesn't match Code Hash")
            m_addressToAccount.erase(it2.first);
            return nullptr;
        }
        // Storage Root
        it2.first->second.SetStorageRoot(accountDataRLP[2].toHash<dev::h256>());
    }

    return &it2.first->second;
}

uint256_t AccountStore::GetNumOfAccounts() const
{
    LOG_MARKER();
    return m_addressToAccount.size();
}

bool AccountStore::UpdateStateTrieAll()
{
    bool ret = true;
    for (auto entry : m_addressToAccount)
    {
        if (!UpdateStateTrie(entry.first, entry.second))
        {
            ret = false;
            break;
        }
    }
    return ret;
}

bool AccountStore::UpdateStateTrie(const Address& address,
                                   const Account& account)
{
    //LOG_MARKER();

    dev::RLPStream rlpStream(4);
    rlpStream << account.GetBalance() << account.GetNonce()
              << account.GetStorageRoot() << account.GetCodeHash();
    m_state.insert(address, &rlpStream.out());

    return true;
}

bool AccountStore::IncreaseBalance(
    const Address& address, const boost::multiprecision::uint256_t& delta)
{
    // LOG_MARKER();

    if (delta == 0)
    {
        return true;
    }

    Account* account = GetAccount(address);

    if (account != nullptr && account->IncreaseBalance(delta))
    {
        // UpdateStateTrie(address, *account);
        return true;
    }
    else if (account == nullptr)
    {
        AddAccount(address, {delta, 0});
        return true;
    }

    return false;
}

bool AccountStore::DecreaseBalance(
    const Address& address, const boost::multiprecision::uint256_t& delta)
{
    // LOG_MARKER();

    if (delta == 0)
    {
        return true;
    }

    Account* account = GetAccount(address);

    if (account != nullptr && account->DecreaseBalance(delta))
    {
        // UpdateStateTrie(address, *account);
        return true;
    }
    // TODO: remove this, temporary way to test transactions
    else if (account == nullptr)
    {
        AddAccount(address, {10000000000, 0});
    }

    return false;
}

bool AccountStore::TransferBalance(
    const Address& from, const Address& to,
    const boost::multiprecision::uint256_t& delta)
{
    // LOG_MARKER();

    if (DecreaseBalance(from, delta) && IncreaseBalance(to, delta))
    {
        return true;
    }

    return false;
}

boost::multiprecision::uint256_t
AccountStore::GetBalance(const Address& address)
{
    LOG_MARKER();

    const Account* account = GetAccount(address);

    if (account != nullptr)
    {
        return account->GetBalance();
    }

    return 0;
}

bool AccountStore::IncreaseNonce(const Address& address)
{
    //LOG_MARKER();

    Account* account = GetAccount(address);

    if (account != nullptr && account->IncreaseNonce())
    {
        // UpdateStateTrie(address, *account);
        return true;
    }

    return false;
}

boost::multiprecision::uint256_t AccountStore::GetNonce(const Address& address)
{
    //LOG_MARKER();

    Account* account = GetAccount(address);

    if (account != nullptr)
    {
        return account->GetNonce();
    }

    return 0;
}

dev::h256 AccountStore::GetStateRootHash() const
{
    LOG_MARKER();

    return m_state.root();
}

void AccountStore::MoveRootToDisk(const dev::h256& root)
{
    //convert h256 to bytes
    if (!BlockStorage::GetBlockStorage().PutMetadata(STATEROOT, root.asBytes()))
        LOG_GENERAL(INFO, "FAIL: Put metadata failed");
}

void AccountStore::MoveUpdatesToDisk()
{
    LOG_MARKER();

    ContractStorage::GetContractStorage().GetStateDB().commit();
    for (auto i : m_addressToAccount)
    {
        if (ContractStorage::GetContractStorage().PutContractCode(
                i.first, i.second.GetCode()))
        {
            LOG_GENERAL(WARNING, "Write Contract Code to Disk Failed");
            continue;
        }
        i.second.Commit();
    }
    m_state.db()->commit();
    prevRoot = m_state.root();
    MoveRootToDisk(prevRoot);
}

void AccountStore::DiscardUnsavedUpdates()
{
    LOG_MARKER();

    ContractStorage::GetContractStorage().GetStateDB().rollback();
    for (auto i : m_addressToAccount)
    {
        i.second.RollBack();
    }
    m_state.db()->rollback();
    m_state.setRoot(prevRoot);
    m_addressToAccount.clear();
}

void AccountStore::PrintAccountState()
{
    LOG_MARKER();

    LOG_GENERAL(INFO, "Printing Account State");
    for (auto entry : m_addressToAccount)
    {
        LOG_GENERAL(INFO, entry.first << " " << entry.second);
    }
    LOG_GENERAL(INFO, "State Root: " << GetStateRootHash());
}

string AccountStore::GetBlockStateJsonStr(const uint64_t& BlockNum)
{
    Json::Value obj;
    Json::Value blockItem;
    blockItem["vname"] = "BLOCKNUMBER";
    blockItem["type"] = "BNum";
    blockItem["value"] = to_string(BlockNum);
    obj.append(blockItem);
    return obj.asString();
}

bool AccountStore::RetrieveFromDisk()
{
    LOG_MARKER();
    std::vector<unsigned char> rootBytes;
    if (!BlockStorage::GetBlockStorage().GetMetadata(STATEROOT, rootBytes))
    {
        return false;
    }
    dev::h256 root(rootBytes);
    m_state.setRoot(root);
    for (auto i : m_state)
    {
        Address address(i.first);
        dev::RLP rlp(i.second);
        if (rlp.itemCount() != 4)
        {
            LOG_GENERAL(WARNING, "Account data corrupted");
            continue;
        }
        Account account(rlp[0].toInt<boost::multiprecision::uint256_t>(),
                        rlp[1].toInt<boost::multiprecision::uint256_t>());
        // Code Hash
        if (rlp[3].toHash<dev::h256>() != dev::h256())
        {
            // Extract Code Content
            account.SetCode(
                ContractStorage::GetContractStorage().GetContractCode(address));
            if (rlp[3].toHash<dev::h256>() != account.GetCodeHash())
            {
                LOG_GENERAL(WARNING,
                            "Account Code Content doesn't match Code Hash")
                continue;
            }
            // Storage Root
            account.SetStorageRoot(rlp[2].toHash<dev::h256>());
        }
        m_addressToAccount.insert({address, account});
    }
    return true;
}

void AccountStore::RepopulateStateTrie()
{
    LOG_MARKER();
    m_state.init();
    prevRoot = m_state.root();
    UpdateStateTrieAll();
}